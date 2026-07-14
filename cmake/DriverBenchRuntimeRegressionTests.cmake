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
        db_suite_make_test_name(db_test_name "${db_prefix}"
                                "regression_cpu_gradient_execution_counts")
        add_test(
            NAME ${db_test_name}
            COMMAND
                ${CMAKE_COMMAND} -DTEST_BIN=${db_test_bin}
                -DTEST_ARGS=--api\ cpu\ --display\ glfw_window\ --glfw-hidden-window\ 1\ --working-format\ rgba16f\ --benchmark-mode\ gradient_fill\ --random-seed\ 123456\ --frame-limit\ 2\ --hash\ pixel
                -DTEST_EXPECTED_FIELDS=target_strategy=cpu_surface\;solid_commands=0\;gradient_commands=1\;solid_draws=0\;gradient_draws=1
                -P ${CMAKE_SOURCE_DIR}/cmake/RunNativeExecutionReportTest.cmake)
        set_tests_properties(${db_test_name}
                             PROPERTIES LABELS "${db_regression_labels}")
        db_test_apply_skip_regex("${db_test_name}"
                                 "${DB_TEST_GLFW_ENV_SKIP_REGEX}")

        db_suite_make_test_name(db_test_name "${db_prefix}"
                                "regression_vulkan_semantic_diagnostic")
        add_test(
            NAME ${db_test_name}
            COMMAND
                ${CMAKE_COMMAND} -DTEST_BIN=${db_test_bin}
                -DTEST_ARGS=--api\ vulkan\ --display\ glfw_window\ --glfw-hidden-window\ 1\ --working-format\ rgba16f\ --benchmark-mode\ gradient_fill\ --random-seed\ 123456\ --vk-gradient\ semantic\ --frame-limit\ 2\ --hash\ pixel
                -DTEST_EXPECTED_FIELDS=target_strategy=vulkan_persistent_image\;solid_path=vulkan_instanced_solid\;gradient_path=vulkan_semantic_gradient\;qualified=false\;diagnostic_forced=true\;fallback_instances=0
                -P ${CMAKE_SOURCE_DIR}/cmake/RunNativeExecutionReportTest.cmake)
        set_tests_properties(${db_test_name}
                             PROPERTIES LABELS "${db_regression_labels}")
        db_test_apply_skip_regex("${db_test_name}"
                                 "${DB_TEST_GLFW_ENV_SKIP_REGEX}")

        db_suite_make_test_name(db_test_name "${db_prefix}"
                                "regression_gl1_native_fbo_execution")
        add_test(
            NAME ${db_test_name}
            COMMAND
                ${CMAKE_COMMAND} -DTEST_BIN=${db_test_bin}
                -DTEST_ARGS=--api\ opengl\ --renderer\ gl1_5_gles1_1\ --display\ offscreen\ --working-format\ rgba8\ --benchmark-mode\ bands\ --frame-limit\ 2\ --hash\ pixel
                -DTEST_EXPECTED_FIELDS=target_strategy=gl1_persistent_fbo\;solid_path=gl1_fixed_function\;cpu_pixels_written=0\;uploaded_bytes=0
                -P ${CMAKE_SOURCE_DIR}/cmake/RunNativeExecutionReportTest.cmake)
        set_tests_properties(${db_test_name}
                             PROPERTIES LABELS "${db_regression_labels}")
        db_test_apply_skip_regex("${db_test_name}"
                                 "${DB_TEST_GLFW_ENV_SKIP_REGEX}")

        db_suite_make_test_name(db_test_name "${db_prefix}"
                                "regression_gl1_direct_window_diagnostic")
        add_test(
            NAME ${db_test_name}
            COMMAND
                ${CMAKE_COMMAND} -DTEST_BIN=${db_test_bin}
                -DTEST_ARGS=--api\ opengl\ --renderer\ gl1_5_gles1_1\ --display\ offscreen\ --working-format\ rgba8\ --benchmark-mode\ bands\ --random-seed\ 123456\ --gl1-target\ direct-window\ --frame-limit\ 1\ --hash\ pixel
                -DTEST_EXPECTED_FIELDS=target_strategy=gl1_direct_window\;qualified=false\;diagnostic_forced=true\;cpu_pixels_written=0\;uploaded_bytes=0
                -P ${CMAKE_SOURCE_DIR}/cmake/RunNativeExecutionReportTest.cmake)
        set_tests_properties(${db_test_name}
                             PROPERTIES LABELS "${db_regression_labels}")
        db_test_apply_skip_regex("${db_test_name}"
                                 "${DB_TEST_GLFW_ENV_SKIP_REGEX}")

        db_suite_make_test_name(db_test_name "${db_prefix}"
                                "regression_gl3_semantic_diagnostic")
        add_test(
            NAME ${db_test_name}
            COMMAND
                ${CMAKE_COMMAND} -DTEST_BIN=${db_test_bin}
                -DTEST_ARGS=--api\ opengl\ --renderer\ gl3_3\ --display\ offscreen\ --working-format\ rgba16f\ --benchmark-mode\ gradient_fill\ --random-seed\ 123456\ --gl3-gradient\ semantic\ --frame-limit\ 2\ --hash\ pixel
                -DTEST_EXPECTED_FIELDS=target_strategy=gl3_persistent_fbo\;gradient_path=gl3_semantic_gradient\;qualified=false\;diagnostic_forced=true\;fallback_instances=0
                -P ${CMAKE_SOURCE_DIR}/cmake/RunNativeExecutionReportTest.cmake)
        set_tests_properties(${db_test_name}
                             PROPERTIES LABELS "${db_regression_labels}")
        db_test_apply_skip_regex("${db_test_name}"
                                 "${DB_TEST_GLFW_ENV_SKIP_REGEX}")

        foreach(db_qualification_kind IN ITEMS gl3_semantic vulkan_exact)
            db_suite_make_test_name(
                db_test_name "${db_prefix}"
                "regression_automatic_qualification_${db_qualification_kind}")
            add_test(
                NAME ${db_test_name}
                COMMAND
                    ${CMAKE_COMMAND} -DTEST_BIN=${db_test_bin}
                    -DTEST_KIND=${db_qualification_kind} -P
                    ${CMAKE_SOURCE_DIR}/cmake/RunAutomaticQualificationTest.cmake
            )
            db_test_timeout_seconds(db_qualification_timeout determinism_gpu)
            set_tests_properties(
                ${db_test_name}
                PROPERTIES LABELS "${db_regression_labels};probe" TIMEOUT
                           "${db_qualification_timeout}" RESOURCE_LOCK
                           driverbench_gpu_matrix)
            db_test_apply_skip_regex("${db_test_name}"
                                     "${DB_TEST_CANONICAL_SKIP_REGEX}")
        endforeach()

        if("${db_prefix}" STREQUAL "")
            foreach(db_hardware_kind IN ITEMS gl1_direct_window gl3_native)
                set(db_test_name "hardware_${db_hardware_kind}_auto")
                add_test(
                    NAME ${db_test_name}
                    COMMAND
                        ${CMAKE_COMMAND} -DTEST_BIN=${db_test_bin}
                        -DTEST_KIND=${db_hardware_kind} -P
                        ${CMAKE_SOURCE_DIR}/cmake/RunHardwareQualificationTest.cmake
                )
                set_tests_properties(
                    ${db_test_name}
                    PROPERTIES LABELS "hardware;qualification" TIMEOUT 120
                               RESOURCE_LOCK driverbench_gpu_matrix)
                db_test_apply_skip_regex("${db_test_name}"
                                         "${DB_TEST_CANONICAL_SKIP_REGEX}")
            endforeach()
            if(DB_BUILD_VULKAN AND DB_VULKAN_LIB)
                foreach(db_hardware_kind IN
                        ITEMS vulkan_single vulkan_device_group
                              vulkan_independent)
                    set(db_test_name "hardware_${db_hardware_kind}_auto")
                    add_test(
                        NAME ${db_test_name}
                        COMMAND
                            ${CMAKE_COMMAND} -DTEST_BIN=${db_test_bin}
                            -DTEST_KIND=${db_hardware_kind} -P
                            ${CMAKE_SOURCE_DIR}/cmake/RunHardwareQualificationTest.cmake
                    )
                    set_tests_properties(
                        ${db_test_name}
                        PROPERTIES LABELS "hardware;qualification" TIMEOUT 180
                                   RESOURCE_LOCK driverbench_gpu_matrix)
                    db_test_apply_skip_regex("${db_test_name}"
                                             "${DB_TEST_CANONICAL_SKIP_REGEX}")
                endforeach()
            endif()
        endif()

        db_suite_make_test_name(db_test_name "${db_prefix}"
                                "regression_gl1_gradient_row_fill_execution")
        add_test(
            NAME ${db_test_name}
            COMMAND
                ${CMAKE_COMMAND} -DTEST_BIN=${db_test_bin}
                -DTEST_ARGS=--api\ opengl\ --renderer\ gl1_5_gles1_1\ --display\ offscreen\ --benchmark-mode\ gradient_fill\ --gl1-gradient\ row-fill\ --frame-limit\ 2\ --hash\ pixel
                -DTEST_EXPECTED_FIELDS=target_strategy=gl1_persistent_fbo\;gradient_path=gl1_row_fill\;cpu_pixels_written=0\;uploaded_bytes=0
                -P ${CMAKE_SOURCE_DIR}/cmake/RunNativeExecutionReportTest.cmake)
        set_tests_properties(${db_test_name}
                             PROPERTIES LABELS "${db_regression_labels}")
        db_test_apply_skip_regex("${db_test_name}"
                                 "${DB_TEST_GLFW_ENV_SKIP_REGEX}")

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
        db_test_apply_skip_regex("${db_test_name}"
                                 "${DB_TEST_CANONICAL_SKIP_REGEX}")

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
