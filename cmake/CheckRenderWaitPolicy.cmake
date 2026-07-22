if(NOT DEFINED SOURCE_ROOT)
    message(FATAL_ERROR "SOURCE_ROOT is required")
endif()

file(GLOB_RECURSE DB_WAIT_POLICY_FILES "${SOURCE_ROOT}/src/*.c"
     "${SOURCE_ROOT}/src/*.h")

set(DB_DIRECT_WAIT_RULES
    "vkWaitForFences[ \\t\\r\\n]*\\(|src/renderers/vulkan_1_2_multi_gpu/vk_wait.c"
    "vkAcquireNextImageKHR[ \\t\\r\\n]*\\(|src/renderers/vulkan_1_2_multi_gpu/vk_wait.c"
    "client_wait_sync[ \t\r\n]*\\(|src/renderers/gl_upload_stream.c"
    "glfwWaitEventsTimeout|src/displays/glfw_window/glfw_window_common.c"
    "select[ \t\r\n]*\\(|src/displays/linux_kms_atomic/kms_page_flip.c"
    "poll[ \t\r\n]*\\(|src/core/db_probe_process.c"
    "nanosleep[ \t\r\n]*\\(|src/core/db_core.c")

set(DB_FORBIDDEN_WAIT_PATTERNS
    "vkQueueWaitIdle" "vkDeviceWaitIdle" "VK_QUERY_RESULT_WAIT_BIT"
    "atomic_flag_test_and_set" "while[ \t\r\n]*\\([ \t\r\n]*1[ \t\r\n]*\\)")

set(DB_FORBIDDEN_PROGRESS_PATTERNS
    "DB_METRIC_SAMPLE_INITIAL_CAPACITY"
    "DB_METRIC_SAMPLE_MAX_CAPACITY"
    "render_frame_samples"
    "display_frame_time_samples"
    "DB_GL_UPLOAD_STREAM_PREPARE_RETRY_LIMIT"
    "DB_PROBE_PROCESS_POLL_INTERVAL"
    "realloc[ \t\r\n]*\\([^;]*(sample|metric|timing)"
    "reserve_array[^;]*(sample|metric|timing)")
set(DB_PROGRESS_POLICY_IMPLEMENTATION "src/core/db_progress_policy.c")

set(db_violations "")
foreach(db_path IN LISTS DB_WAIT_POLICY_FILES)
    file(RELATIVE_PATH db_file "${SOURCE_ROOT}" "${db_path}")
    file(READ "${db_path}" db_content)

    foreach(db_pattern IN LISTS DB_FORBIDDEN_WAIT_PATTERNS)
        if(db_content MATCHES "${db_pattern}")
            list(APPEND db_violations
                 "${db_file}: forbidden unbounded wait pattern '${db_pattern}'")
        endif()
    endforeach()

    foreach(db_pattern IN LISTS DB_FORBIDDEN_PROGRESS_PATTERNS)
        if(db_content MATCHES "${db_pattern}")
            list(
                APPEND
                db_violations
                "${db_file}: obsolete local progress or growable-metric pattern '${db_pattern}'"
            )
        endif()
    endforeach()

    if(NOT db_file STREQUAL "${DB_PROGRESS_POLICY_IMPLEMENTATION}"
       AND db_content MATCHES
           "policy->(max_attempts|attempt_timeout_ns|total_timeout_ns)")
        list(
            APPEND
            db_violations
            "${db_file}: progress policy budgets must be consumed through a session or executor"
        )
    endif()

    if(NOT db_file STREQUAL "${DB_PROGRESS_POLICY_IMPLEMENTATION}")
        set(db_local_policy_content "${db_content}")
        # Split-search retries are a fixed calibration dataset, not a runtime
        # progress loop.
        string(REPLACE "DB_VK_SPLIT_INVALID_RETRY_LIMIT" ""
                       db_local_policy_content "${db_local_policy_content}")
        if(db_local_policy_content
           MATCHES
           "DB_[A-Z0-9_]*(POLL|RETRY|DRAIN|REAP)[A-Z0-9_]*(LIMIT|TIMEOUT|INTERVAL)"
        )
            list(
                APPEND
                db_violations
                "${db_file}: local progress budget must be a named registry profile"
            )
        endif()
    endif()

    foreach(db_rule IN LISTS DB_DIRECT_WAIT_RULES)
        string(REPLACE "|" ";" db_rule_parts "${db_rule}")
        list(GET db_rule_parts 0 db_pattern)
        list(GET db_rule_parts 1 db_allowed_file)
        if(db_content MATCHES "${db_pattern}" AND NOT db_file STREQUAL
                                                  db_allowed_file)
            list(
                APPEND
                db_violations
                "${db_file}: direct wait '${db_pattern}' must use the audited adapter in ${db_allowed_file}"
            )
        endif()
    endforeach()
endforeach()

if(db_violations)
    list(JOIN db_violations "\n  " db_violation_text)
    message(
        FATAL_ERROR
            "Render wait policy violations:\n  ${db_violation_text}\nUse a named db_progress_policy_id_t profile."
    )
endif()
