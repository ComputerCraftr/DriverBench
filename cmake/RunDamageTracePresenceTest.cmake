cmake_minimum_required(VERSION 3.24)

include("${CMAKE_CURRENT_LIST_DIR}/TestRunnerCommon.cmake")

db_test_require_defined(TEST_BIN)
db_test_require_defined(TEST_ARGS)
db_test_require_defined(TEST_DAMAGE_BACKEND)
db_test_require_defined(TEST_DAMAGE_STAGES)
db_test_run_command(output skip_reason status "${TEST_ARGS}"
                    "Damage trace command failed")
if(NOT "${skip_reason}" STREQUAL "")
    return()
endif()

db_test_parse_list_var(TEST_DAMAGE_STAGES)
set(expected_stages "${TEST_DAMAGE_STAGES}")

function(db_damage_presence_field line key out_value)
    if("${line}" MATCHES "(^| )${key}=([^ ]+)")
        set(${out_value}
            "${CMAKE_MATCH_2}"
            PARENT_SCOPE)
    else()
        message(
            FATAL_ERROR "Malformed damage event: missing '${key}' in:\n${line}")
    endif()
endfunction()

string(REPLACE "\r" "" output "${output}")
string(REPLACE "\n" ";" lines "${output}")
set(observed_stages "")
foreach(line IN LISTS lines)
    if(NOT line MATCHES "damage_summary "
       OR NOT line MATCHES "(^| )backend=${TEST_DAMAGE_BACKEND}( |$)")
        continue()
    endif()
    db_damage_presence_field("${line}" "stage" stage)
    db_damage_presence_field("${line}" "rejected" rejected)
    db_damage_presence_field("${line}" "truncated" truncated)
    db_damage_presence_field("${line}" "result" result)
    if(NOT "${rejected}" STREQUAL "0")
        message(FATAL_ERROR "Rejected damage blocks:\n${line}")
    endif()
    if(NOT "${truncated}" STREQUAL "false")
        message(FATAL_ERROR "Truncated damage event:\n${line}")
    endif()
    if("${result}" STREQUAL "failed")
        message(FATAL_ERROR "Failed damage event:\n${line}")
    endif()
    list(APPEND observed_stages "${stage}")
endforeach()

foreach(expected IN LISTS expected_stages)
    if(NOT "${expected}" IN_LIST observed_stages)
        message(
            FATAL_ERROR
                "Missing ${TEST_DAMAGE_BACKEND} damage stage '${expected}'. "
                "Observed: ${observed_stages}\n${output}")
    endif()
endforeach()
message(
    STATUS "${TEST_DAMAGE_BACKEND} damage stages present: ${observed_stages}")
