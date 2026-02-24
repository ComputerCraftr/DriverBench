if(NOT DEFINED TEST_BIN)
  message(FATAL_ERROR "TEST_BIN is required")
endif()

if(NOT DEFINED TEST_HASH_CHECKS OR "${TEST_HASH_CHECKS}" STREQUAL "")
  if(NOT DEFINED TEST_HASH_KEY)
    set(TEST_HASH_KEY "state_hash_final")
  endif()
  if(DEFINED TEST_EXPECTED_HASH AND NOT "${TEST_EXPECTED_HASH}" STREQUAL "")
    set(TEST_HASH_CHECKS "${TEST_HASH_KEY}=${TEST_EXPECTED_HASH}")
  else()
    set(TEST_HASH_CHECKS "${TEST_HASH_KEY}")
  endif()
endif()
string(REPLACE "|" ";" TEST_HASH_CHECKS "${TEST_HASH_CHECKS}")
string(REPLACE "," ";" TEST_HASH_CHECKS "${TEST_HASH_CHECKS}")

function(db_run_once out_output args_string)
  set(test_command ${TEST_BIN})
  if(NOT "${args_string}" STREQUAL "")
    separate_arguments(test_args_list NATIVE_COMMAND "${args_string}")
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
      "Determinism run failed (status=${run_status})\n"
      "stdout:\n${run_stdout}\n"
      "stderr:\n${run_stderr}\n")
  endif()
  set(${out_output} "${run_stdout}\n${run_stderr}" PARENT_SCOPE)
endfunction()

function(db_extract_hash_or_fail output hash_key out_hash_value)
  string(REGEX MATCH "${hash_key}=0x[0-9a-fA-F]+" hash_match "${output}")
  if(hash_match STREQUAL "")
    message(FATAL_ERROR
      "Hash key '${hash_key}' not found in output.\n"
      "output:\n${output}\n")
  endif()
  string(REGEX REPLACE "^${hash_key}=" "" hash_value "${hash_match}")
  set(${out_hash_value} "${hash_value}" PARENT_SCOPE)
endfunction()

if(DEFINED TEST_ARGS AND NOT "${TEST_ARGS}" STREQUAL "")
  set(run1_args "${TEST_ARGS}")
else()
  set(run1_args "")
endif()

if(DEFINED TEST_ARGS_B AND NOT "${TEST_ARGS_B}" STREQUAL "")
  set(run2_args "${TEST_ARGS_B}")
else()
  set(run2_args "${run1_args}")
endif()

db_run_once(run1_output "${run1_args}")
db_run_once(run2_output "${run2_args}")

set(hash_summary "")
foreach(hash_check IN LISTS TEST_HASH_CHECKS)
  string(FIND "${hash_check}" "=" eq_pos)
  if(eq_pos EQUAL -1)
    set(hash_key "${hash_check}")
    set(expected_hash "")
  else()
    string(SUBSTRING "${hash_check}" 0 ${eq_pos} hash_key)
    math(EXPR expected_start "${eq_pos} + 1")
    string(SUBSTRING "${hash_check}" ${expected_start} -1 expected_hash)
  endif()

  db_extract_hash_or_fail("${run1_output}" "${hash_key}" run1_hash)
  db_extract_hash_or_fail("${run2_output}" "${hash_key}" run2_hash)

  if(NOT run1_hash STREQUAL run2_hash)
    message(FATAL_ERROR
      "Determinism mismatch for ${TEST_BIN} key '${hash_key}': ${run1_hash} != ${run2_hash}\n"
      "run1:\n${run1_output}\n"
      "run2:\n${run2_output}\n")
  endif()

  if(NOT "${expected_hash}" STREQUAL "" AND NOT run1_hash STREQUAL expected_hash)
    message(FATAL_ERROR
      "Golden hash mismatch for ${TEST_BIN} key '${hash_key}': expected ${expected_hash}, got ${run1_hash}\n"
      "run output:\n${run1_output}\n")
  endif()

  list(APPEND hash_summary "${hash_key}=${run1_hash}")
endforeach()

string(JOIN ", " hash_summary_text ${hash_summary})
message(STATUS "Determinism OK for ${TEST_BIN}: ${hash_summary_text}")
