include("${CMAKE_CURRENT_LIST_DIR}/TestRunnerCommon.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/DriverBenchTestTimeouts.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/DriverBenchTestCapabilities.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/DriverBenchAlternateSuites.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/DriverBenchTestHelpers.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/DriverBenchDeterminismSuites.cmake")
function(
    db_define_release_suite
    suite_id
    test_prefix
    test_bin
    unit_bin
    extra_labels
    glfw_enabled)
    set(db_suite_ids "${DB_TEST_SUITE_IDS}")
    list(APPEND db_suite_ids "${suite_id}")
    set(DB_TEST_SUITE_IDS
        "${db_suite_ids}"
        PARENT_SCOPE)
    set(DB_TEST_SUITE_${suite_id}_PREFIX
        "${test_prefix}"
        PARENT_SCOPE)
    set(DB_TEST_SUITE_${suite_id}_BINARY
        "${test_bin}"
        PARENT_SCOPE)
    set(DB_TEST_SUITE_${suite_id}_UNIT_BINARY
        "${unit_bin}"
        PARENT_SCOPE)
    set(DB_TEST_SUITE_${suite_id}_EXTRA_LABELS
        "${extra_labels}"
        PARENT_SCOPE)
    set(DB_TEST_SUITE_${suite_id}_GLFW_ENABLED
        "${glfw_enabled}"
        PARENT_SCOPE)
endfunction()

