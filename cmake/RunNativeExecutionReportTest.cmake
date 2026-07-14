include("${CMAKE_CURRENT_LIST_DIR}/TestRunnerCommon.cmake")

if(NOT DEFINED TEST_BIN
   OR NOT DEFINED TEST_ARGS
   OR NOT DEFINED TEST_EXPECTED_FIELDS)
    message(
        FATAL_ERROR
            "native execution test requires TEST_BIN, TEST_ARGS, and TEST_EXPECTED_FIELDS"
    )
endif()

db_test_run_command(db_output db_skip db_result "${TEST_ARGS}"
                    "native execution command failed")
if(NOT "${db_skip}" STREQUAL "")
    return()
endif()
if(NOT db_output MATCHES "event=renderer_execution schema=2")
    message(FATAL_ERROR "renderer_execution event is missing:\n${db_output}")
endif()
foreach(db_field IN LISTS TEST_EXPECTED_FIELDS)
    if(NOT db_output MATCHES "${db_field}")
        message(
            FATAL_ERROR
                "missing native execution field '${db_field}':\n${db_output}")
    endif()
endforeach()
