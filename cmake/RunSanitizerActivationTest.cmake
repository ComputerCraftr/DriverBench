if(NOT DEFINED TEST_BIN OR NOT DEFINED TEST_PATTERN)
    message(FATAL_ERROR "TEST_BIN and TEST_PATTERN are required")
endif()

execute_process(
    COMMAND "${TEST_BIN}"
    RESULT_VARIABLE test_status
    OUTPUT_VARIABLE test_stdout
    ERROR_VARIABLE test_stderr)
set(test_output "${test_stdout}\n${test_stderr}")
if(test_status EQUAL 0)
    message(FATAL_ERROR "sanitizer activation child exited successfully")
endif()
if(NOT test_output MATCHES "${TEST_PATTERN}")
    message(
        FATAL_ERROR
            "sanitizer activation diagnostic '${TEST_PATTERN}' was absent:\n${test_output}"
    )
endif()