function(db_register_release_suite suite_id)
    set(db_prefix "${DB_TEST_SUITE_${suite_id}_PREFIX}")
    set(db_test_bin "${DB_TEST_SUITE_${suite_id}_BINARY}")
    set(db_unit_bin "${DB_TEST_SUITE_${suite_id}_UNIT_BINARY}")
    set(db_extra_labels "${DB_TEST_SUITE_${suite_id}_EXTRA_LABELS}")
    set(db_glfw_enabled "${DB_TEST_SUITE_${suite_id}_GLFW_ENABLED}")

    db_suite_compose_labels(db_regression_labels "regression"
                            "${db_extra_labels}")
    db_suite_compose_labels(db_cli_labels "cli;regression" "${db_extra_labels}")
    db_suite_compose_labels(db_unit_labels "unit" "${db_extra_labels}")
    db_suite_compose_labels(db_golden_labels "golden;regression"
                            "${db_extra_labels}")

    if("${db_prefix}" STREQUAL "")
        set(db_test_name "unit_driverbench_native")
    else()
        db_suite_make_test_name(db_test_name "${db_prefix}" "unit_driverbench")
    endif()
    db_add_unit_binary_test("${db_test_name}" "${db_unit_bin}"
                            "${db_unit_labels}")

    db_suite_make_test_name(db_test_name "${db_prefix}"
                            "regression_structured_log_contract")
    add_test(
        NAME ${db_test_name}
        COMMAND
            ${CMAKE_COMMAND} -DTEST_BIN=${db_test_bin}
            -DTEST_ARGS=--api\ cpu\ --display\ offscreen\ --benchmark-mode\ snake_grid\ --frame-limit\ 2\ --trace-damage\ 2\ --hash\ both
            -P ${CMAKE_SOURCE_DIR}/cmake/RunStructuredLogContractTest.cmake)
    set_tests_properties(${db_test_name} PROPERTIES LABELS
                                                    "${db_regression_labels}")

    db_suite_make_test_name(db_test_name "${db_prefix}"
                            "regression_default_output_bounded")
    add_test(
        NAME ${db_test_name}
        COMMAND
            ${CMAKE_COMMAND} -DTEST_BIN=${db_test_bin}
            -DTEST_ARGS=--api\ cpu\ --display\ offscreen\ --benchmark-mode\ snake_grid\ --frame-limit\ 1
            -DTEST_MAX_LINES=12 -P
            ${CMAKE_SOURCE_DIR}/cmake/RunLogCardinalityTest.cmake)
    set_tests_properties(${db_test_name} PROPERTIES LABELS
                                                    "${db_regression_labels}")

    if(db_glfw_enabled)
        foreach(db_cardinality_renderer IN ITEMS gl1 gl3 vulkan)
            if(db_cardinality_renderer STREQUAL "gl1")
                set(db_cardinality_args
                    "--api opengl --renderer gl1_5_gles1_1 --display glfw_window --glfw-hidden-window 1 --benchmark-mode snake_grid --frame-limit 1"
                )
                set(db_cardinality_max 18)
            elseif(db_cardinality_renderer STREQUAL "gl3")
                set(db_cardinality_args
                    "--api opengl --renderer gl3_3 --display glfw_window --glfw-hidden-window 1 --benchmark-mode snake_grid --frame-limit 1"
                )
                set(db_cardinality_max 16)
            else()
                set(db_cardinality_args
                    "--api vulkan --display glfw_window --glfw-hidden-window 1 --benchmark-mode snake_grid --frame-limit 1"
                )
                set(db_cardinality_max 32)
            endif()
            db_suite_make_test_name(
                db_test_name "${db_prefix}"
                "regression_default_output_bounded_${db_cardinality_renderer}")
            add_test(
                NAME ${db_test_name}
                COMMAND
                    ${CMAKE_COMMAND} -DTEST_BIN=${db_test_bin}
                    -DTEST_ARGS=${db_cardinality_args}
                    -DTEST_MAX_LINES=${db_cardinality_max} -P
                    ${CMAKE_SOURCE_DIR}/cmake/RunLogCardinalityTest.cmake)
            set_tests_properties(${db_test_name}
                                 PROPERTIES LABELS "${db_regression_labels}")
            db_test_apply_skip_regex("${db_test_name}"
                                     "${DB_TEST_GLFW_ENV_SKIP_REGEX}")
        endforeach()

        db_suite_make_test_name(db_test_name "${db_prefix}"
                                "regression_gl3_persistent_geometry_reuse")
        add_test(
            NAME ${db_test_name}
            COMMAND
                ${CMAKE_COMMAND} -DTEST_BIN=${db_test_bin} -P
                ${CMAKE_SOURCE_DIR}/cmake/RunGl3PersistentGeometryReuseTest.cmake
        )
        db_test_timeout_seconds(db_gl3_reuse_timeout determinism_gpu)
        set_tests_properties(
            ${db_test_name}
            PROPERTIES LABELS "${db_regression_labels}" TIMEOUT
                       "${db_gl3_reuse_timeout}" RESOURCE_LOCK
                       driverbench_gpu_matrix)

        db_suite_make_test_name(db_test_name "${db_prefix}"
                                "regression_glfw_buffer_age_deduplicated")
        add_test(NAME ${db_test_name}
                 COMMAND ${CMAKE_COMMAND} -DTEST_BIN=${db_test_bin} -P
                         ${CMAKE_SOURCE_DIR}/cmake/RunBufferAgeDedupTest.cmake)
        set_tests_properties(${db_test_name}
                             PROPERTIES LABELS "${db_regression_labels}")
        db_test_apply_skip_regex("${db_test_name}"
                                 "${DB_TEST_GLFW_ENV_SKIP_REGEX}")

        db_suite_make_test_name(db_test_name "${db_prefix}"
                                "regression_vulkan_trace_levels")
        add_test(
            NAME ${db_test_name}
            COMMAND ${CMAKE_COMMAND} -DTEST_BIN=${db_test_bin} -P
                    ${CMAKE_SOURCE_DIR}/cmake/RunVulkanTraceLevelTest.cmake)
        set_tests_properties(${db_test_name}
                             PROPERTIES LABELS "${db_regression_labels}")
        db_test_apply_skip_regex("${db_test_name}"
                                 "${DB_TEST_GLFW_ENV_SKIP_REGEX}")

        db_suite_make_test_name(db_test_name "${db_prefix}"
                                "regression_glfw_resize_contract")
        add_test(
            NAME ${db_test_name}
            COMMAND ${CMAKE_COMMAND} -DTEST_BIN=${db_test_bin} -P
                    ${CMAKE_SOURCE_DIR}/cmake/RunGlfwResizeContractTest.cmake)
        set_tests_properties(${db_test_name}
                             PROPERTIES LABELS "${db_regression_labels}")
    endif()

    db_suite_make_test_name(db_test_name "${db_prefix}"
                            "regression_cpu_persistent_target_trace")
    add_test(
        NAME ${db_test_name}
        COMMAND
            ${CMAKE_COMMAND} -DTEST_BIN=${db_test_bin}
            -DTEST_ARGS=--api\ cpu\ --display\ offscreen\ --benchmark-mode\ snake_grid\ --frame-limit\ 2\ --trace-damage\ 1
            -DTEST_BACKEND=cpu -DTEST_TARGET=cpu_surface
            -DTEST_PRESENT_METHOD=none -P
            ${CMAKE_SOURCE_DIR}/cmake/RunPersistentTargetTraceTest.cmake)
    set_tests_properties(${db_test_name} PROPERTIES LABELS
                                                    "${db_regression_labels}")

    db_suite_make_test_name(db_test_name "${db_prefix}"
                            "regression_cli_help_text")
    db_add_command_contract_test(
        "${db_test_name}"
        "${db_test_bin}"
        "--help"
        0
        "--backbuffer-draw-mode|--present-buffer-mode|--working-format <rgba8|rgba16f>|--output-format <auto|sdr|hdr>|--trace-damage <0|1|2|3>|--trace-shadow-upload <0|1|2|3>|--trace-vulkan <0|1|2>|--trace-gl-errors <0|1>|Build-time GLFW provider:"
        "--cpu-hdr"
        ""
        ""
        "${db_cli_labels}")

    db_suite_make_test_name(db_test_name "${db_prefix}"
                            "regression_cli_invalid_present_mode_cpu_offscreen")
    db_add_command_contract_test(
        "${db_test_name}"
        "${db_test_bin}"
        "--display offscreen --api cpu --present-buffer-mode ring"
        1
        "--present-buffer-mode|CPU|--display glfw_window"
        ""
        ""
        ""
        "${db_cli_labels}")

    db_suite_make_test_name(db_test_name "${db_prefix}"
                            "regression_cli_invalid_api_value")
    db_add_command_contract_test(
        "${db_test_name}"
        "${db_test_bin}"
        "--display offscreen --api nope"
        1
        "Unsupported api|nope"
        ""
        ""
        ""
        "${db_cli_labels}")

    db_suite_make_test_name(
        db_test_name "${db_prefix}"
        "regression_explicit_hdr_requires_native_capability")
    db_add_command_contract_test(
        "${db_test_name}"
        "${db_test_bin}"
        "--display offscreen --api cpu --output-format hdr --frame-limit 1"
        1
        "output-format=hdr requested|native HDR is unavailable"
        ""
        ""
        ""
        "${db_cli_labels}")

    db_suite_make_test_name(db_test_name "${db_prefix}"
                            "regression_release_binary_no_test_symbol_leaks")
    db_add_binary_pattern_check_test(
        "${db_test_name}"
        "${db_test_bin}"
        "db_cli_test_run_all,db_gl_shadow_present_test_run_all,driverbench_unit_tests,renderer_snake_test_support,db_snake_test_"
        "${db_regression_labels}")

    db_register_gradient_family("${db_prefix}" "${db_test_bin}"
                                "${db_golden_labels}" "${db_glfw_enabled}")
    db_register_bands_family(
        "${db_prefix}" "${db_test_bin}" "${db_golden_labels}"
        "${db_regression_labels}" "${db_cli_labels}" "${db_glfw_enabled}")
    db_register_snake_grid_family(
        "${db_prefix}" "${db_test_bin}" "${db_golden_labels}"
        "${db_regression_labels}" "${db_glfw_enabled}")
    db_register_snake_rect_family(
        "${db_prefix}" "${db_test_bin}" "${db_golden_labels}"
        "${db_regression_labels}" "${db_glfw_enabled}")
    db_register_snake_shapes_family(
        "${db_prefix}" "${db_test_bin}" "${db_golden_labels}"
        "${db_regression_labels}" "${db_glfw_enabled}")
