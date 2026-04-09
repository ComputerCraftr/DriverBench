include("${CMAKE_CURRENT_LIST_DIR}/TestRunnerCommon.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/DriverBenchTestPlatform.cmake")

function(db_add_determinism_test test_name test_args hash_checks)
  if(NOT TARGET ${DB_UNIFIED_TARGET})
    return()
  endif()
  add_test(
    NAME ${test_name}
    COMMAND ${CMAKE_COMMAND}
      -DTEST_BIN=$<TARGET_FILE:${DB_UNIFIED_TARGET}>
      -DTEST_ARGS=${test_args}
      -DTEST_HASH_CHECKS=${hash_checks}
      -P ${CMAKE_SOURCE_DIR}/cmake/RunDeterminismTest.cmake
  )
  set_tests_properties(${test_name} PROPERTIES LABELS "golden;regression")
endfunction()

function(db_add_hash_equivalence_triplet_test
         test_name test_args_a test_args_b test_args_c hash_checks)
  if(NOT TARGET ${DB_UNIFIED_TARGET})
    return()
  endif()
  add_test(
    NAME ${test_name}
    COMMAND ${CMAKE_COMMAND}
      -DTEST_BIN=$<TARGET_FILE:${DB_UNIFIED_TARGET}>
      -DTEST_ARGS=${test_args_a}
      -DTEST_ARGS_B=${test_args_b}
      -DTEST_ARGS_C=${test_args_c}
      -DTEST_HASH_CHECKS=${hash_checks}
      -P ${CMAKE_SOURCE_DIR}/cmake/RunDeterminismTest.cmake
  )
  set_tests_properties(${test_name} PROPERTIES LABELS "golden;regression")
endfunction()

function(db_add_hash_difference_test
         test_name test_args_a test_args_b hash_key)
  if(NOT TARGET ${DB_UNIFIED_TARGET})
    return()
  endif()
  add_test(
    NAME ${test_name}
    COMMAND ${CMAKE_COMMAND}
      -DTEST_BIN=$<TARGET_FILE:${DB_UNIFIED_TARGET}>
      -DTEST_ARGS_A=${test_args_a}
      -DTEST_ARGS_B=${test_args_b}
      -DTEST_HASH_KEY=${hash_key}
      -P ${CMAKE_SOURCE_DIR}/cmake/RunHashDifferenceTest.cmake
  )
  set_tests_properties(${test_name} PROPERTIES LABELS "regression")
endfunction()

function(db_add_output_expectation_test
         test_name test_args required_patterns forbidden_patterns)
  if(NOT TARGET ${DB_UNIFIED_TARGET})
    return()
  endif()
  add_test(
    NAME ${test_name}
    COMMAND ${CMAKE_COMMAND}
      -DTEST_BIN=$<TARGET_FILE:${DB_UNIFIED_TARGET}>
      -DTEST_ARGS=${test_args}
      -DTEST_REQUIRED_PATTERNS=${required_patterns}
      -DTEST_FORBIDDEN_PATTERNS=${forbidden_patterns}
      -P ${CMAKE_SOURCE_DIR}/cmake/RunOutputExpectationTest.cmake
  )
  set_tests_properties(${test_name} PROPERTIES LABELS "cli;regression")
endfunction()

function(db_add_command_expectation_test
         test_name test_args test_exit_code required_patterns forbidden_patterns)
  if(NOT TARGET ${DB_UNIFIED_TARGET})
    return()
  endif()
  add_test(
    NAME ${test_name}
    COMMAND ${CMAKE_COMMAND}
      -DTEST_BIN=$<TARGET_FILE:${DB_UNIFIED_TARGET}>
      -DTEST_ARGS=${test_args}
      -DTEST_EXIT_CODE=${test_exit_code}
      -DTEST_REQUIRED_PATTERNS=${required_patterns}
      -DTEST_FORBIDDEN_PATTERNS=${forbidden_patterns}
      -P ${CMAKE_SOURCE_DIR}/cmake/RunCommandExpectationTest.cmake
  )
  set_tests_properties(${test_name} PROPERTIES LABELS "cli;regression")
