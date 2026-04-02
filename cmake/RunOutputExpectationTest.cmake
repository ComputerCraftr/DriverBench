if(NOT DEFINED TEST_BIN)
  message(FATAL_ERROR "TEST_BIN is required")
endif()

if(NOT DEFINED TEST_ARGS)
  set(TEST_ARGS "")
endif()

if(NOT DEFINED TEST_REQUIRED_PATTERNS)
  set(TEST_REQUIRED_PATTERNS "")
endif()
string(REPLACE "," ";" TEST_REQUIRED_PATTERNS "${TEST_REQUIRED_PATTERNS}")

if(NOT DEFINED TEST_FORBIDDEN_PATTERNS)
  set(TEST_FORBIDDEN_PATTERNS "")
endif()
string(REPLACE "," ";" TEST_FORBIDDEN_PATTERNS "${TEST_FORBIDDEN_PATTERNS}")

set(test_command ${TEST_BIN})
if(NOT "${TEST_ARGS}" STREQUAL "")
  separate_arguments(test_args_list NATIVE_COMMAND "${TEST_ARGS}")
  list(APPEND test_command ${test_args_list})
endif()

execute_process(
  COMMAND ${test_command}
  RESULT_VARIABLE run_status
  OUTPUT_VARIABLE run_stdout
  ERROR_VARIABLE run_stderr
)

if(NOT run_status EQUAL 0)
  message(FATAL_ERROR
    "Output-expectation run failed (status=${run_status})\n"
    "stdout:\n${run_stdout}\n"
    "stderr:\n${run_stderr}\n")
endif()

set(combined_output "${run_stdout}\n${run_stderr}")

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
