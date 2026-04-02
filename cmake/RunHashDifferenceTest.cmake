if(NOT DEFINED TEST_BIN)
  message(FATAL_ERROR "TEST_BIN is required")
endif()

if(NOT DEFINED TEST_ARGS_A OR "${TEST_ARGS_A}" STREQUAL "")
  message(FATAL_ERROR "TEST_ARGS_A is required")
endif()

if(NOT DEFINED TEST_ARGS_B OR "${TEST_ARGS_B}" STREQUAL "")
  message(FATAL_ERROR "TEST_ARGS_B is required")
endif()

if(NOT DEFINED TEST_HASH_KEY OR "${TEST_HASH_KEY}" STREQUAL "")
  set(TEST_HASH_KEY "framebuffer_hash_final")
endif()

function(db_run_once out_output args_string)
  set(test_command ${TEST_BIN})
  separate_arguments(test_args_list NATIVE_COMMAND "${args_string}")
  list(APPEND test_command ${test_args_list})
  execute_process(
    COMMAND ${test_command}
    RESULT_VARIABLE run_status
    OUTPUT_VARIABLE run_stdout
    ERROR_VARIABLE run_stderr
  )
  if(NOT run_status EQUAL 0)
    message(FATAL_ERROR
      "Hash-difference run failed (status=${run_status})\n"
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

db_run_once(output_a "${TEST_ARGS_A}")
db_run_once(output_b "${TEST_ARGS_B}")

db_extract_hash_or_fail("${output_a}" "${TEST_HASH_KEY}" hash_a)
db_extract_hash_or_fail("${output_b}" "${TEST_HASH_KEY}" hash_b)

if(hash_a STREQUAL hash_b)
  message(FATAL_ERROR
    "Expected differing hashes for key '${TEST_HASH_KEY}', but both runs "
    "produced ${hash_a}\n"
    "run A:\n${output_a}\n"
    "run B:\n${output_b}\n")
endif()

message(STATUS
  "Hash difference OK for ${TEST_BIN}: ${TEST_HASH_KEY} ${hash_a} != ${hash_b}")
