include("${CMAKE_CURRENT_LIST_DIR}/TestRunnerCommon.cmake")

db_test_require_defined(TEST_BIN)
db_test_require_defined(TEST_ARGS_A)
db_test_require_defined(TEST_ARGS_B)

if(NOT DEFINED TEST_HASH_KEY OR "${TEST_HASH_KEY}" STREQUAL "")
  set(TEST_HASH_KEY "framebuffer_hash_final")
endif()

db_test_run_command(output_a skip_reason_a run_status_a "${TEST_ARGS_A}"
  "Hash-difference run failed")
if(NOT "${skip_reason_a}" STREQUAL "")
  message(STATUS "Hash-difference test skipped: ${skip_reason_a}\n${output_a}")
  return()
endif()
db_test_run_command(output_b skip_reason_b run_status_b "${TEST_ARGS_B}"
  "Hash-difference run failed")
if(NOT "${skip_reason_b}" STREQUAL "")
  message(STATUS "Hash-difference test skipped: ${skip_reason_b}\n${output_b}")
  return()
endif()

db_test_extract_hash_or_fail("output_a" "${TEST_HASH_KEY}" hash_a)
db_test_extract_hash_or_fail("output_b" "${TEST_HASH_KEY}" hash_b)

if(hash_a STREQUAL hash_b)
  message(FATAL_ERROR
    "Expected differing hashes for key '${TEST_HASH_KEY}', but both runs "
    "produced ${hash_a}\n"
    "run A:\n${output_a}\n"
    "run B:\n${output_b}\n")
endif()

message(STATUS
  "Hash difference OK for ${TEST_BIN}: ${TEST_HASH_KEY} ${hash_a} != ${hash_b}")
