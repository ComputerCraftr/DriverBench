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
    "select[ \t\r\n]*\\(|src/displays/linux_kms_atomic/kms_core.c"
    "nanosleep[ \t\r\n]*\\(|src/core/db_core.c")

set(DB_FORBIDDEN_WAIT_PATTERNS
    "vkQueueWaitIdle" "vkDeviceWaitIdle" "VK_QUERY_RESULT_WAIT_BIT"
    "atomic_flag_test_and_set" "while[ \t\r\n]*\\([ \t\r\n]*1[ \t\r\n]*\\)")

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
