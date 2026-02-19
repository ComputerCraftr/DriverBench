if(NOT DEFINED TEST_BIN)
  message(FATAL_ERROR "TEST_BIN is required")
endif()

if(NOT DEFINED TEST_HASH_KEY)
  set(TEST_HASH_KEY "bo_hash_final")
endif()

function(db_run_once out_hash out_output)
  execute_process(
    COMMAND ${TEST_BIN}
    RESULT_VARIABLE run_status
    OUTPUT_VARIABLE run_stdout
    ERROR_VARIABLE run_stderr
  )
  if(NOT run_status EQUAL 0)
    message(FATAL_ERROR
      "Determinism run failed (status=${run_status})\n"
      "stdout:\n${run_stdout}\n"
      "stderr:\n${run_stderr}\n")
  endif()

  set(run_all_output "${run_stdout}\n${run_stderr}")
  string(REGEX MATCH "${TEST_HASH_KEY}=0x[0-9a-fA-F]+" hash_match "${run_all_output}")
  if(hash_match STREQUAL "")
    message(FATAL_ERROR
      "Hash key '${TEST_HASH_KEY}' not found in output.\n"
      "stdout:\n${run_stdout}\n"
      "stderr:\n${run_stderr}\n")
  endif()
  string(REGEX REPLACE "^${TEST_HASH_KEY}=" "" hash_value "${hash_match}")

  set(${out_hash} "${hash_value}" PARENT_SCOPE)
  set(${out_output} "${run_all_output}" PARENT_SCOPE)
endfunction()

db_run_once(run1_hash run1_output)
db_run_once(run2_hash run2_output)

if(NOT run1_hash STREQUAL run2_hash)
  message(FATAL_ERROR
    "Determinism mismatch for ${TEST_BIN}: ${run1_hash} != ${run2_hash}\n"
    "run1:\n${run1_output}\n"
    "run2:\n${run2_output}\n")
endif()

if(DEFINED TEST_EXPECTED_HASH AND NOT "${TEST_EXPECTED_HASH}" STREQUAL "")
  if(NOT run1_hash STREQUAL TEST_EXPECTED_HASH)
    message(FATAL_ERROR
      "Golden hash mismatch for ${TEST_BIN}: expected ${TEST_EXPECTED_HASH}, got ${run1_hash}\n"
      "run output:\n${run1_output}\n")
  endif()
endif()

message(STATUS "Determinism OK for ${TEST_BIN}: ${run1_hash}")