endfunction()

function(db_add_binary_pattern_check_test test_name forbidden_patterns)
  if(NOT TARGET ${DB_UNIFIED_TARGET})
    return()
  endif()
  add_test(
    NAME ${test_name}
    COMMAND ${CMAKE_COMMAND}
      -DTEST_BIN=$<TARGET_FILE:${DB_UNIFIED_TARGET}>
      -DNM_BIN=${CMAKE_NM}
      -DTEST_FORBIDDEN_PATTERNS=${forbidden_patterns}
      -P ${CMAKE_SOURCE_DIR}/cmake/RunBinaryPatternCheck.cmake
  )
  set_tests_properties(${test_name} PROPERTIES LABELS "regression")
endfunction()

function(db_mark_glfw_env_skip test_name)
  set_tests_properties(${test_name} PROPERTIES
    SKIP_REGULAR_EXPRESSION "${DB_TEST_GLFW_ENV_SKIP_REGEX}"
  )
endfunction()

function(db_add_cpu_renderer_aggregate_test
         test_name benchmark_mode extra_args hash_checks)
  db_add_determinism_test(
    ${test_name}
    "${DB_DETERMINISM_CPU_OFFSCREEN_PREFIX} --benchmark-mode ${benchmark_mode} ${DB_DETERMINISM_COMMON_ARGS} ${DB_DETERMINISM_HASH} ${DB_DETERMINISM_HASH_REPORT} ${extra_args}"
    "${hash_checks}"
  )
endfunction()

function(db_add_cross_renderer_snake_state_triplet
         test_name benchmark_mode expected_state_hash)
  db_add_hash_equivalence_triplet_test(
    ${test_name}
    "${DB_DETERMINISM_CPU_OFFSCREEN_PREFIX} --benchmark-mode ${benchmark_mode} ${DB_DETERMINISM_COMMON_ARGS} ${DB_DETERMINISM_STATE_AGGREGATE_ARGS} ${DB_DETERMINISM_SNAKE_ARGS}"
    "${DB_DETERMINISM_GL1_OFFSCREEN_PREFIX} --benchmark-mode ${benchmark_mode} ${DB_DETERMINISM_COMMON_ARGS} ${DB_DETERMINISM_STATE_AGGREGATE_ARGS} ${DB_DETERMINISM_SNAKE_ARGS}"
    "${DB_DETERMINISM_GL3_OFFSCREEN_PREFIX} --benchmark-mode ${benchmark_mode} ${DB_DETERMINISM_COMMON_ARGS} ${DB_DETERMINISM_STATE_AGGREGATE_ARGS} ${DB_DETERMINISM_SNAKE_ARGS}"
    "state_hash_aggregate=${expected_state_hash}"
  )
endfunction()

