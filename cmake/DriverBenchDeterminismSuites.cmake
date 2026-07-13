# Canonical determinism matrices and non-overlapping renderer behavior tests.
function(db_register_canonical_matrix test_prefix test_bin benchmark_mode
         golden_labels glfw_enabled)
    db_suite_make_test_name(db_test_name "${test_prefix}"
                            "determinism_${benchmark_mode}_canonical_matrix")
    set(vulkan_enabled OFF)
    if(glfw_enabled
       AND DB_BUILD_VULKAN
       AND DB_VULKAN_LIB)
        set(vulkan_enabled ON)
    endif()
    add_test(
        NAME ${db_test_name}
        COMMAND
            ${CMAKE_COMMAND} -DTEST_BIN=${test_bin}
            -DTEST_BENCHMARK=${benchmark_mode}
            -DTEST_GLFW_ENABLED=${glfw_enabled}
            -DTEST_VULKAN_ENABLED=${vulkan_enabled} -P
            ${CMAKE_SOURCE_DIR}/cmake/RunCanonicalDeterminismMatrix.cmake)
    set_tests_properties(
        ${db_test_name} PROPERTIES LABELS "${golden_labels}" TIMEOUT 240
                                   RESOURCE_LOCK driverbench_gpu_matrix)
endfunction()

function(db_register_gradient_family test_prefix test_bin golden_labels
         glfw_enabled)
    db_register_canonical_matrix("${test_prefix}" "${test_bin}" gradient_fill
                                 "${golden_labels}" "${glfw_enabled}")
    db_register_canonical_matrix("${test_prefix}" "${test_bin}" gradient_sweep
                                 "${golden_labels}" "${glfw_enabled}")
endfunction()

function(db_register_present_progress test_prefix test_bin regression_labels
         glfw_enabled)
    if(NOT glfw_enabled)
        return()
    endif()
    set(common
        "--benchmark-mode bands --random-seed 123456 --working-format rgba16f --output-format sdr --bench-speed 1 --vsync 0 --fps-cap 0 --hash pixel --hash-report final"
    )
    set(progress_cases cpu gl1 gl3)
    if(DB_BUILD_VULKAN AND DB_VULKAN_LIB)
        list(APPEND progress_cases vulkan)
    endif()
    foreach(progress_case IN LISTS progress_cases)
        if(progress_case STREQUAL "cpu")
            set(prefix "--api cpu --display glfw_window --glfw-hidden-window 1")
        elseif(progress_case STREQUAL "gl1")
            set(prefix
                "--api opengl --renderer gl1_5_gles1_1 --display glfw_window --glfw-hidden-window 1 --backbuffer-draw-mode dirty --present-buffer-mode single_source"
            )
        elseif(progress_case STREQUAL "gl3")
            set(prefix
                "--api opengl --renderer gl3_3 --display glfw_window --glfw-hidden-window 1 --backbuffer-draw-mode dirty"
            )
        else()
            set(prefix
                "--api vulkan --display glfw_window --glfw-hidden-window 1 --backbuffer-draw-mode dirty --vk-multi-device-policy auto"
            )
        endif()
        db_suite_make_test_name(
            db_test_name "${test_prefix}"
            "regression_${progress_case}_presentation_progress")
        db_add_hash_difference_test(
            "${db_test_name}"
            "${test_bin}"
            "${prefix} ${common} --frame-limit 1"
            "${prefix} ${common} --frame-limit 3"
            framebuffer_hash_final
            "${regression_labels}")
        db_test_apply_skip_regex("${db_test_name}"
                                 "${DB_TEST_GLFW_ENV_SKIP_REGEX}")
    endforeach()
endfunction()

function(
    db_register_bands_family
    test_prefix
    test_bin
    golden_labels
    regression_labels
    cli_labels
    glfw_enabled)
    db_register_canonical_matrix("${test_prefix}" "${test_bin}" bands
                                 "${golden_labels}" "${glfw_enabled}")
    db_register_present_progress("${test_prefix}" "${test_bin}"
                                 "${regression_labels}" "${glfw_enabled}")
    if(glfw_enabled)
        db_suite_make_test_name(db_test_name "${test_prefix}"
                                "regression_hidden_glfw_cpu_backend_identity")
        db_add_command_contract_test(
            "${db_test_name}"
            "${test_bin}"
            "--api cpu --display glfw_window --glfw-hidden-window 1 --benchmark-mode bands --frame-limit 1"
            0
            "event=window_visibility schema=2 visibility=hidden"
            ""
            "backend=display_glfw_window_cpu_renderer"
            "backend=display_offscreen"
            "${cli_labels}")
        db_test_apply_skip_regex("${db_test_name}"
                                 "${DB_TEST_GLFW_ENV_SKIP_REGEX}")

        db_suite_make_test_name(db_test_name "${test_prefix}"
                                "regression_cpu_offscreen_backend_identity")
        db_add_command_contract_test(
            "${db_test_name}"
            "${test_bin}"
            "--api cpu --display offscreen --benchmark-mode bands --frame-limit 1"
            0
            ""
            ""
            "backend=display_offscreen"
            "backend=display_glfw_window_cpu_renderer"
            "${cli_labels}")
    endif()
endfunction()

