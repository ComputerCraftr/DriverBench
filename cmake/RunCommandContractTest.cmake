include("${CMAKE_CURRENT_LIST_DIR}/TestRunnerCommon.cmake")

db_test_require_defined(TEST_BIN)
db_test_default_string(TEST_ARGS "")
db_test_default_string(TEST_EXIT_CODE "0")
db_test_default_string(TEST_REQUIRED_SUBSTRINGS "")
db_test_default_string(TEST_FORBIDDEN_SUBSTRINGS "")
db_test_default_string(TEST_REQUIRED_FIELDS "")
db_test_default_string(TEST_FORBIDDEN_FIELDS "")
db_test_parse_pipe_list_var(TEST_REQUIRED_SUBSTRINGS)
db_test_parse_pipe_list_var(TEST_FORBIDDEN_SUBSTRINGS)
db_test_parse_pipe_list_var(TEST_REQUIRED_FIELDS)
db_test_parse_pipe_list_var(TEST_FORBIDDEN_FIELDS)

set(test_command ${TEST_BIN})
if(NOT "${TEST_ARGS}" STREQUAL "")
    separate_arguments(test_args_list NATIVE_COMMAND "${TEST_ARGS}")
    list(APPEND test_command ${test_args_list})
endif()

execute_process(
    COMMAND ${test_command}
    RESULT_VARIABLE test_result
    OUTPUT_VARIABLE test_stdout
    ERROR_VARIABLE test_stderr)

set(combined_output "${test_stdout}\n${test_stderr}")
if(NOT test_result EQUAL 0 AND combined_output MATCHES
                               "${DB_TEST_GLFW_ENV_SKIP_REGEX}")
    message(
        STATUS
            "Command-contract test skipped: ${DB_TEST_GLFW_ENV_SKIP_REASON}\n${combined_output}"
    )
    return()
endif()

if(NOT "${test_result}" STREQUAL "${TEST_EXIT_CODE}")
    message(
        FATAL_ERROR
            "Unexpected exit code.\nexpected: ${TEST_EXIT_CODE}\nactual: ${test_result}\noutput:\n${combined_output}\n"
    )
endif()

foreach(required_substring IN LISTS TEST_REQUIRED_SUBSTRINGS)
    if("${required_substring}" STREQUAL "")
        continue()
    endif()
    db_test_assert_substring_contains(
        "${combined_output}" "${required_substring}" "${TEST_BIN} output")
endforeach()

foreach(forbidden_substring IN LISTS TEST_FORBIDDEN_SUBSTRINGS)
    if("${forbidden_substring}" STREQUAL "")
        continue()
    endif()
    db_test_assert_substring_not_contains(
        "${combined_output}" "${forbidden_substring}" "${TEST_BIN} output")
endforeach()

foreach(required_field IN LISTS TEST_REQUIRED_FIELDS)
    if("${required_field}" STREQUAL "")
        continue()
    endif()
    string(FIND "${required_field}" "=" eq_index)
    if(eq_index EQUAL -1)
        message(
            FATAL_ERROR
                "Invalid required field contract '${required_field}'. Expected field=value"
        )
    endif()
    string(SUBSTRING "${required_field}" 0 ${eq_index} field_name)
    math(EXPR value_start "${eq_index} + 1")
    string(SUBSTRING "${required_field}" ${value_start} -1 field_value)
    db_test_assert_field_equals("${combined_output}" "${field_name}"
                                "${field_value}" "${TEST_BIN} output")
endforeach()

foreach(forbidden_field IN LISTS TEST_FORBIDDEN_FIELDS)
    if("${forbidden_field}" STREQUAL "")
        continue()
    endif()
    string(FIND "${forbidden_field}" "=" eq_index)
    if(eq_index EQUAL -1)
        message(
            FATAL_ERROR
                "Invalid forbidden field contract '${forbidden_field}'. Expected field=value"
        )
    endif()
    string(SUBSTRING "${forbidden_field}" 0 ${eq_index} field_name)
    math(EXPR value_start "${eq_index} + 1")
    string(SUBSTRING "${forbidden_field}" ${value_start} -1 field_value)
    db_test_assert_field_not_equals_if_present(
        "${combined_output}" "${field_name}" "${field_value}"
        "${TEST_BIN} output")
endforeach()

message(STATUS "Command contract OK for ${TEST_BIN}")
