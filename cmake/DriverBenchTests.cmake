include("${CMAKE_CURRENT_LIST_DIR}/TestRunnerCommon.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/DriverBenchTestCapabilities.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/DriverBenchAlternateSuites.cmake")

function(db_add_determinism_test
         test_name test_bin test_args hash_checks test_labels)
  add_test(
    NAME ${test_name}
    COMMAND ${CMAKE_COMMAND}
      -DTEST_BIN=${test_bin}
      -DTEST_ARGS=${test_args}
      -DTEST_HASH_CHECKS=${hash_checks}
      -P ${CMAKE_SOURCE_DIR}/cmake/RunDeterminismTest.cmake
  )
  set_tests_properties(${test_name} PROPERTIES LABELS "${test_labels}")
endfunction()

function(db_add_hash_equivalence_triplet_test
         test_name test_bin test_args_a test_args_b test_args_c
         hash_checks test_labels)
  add_test(
    NAME ${test_name}
    COMMAND ${CMAKE_COMMAND}
      -DTEST_BIN=${test_bin}
      -DTEST_ARGS=${test_args_a}
      -DTEST_ARGS_B=${test_args_b}
      -DTEST_ARGS_C=${test_args_c}
      -DTEST_HASH_CHECKS=${hash_checks}
      -P ${CMAKE_SOURCE_DIR}/cmake/RunDeterminismTest.cmake
  )
  set_tests_properties(${test_name} PROPERTIES LABELS "${test_labels}")
endfunction()

function(db_add_hash_difference_test
         test_name test_bin test_args_a test_args_b hash_key test_labels)
  add_test(
    NAME ${test_name}
    COMMAND ${CMAKE_COMMAND}
      -DTEST_BIN=${test_bin}
      -DTEST_ARGS_A=${test_args_a}
      -DTEST_ARGS_B=${test_args_b}
      -DTEST_HASH_KEY=${hash_key}
      -P ${CMAKE_SOURCE_DIR}/cmake/RunHashDifferenceTest.cmake
  )
  set_tests_properties(${test_name} PROPERTIES LABELS "${test_labels}")
endfunction()

function(db_add_output_expectation_test
         test_name test_bin test_args required_patterns forbidden_patterns
         test_labels)
  add_test(
    NAME ${test_name}
    COMMAND ${CMAKE_COMMAND}
      -DTEST_BIN=${test_bin}
      -DTEST_ARGS=${test_args}
      -DTEST_REQUIRED_PATTERNS=${required_patterns}
      -DTEST_FORBIDDEN_PATTERNS=${forbidden_patterns}
      -P ${CMAKE_SOURCE_DIR}/cmake/RunOutputExpectationTest.cmake
  )
  set_tests_properties(${test_name} PROPERTIES LABELS "${test_labels}")
endfunction()

function(db_add_command_expectation_test
         test_name test_bin test_args test_exit_code required_patterns
         forbidden_patterns test_labels)
  add_test(
    NAME ${test_name}
    COMMAND ${CMAKE_COMMAND}
      -DTEST_BIN=${test_bin}
      -DTEST_ARGS=${test_args}
      -DTEST_EXIT_CODE=${test_exit_code}
      -DTEST_REQUIRED_PATTERNS=${required_patterns}
      -DTEST_FORBIDDEN_PATTERNS=${forbidden_patterns}
      -P ${CMAKE_SOURCE_DIR}/cmake/RunCommandExpectationTest.cmake
  )
  set_tests_properties(${test_name} PROPERTIES LABELS "${test_labels}")
endfunction()

function(db_add_binary_pattern_check_test
         test_name test_bin forbidden_patterns test_labels)
  add_test(
    NAME ${test_name}
    COMMAND ${CMAKE_COMMAND}
      -DTEST_BIN=${test_bin}
      -DNM_BIN=${CMAKE_NM}
      -DTEST_FORBIDDEN_PATTERNS=${forbidden_patterns}
      -P ${CMAKE_SOURCE_DIR}/cmake/RunBinaryPatternCheck.cmake
  )
  set_tests_properties(${test_name} PROPERTIES LABELS "${test_labels}")