endfunction()

function(db_register_driverbench_tests)
    if(NOT DB_IS_RELEASE_LIKE)
        message(
            STATUS
                "Non-release build detected: full ctest may be slow; Release is the "
                "default validation target for renderer performance regressions."
        )
    endif()

    add_test(
        NAME source_file_line_limit
        COMMAND
            ${CMAKE_COMMAND} -DSOURCE_ROOT=${CMAKE_SOURCE_DIR}
            -DLINE_LIMIT=${DB_SOURCE_FILE_LINE_LIMIT} -P
            ${CMAKE_SOURCE_DIR}/cmake/CheckSourceFileLineLimit.cmake)
    set_tests_properties(source_file_line_limit PROPERTIES LABELS "regression")

    find_program(DB_HEADER_CLANG_TIDY_EXECUTABLE NAMES clang-tidy)
    find_program(DB_HEADER_CLANG_TIDY_PYTHON NAMES python3)
    if(DB_HEADER_CLANG_TIDY_EXECUTABLE AND DB_HEADER_CLANG_TIDY_PYTHON)
        add_test(
            NAME source_header_clang_tidy
            COMMAND
                ${DB_HEADER_CLANG_TIDY_PYTHON}
                ${CMAKE_SOURCE_DIR}/scripts/run_header_clang_tidy.py
                --source-root ${CMAKE_SOURCE_DIR} --build-dir
                ${CMAKE_BINARY_DIR} --clang-tidy
                ${DB_HEADER_CLANG_TIDY_EXECUTABLE})
        db_test_timeout_seconds(db_header_lint_timeout lint)
        set_tests_properties(
            source_header_clang_tidy PROPERTIES LABELS "regression" TIMEOUT
                                                "${db_header_lint_timeout}")
    else()
        message(
            STATUS
                "Header clang-tidy CTest disabled: clang-tidy or python3 unavailable"
        )
    endif()

    add_test(
        NAME source_platform_policy_cmake
        COMMAND
            ${CMAKE_COMMAND} -DSOURCE_ROOT=${CMAKE_SOURCE_DIR}
            -DRULE_SET=cmake_platform_policy -P
            ${CMAKE_SOURCE_DIR}/cmake/CheckPlatformPolicyBoundaries.cmake)
    set_tests_properties(source_platform_policy_cmake PROPERTIES LABELS
                                                                 "regression")

    add_test(
        NAME source_platform_policy_display_glfw
        COMMAND
            ${CMAKE_COMMAND} -DSOURCE_ROOT=${CMAKE_SOURCE_DIR}
            -DRULE_SET=display_glfw_policy -P
            ${CMAKE_SOURCE_DIR}/cmake/CheckPlatformPolicyBoundaries.cmake)
    set_tests_properties(source_platform_policy_display_glfw
                         PROPERTIES LABELS "regression")

    add_test(
        NAME source_platform_policy_renderer_gl_upload
        COMMAND
            ${CMAKE_COMMAND} -DSOURCE_ROOT=${CMAKE_SOURCE_DIR}
            -DRULE_SET=renderer_gl_upload_policy -P
            ${CMAKE_SOURCE_DIR}/cmake/CheckPlatformPolicyBoundaries.cmake)
    set_tests_properties(source_platform_policy_renderer_gl_upload
                         PROPERTIES LABELS "regression")

    add_test(
        NAME source_platform_policy_proc_loading
        COMMAND
            ${CMAKE_COMMAND} -DSOURCE_ROOT=${CMAKE_SOURCE_DIR}
            -DRULE_SET=platform_proc_loading -P
            ${CMAKE_SOURCE_DIR}/cmake/CheckPlatformPolicyBoundaries.cmake)
    set_tests_properties(source_platform_policy_proc_loading
                         PROPERTIES LABELS "regression")

    add_test(
        NAME source_logging_policy
        COMMAND
            ${CMAKE_COMMAND} -DSOURCE_ROOT=${CMAKE_SOURCE_DIR}
            -DRULE_SET=logging_policy -P
            ${CMAKE_SOURCE_DIR}/cmake/CheckPlatformPolicyBoundaries.cmake)
    set_tests_properties(source_logging_policy PROPERTIES LABELS "regression")

    add_test(
        NAME source_cmake_test_registration_policy
        COMMAND ${CMAKE_COMMAND} -DSOURCE_ROOT=${CMAKE_SOURCE_DIR} -P
                ${CMAKE_SOURCE_DIR}/cmake/CheckCMakeTestRegistrationPolicy.cmake
    )
    set_tests_properties(source_cmake_test_registration_policy
                         PROPERTIES LABELS "regression")

    add_test(
        NAME source_bool_normalization_policy
        COMMAND
            ${CMAKE_COMMAND} -DSOURCE_ROOT=${CMAKE_SOURCE_DIR}
            -DRULE_SET=bool_normalization_policy -P
            ${CMAKE_SOURCE_DIR}/cmake/CheckPlatformPolicyBoundaries.cmake)
    set_tests_properties(source_bool_normalization_policy
                         PROPERTIES LABELS "regression")

    add_test(
        NAME source_numeric_boundary_policy
        COMMAND
            ${CMAKE_COMMAND} -DSOURCE_ROOT=${CMAKE_SOURCE_DIR}
            -DRULE_SET=numeric_boundary_policy -P
            ${CMAKE_SOURCE_DIR}/cmake/CheckPlatformPolicyBoundaries.cmake)
    set_tests_properties(source_numeric_boundary_policy PROPERTIES LABELS
                                                                   "regression")

    add_test(
        NAME source_sorting_policy
        COMMAND
            ${CMAKE_COMMAND} -DSOURCE_ROOT=${CMAKE_SOURCE_DIR}
            -DRULE_SET=sorting_policy -P
            ${CMAKE_SOURCE_DIR}/cmake/CheckPlatformPolicyBoundaries.cmake)
    set_tests_properties(source_sorting_policy PROPERTIES LABELS "regression")

    add_test(NAME source_hash_tree_policy
             COMMAND ${CMAKE_COMMAND} -DSOURCE_ROOT=${CMAKE_SOURCE_DIR} -P
                     ${CMAKE_SOURCE_DIR}/cmake/CheckHashTreePolicy.cmake)
    set_tests_properties(source_hash_tree_policy PROPERTIES LABELS
                                                            "regression;hash")

    add_test(NAME source_vulkan_multi_gpu_policy
             COMMAND ${CMAKE_COMMAND} -DSOURCE_ROOT=${CMAKE_SOURCE_DIR} -P
                     ${CMAKE_SOURCE_DIR}/cmake/CheckVulkanMultiGpuPolicy.cmake)
    set_tests_properties(source_vulkan_multi_gpu_policy PROPERTIES LABELS
                                                                   "regression")

    add_test(
        NAME config_glfw_provider_default_vendored
        COMMAND
            ${CMAKE_COMMAND} -DSOURCE_ROOT=${CMAKE_SOURCE_DIR}
            -DTEST_BINARY_DIR=${CMAKE_BINARY_DIR}/ctest_glfw_provider_vendored
            -DTEST_PROVIDER=vendored -DEXPECT_GLFW_AVAILABLE=ON
            -DEXPECT_PROVIDER_RESOLVED=vendored
            -DTEST_GENERATOR=${CMAKE_GENERATOR}
            -DTEST_BUILD_TYPE=${CMAKE_BUILD_TYPE}
            -DTEST_C_COMPILER=${CMAKE_C_COMPILER}
            -DTEST_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM} -P
            ${CMAKE_SOURCE_DIR}/cmake/CheckGLFWProviderConfig.cmake)
    set_tests_properties(config_glfw_provider_default_vendored
                         PROPERTIES LABELS "regression")

    add_test(
        NAME config_glfw_provider_off
        COMMAND
            ${CMAKE_COMMAND} -DSOURCE_ROOT=${CMAKE_SOURCE_DIR}
            -DTEST_BINARY_DIR=${CMAKE_BINARY_DIR}/ctest_glfw_provider_off
            -DTEST_PROVIDER=off -DEXPECT_GLFW_AVAILABLE=OFF
            -DEXPECT_PROVIDER_RESOLVED=off -DTEST_GENERATOR=${CMAKE_GENERATOR}
            -DTEST_BUILD_TYPE=${CMAKE_BUILD_TYPE}
            -DTEST_C_COMPILER=${CMAKE_C_COMPILER}
            -DTEST_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM} -P
            ${CMAKE_SOURCE_DIR}/cmake/CheckGLFWProviderConfig.cmake)
    set_tests_properties(config_glfw_provider_off PROPERTIES LABELS
                                                             "regression")

    add_test(NAME regression_render_wait_policy_bounded
             COMMAND ${CMAKE_COMMAND} -DSOURCE_ROOT=${CMAKE_SOURCE_DIR} -P
                     ${CMAKE_SOURCE_DIR}/cmake/CheckRenderWaitPolicy.cmake)
    set_tests_properties(regression_render_wait_policy_bounded
                         PROPERTIES LABELS "regression")

    add_test(NAME source_kms_presentation_policy
             COMMAND ${CMAKE_COMMAND} -DSOURCE_ROOT=${CMAKE_SOURCE_DIR} -P
                     ${CMAKE_SOURCE_DIR}/cmake/CheckKMSPresentationPolicy.cmake)
    set_tests_properties(source_kms_presentation_policy PROPERTIES LABELS
                                                                   "regression")

    add_executable(
        driverbench_unit_tests
        tests/test_main.c
        tests/test_benchmark_seeding.c
        tests/test_benchmark_emitters.c
        tests/test_cli.c
        tests/test_core_logging.c
        tests/test_damage_trace.c
        tests/test_display_gl_runtime.c
        tests/test_gl_shadow_present.c
        tests/test_hash.c
        tests/test_numeric.c
        tests/test_poll_policy.c)
    target_sources(driverbench_unit_tests PRIVATE tests/test_sort.c)
    if(DB_BUILD_VULKAN AND DB_VULKAN_LIB)
        target_sources(driverbench_unit_tests PRIVATE tests/test_vk_scheduler.c)
    endif()
    db_apply_perf_options(driverbench_unit_tests)
    target_compile_definitions(driverbench_unit_tests
                               PRIVATE ${DB_DRIVERBENCH_DEFS})
    target_include_directories(
        driverbench_unit_tests PRIVATE ${CMAKE_SOURCE_DIR}/src
                                       ${CMAKE_SOURCE_DIR}/tests)
    target_link_libraries(
        driverbench_unit_tests
        PRIVATE driverbench_app driverbench_cli_core driverbench_render_core
                driverbench_core driverbench_app ${DB_DRIVERBENCH_LIBS})

    add_executable(driverbench_hash_conformance tests/hash_conformance.c)
    db_apply_perf_options(driverbench_hash_conformance)
    target_include_directories(driverbench_hash_conformance
                               PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(driverbench_hash_conformance PRIVATE driverbench_core)
    add_test(NAME hash_conformance_native_host
             COMMAND driverbench_hash_conformance auto)
    set_tests_properties(hash_conformance_native_host PROPERTIES LABELS
                                                                 "unit;hash")

    db_test_timeout_seconds(db_qemu_hash_timeout qemu_hash)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64)$" AND CMAKE_SIZEOF_VOID_P
                                                             EQUAL 8)
        find_program(DB_QEMU_X86_64_EXECUTABLE NAMES qemu-x86_64)
        if(DB_QEMU_X86_64_EXECUTABLE)
            add_test(
                NAME hash_conformance_qemu_host_x86_64_sse2
                COMMAND
                    ${DB_QEMU_X86_64_EXECUTABLE} -cpu
                    qemu64,-sse3,-ssse3,-sse4.1,-sse4.2,-avx,-avx2
                    $<TARGET_FILE:driverbench_hash_conformance> sse2)
            add_test(
                NAME hash_conformance_qemu_host_x86_64_avx2
                COMMAND ${DB_QEMU_X86_64_EXECUTABLE} -cpu Haswell,-hle,-rtm
                        $<TARGET_FILE:driverbench_hash_conformance> avx2)
            set_tests_properties(
                hash_conformance_qemu_host_x86_64_sse2
                hash_conformance_qemu_host_x86_64_avx2
                PROPERTIES LABELS "unit;hash;qemu" TIMEOUT
                           "${db_qemu_hash_timeout}")
        else()
            message(
                STATUS
                    "QEMU hash SSE2/AVX2 tests skipped: qemu-x86_64 not found")
        endif()
    endif()

    find_program(DB_QEMU_HASH_PYTHON NAMES python3)
    if(DB_QEMU_HASH_PYTHON)
        foreach(db_qemu_hash_arch IN ITEMS aarch64 i686)
            if(db_qemu_hash_arch STREQUAL "aarch64")
                set(db_qemu_hash_test_name
                    hash_conformance_qemu_linux_aarch64_neon)
            else()
                set(db_qemu_hash_test_name
                    hash_conformance_qemu_linux_i686_sse2)
            endif()
            add_test(
                NAME ${db_qemu_hash_test_name}
                COMMAND
                    ${DB_QEMU_HASH_PYTHON}
                    ${CMAKE_SOURCE_DIR}/scripts/run_qemu_hash_conformance.py
                    --arch ${db_qemu_hash_arch} --source-root
                    ${CMAKE_SOURCE_DIR} --build-root
                    ${CMAKE_BINARY_DIR}/qemu-hash/${db_qemu_hash_arch})
            set_tests_properties(
                ${db_qemu_hash_test_name}
                PROPERTIES LABELS "unit;hash;qemu" SKIP_RETURN_CODE 77 TIMEOUT
                           "${db_qemu_hash_timeout}")
        endforeach()
    else()
        message(STATUS "QEMU hash cross tests skipped: python3 not found")
    endif()

    set(DB_DETERMINISM_CPU_OFFSCREEN_PREFIX "--api cpu --display offscreen")
    set(DB_DETERMINISM_GL1_OFFSCREEN_PREFIX
        "--api opengl --renderer gl1_5_gles1_1 --display offscreen --vsync 0")
    set(DB_DETERMINISM_GL3_OFFSCREEN_PREFIX
        "--api opengl --renderer gl3_3 --display offscreen --vsync 0")
    set(DB_DETERMINISM_COMMON_ARGS
        "--random-seed 123456 --fps-cap 0 --working-format rgba16f --output-format sdr"
    )
    db_configure_test_runtime_capabilities()
    db_discover_alternate_release_suites()

    set(DB_TEST_SUITE_IDS "")
    set(db_native_glfw_enabled OFF)
    if(NOT DB_TEST_HEADLESS_ONLY
       AND DB_BUILD_GLFW_WINDOW_DISPLAY
       AND NOT (DB_GLFW_LINK_LIB STREQUAL ""))
        set(db_native_glfw_enabled ON)
    endif()
    if(DB_TEST_HEADLESS_ONLY)
        message(
            STATUS
                "Headless CTest registration enabled: native-window and graphics-runtime tests are omitted"
        )
    endif()
    find_program(DB_GOLDEN_UPDATE_PYTHON NAMES python3)
    if(DB_GOLDEN_UPDATE_PYTHON)
        set(db_native_vulkan_enabled OFF)
        if(db_native_glfw_enabled
           AND DB_BUILD_VULKAN
           AND DB_VULKAN_LIB)
            set(db_native_vulkan_enabled ON)
        endif()
        add_custom_target(
            update_canonical_goldens
            COMMAND
                ${DB_GOLDEN_UPDATE_PYTHON}
                ${CMAKE_SOURCE_DIR}/scripts/update_canonical_goldens.py --binary
                $<TARGET_FILE:${DB_UNIFIED_TARGET}> --manifest
                ${CMAKE_SOURCE_DIR}/cmake/DriverBenchCanonicalGoldens.cmake
                --runner
                ${CMAKE_SOURCE_DIR}/cmake/RunCanonicalDeterminismMatrix.cmake
                --cmake ${CMAKE_COMMAND} --glfw-enabled
                ${db_native_glfw_enabled} --vulkan-enabled
                ${db_native_vulkan_enabled}
            DEPENDS ${DB_UNIFIED_TARGET}
            USES_TERMINAL)
    endif()
    db_define_release_suite(
        native "" "$<TARGET_FILE:${DB_UNIFIED_TARGET}>"
        "$<TARGET_FILE:driverbench_unit_tests>" "" "${db_native_glfw_enabled}")

    foreach(db_alt_lane_id IN LISTS DB_TEST_ALTERNATE_LANE_IDS)
        if(DB_TEST_ALT_${db_alt_lane_id}_ENABLED)
            set(db_alt_glfw_enabled
                "${DB_TEST_ALT_${db_alt_lane_id}_GLFW_AVAILABLE}")
            if(DB_TEST_HEADLESS_ONLY)
                set(db_alt_glfw_enabled OFF)
            endif()
            db_define_release_suite(
                "${db_alt_lane_id}"
                "alt_${db_alt_lane_id}_"
                "${DB_TEST_ALT_${db_alt_lane_id}_BINARY}"
                "${DB_TEST_ALT_${db_alt_lane_id}_UNIT_BINARY}"
                "alternate;${db_alt_lane_id}"
                "${db_alt_glfw_enabled}")
        endif()
    endforeach()

    foreach(db_suite_id IN LISTS DB_TEST_SUITE_IDS)
        db_register_release_suite("${db_suite_id}")
    endforeach()
endfunction()
