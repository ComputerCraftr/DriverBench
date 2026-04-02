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

set(run_arg_sets "")
if(DEFINED TEST_ARGS AND NOT "${TEST_ARGS}" STREQUAL "")
  list(APPEND run_arg_sets "${TEST_ARGS}")
else()
  list(APPEND run_arg_sets "")
endif()

if(DEFINED TEST_ARGS_B AND NOT "${TEST_ARGS_B}" STREQUAL "")
  list(APPEND run_arg_sets "${TEST_ARGS_B}")
else()
  list(APPEND run_arg_sets "${TEST_ARGS}")
endif()

if(DEFINED TEST_ARGS_C AND NOT "${TEST_ARGS_C}" STREQUAL "")
  list(APPEND run_arg_sets "${TEST_ARGS_C}")
endif()

set(run_outputs "")
foreach(run_args IN LISTS run_arg_sets)
  db_run_once(run_output "${run_args}")
  list(APPEND run_outputs "${run_output}")
endforeach()

list(GET run_outputs 0 reference_output)

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

  db_extract_hash_or_fail("${reference_output}" "${hash_key}" reference_hash)
  set(candidate_outputs "${run_outputs}")
  list(REMOVE_AT candidate_outputs 0)
  set(run_index 1)
  foreach(run_output IN LISTS candidate_outputs)
    db_extract_hash_or_fail("${run_output}" "${hash_key}" candidate_hash)
    if(NOT reference_hash STREQUAL candidate_hash)
      message(FATAL_ERROR
        "Determinism mismatch for ${TEST_BIN} key '${hash_key}': ${reference_hash} != ${candidate_hash}\n"
        "reference:\n${reference_output}\n"
        "candidate #${run_index}:\n${run_output}\n")
    endif()
    math(EXPR run_index "${run_index} + 1")
  endforeach()

  if(NOT "${expected_hash}" STREQUAL "" AND NOT reference_hash STREQUAL expected_hash)
    message(FATAL_ERROR
      "Golden hash mismatch for ${TEST_BIN} key '${hash_key}': expected ${expected_hash}, got ${reference_hash}\n"
      "run output:\n${reference_output}\n")
  endif()

  list(APPEND hash_summary "${hash_key}=${reference_hash}")
endforeach()

string(JOIN ", " hash_summary_text ${hash_summary})
message(STATUS "Determinism OK for ${TEST_BIN}: ${hash_summary_text}")