function(db_add_cpu_gradient_speed_equivalence_triplet
         test_name benchmark_mode expected_bo_hash_final)
  db_add_hash_equivalence_triplet_test(
    ${test_name}
    "${DB_DETERMINISM_CPU_OFFSCREEN_PREFIX} --benchmark-mode ${benchmark_mode} ${DB_DETERMINISM_COMMON_ARGS} ${DB_DETERMINISM_PIXEL_FINAL_ARGS} --bench-speed 1 --frame-limit 800"
    "${DB_DETERMINISM_CPU_OFFSCREEN_PREFIX} --benchmark-mode ${benchmark_mode} ${DB_DETERMINISM_COMMON_ARGS} ${DB_DETERMINISM_PIXEL_FINAL_ARGS} --bench-speed 2 --frame-limit 400"
    "${DB_DETERMINISM_CPU_OFFSCREEN_PREFIX} --benchmark-mode ${benchmark_mode} ${DB_DETERMINISM_COMMON_ARGS} ${DB_DETERMINISM_PIXEL_FINAL_ARGS} --bench-speed 800 --frame-limit 1"
    "bo_hash_final=${expected_bo_hash_final}"
  )
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
    ${DB_DRIVERBENCH_LIBS}
  )
  add_test(NAME unit_driverbench_native COMMAND driverbench_unit_tests)
  set_tests_properties(unit_driverbench_native PROPERTIES LABELS "unit")

  db_add_command_expectation_test(
    regression_cli_help_text
    "--help"
    0
    "--backbuffer-draw-mode,--present-buffer-mode"
    ""
  )

  db_add_command_expectation_test(
    regression_cli_invalid_present_mode_cpu_offscreen
    "--display offscreen --api cpu --present-buffer-mode ring"
    1
    "--present-buffer-mode is only supported for CPU with --display glfw_window"
    ""
  )

  db_add_command_expectation_test(
    regression_cli_invalid_api_value
    "--display offscreen --api nope"
    1
    "Unsupported api: nope"
    ""
  )

  db_add_binary_pattern_check_test(
    regression_release_binary_no_test_symbol_leaks
    "db_cli_test_run_all,db_gl_shadow_present_test_run_all,db_snake_optimizer_test_run_all,driverbench_unit_tests,renderer_snake_test_support,db_snake_test_"
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
    "state_hash_aggregate=0xa84f5f681a12809d,framebuffer_hash_aggregate=0x79eb8f40449b6b6e"
  )
  set(DB_DETERMINISM_GL1_OFFSCREEN_DIRTY_GOLDEN_HASH_CHECKS
    "state_hash_aggregate=0xa84f5f681a12809d,framebuffer_hash_aggregate=0xdc6bc6acdc4ce3ec"
  )
  db_configure_test_platform_policy()

  db_add_cpu_renderer_aggregate_test(
    determinism_cpu_renderer_snake_grid
    snake_grid
    "${DB_DETERMINISM_SNAKE_ARGS}"
    "state_hash_aggregate=0xa84f5f681a12809d,bo_hash_aggregate=0xb748399eb94eb7db"
  )
  db_add_cpu_renderer_aggregate_test(
    determinism_cpu_renderer_gradient_fill
    gradient_fill
    "${DB_DETERMINISM_FRAME_LIMIT}"
    "state_hash_aggregate=0x694defa6794385e5,bo_hash_aggregate=0x3752a70a8248d9d8"
  )
  db_add_cpu_renderer_aggregate_test(
    determinism_cpu_renderer_snake_shapes
    snake_shapes
    "${DB_DETERMINISM_SNAKE_ARGS}"
    "state_hash_aggregate=0x90f208d3f4e2b13f,bo_hash_aggregate=0x60d8aea2dcf959e3"
  )
  db_add_cpu_renderer_aggregate_test(
    determinism_cpu_renderer_gradient_sweep
    gradient_sweep
    "${DB_DETERMINISM_FRAME_LIMIT}"
    "state_hash_aggregate=0xea73cec6de493fd6,bo_hash_aggregate=0x3752a70a8248d9d8"
  )
  db_add_cpu_renderer_aggregate_test(
    determinism_cpu_renderer_bands
    bands
    "${DB_DETERMINISM_FRAME_LIMIT}"
    "state_hash_aggregate=0x170b3eb26cd1c04c,bo_hash_aggregate=0x5f2154063ac1c3bd"
  )

  db_add_cpu_gradient_speed_equivalence_triplet(
    determinism_cpu_gradient_fill_speed_equivalence_triplet
    gradient_fill
    0xfe4471a7fe601b04
  )
  db_add_cpu_gradient_speed_equivalence_triplet(
    determinism_cpu_gradient_sweep_speed_equivalence_triplet
    gradient_sweep
    0x00a1e8f01b888a89
  )

  if(DB_BUILD_GLFW_WINDOW_DISPLAY AND DB_GLFW_LINK_LIB)
    db_add_hash_difference_test(
      regression_hidden_glfw_cpu_bands_framebuffer_progress
      "--api cpu --display glfw_window --glfw-hidden-window 1 --benchmark-mode bands --vsync 0 --fps-cap 0 --frame-limit 1 --hash both --hash-report final"
      "--api cpu --display glfw_window --glfw-hidden-window 1 --benchmark-mode bands --vsync 0 --fps-cap 0 --frame-limit 5 --hash both --hash-report final"
      "framebuffer_hash_final"
    )
    db_mark_glfw_env_skip(regression_hidden_glfw_cpu_bands_framebuffer_progress)

    db_add_output_expectation_test(
      regression_hidden_glfw_cpu_backend_identity
      "--api cpu --display glfw_window --glfw-hidden-window 1 --benchmark-mode bands --vsync 0 --fps-cap 0 --frame-limit 1"
      "glfw window visibility=hidden,backend=display_glfw_window_cpu_renderer"
      "backend=display_offscreen"
    )
    db_mark_glfw_env_skip(regression_hidden_glfw_cpu_backend_identity)

    db_add_output_expectation_test(
      regression_cpu_offscreen_backend_identity
      "--api cpu --display offscreen --benchmark-mode bands --vsync 0 --fps-cap 0 --frame-limit 1"
      "backend=display_offscreen"
      "glfw window visibility=hidden,backend=display_glfw_window_cpu_renderer"
    )

    db_add_cross_renderer_snake_state_triplet(
      determinism_cross_renderer_snake_grid_state_triplet
      snake_grid
      0xa84f5f681a12809d
    )
    db_mark_glfw_env_skip(determinism_cross_renderer_snake_grid_state_triplet)

    db_add_cross_renderer_snake_state_triplet(
      determinism_cross_renderer_snake_rect_state_triplet
      snake_rect
      0x61cd6f247ff98d89
    )
    db_mark_glfw_env_skip(determinism_cross_renderer_snake_rect_state_triplet)

    db_add_cross_renderer_snake_state_triplet(
      determinism_cross_renderer_snake_shapes_state_triplet
      snake_shapes
      0x90f208d3f4e2b13f
    )
    db_mark_glfw_env_skip(determinism_cross_renderer_snake_shapes_state_triplet)

    db_add_determinism_test(
      determinism_offscreen_gl1_5
      "${DB_TEST_GL1_OFFSCREEN_ARGS}"
      "${DB_TEST_GL1_OFFSCREEN_HASH_CHECKS}"
    )
    db_mark_glfw_env_skip(determinism_offscreen_gl1_5)

    db_add_determinism_test(
      determinism_offscreen_gl3_3_fbo
      "${DB_DETERMINISM_GL3_OFFSCREEN_PREFIX} --benchmark-mode snake_grid ${DB_DETERMINISM_COMMON_ARGS} ${DB_DETERMINISM_HASH} ${DB_DETERMINISM_HASH_REPORT} ${DB_DETERMINISM_SNAKE_ARGS}"
      "state_hash_aggregate=0xa84f5f681a12809d,fbo_hash16f_aggregate=0xbdf6bad5fa808825"
    )
    db_mark_glfw_env_skip(determinism_offscreen_gl3_3_fbo)

    if(DB_TEST_ENABLE_GL1_DIRTY_PLATFORM_VARIANT)
      db_add_determinism_test(
        determinism_offscreen_gl1_5_dirty_macos
        "${DB_TEST_GL1_DIRTY_PLATFORM_ARGS}"
        "${DB_TEST_GL1_DIRTY_PLATFORM_HASH_CHECKS}"
      )
      db_mark_glfw_env_skip(determinism_offscreen_gl1_5_dirty_macos)
    endif()
  endif()
endfunction()