endfunction()

function(db_add_unit_binary_test test_name test_bin test_labels)
  add_test(
    NAME ${test_name}
    COMMAND ${CMAKE_COMMAND}
      -DTEST_BIN=${test_bin}
      -DTEST_ARGS=
      -DTEST_EXIT_CODE=0
      -DTEST_REQUIRED_PATTERNS=
      -DTEST_FORBIDDEN_PATTERNS=
      -P ${CMAKE_SOURCE_DIR}/cmake/RunCommandExpectationTest.cmake
  )
  set_tests_properties(${test_name} PROPERTIES LABELS "${test_labels}")
endfunction()

function(db_test_apply_skip_regex test_name skip_regex)
  if(NOT "${skip_regex}" STREQUAL "")
    set_tests_properties(${test_name} PROPERTIES
      SKIP_REGULAR_EXPRESSION "${skip_regex}"
    )
  endif()
endfunction()

function(db_suite_make_test_name out_var test_prefix base_name)
  if("${test_prefix}" STREQUAL "")
    set(${out_var} "${base_name}" PARENT_SCOPE)
  else()
    set(${out_var} "${test_prefix}${base_name}" PARENT_SCOPE)
  endif()
endfunction()

function(db_suite_compose_labels out_var base_labels extra_labels)
  if("${extra_labels}" STREQUAL "")
    set(${out_var} "${base_labels}" PARENT_SCOPE)
  else()
    set(${out_var} "${base_labels};${extra_labels}" PARENT_SCOPE)
  endif()
endfunction()

function(db_define_release_suite
         suite_id test_prefix test_bin unit_bin extra_labels glfw_enabled)
  set(db_suite_ids "${DB_TEST_SUITE_IDS}")
  list(APPEND db_suite_ids "${suite_id}")
  set(DB_TEST_SUITE_IDS "${db_suite_ids}" PARENT_SCOPE)
  set(DB_TEST_SUITE_${suite_id}_PREFIX "${test_prefix}" PARENT_SCOPE)
  set(DB_TEST_SUITE_${suite_id}_BINARY "${test_bin}" PARENT_SCOPE)
  set(DB_TEST_SUITE_${suite_id}_UNIT_BINARY "${unit_bin}" PARENT_SCOPE)
  set(DB_TEST_SUITE_${suite_id}_EXTRA_LABELS "${extra_labels}" PARENT_SCOPE)
  set(DB_TEST_SUITE_${suite_id}_GLFW_ENABLED "${glfw_enabled}" PARENT_SCOPE)
endfunction()

