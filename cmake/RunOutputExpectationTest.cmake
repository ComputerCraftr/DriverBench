include("${CMAKE_CURRENT_LIST_DIR}/TestRunnerCommon.cmake")

db_test_require_defined(TEST_BIN)
db_test_default_string(TEST_ARGS "")
db_test_default_string(TEST_REQUIRED_PATTERNS "")
db_test_default_string(TEST_FORBIDDEN_PATTERNS "")
db_test_parse_list_var(TEST_REQUIRED_PATTERNS)
db_test_parse_list_var(TEST_FORBIDDEN_PATTERNS)

db_test_run_command(combined_output skip_reason run_status "${TEST_ARGS}"
                    "Output-expectation run failed")
if(NOT "${skip_reason}" STREQUAL "")
    return()
endif()

foreach(required_pattern IN LISTS TEST_REQUIRED_PATTERNS)
    if("${required_pattern}" STREQUAL "")
        continue()
    endif()
    db_test_assert_substring_contains(
        "${combined_output}" "${required_pattern}" "${TEST_BIN} output")
endforeach()

foreach(forbidden_pattern IN LISTS TEST_FORBIDDEN_PATTERNS)
    if("${forbidden_pattern}" STREQUAL "")
        continue()
    endif()
    db_test_assert_substring_not_contains(
        "${combined_output}" "${forbidden_pattern}" "${TEST_BIN} output")
endforeach()

message(STATUS "Output expectations OK for ${TEST_BIN}")
