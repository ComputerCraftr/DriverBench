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
  message(STATUS
    "Output-expectation test skipped: ${skip_reason}\n${combined_output}")
  return()
endif()

foreach(required_pattern IN LISTS TEST_REQUIRED_PATTERNS)
  if("${required_pattern}" STREQUAL "")
    continue()
  endif()
  string(REGEX MATCH "${required_pattern}" pattern_match "${combined_output}")
  if(pattern_match STREQUAL "")
    message(FATAL_ERROR
      "Required pattern '${required_pattern}' not found.\n"
      "output:\n${combined_output}\n")
  endif()
endforeach()

foreach(forbidden_pattern IN LISTS TEST_FORBIDDEN_PATTERNS)
  if("${forbidden_pattern}" STREQUAL "")
    continue()
  endif()
  string(REGEX MATCH "${forbidden_pattern}" pattern_match "${combined_output}")
  if(NOT pattern_match STREQUAL "")
    message(FATAL_ERROR
      "Forbidden pattern '${forbidden_pattern}' was found.\n"
      "output:\n${combined_output}\n")
  endif()
endforeach()

message(STATUS "Output expectations OK for ${TEST_BIN}")
