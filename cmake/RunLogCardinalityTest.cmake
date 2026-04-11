include("${CMAKE_CURRENT_LIST_DIR}/TestRunnerCommon.cmake")

db_test_require_defined(TEST_BIN)
db_test_require_defined(TEST_ARGS)
db_test_require_defined(TEST_MAX_LINES)
db_test_run_command(output skip_reason status "${TEST_ARGS}"
                    "Default-output command failed")
if(NOT "${skip_reason}" STREQUAL "")
    message(STATUS "Default-output test skipped: ${skip_reason}")
    return()
endif()

string(REPLACE "\r" "" output "${output}")
string(REPLACE "\n" ";" lines "${output}")
set(project_line_count 0)
foreach(line IN LISTS lines)
    if(line MATCHES "^\\[[A-Za-z0-9_.-]+\\]\\[(info|error)\\] ")
        math(EXPR project_line_count "${project_line_count} + 1")
    endif()
    if(line
       MATCHES
       " event=(damage_block|shadow_upload_span|vk_memory_type_candidate|vk_memory_plane_layout|vk_memory_transport_attempt|vk_calibration_pair|vk_split_search_sample) "
    )
        message(FATAL_ERROR "Default output emitted trace detail:\n${line}")
    endif()
    if(line MATCHES " event=(runtime_status|runtime_error|fatal_error) ")
        message(
            FATAL_ERROR "Default runtime used a compatibility event:\n${line}")
    endif()
endforeach()

if(project_line_count GREATER TEST_MAX_LINES)
    message(
        FATAL_ERROR
            "Default output exceeded ${TEST_MAX_LINES} project lines: ${project_line_count}\n${output}"
    )
endif()
message(STATUS "Default output is bounded: lines=${project_line_count}")
