include("${CMAKE_CURRENT_LIST_DIR}/TestRunnerCommon.cmake")

db_test_require_defined(TEST_BIN)

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
db_test_parse_list_var(TEST_HASH_CHECKS)

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

set(run_output_count 0)
foreach(run_args IN LISTS run_arg_sets)
  db_test_run_command(run_output skip_reason run_status "${run_args}"
    "Determinism run failed")
  if(NOT "${skip_reason}" STREQUAL "")
    message(STATUS
      "Determinism test skipped: ${skip_reason}\n${run_output}")
    return()
  endif()
  set(run_output_${run_output_count} "${run_output}")
  math(EXPR run_output_count "${run_output_count} + 1")
endforeach()

if(run_output_count EQUAL 0)
  message(FATAL_ERROR "No determinism runs were executed")
endif()
set(reference_output_var "run_output_0")

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

  db_test_extract_hash_or_fail("${reference_output_var}" "${hash_key}" reference_hash)
  set(run_index 1)
  while(run_index LESS run_output_count)
    set(candidate_output_var "run_output_${run_index}")
    db_test_extract_hash_or_fail("${candidate_output_var}" "${hash_key}" candidate_hash)
    if(NOT reference_hash STREQUAL candidate_hash)
      message(FATAL_ERROR
        "Determinism mismatch for ${TEST_BIN} key '${hash_key}': ${reference_hash} != ${candidate_hash}\n"
        "reference:\n${${reference_output_var}}\n"
        "candidate #${run_index}:\n${${candidate_output_var}}\n")
    endif()
    math(EXPR run_index "${run_index} + 1")
  endwhile()

  if(NOT "${expected_hash}" STREQUAL "" AND NOT reference_hash STREQUAL expected_hash)
    message(FATAL_ERROR
      "Golden hash mismatch for ${TEST_BIN} key '${hash_key}': expected ${expected_hash}, got ${reference_hash}\n"
      "run output:\n${${reference_output_var}}\n")
  endif()

  list(APPEND hash_summary "${hash_key}=${reference_hash}")
endforeach()

string(JOIN ", " hash_summary_text ${hash_summary})
message(STATUS "Determinism OK for ${TEST_BIN}: ${hash_summary_text}")
