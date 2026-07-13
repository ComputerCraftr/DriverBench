cmake_minimum_required(VERSION 3.24)

include("${CMAKE_CURRENT_LIST_DIR}/TestRunnerCommon.cmake")

db_test_require_defined(TEST_BIN)
db_test_default_string(TEST_ARGS "")
db_test_run_command(output skip_reason status "${TEST_ARGS}"
                    "Structured-log command failed")
if(NOT "${skip_reason}" STREQUAL "")
    message(STATUS "Structured-log test skipped: ${skip_reason}")
    return()
endif()

string(REPLACE "\r" "" output "${output}")
string(REPLACE "\n" ";" lines "${output}")
set(project_line_count 0)
set(final_identity_found FALSE)
foreach(line IN LISTS lines)
    if(NOT line MATCHES "^\\[[A-Za-z0-9_.-]+\\]\\[(info|error)\\] ")
        continue()
    endif()
    math(EXPR project_line_count "${project_line_count} + 1")
    if(line MATCHES " event=(info_message|error_message|fatal_error) ")
        message(
            FATAL_ERROR
                "Production runtime used a compatibility log event:\n${line}")
    endif()
    if(NOT
       line
       MATCHES
       "^\\[[A-Za-z0-9_.-]+\\]\\[(info|error)\\] event=[a-z][a-z0-9_]* schema=2( |$)"
    )
        message(FATAL_ERROR "Malformed structured log line:\n${line}")
    endif()

    string(REGEX REPLACE "^\\[[A-Za-z0-9_.-]+\\]\\[(info|error)\\] " "" payload
                         "${line}")
    string(REGEX REPLACE "\"([^\"\\\\]|\\\\.)*\"" "\"quoted\"" normalized
                         "${payload}")
    if(NOT normalized MATCHES
       "^event=[a-z][a-z0-9_]* schema=2( [a-z][a-z0-9_]*=[^ ]+)*$")
        message(
            FATAL_ERROR
                "Unescaped whitespace or malformed field in structured log line:\n${line}"
        )
    endif()

    string(REGEX MATCHALL "(^| )[a-z][a-z0-9_]*=" field_tokens "${payload}")
    set(field_names "")
    foreach(field_token IN LISTS field_tokens)
        string(STRIP "${field_token}" field_token)
        string(REGEX REPLACE "=$" "" field_name "${field_token}")
        if(field_name IN_LIST field_names)
            message(FATAL_ERROR "Duplicate field '${field_name}' in:\n${line}")
        endif()
        list(APPEND field_names "${field_name}")
    endforeach()
    if(line MATCHES " event=benchmark_final ")
        foreach(
            required_field IN
            ITEMS benchmark_mode
                  renderer
                  presenter
                  execution_strategy
                  working_format
                  native_format
                  present_method)
            if(NOT required_field IN_LIST field_names)
                message(
                    FATAL_ERROR
                        "Final benchmark event is missing '${required_field}':\n${line}"
                )
            endif()
        endforeach()
        set(final_identity_found TRUE)
    endif()
endforeach()

if(project_line_count EQUAL 0)
    message(
        FATAL_ERROR "Command emitted no project structured logs:\n${output}")
endif()
if(NOT final_identity_found)
    message(FATAL_ERROR "Command emitted no typed benchmark_final event")
endif()
message(STATUS "Structured log contract passed: lines=${project_line_count}")
