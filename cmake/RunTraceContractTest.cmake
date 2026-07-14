include("${CMAKE_CURRENT_LIST_DIR}/TestRunnerCommon.cmake")

db_test_require_defined(TEST_BIN)
db_test_default_string(TEST_ARGS "")
db_test_default_string(TEST_GLOBAL_REQUIRED "")
db_test_default_string(TEST_GLOBAL_FORBIDDEN "")
db_test_default_string(TEST_TRACE_REQUIRED "")
db_test_default_string(TEST_TRACE_FORBIDDEN "")
db_test_parse_pipe_list_var(TEST_GLOBAL_REQUIRED)
db_test_parse_pipe_list_var(TEST_GLOBAL_FORBIDDEN)
db_test_parse_pipe_list_var(TEST_TRACE_REQUIRED)
db_test_parse_pipe_list_var(TEST_TRACE_FORBIDDEN)

db_test_run_command(output skip_reason status "${TEST_ARGS}"
                    "Trace-contract run failed")
if(NOT "${skip_reason}" STREQUAL "")
    return()
endif()

function(db_trace_find_event output event frame out_line)
    string(REPLACE "\r" "" normalized "${output}")
    string(REPLACE "\n" ";" lines "${normalized}")
    foreach(line IN LISTS lines)
        if(NOT line MATCHES "(^| )event=${event}( |$)")
            continue()
        endif()
        if(NOT "${frame}" STREQUAL "*" AND NOT line MATCHES
                                           "(^| )frame=${frame}( |$)")
            continue()
        endif()
        set(${out_line}
            "${line}"
            PARENT_SCOPE)
        return()
    endforeach()
    set(${out_line}
        ""
        PARENT_SCOPE)
endfunction()

function(db_trace_assert spec should_exist)
    string(REGEX MATCH "^([a-z][a-z0-9_]*)@([0-9*]+)\\.([a-z][a-z0-9_]*)=(.*)$"
                 matched "${spec}")
    if(matched STREQUAL "")
        message(
            FATAL_ERROR
                "Invalid trace contract '${spec}'; expected event@frame.field=value"
        )
    endif()
    set(event "${CMAKE_MATCH_1}")
    set(frame "${CMAKE_MATCH_2}")
    set(field "${CMAKE_MATCH_3}")
    set(expected "${CMAKE_MATCH_4}")
    db_trace_find_event("${output}" "${event}" "${frame}" line)
    if(line STREQUAL "")
        if(should_exist)
            message(
                FATAL_ERROR "Missing trace event ${event}@${frame}\n${output}")
        endif()
        return()
    endif()
    if(line MATCHES "(^| )${field}=([^ ]+)")
        set(actual "${CMAKE_MATCH_2}")
    else()
        set(actual "")
    endif()
    if(should_exist AND NOT "${actual}" STREQUAL "${expected}")
        message(
            FATAL_ERROR
                "Trace field mismatch ${event}@${frame}.${field}: expected "
                "'${expected}', got '${actual}'\n${line}")
    elseif(NOT should_exist AND "${actual}" STREQUAL "${expected}")
        message(
            FATAL_ERROR
                "Forbidden trace field ${event}@${frame}.${field}=${expected}\n${line}"
        )
    endif()
endfunction()

foreach(required IN LISTS TEST_GLOBAL_REQUIRED)
    db_test_assert_substring_contains("${output}" "${required}" "trace output")
endforeach()
foreach(forbidden IN LISTS TEST_GLOBAL_FORBIDDEN)
    db_test_assert_substring_not_contains("${output}" "${forbidden}"
                                          "trace output")
endforeach()
foreach(spec IN LISTS TEST_TRACE_REQUIRED)
    db_trace_assert("${spec}" TRUE)
endforeach()
foreach(spec IN LISTS TEST_TRACE_FORBIDDEN)
    db_trace_assert("${spec}" FALSE)
endforeach()

message(STATUS "Structured trace contract passed for ${TEST_BIN}")
