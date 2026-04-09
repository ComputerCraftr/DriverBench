if(NOT DEFINED TEST_BIN)
  message(FATAL_ERROR "TEST_BIN is required")
endif()

if(DEFINED NM_BIN AND NOT "${NM_BIN}" STREQUAL "")
  set(DB_NM_CMD "${NM_BIN}")
else()
  find_program(DB_NM_CMD NAMES llvm-nm nm)
endif()

if(NOT DB_NM_CMD)
  message(FATAL_ERROR "No nm-compatible tool available for binary symbol check")
endif()

execute_process(
  COMMAND "${DB_NM_CMD}" -a "${TEST_BIN}"
  RESULT_VARIABLE TEST_RESULT
  OUTPUT_VARIABLE TEST_STDOUT
  ERROR_VARIABLE TEST_STDERR
)

if(NOT TEST_RESULT EQUAL 0)
  message(FATAL_ERROR
    "binary symbol scan failed\nresult: ${TEST_RESULT}\nstderr:\n${TEST_STDERR}")
endif()

set(TEST_OUTPUT "${TEST_STDOUT}${TEST_STDERR}")
if(DEFINED TEST_FORBIDDEN_PATTERNS AND NOT "${TEST_FORBIDDEN_PATTERNS}" STREQUAL "")
  string(REPLACE "," ";" TEST_FORBIDDEN_LIST "${TEST_FORBIDDEN_PATTERNS}")
  foreach(PATTERN IN LISTS TEST_FORBIDDEN_LIST)
    string(STRIP "${PATTERN}" PATTERN)
    if(PATTERN STREQUAL "")
      continue()
    endif()
    string(FIND "${TEST_OUTPUT}" "${PATTERN}" PATTERN_INDEX)
    if(NOT PATTERN_INDEX EQUAL -1)
      message(FATAL_ERROR
        "found forbidden binary pattern '${PATTERN}' in ${TEST_BIN}")
    endif()
  endforeach()
endif()