function(db_register_release_suite suite_id)
  set(db_prefix "${DB_TEST_SUITE_${suite_id}_PREFIX}")
  set(db_test_bin "${DB_TEST_SUITE_${suite_id}_BINARY}")
  set(db_unit_bin "${DB_TEST_SUITE_${suite_id}_UNIT_BINARY}")
  set(db_extra_labels "${DB_TEST_SUITE_${suite_id}_EXTRA_LABELS}")
  set(db_glfw_enabled "${DB_TEST_SUITE_${suite_id}_GLFW_ENABLED}")

  db_suite_compose_labels(db_regression_labels "regression" "${db_extra_labels}")
  db_suite_compose_labels(db_cli_labels "cli;regression" "${db_extra_labels}")
  db_suite_compose_labels(db_unit_labels "unit" "${db_extra_labels}")
  db_suite_compose_labels(db_golden_labels "golden;regression" "${db_extra_labels}")

  if("${db_prefix}" STREQUAL "")
    set(db_test_name "unit_driverbench_native")
  else()
    db_suite_make_test_name(db_test_name "${db_prefix}" "unit_driverbench")
  endif()
  db_add_unit_binary_test("${db_test_name}" "${db_unit_bin}" "${db_unit_labels}")

  db_suite_make_test_name(db_test_name "${db_prefix}" "regression_cli_help_text")
  db_add_command_expectation_test(
    "${db_test_name}" "${db_test_bin}" "--help" 0
    "--backbuffer-draw-mode,--present-buffer-mode,Build-time GLFW provider:" ""
    "${db_cli_labels}"
  )

  db_suite_make_test_name(db_test_name "${db_prefix}" "regression_cli_invalid_present_mode_cpu_offscreen")
  db_add_command_expectation_test(
    "${db_test_name}" "${db_test_bin}"
    "--display offscreen --api cpu --present-buffer-mode ring" 1
    "--present-buffer-mode is only supported for CPU with --display glfw_window" ""
    "${db_cli_labels}"
  )

  db_suite_make_test_name(db_test_name "${db_prefix}" "regression_cli_invalid_api_value")
  db_add_command_expectation_test(
    "${db_test_name}" "${db_test_bin}" "--display offscreen --api nope" 1
    "Unsupported api: nope" "" "${db_cli_labels}"
  )

  db_suite_make_test_name(db_test_name "${db_prefix}" "regression_release_binary_no_test_symbol_leaks")
  db_add_binary_pattern_check_test(
    "${db_test_name}" "${db_test_bin}"
    "db_cli_test_run_all,db_gl_shadow_present_test_run_all,db_snake_optimizer_test_run_all,driverbench_unit_tests,renderer_snake_test_support,db_snake_test_"
    "${db_regression_labels}"
  )

  foreach(db_cpu_case
          "snake_grid|snake_grid|${DB_DETERMINISM_SNAKE_ARGS}|state_hash_aggregate=0xa84f5f681a12809d,bo_hash_aggregate=0xb748399eb94eb7db"
          "gradient_fill|gradient_fill|${DB_DETERMINISM_FRAME_LIMIT}|state_hash_aggregate=0x694defa6794385e5,bo_hash_aggregate=0x3752a70a8248d9d8"
          "snake_shapes|snake_shapes|${DB_DETERMINISM_SNAKE_ARGS}|state_hash_aggregate=0x90f208d3f4e2b13f,bo_hash_aggregate=0x60d8aea2dcf959e3"
          "gradient_sweep|gradient_sweep|${DB_DETERMINISM_FRAME_LIMIT}|state_hash_aggregate=0xea73cec6de493fd6,bo_hash_aggregate=0x3752a70a8248d9d8"
          "bands|bands|${DB_DETERMINISM_FRAME_LIMIT}|state_hash_aggregate=0x170b3eb26cd1c04c,bo_hash_aggregate=0x5f2154063ac1c3bd")
    string(REPLACE "|" ";" db_cpu_case_parts "${db_cpu_case}")
    list(GET db_cpu_case_parts 0 db_base_name)
    list(GET db_cpu_case_parts 1 db_benchmark_mode)
    list(GET db_cpu_case_parts 2 db_extra_args)
    list(GET db_cpu_case_parts 3 db_hash_checks)
    db_suite_make_test_name(db_test_name "${db_prefix}" "determinism_cpu_renderer_${db_base_name}")
    db_add_determinism_test(
      "${db_test_name}" "${db_test_bin}"
      "${DB_DETERMINISM_CPU_OFFSCREEN_PREFIX} --benchmark-mode ${db_benchmark_mode} ${DB_DETERMINISM_COMMON_ARGS} ${DB_DETERMINISM_HASH} ${DB_DETERMINISM_HASH_REPORT} ${db_extra_args}"
      "${db_hash_checks}" "${db_golden_labels}"
    )
  endforeach()

  foreach(db_speed_case
          "gradient_fill|0xfe4471a7fe601b04"
          "gradient_sweep|0x00a1e8f01b888a89")
    string(REPLACE "|" ";" db_speed_case_parts "${db_speed_case}")
    list(GET db_speed_case_parts 0 db_benchmark_mode)
    list(GET db_speed_case_parts 1 db_expected_hash)
    db_suite_make_test_name(db_test_name "${db_prefix}" "determinism_cpu_${db_benchmark_mode}_speed_equivalence_triplet")
    db_add_hash_equivalence_triplet_test(
      "${db_test_name}" "${db_test_bin}"
      "${DB_DETERMINISM_CPU_OFFSCREEN_PREFIX} --benchmark-mode ${db_benchmark_mode} ${DB_DETERMINISM_COMMON_ARGS} ${DB_DETERMINISM_PIXEL_FINAL_ARGS} --bench-speed 1 --frame-limit 800"
      "${DB_DETERMINISM_CPU_OFFSCREEN_PREFIX} --benchmark-mode ${db_benchmark_mode} ${DB_DETERMINISM_COMMON_ARGS} ${DB_DETERMINISM_PIXEL_FINAL_ARGS} --bench-speed 2 --frame-limit 400"
      "${DB_DETERMINISM_CPU_OFFSCREEN_PREFIX} --benchmark-mode ${db_benchmark_mode} ${DB_DETERMINISM_COMMON_ARGS} ${DB_DETERMINISM_PIXEL_FINAL_ARGS} --bench-speed 800 --frame-limit 1"
      "bo_hash_final=${db_expected_hash}" "${db_golden_labels}"
    )
  endforeach()

  if(db_glfw_enabled)
    db_suite_make_test_name(db_test_name "${db_prefix}" "regression_hidden_glfw_cpu_bands_framebuffer_progress")
    db_add_hash_difference_test(
      "${db_test_name}" "${db_test_bin}"
      "--api cpu --display glfw_window --glfw-hidden-window 1 --benchmark-mode bands --vsync 0 --fps-cap 0 --frame-limit 1 --hash both --hash-report final"
      "--api cpu --display glfw_window --glfw-hidden-window 1 --benchmark-mode bands --vsync 0 --fps-cap 0 --frame-limit 5 --hash both --hash-report final"
      "framebuffer_hash_final" "${db_regression_labels}"
    )
    db_test_apply_skip_regex("${db_test_name}" "${DB_TEST_GLFW_ENV_SKIP_REGEX}")

    db_suite_make_test_name(db_test_name "${db_prefix}" "regression_hidden_glfw_cpu_backend_identity")
    db_add_output_expectation_test(
      "${db_test_name}" "${db_test_bin}"
      "--api cpu --display glfw_window --glfw-hidden-window 1 --benchmark-mode bands --vsync 0 --fps-cap 0 --frame-limit 1"
      "glfw window visibility=hidden,backend=display_glfw_window_cpu_renderer"
      "backend=display_offscreen" "${db_cli_labels}"
    )
    db_test_apply_skip_regex("${db_test_name}" "${DB_TEST_GLFW_ENV_SKIP_REGEX}")

    db_suite_make_test_name(db_test_name "${db_prefix}" "regression_cpu_offscreen_backend_identity")
    db_add_output_expectation_test(
      "${db_test_name}" "${db_test_bin}"
      "--api cpu --display offscreen --benchmark-mode bands --vsync 0 --fps-cap 0 --frame-limit 1"
      "backend=display_offscreen"
      "glfw window visibility=hidden,backend=display_glfw_window_cpu_renderer"
      "${db_cli_labels}"
    )

    foreach(db_state_case
            "snake_grid|0xa84f5f681a12809d"
            "snake_rect|0x61cd6f247ff98d89"
            "snake_shapes|0x90f208d3f4e2b13f")
      string(REPLACE "|" ";" db_state_case_parts "${db_state_case}")
      list(GET db_state_case_parts 0 db_benchmark_mode)
      list(GET db_state_case_parts 1 db_expected_hash)
      db_suite_make_test_name(db_test_name "${db_prefix}" "determinism_cross_renderer_${db_benchmark_mode}_state_triplet")
      db_add_hash_equivalence_triplet_test(
        "${db_test_name}" "${db_test_bin}"
        "${DB_DETERMINISM_CPU_OFFSCREEN_PREFIX} --benchmark-mode ${db_benchmark_mode} ${DB_DETERMINISM_COMMON_ARGS} ${DB_DETERMINISM_STATE_AGGREGATE_ARGS} ${DB_DETERMINISM_SNAKE_ARGS}"
        "${DB_DETERMINISM_GL1_OFFSCREEN_PREFIX} --benchmark-mode ${db_benchmark_mode} ${DB_DETERMINISM_COMMON_ARGS} ${DB_DETERMINISM_STATE_AGGREGATE_ARGS} ${DB_DETERMINISM_SNAKE_ARGS}"
        "${DB_DETERMINISM_GL3_OFFSCREEN_PREFIX} --benchmark-mode ${db_benchmark_mode} ${DB_DETERMINISM_COMMON_ARGS} ${DB_DETERMINISM_STATE_AGGREGATE_ARGS} ${DB_DETERMINISM_SNAKE_ARGS}"
        "state_hash_aggregate=${db_expected_hash}" "${db_golden_labels}"
      )
      db_test_apply_skip_regex("${db_test_name}" "${DB_TEST_GLFW_ENV_SKIP_REGEX}")
    endforeach()

    db_suite_make_test_name(db_test_name "${db_prefix}" "determinism_offscreen_gl1_5")
    db_add_determinism_test(
      "${db_test_name}" "${db_test_bin}"
      "${DB_TEST_RUNTIME_GL1_OFFSCREEN_ARGS}"
      "${DB_TEST_RUNTIME_GL1_OFFSCREEN_HASH_CHECKS}" "${db_golden_labels}"
    )
    db_test_apply_skip_regex("${db_test_name}" "${DB_TEST_GLFW_ENV_SKIP_REGEX}")

    db_suite_make_test_name(db_test_name "${db_prefix}" "determinism_offscreen_gl3_3_fbo")
    db_add_determinism_test(
      "${db_test_name}" "${db_test_bin}"
      "${DB_DETERMINISM_GL3_OFFSCREEN_PREFIX} --benchmark-mode snake_grid ${DB_DETERMINISM_COMMON_ARGS} ${DB_DETERMINISM_HASH} ${DB_DETERMINISM_HASH_REPORT} ${DB_DETERMINISM_SNAKE_ARGS}"
      "state_hash_aggregate=0xa84f5f681a12809d,fbo_hash16f_aggregate=0xbdf6bad5fa808825"
      "${db_golden_labels}"
    )
    db_test_apply_skip_regex("${db_test_name}" "${DB_TEST_GLFW_ENV_SKIP_REGEX}")

    if(NOT DB_TEST_RUNTIME_GL1_DIRTY_VARIANT_ARGS STREQUAL "")
      db_suite_make_test_name(db_test_name "${db_prefix}" "determinism_offscreen_gl1_5_dirty_macos")
      db_add_determinism_test(
        "${db_test_name}" "${db_test_bin}"
        "${DB_TEST_RUNTIME_GL1_DIRTY_VARIANT_ARGS}"
        "${DB_TEST_RUNTIME_GL1_DIRTY_VARIANT_HASH_CHECKS}" "${db_golden_labels}"
      )
      db_test_apply_skip_regex("${db_test_name}" "${DB_TEST_GLFW_ENV_SKIP_REGEX}")
    endif()
  endif()
