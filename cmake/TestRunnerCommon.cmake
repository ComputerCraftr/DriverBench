set(DB_TEST_GLFW_ENV_SKIP_REGEX
  "Refusing forwarded X11 session|glfwInit failed|glfwCreateWindow failed|glfwCreateWindow failed for both OpenGL and OpenGL ES"
)
set(DB_TEST_GLFW_ENV_SKIP_REASON
  "environment does not provide a usable local GLFW display/context for this test"
)

function(db_test_require_defined var_name)
  if(NOT DEFINED ${var_name} OR "${${var_name}}" STREQUAL "")
    message(FATAL_ERROR "${var_name} is required")
  endif()
endfunction()

function(db_test_default_string var_name default_value)
  if(NOT DEFINED ${var_name})
    set(${var_name} "${default_value}" PARENT_SCOPE)
  endif()
endfunction()

function(db_test_parse_list_var var_name)
  if(NOT DEFINED ${var_name})
    return()
  endif()
  string(REPLACE "|" ";" parsed_value "${${var_name}}")
  string(REPLACE "," ";" parsed_value "${parsed_value}")
  set(${var_name} "${parsed_value}" PARENT_SCOPE)
endfunction()

function(db_test_run_command out_output out_skip_reason out_status args_string failure_label)
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

  set(run_combined_output "${run_stdout}\n${run_stderr}")
  set(skip_reason "")
  if(NOT run_status EQUAL 0 AND run_combined_output MATCHES "${DB_TEST_GLFW_ENV_SKIP_REGEX}")
    set(skip_reason "${DB_TEST_GLFW_ENV_SKIP_REASON}")
  endif()

  set(${out_output} "${run_combined_output}" PARENT_SCOPE)
  set(${out_skip_reason} "${skip_reason}" PARENT_SCOPE)
  set(${out_status} "${run_status}" PARENT_SCOPE)

  if(NOT run_status EQUAL 0 AND "${skip_reason}" STREQUAL "")
    message(FATAL_ERROR
      "${failure_label} (status=${run_status})\n"
      "stdout:\n${run_stdout}\n"
      "stderr:\n${run_stderr}\n")
  endif()
endfunction()

function(db_test_extract_hash_or_fail output_var_name hash_key out_hash_value)
  set(output_text "${${output_var_name}}")
  string(REGEX MATCH "${hash_key}=0x[0-9a-fA-F]+" hash_match "${output_text}")
  if(hash_match STREQUAL "")
    message(FATAL_ERROR
      "Hash key '${hash_key}' not found in output.\n"
      "output:\n${output_text}\n")
  endif()
  string(REGEX REPLACE "^${hash_key}=" "" hash_value "${hash_match}")
  set(${out_hash_value} "${hash_value}" PARENT_SCOPE)
endfunction()