function(db_register_snake_grid_family test_prefix test_bin golden_labels
         regression_labels glfw_enabled)
    db_register_canonical_matrix("${test_prefix}" "${test_bin}" snake_grid
                                 "${golden_labels}" "${glfw_enabled}")
    if(NOT glfw_enabled)
        return()
    endif()
    db_register_cross_renderer_damage_trace_contract(
        "${test_prefix}" "${test_bin}" "${regression_labels}" snake_grid)

    db_suite_make_test_name(db_test_name "${test_prefix}"
                            "regression_gl3_snake_grid_damage_stages")
    db_add_damage_trace_presence_test(
        "${db_test_name}"
        "${test_bin}"
        "${DB_DETERMINISM_GL3_OFFSCREEN_PREFIX} --benchmark-mode snake_grid ${DB_DETERMINISM_COMMON_ARGS} --bench-speed 1024 --frame-limit 2 --trace-damage 1"
        gl3
        "render_target|present"
        "${regression_labels}")
    db_test_apply_skip_regex("${db_test_name}" "${DB_TEST_GLFW_ENV_SKIP_REGEX}")

    if(DB_BUILD_VULKAN AND DB_VULKAN_LIB)
        db_suite_make_test_name(db_test_name "${test_prefix}"
                                "regression_vulkan_snake_grid_damage_stages")
        db_add_damage_trace_presence_test(
            "${db_test_name}"
            "${test_bin}"
            "--api vulkan --display glfw_window --glfw-hidden-window 1 --benchmark-mode snake_grid ${DB_DETERMINISM_COMMON_ARGS} --bench-speed 1024 --frame-limit 2 --trace-damage 1"
            vulkan
            renderer_write
            "${regression_labels}")
        db_test_apply_skip_regex("${db_test_name}"
                                 "${DB_TEST_GLFW_ENV_SKIP_REGEX}")
    endif()

    if(NOT DB_TEST_RUNTIME_GL1_DIRTY_TRACE_ARGS STREQUAL "")
        db_suite_make_test_name(db_test_name "${test_prefix}"
                                "regression_gl1_dirty_upload_mode")
        db_add_command_contract_test(
            "${db_test_name}"
            "${test_bin}"
            "${DB_TEST_RUNTIME_GL1_DIRTY_TRACE_ARGS}"
            0
            "event=draw_stats|event=frame_plan|geometry_operation=incremental|effective_full_upload=\"map_buffer\"|effective_partial_upload=\"map_buffer\"|mapping_validated=true"
            "shape_fallback|event=gradient_config"
            ""
            ""
            "${regression_labels}")

        db_suite_make_test_name(db_test_name "${test_prefix}"
                                "regression_gl1_dirty_canonical_damage_stages")
        db_add_damage_trace_presence_test(
            "${db_test_name}" "${test_bin}"
            "${DB_TEST_RUNTIME_GL1_DIRTY_TRACE_ARGS}" gl1
            "logical,normalized,shadow_write,upload" "${regression_labels}")
    endif()

    if(NOT DB_TEST_RUNTIME_GL1_FULL_TRACE_ARGS STREQUAL "")
        db_suite_make_test_name(db_test_name "${test_prefix}"
                                "regression_gl1_full_canonical_damage_stages")
        db_add_damage_trace_presence_test(
            "${db_test_name}" "${test_bin}"
            "${DB_TEST_RUNTIME_GL1_FULL_TRACE_ARGS}" gl1
            "logical,normalized,shadow_write,upload" "${regression_labels}")

        db_suite_make_test_name(db_test_name "${test_prefix}"
                                "regression_gl1_full_buffer_progress")
        db_add_damage_trace_invariant_test(
            "${db_test_name}" "${test_bin}"
            "${DB_TEST_RUNTIME_GL1_FULL_TRACE_ARGS}" "${regression_labels}")
    endif()

    db_suite_make_test_name(db_test_name "${test_prefix}"
                            "regression_gl1_single_source_dirty_updates")
    db_add_command_contract_test(
        "${db_test_name}"
        "${test_bin}"
        "--api opengl --renderer gl1_5_gles1_1 --display glfw_window --glfw-hidden-window 1 --benchmark-mode snake_grid ${DB_DETERMINISM_COMMON_ARGS} --backbuffer-draw-mode dirty --present-buffer-mode single_source --bench-speed 1 --frame-limit 3 --trace-damage 1"
        0
        "preserve=\"single_source\"|event=frame_plan|geometry_operation=incremental"
        "compact_capacity|dirty update failed|dirty_mode_compact_fallback|event=gradient_config"
        "shadow_fallback_frames=0"
        ""
        "${regression_labels}")
endfunction()

function(db_register_snake_rect_family test_prefix test_bin golden_labels
         regression_labels glfw_enabled)
    db_register_canonical_matrix("${test_prefix}" "${test_bin}" snake_rect
                                 "${golden_labels}" "${glfw_enabled}")
    if(glfw_enabled)
        db_register_cross_renderer_damage_trace_contract(
            "${test_prefix}" "${test_bin}" "${regression_labels}" snake_rect)
    endif()
endfunction()

function(db_register_snake_shapes_family test_prefix test_bin golden_labels
         regression_labels glfw_enabled)
    db_register_canonical_matrix("${test_prefix}" "${test_bin}" snake_shapes
                                 "${golden_labels}" "${glfw_enabled}")
    if(glfw_enabled)
        db_register_cross_renderer_damage_trace_contract(
            "${test_prefix}" "${test_bin}" "${regression_labels}" snake_shapes)
    endif()
    if(glfw_enabled
       AND NOT DB_TEST_RUNTIME_GL1_DIRTY_SNAKE_SHAPES_CONTRACT_ARGS STREQUAL "")
        db_suite_make_test_name(
            db_test_name "${test_prefix}"
            "regression_gl1_dirty_snake_shapes_incremental_backing")
        db_add_command_contract_test(
            "${db_test_name}"
            "${test_bin}"
            "${DB_TEST_RUNTIME_GL1_DIRTY_SNAKE_SHAPES_CONTRACT_ARGS} --trace-damage 1"
            0
            "event=draw_stats|event=frame_plan|geometry_operation=incremental"
            shape_fallback
            "shadow_fallback_frames=0"
            ""
            "${regression_labels}")
    endif()
endfunction()