endfunction()

function(db_register_driverbench_tests)
  if(NOT DB_IS_RELEASE_LIKE)
    message(STATUS
      "Non-release build detected: full ctest may be slow; Release is the "
      "default validation target for renderer performance regressions."
    )
  endif()

  add_test(
    NAME source_file_line_limit
    COMMAND ${CMAKE_COMMAND}
      -DSOURCE_ROOT=${CMAKE_SOURCE_DIR}
      -DLINE_LIMIT=${DB_SOURCE_FILE_LINE_LIMIT}
      -P ${CMAKE_SOURCE_DIR}/cmake/CheckSourceFileLineLimit.cmake
  )
  set_tests_properties(source_file_line_limit PROPERTIES LABELS "regression")

  add_test(
    NAME source_no_direct_recursion
    COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_SOURCE_DIR}/cmake/CheckNoDirectRecursion.py
      ${CMAKE_SOURCE_DIR}
  )
  set_tests_properties(source_no_direct_recursion PROPERTIES LABELS "regression")

  add_test(
    NAME source_platform_policy_cmake
    COMMAND ${CMAKE_COMMAND}
      -DSOURCE_ROOT=${CMAKE_SOURCE_DIR}
      -DRULE_SET=cmake_platform_policy
      -P ${CMAKE_SOURCE_DIR}/cmake/CheckPlatformPolicyBoundaries.cmake
  )
  set_tests_properties(source_platform_policy_cmake PROPERTIES LABELS "regression")

  add_test(
    NAME source_platform_policy_display_glfw
    COMMAND ${CMAKE_COMMAND}
      -DSOURCE_ROOT=${CMAKE_SOURCE_DIR}
      -DRULE_SET=display_glfw_policy
      -P ${CMAKE_SOURCE_DIR}/cmake/CheckPlatformPolicyBoundaries.cmake
  )
  set_tests_properties(source_platform_policy_display_glfw PROPERTIES LABELS "regression")

  add_test(
    NAME source_platform_policy_renderer_gl_upload
    COMMAND ${CMAKE_COMMAND}
      -DSOURCE_ROOT=${CMAKE_SOURCE_DIR}
      -DRULE_SET=renderer_gl_upload_policy
      -P ${CMAKE_SOURCE_DIR}/cmake/CheckPlatformPolicyBoundaries.cmake
  )
  set_tests_properties(source_platform_policy_renderer_gl_upload PROPERTIES LABELS "regression")

  add_test(
    NAME source_platform_policy_proc_loading
    COMMAND ${CMAKE_COMMAND}
      -DSOURCE_ROOT=${CMAKE_SOURCE_DIR}
      -DRULE_SET=platform_proc_loading
      -P ${CMAKE_SOURCE_DIR}/cmake/CheckPlatformPolicyBoundaries.cmake
  )
  set_tests_properties(source_platform_policy_proc_loading PROPERTIES LABELS "regression")

  add_test(
    NAME source_logging_policy
    COMMAND ${CMAKE_COMMAND}
      -DSOURCE_ROOT=${CMAKE_SOURCE_DIR}
      -DRULE_SET=logging_policy
      -P ${CMAKE_SOURCE_DIR}/cmake/CheckPlatformPolicyBoundaries.cmake
  )
  set_tests_properties(source_logging_policy PROPERTIES LABELS "regression")

  add_test(
    NAME source_cmake_test_registration_policy
    COMMAND ${CMAKE_COMMAND}
      -DSOURCE_ROOT=${CMAKE_SOURCE_DIR}
      -P ${CMAKE_SOURCE_DIR}/cmake/CheckCMakeTestRegistrationPolicy.cmake
  )
  set_tests_properties(source_cmake_test_registration_policy PROPERTIES LABELS "regression")

  add_test(
    NAME config_glfw_provider_default_vendored
    COMMAND ${CMAKE_COMMAND}
      -DSOURCE_ROOT=${CMAKE_SOURCE_DIR}
      -DTEST_BINARY_DIR=${CMAKE_BINARY_DIR}/ctest_glfw_provider_vendored
      -DTEST_PROVIDER=vendored
      -DEXPECT_GLFW_AVAILABLE=ON
      -DEXPECT_PROVIDER_RESOLVED=vendored
      -DTEST_GENERATOR=${CMAKE_GENERATOR}
      -DTEST_BUILD_TYPE=${CMAKE_BUILD_TYPE}
      -DTEST_C_COMPILER=${CMAKE_C_COMPILER}
      -DTEST_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}
      -P ${CMAKE_SOURCE_DIR}/cmake/CheckGLFWProviderConfig.cmake
  )
  set_tests_properties(config_glfw_provider_default_vendored PROPERTIES
    LABELS "regression")

  add_test(
    NAME config_glfw_provider_off
    COMMAND ${CMAKE_COMMAND}
      -DSOURCE_ROOT=${CMAKE_SOURCE_DIR}
      -DTEST_BINARY_DIR=${CMAKE_BINARY_DIR}/ctest_glfw_provider_off
      -DTEST_PROVIDER=off
      -DEXPECT_GLFW_AVAILABLE=OFF
      -DEXPECT_PROVIDER_RESOLVED=off
      -DTEST_GENERATOR=${CMAKE_GENERATOR}
      -DTEST_BUILD_TYPE=${CMAKE_BUILD_TYPE}
      -DTEST_C_COMPILER=${CMAKE_C_COMPILER}
      -DTEST_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}
      -P ${CMAKE_SOURCE_DIR}/cmake/CheckGLFWProviderConfig.cmake
  )
  set_tests_properties(config_glfw_provider_off PROPERTIES
    LABELS "regression")

  add_executable(driverbench_unit_tests
    tests/test_main.c
    tests/test_cli.c
    tests/test_core_logging.c
    tests/test_display_gl_runtime.c
    tests/test_frame_delta.c
    tests/test_gl_shadow_present.c
    tests/test_snake_history.c
    tests/test_snake_optimizer.c
  )
  db_apply_perf_options(driverbench_unit_tests)
  target_compile_definitions(driverbench_unit_tests PRIVATE ${DB_DRIVERBENCH_DEFS})
  target_include_directories(driverbench_unit_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/tests
  )
  target_link_libraries(driverbench_unit_tests PRIVATE
    driverbench_app
    driverbench_cli_core
    driverbench_render_core
    driverbench_core
    driverbench_app
    ${DB_DRIVERBENCH_LIBS}
  )

  set(DB_DETERMINISM_CPU_OFFSCREEN_PREFIX "--api cpu --display offscreen")
  set(DB_DETERMINISM_GL1_OFFSCREEN_PREFIX
    "--api opengl --renderer gl1_5_gles1_1 --display offscreen --vsync 0"
  )
  set(DB_DETERMINISM_GL3_OFFSCREEN_PREFIX
    "--api opengl --renderer gl3_3 --display offscreen --vsync 0"
  )
  set(DB_DETERMINISM_COMMON_ARGS "--random-seed 123456 --fps-cap 0 --cpu-hdr 1")
  set(DB_DETERMINISM_HASH "--hash both")
  set(DB_DETERMINISM_HASH_REPORT "--hash-report aggregate")
  set(DB_DETERMINISM_STATE_AGGREGATE_ARGS "--hash state --hash-report aggregate")
  set(DB_DETERMINISM_PIXEL_FINAL_ARGS "--hash pixel --hash-report final")
  set(DB_DETERMINISM_FRAME_LIMIT "--frame-limit 600")
  set(DB_DETERMINISM_SNAKE_ARGS "--bench-speed 1024 --frame-limit 240")
  set(DB_DETERMINISM_GL1_OFFSCREEN_FULL_GOLDEN_HASH_CHECKS
    "state_hash_aggregate=0xa84f5f681a12809d,framebuffer_hash_aggregate=0x7baea8c7f6f53d1c"
  )
  set(DB_DETERMINISM_GL1_OFFSCREEN_DIRTY_GOLDEN_HASH_CHECKS
    "state_hash_aggregate=0xa84f5f681a12809d,framebuffer_hash_aggregate=0xdc6bc6acdc4ce3ec"
  )
  db_configure_test_runtime_capabilities()
  db_discover_alternate_release_suites()

  set(DB_TEST_SUITE_IDS "")
  set(db_native_glfw_enabled OFF)
  if(DB_BUILD_GLFW_WINDOW_DISPLAY AND NOT (DB_GLFW_LINK_LIB STREQUAL ""))
    set(db_native_glfw_enabled ON)
  endif()
  db_define_release_suite(
    native
    ""
    "$<TARGET_FILE:${DB_UNIFIED_TARGET}>"
    "$<TARGET_FILE:driverbench_unit_tests>"
    ""
    "${db_native_glfw_enabled}"
  )

  foreach(db_alt_lane_id IN LISTS DB_TEST_ALTERNATE_LANE_IDS)
    if(DB_TEST_ALT_${db_alt_lane_id}_ENABLED)
      db_define_release_suite(
        "${db_alt_lane_id}"
        "alt_${db_alt_lane_id}_"
        "${DB_TEST_ALT_${db_alt_lane_id}_BINARY}"
        "${DB_TEST_ALT_${db_alt_lane_id}_UNIT_BINARY}"
        "alternate;${db_alt_lane_id}"
        "${DB_TEST_ALT_${db_alt_lane_id}_GLFW_AVAILABLE}"
      )
    endif()
  endforeach()

  foreach(db_suite_id IN LISTS DB_TEST_SUITE_IDS)
    db_register_release_suite("${db_suite_id}")
  endforeach()
endfunction()
