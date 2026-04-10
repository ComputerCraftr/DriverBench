function(db_alt_suite_parse_entries out_var raw_value)
  set(db_alt_entries "")
  if(NOT "${raw_value}" STREQUAL "")
    string(REPLACE "," ";" db_alt_entries "${raw_value}")
  endif()
  set(${out_var} "${db_alt_entries}" PARENT_SCOPE)
endfunction()

function(db_alt_suite_sanitize_id out_var lane_name)
  string(TOLOWER "${lane_name}" db_lane_lower)
  string(REGEX REPLACE "[^a-z0-9_]+" "_" db_lane_sanitized
                       "${db_lane_lower}")
  string(REGEX REPLACE "_+" "_" db_lane_sanitized "${db_lane_sanitized}")
  string(REGEX REPLACE "^_|_$" "" db_lane_sanitized "${db_lane_sanitized}")
  set(${out_var} "${db_lane_sanitized}" PARENT_SCOPE)
endfunction()

function(db_alt_suite_probe entry_text)
  string(FIND "${entry_text}" "=" db_eq_pos)
  if(db_eq_pos EQUAL -1)
    message(WARNING
      "Ignoring DB_TEST_ALTERNATE_RELEASE_ROOTS entry '${entry_text}'; "
      "expected lane=build_dir."
    )
    return()
  endif()

  string(SUBSTRING "${entry_text}" 0 ${db_eq_pos} db_lane_name)
  math(EXPR db_path_start "${db_eq_pos} + 1")
  string(SUBSTRING "${entry_text}" ${db_path_start} -1 db_build_dir)
  string(STRIP "${db_lane_name}" db_lane_name)
  string(STRIP "${db_build_dir}" db_build_dir)
  if("${db_lane_name}" STREQUAL "" OR "${db_build_dir}" STREQUAL "")
    message(WARNING
      "Ignoring DB_TEST_ALTERNATE_RELEASE_ROOTS entry '${entry_text}'; "
      "lane and build_dir are both required."
    )
    return()
  endif()

  db_alt_suite_sanitize_id(db_lane_id "${db_lane_name}")
  get_filename_component(db_build_dir_abs "${db_build_dir}" ABSOLUTE
                         BASE_DIR "${CMAKE_SOURCE_DIR}")
  set(db_cache_file "${db_build_dir_abs}/CMakeCache.txt")
  if(NOT EXISTS "${db_cache_file}")
    message(STATUS
      "Skipping alternate suite '${db_lane_name}': build root missing "
      "CMakeCache.txt (${db_build_dir_abs})."
    )
    return()
  endif()

  file(READ "${db_cache_file}" db_cache_text)

  set(db_exe_suffix "${CMAKE_EXECUTABLE_SUFFIX}")
  set(db_driverbench_bin "${db_build_dir_abs}/driverbench${db_exe_suffix}")
  set(db_unit_bin "${db_build_dir_abs}/driverbench_unit_tests${db_exe_suffix}")
  if(NOT EXISTS "${db_driverbench_bin}")
    message(STATUS
      "Skipping alternate suite '${db_lane_name}': missing ${db_driverbench_bin}."
    )
    return()
  endif()
  if(NOT EXISTS "${db_unit_bin}")
    message(STATUS
      "Skipping alternate suite '${db_lane_name}': missing ${db_unit_bin}."
    )
    return()
  endif()

  execute_process(
    COMMAND "${db_driverbench_bin}" --help
    RESULT_VARIABLE db_probe_result
    OUTPUT_VARIABLE db_probe_stdout
    ERROR_VARIABLE db_probe_stderr
    TIMEOUT 10
  )
  if(NOT db_probe_result EQUAL 0)
    set(db_probe_reason
      "Skipping alternate suite '${db_lane_name}': driverbench is not directly runnable on this host.")
    if(EXISTS "${db_driverbench_bin}" AND
       ("${db_probe_stderr}" MATCHES "no such file or directory" OR
        "${db_probe_result}" MATCHES "no such file or directory"))
      find_program(DB_ALT_READELF NAMES llvm-readelf readelf)
      if(DB_ALT_READELF)
        execute_process(
          COMMAND "${DB_ALT_READELF}" -l "${db_driverbench_bin}"
          RESULT_VARIABLE db_interp_result
          OUTPUT_VARIABLE db_interp_output
          ERROR_QUIET
        )
        if(db_interp_result EQUAL 0 AND
           db_interp_output MATCHES "Requesting program interpreter: ([^]\n]+)")
          set(db_interp_path "${CMAKE_MATCH_1}")
          string(STRIP "${db_interp_path}" db_interp_path)
          if(NOT EXISTS "${db_interp_path}")
            string(APPEND db_probe_reason
              " Missing ELF interpreter '${db_interp_path}' on the host.")
          endif()
        endif()
      endif()
    endif()
    message(STATUS
      "${db_probe_reason}\n${db_probe_stdout}\n${db_probe_stderr}"
    )
    return()
  endif()

  set(DB_TEST_ALT_${db_lane_id}_ENABLED ON PARENT_SCOPE)
  set(DB_TEST_ALT_${db_lane_id}_LABEL "${db_lane_name}" PARENT_SCOPE)
  set(DB_TEST_ALT_${db_lane_id}_BINARY "${db_driverbench_bin}" PARENT_SCOPE)
  set(DB_TEST_ALT_${db_lane_id}_UNIT_BINARY "${db_unit_bin}" PARENT_SCOPE)
  if(db_cache_text MATCHES "DB_GLFW_AVAILABLE:INTERNAL=ON")
    set(DB_TEST_ALT_${db_lane_id}_GLFW_AVAILABLE ON PARENT_SCOPE)
  else()
    set(DB_TEST_ALT_${db_lane_id}_GLFW_AVAILABLE OFF PARENT_SCOPE)
  endif()
  message(STATUS
    "Registered alternate release suite '${db_lane_name}' from ${db_build_dir_abs}."
  )

  set(db_registered_ids "${DB_TEST_ALTERNATE_LANE_IDS}")
  list(APPEND db_registered_ids "${db_lane_id}")
  set(DB_TEST_ALTERNATE_LANE_IDS "${db_registered_ids}" PARENT_SCOPE)
endfunction()

function(db_discover_alternate_release_suites)
  set(DB_TEST_ALTERNATE_LANE_IDS "")
  db_alt_suite_parse_entries(db_alt_entries
                             "${DB_TEST_ALTERNATE_RELEASE_ROOTS}")
  foreach(db_alt_entry IN LISTS db_alt_entries)
    db_alt_suite_probe("${db_alt_entry}")
  endforeach()

  set(DB_TEST_ALTERNATE_LANE_IDS "${DB_TEST_ALTERNATE_LANE_IDS}" PARENT_SCOPE)
  foreach(db_alt_lane_id IN LISTS DB_TEST_ALTERNATE_LANE_IDS)
    foreach(db_alt_suffix IN ITEMS ENABLED LABEL BINARY UNIT_BINARY GLFW_AVAILABLE)
      set(DB_TEST_ALT_${db_alt_lane_id}_${db_alt_suffix}
          "${DB_TEST_ALT_${db_alt_lane_id}_${db_alt_suffix}}" PARENT_SCOPE)
    endforeach()
  endforeach()
endfunction()
