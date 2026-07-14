set(DB_TEST_GLFW_ENV_SKIP_REGEX
    "Refusing forwarded X11 session|glfwInit failed|glfwCreateWindow failed|glfwCreateWindow failed for both OpenGL and OpenGL ES|BadIDChoice \\(invalid resource ID chosen for this connection\\)"
)
set(DB_TEST_GLFW_ENV_SKIP_REASON
    "environment does not provide a usable local GLFW display/context for this test"
)
set(DB_TEST_CANONICAL_SKIP_REGEX
    "DB_CTEST_OUTCOME=SKIP reason=[a-z0-9_]+ capability=[a-z0-9_]+")

function(db_test_report_skip reason capability)
    if(NOT reason MATCHES "^[a-z0-9_]+$" OR NOT capability MATCHES
                                            "^[a-z0-9_]+$")
        message(FATAL_ERROR "Invalid canonical skip: ${reason}/${capability}")
    endif()
    message(
        STATUS "DB_CTEST_OUTCOME=SKIP reason=${reason} capability=${capability}"
    )
endfunction()

function(db_test_require_defined var_name)
    if(NOT DEFINED ${var_name} OR "${${var_name}}" STREQUAL "")
        message(FATAL_ERROR "${var_name} is required")
    endif()
endfunction()

function(db_test_default_string var_name default_value)
    if(NOT DEFINED ${var_name})
        set(${var_name}
            "${default_value}"
            PARENT_SCOPE)
    endif()
endfunction()

function(db_test_parse_list_var var_name)
    if(NOT DEFINED ${var_name})
        return()
    endif()
    string(REPLACE "|" ";" parsed_value "${${var_name}}")
    string(REPLACE "," ";" parsed_value "${parsed_value}")
    set(${var_name}
        "${parsed_value}"
        PARENT_SCOPE)
endfunction()

function(db_test_parse_pipe_list_var var_name)
    if(NOT DEFINED ${var_name})
        return()
    endif()
    string(REPLACE "|" ";" parsed_value "${${var_name}}")
    set(${var_name}
        "${parsed_value}"
        PARENT_SCOPE)
endfunction()

function(db_test_assert_substring_contains text required context)
    string(FIND "${text}" "${required}" match_index)
    if(match_index EQUAL -1)
        message(
            FATAL_ERROR
                "Required substring '${required}' not found in ${context}.\n"
                "text:\n${text}\n")
    endif()
endfunction()

function(db_test_assert_substring_not_contains text forbidden context)
    string(FIND "${text}" "${forbidden}" match_index)
    if(NOT match_index EQUAL -1)
        message(
            FATAL_ERROR
                "Forbidden substring '${forbidden}' was found in ${context}.\n"
                "text:\n${text}\n")
    endif()
endfunction()

function(db_test_normalize_identifier input_text out_var)
    string(TOLOWER "${input_text}" normalized)
    string(REGEX REPLACE "[^a-z0-9]+" "_" normalized "${normalized}")
    string(REGEX REPLACE "_+" "_" normalized "${normalized}")
    string(REGEX REPLACE "^_|_$" "" normalized "${normalized}")
    set(${out_var}
        "${normalized}"
        PARENT_SCOPE)
endfunction()

function(db_test_extract_field_or_empty text field_name out_value)
    set(field_prefix "${field_name}=")
    string(FIND "${text}" "${field_prefix}" prefix_index)
    if(prefix_index EQUAL -1)
        set(${out_value}
            ""
            PARENT_SCOPE)
        return()
    endif()

    string(LENGTH "${field_prefix}" prefix_length)
    math(EXPR value_start "${prefix_index} + ${prefix_length}")
    string(SUBSTRING "${text}" ${value_start} -1 value_remainder)

    string(FIND "${value_remainder}" " " space_index)
    string(FIND "${value_remainder}" "," comma_index)
    string(FIND "${value_remainder}" "\n" newline_index)
    set(value_end -1)
    foreach(candidate_index ${space_index} ${comma_index} ${newline_index})
        if((candidate_index GREATER -1)
           AND ((value_end EQUAL -1) OR (candidate_index LESS value_end)))
            set(value_end ${candidate_index})
        endif()
    endforeach()

    if(value_end EQUAL -1)
        set(field_value "${value_remainder}")
    else()
        string(SUBSTRING "${value_remainder}" 0 ${value_end} field_value)
    endif()
    string(STRIP "${field_value}" field_value)
    set(${out_value}
        "${field_value}"
        PARENT_SCOPE)
endfunction()

function(db_test_assert_field_equals text field_name expected context)
    db_test_extract_field_or_empty("${text}" "${field_name}" actual_value)
    if("${actual_value}" STREQUAL "")
        message(
            FATAL_ERROR
                "Required field '${field_name}' not found in ${context}.\n"
                "text:\n${text}\n")
    endif()
    if(NOT "${actual_value}" STREQUAL "${expected}")
        message(
            FATAL_ERROR
                "Field '${field_name}' mismatch in ${context}: expected '${expected}', got '${actual_value}'.\n"
                "text:\n${text}\n")
    endif()
endfunction()

function(db_test_assert_field_not_equals_if_present text field_name forbidden
         context)
    db_test_extract_field_or_empty("${text}" "${field_name}" actual_value)
    if("${actual_value}" STREQUAL "")
        return()
    endif()
    if("${actual_value}" STREQUAL "${forbidden}")
        message(
            FATAL_ERROR
                "Field '${field_name}' matched forbidden value '${forbidden}' in ${context}.\n"
                "text:\n${text}\n")
    endif()
endfunction()

function(db_test_run_command out_output out_skip_reason out_status args_string
         failure_label)
    set(test_command ${TEST_BIN})
    if(NOT "${args_string}" STREQUAL "")
        separate_arguments(test_args_list NATIVE_COMMAND "${args_string}")
        list(APPEND test_command ${test_args_list})
    endif()

    execute_process(
        COMMAND ${test_command}
        RESULT_VARIABLE run_status
        OUTPUT_VARIABLE run_stdout
        ERROR_VARIABLE run_stderr)

    set(run_combined_output "${run_stdout}\n${run_stderr}")
    set(skip_reason "")
    if(NOT run_status EQUAL 0 AND run_combined_output MATCHES
                                  "${DB_TEST_GLFW_ENV_SKIP_REGEX}")
        set(skip_reason "glfw_environment_unavailable")
        db_test_report_skip("${skip_reason}" "glfw_context")
    endif()

    set(${out_output}
        "${run_combined_output}"
        PARENT_SCOPE)
    set(${out_skip_reason}
        "${skip_reason}"
        PARENT_SCOPE)
    set(${out_status}
        "${run_status}"
        PARENT_SCOPE)

    if(NOT run_status EQUAL 0 AND "${skip_reason}" STREQUAL "")
        message(
            FATAL_ERROR "${failure_label} (status=${run_status})\n"
                        "stdout:\n${run_stdout}\n" "stderr:\n${run_stderr}\n")
    endif()
endfunction()

function(db_test_extract_hash_or_fail output_var_name hash_key out_hash_value)
    set(output_text "${${output_var_name}}")
    string(REGEX MATCH "${hash_key}=0x[0-9a-fA-F]+" hash_match "${output_text}")
    if(hash_match STREQUAL "")
        message(FATAL_ERROR "Hash key '${hash_key}' not found in output.\n"
                            "output:\n${output_text}\n")
    endif()
    string(REGEX REPLACE "^${hash_key}=" "" hash_value "${hash_match}")
    set(${out_hash_value}
        "${hash_value}"
        PARENT_SCOPE)
endfunction()
