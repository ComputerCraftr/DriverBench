if(NOT DEFINED SOURCE_ROOT OR "${SOURCE_ROOT}" STREQUAL "")
    message(FATAL_ERROR "SOURCE_ROOT is required")
endif()

if(NOT DEFINED RULE_SET OR "${RULE_SET}" STREQUAL "")
    message(FATAL_ERROR "RULE_SET is required")
endif()

function(db_pp_collect_files OUT_VAR)
    set(DB_PP_FILES "")
    foreach(DB_PP_GLOB IN LISTS ARGN)
        file(GLOB_RECURSE DB_PP_GLOB_MATCHES ${DB_PP_GLOB})
        list(APPEND DB_PP_FILES ${DB_PP_GLOB_MATCHES})
    endforeach()
    list(REMOVE_DUPLICATES DB_PP_FILES)
    set(${OUT_VAR}
        "${DB_PP_FILES}"
        PARENT_SCOPE)
endfunction()

function(db_pp_append_failure FILE MESSAGE_TEXT)
    set(DB_FAILURES
        "${DB_FAILURES}${FILE}: ${MESSAGE_TEXT}\n"
        PARENT_SCOPE)
endfunction()

function(db_pp_check_forbidden_outside_allowlist FILES_VAR ALLOWED_VAR
         FORBIDDEN_REGEX MESSAGE_TEXT)
    foreach(DB_PP_FILE IN LISTS ${FILES_VAR})
        list(FIND ${ALLOWED_VAR} "${DB_PP_FILE}" DB_PP_ALLOWED_INDEX)
        if(NOT DB_PP_ALLOWED_INDEX EQUAL -1)
            continue()
        endif()
        file(READ "${DB_PP_FILE}" DB_PP_FILE_CONTENT)
        if(DB_PP_FILE_CONTENT MATCHES "${FORBIDDEN_REGEX}")
            db_pp_append_failure("${DB_PP_FILE}" "${MESSAGE_TEXT}")
        endif()
    endforeach()
    set(DB_FAILURES
        "${DB_FAILURES}"
        PARENT_SCOPE)
endfunction()

function(db_pp_check_forbidden_in_files FILES_VAR FORBIDDEN_REGEX MESSAGE_TEXT)
    foreach(DB_PP_FILE IN LISTS ${FILES_VAR})
        file(READ "${DB_PP_FILE}" DB_PP_FILE_CONTENT)
        if(DB_PP_FILE_CONTENT MATCHES "${FORBIDDEN_REGEX}")
            db_pp_append_failure("${DB_PP_FILE}" "${MESSAGE_TEXT}")
        endif()
    endforeach()
    set(DB_FAILURES
        "${DB_FAILURES}"
        PARENT_SCOPE)
endfunction()

set(DB_FAILURES "")

if(RULE_SET STREQUAL "display_glfw_policy")
    set(DB_PP_SCAN_FILES
        "${SOURCE_ROOT}/src/displays/gl_display_runtime.c"
        "${SOURCE_ROOT}/src/displays/gl_display_runtime.h"
        "${SOURCE_ROOT}/src/displays/glfw_window/glfw_window.c"
        "${SOURCE_ROOT}/src/displays/glfw_window/glfw_window_common.c"
        "${SOURCE_ROOT}/src/displays/glfw_window/glfw_window_common.h")

    set(DB_PP_ALLOWED_FILES
        "${SOURCE_ROOT}/src/displays/gl_display_runtime.c"
        "${SOURCE_ROOT}/src/displays/gl_display_runtime.h"
        "${SOURCE_ROOT}/src/displays/glfw_window/glfw_window_common.c"
        "${SOURCE_ROOT}/src/displays/glfw_window/glfw_window_common.h")

    db_pp_check_forbidden_outside_allowlist(
        DB_PP_SCAN_FILES
        DB_PP_ALLOWED_FILES
        "#ifdef __linux__|#if defined\\(__linux__\\)|#ifdef __APPLE__|#if defined\\(__APPLE__\\)"
        "platform-specific GLFW/OpenGL policy branches must live only in approved display policy/helper files"
    )

    set(DB_PP_WORDING_FILES "${SOURCE_ROOT}/src/displays/gl_display_runtime.c"
                            "${SOURCE_ROOT}/src/displays/gl_display_runtime.h")
    db_pp_check_forbidden_in_files(
        DB_PP_WORDING_FILES
        "Linux GLFW|Apple GLFW|macOS GLFW"
        "shared OpenGL display policy must not contain platform-specific GLFW reason text"
    )

    set(DB_PP_GLFW_HINT_FILES
        "${SOURCE_ROOT}/src/displays/glfw_window/glfw_window.c"
        "${SOURCE_ROOT}/src/displays/glfw_window/glfw_window_common.c")
    db_pp_check_forbidden_in_files(
        DB_PP_GLFW_HINT_FILES "GLFW_(RED|GREEN|BLUE|ALPHA)_BITS"
        "GLFW native color bits must come from the resolved output contract")

    set(DB_PP_KMS_FORMAT_FILES
        "${SOURCE_ROOT}/src/displays/linux_kms_atomic/kms_cpu.c"
        "${SOURCE_ROOT}/src/displays/linux_kms_atomic/kms_core.c")
    db_pp_check_forbidden_in_files(
        DB_PP_KMS_FORMAT_FILES "GBM_FORMAT_(XRGB8888|XRGB2101010)"
        "KMS producers must consume the centralized native output format")

elseif(RULE_SET STREQUAL "renderer_gl_upload_policy")
    db_pp_collect_files(
        DB_PP_SCAN_FILES "${SOURCE_ROOT}/src/renderers/*.c"
        "${SOURCE_ROOT}/src/renderers/*.h" "${SOURCE_ROOT}/src/renderers/*/*.c"
        "${SOURCE_ROOT}/src/renderers/*/*.h")

    set(DB_PP_ALLOWED_FILES
        "${SOURCE_ROOT}/src/renderers/gl_runtime.c"
        "${SOURCE_ROOT}/src/renderers/gl_proc.c"
        "${SOURCE_ROOT}/src/renderers/gl_proc_runtime.h")

    db_pp_check_forbidden_outside_allowlist(
        DB_PP_SCAN_FILES
        DB_PP_ALLOWED_FILES
        "#ifdef __APPLE__|#if defined\\(__APPLE__\\)"
        "Apple-specific OpenGL/buffer policy must live only in the approved GL resolver/proc files"
    )

    db_pp_check_forbidden_outside_allowlist(
        DB_PP_SCAN_FILES
        DB_PP_ALLOWED_FILES
        "Apple GLFW|macOS GLFW|Apple M[0-9]|MacOSX|macOS"
        "Apple-specific OpenGL/buffer policy text must live only in the approved GL resolver/proc files"
    )

elseif(RULE_SET STREQUAL "renderer_ir_policy")
    db_pp_collect_files(
        DB_PP_SCAN_FILES
        "${SOURCE_ROOT}/src/renderers/*.c"
        "${SOURCE_ROOT}/src/renderers/*.h"
        "${SOURCE_ROOT}/src/renderers/*/*.c"
        "${SOURCE_ROOT}/src/renderers/*/*.h"
        "${SOURCE_ROOT}/src/displays/*.c"
        "${SOURCE_ROOT}/src/displays/*.h"
        "${SOURCE_ROOT}/src/displays/*/*.c"
        "${SOURCE_ROOT}/src/displays/*/*.h")

    db_pp_check_forbidden_in_files(
        DB_PP_SCAN_FILES
        "db_colored_f64_block_t|db_geometry_execution_t|db_frame_plan_draw_fill|geometry\\.current_blocks|rebuild_seed\\.geometry"
        "renderers and displays must consume canonical render IR instead of legacy colored-block storage"
    )

    db_pp_check_forbidden_in_files(
        DB_PP_SCAN_FILES "static[ \t]+int[ \t]+cached_result"
        "live backend probe results must not use unkeyed process-global caches")

    db_pp_collect_files(
        DB_PP_CORE_FILES "${SOURCE_ROOT}/src/core/*.c"
        "${SOURCE_ROOT}/src/core/*.h" "${SOURCE_ROOT}/src/benchmarks/*.c"
        "${SOURCE_ROOT}/src/benchmarks/*.h")
    db_pp_check_forbidden_in_files(
        DB_PP_CORE_FILES
        "#[ \t]*include[ \t]*[<\"][^>\"]*(renderers|GLFW|vulkan|OpenGL|GL/)"
        "benchmark and IR core modules must not include renderer or native graphics headers"
    )

elseif(RULE_SET STREQUAL "platform_proc_loading")
    db_pp_collect_files(
        DB_PP_SCAN_FILES "${SOURCE_ROOT}/src/renderers/*.c"
        "${SOURCE_ROOT}/src/renderers/*.h" "${SOURCE_ROOT}/src/renderers/*/*.c"
        "${SOURCE_ROOT}/src/renderers/*/*.h")

    set(DB_PP_ALLOWED_FILES "${SOURCE_ROOT}/src/renderers/gl_proc.c"
                            "${SOURCE_ROOT}/src/renderers/gl_proc_runtime.h")

    db_pp_check_forbidden_outside_allowlist(
        DB_PP_SCAN_FILES
        DB_PP_ALLOWED_FILES
        "<dlfcn\\.h>|dlsym\\(|#if defined\\(__APPLE__\\) \\|\\| defined\\(__linux__\\)|#if defined\\(__linux__\\) \\|\\| defined\\(__APPLE__\\)"
        "platform-specific GL proc-loading logic must live only in the approved proc-runtime files"
    )

elseif(RULE_SET STREQUAL "cmake_platform_policy")
    set(DB_PP_SCAN_FILES "${SOURCE_ROOT}/CMakeLists.txt"
                         "${SOURCE_ROOT}/CMakePresets.json")
    file(GLOB DB_PP_CMAKE_FILES "${SOURCE_ROOT}/cmake/*.cmake"
         "${SOURCE_ROOT}/cmake/toolchains/*.cmake")
    list(APPEND DB_PP_SCAN_FILES ${DB_PP_CMAKE_FILES})

    set(DB_PP_ALLOWED_FILES
        "${SOURCE_ROOT}/cmake/DriverBenchPlatform.cmake"
        "${SOURCE_ROOT}/cmake/DriverBenchLinuxToolchain.cmake"
        "${SOURCE_ROOT}/cmake/toolchains/DriverBenchLinuxMusl.cmake"
        "${SOURCE_ROOT}/cmake/DriverBenchTestCapabilities.cmake"
        "${SOURCE_ROOT}/cmake/DriverBenchAlternateSuites.cmake"
        "${SOURCE_ROOT}/cmake/CheckPlatformPolicyBoundaries.cmake")

    db_pp_check_forbidden_outside_allowlist(
        DB_PP_SCAN_FILES
        DB_PP_ALLOWED_FILES
        "(^|[^A-Za-z0-9_])(APPLE|CMAKE_SYSTEM_NAME)([^A-Za-z0-9_]|$)"
        "direct platform branching must live only in approved platform/test-policy CMake modules"
    )

    db_pp_check_forbidden_outside_allowlist(
        DB_PP_SCAN_FILES
        DB_PP_ALLOWED_FILES
        "/usr/lib32|/usr/i686-pc-linux-gnu|/usr/[A-Za-z0-9._+-]+-linux-musl|NO_CMAKE_FIND_ROOT_PATH|CMAKE_(C|EXE_LINKER)_FLAGS_INIT[^\n]*gcc-toolchain"
        "host-specific Linux cross root/library path forcing must live only in the Linux toolchain policy module"
    )

    set(DB_PP_ALLOWED_TEST_TRACE_CMAKE_FILES
        "${SOURCE_ROOT}/cmake/DriverBenchTests.cmake")
    db_pp_check_forbidden_outside_allowlist(
        DB_PP_SCAN_FILES
        DB_PP_ALLOWED_TEST_TRACE_CMAKE_FILES
        "tests/support/gl1_compact_trace_support\\.c"
        "GL1 compact trace support must be linked only into the unit-test target"
    )

elseif(RULE_SET STREQUAL "architecture_boundaries")
    db_pp_collect_files(
        DB_PP_CORE_BENCHMARK_FILES "${SOURCE_ROOT}/src/core/*.c"
        "${SOURCE_ROOT}/src/core/*.h" "${SOURCE_ROOT}/src/benchmarks/*.c"
        "${SOURCE_ROOT}/src/benchmarks/*.h")
    db_pp_check_forbidden_in_files(
        DB_PP_CORE_BENCHMARK_FILES
        "#include[ \t]+\"[^\"]*(renderers|displays)/"
        "core and benchmark modules must not depend on renderer or display modules"
    )
    db_pp_check_forbidden_in_files(
        DB_PP_CORE_BENCHMARK_FILES
        "db_snake_compact_block|collect_compact_blocks|snake_damage_collector"
        "benchmarks must emit canonical semantic render IR directly")
    db_pp_check_forbidden_in_files(
        DB_PP_CORE_BENCHMARK_FILES
        "color_state_rgb|authoritative_grid_rgb|is_snake_history_texture|active_prior_rgb|db_snake_semantic_emit_completed_shape|completed_shape_count"
        "overlapping benchmark recovery must use the bounded canonical checkpoint rather than replay or duplicate prior-color history"
    )

    db_pp_collect_files(
        DB_PP_RENDER_DISPLAY_FILES
        "${SOURCE_ROOT}/src/renderers/*.c"
        "${SOURCE_ROOT}/src/renderers/*.h"
        "${SOURCE_ROOT}/src/renderers/*/*.c"
        "${SOURCE_ROOT}/src/renderers/*/*.h"
        "${SOURCE_ROOT}/src/displays/*.c"
        "${SOURCE_ROOT}/src/displays/*.h"
        "${SOURCE_ROOT}/src/displays/*/*.c"
        "${SOURCE_ROOT}/src/displays/*/*.h")
    db_pp_check_forbidden_in_files(
        DB_PP_RENDER_DISPLAY_FILES
        "DB_PATTERN_|is_snake|snake\\.|renderer_snake_|db_benchmark_core"
        "renderers and displays must consume generic frame contracts, not benchmark state"
    )

    db_pp_collect_files(
        DB_PP_RENDERER_FILES "${SOURCE_ROOT}/src/renderers/*.c"
        "${SOURCE_ROOT}/src/renderers/*.h" "${SOURCE_ROOT}/src/renderers/*/*.c"
        "${SOURCE_ROOT}/src/renderers/*/*.h")
    db_pp_check_forbidden_in_files(
        DB_PP_RENDERER_FILES "#include[ \t]+\"[^\"]*benchmarks/"
        "renderers must not include benchmark implementation headers")
    db_pp_check_forbidden_in_files(
        DB_PP_RENDERER_FILES
        "#include[ \t]+\"[^\"]*displays/|ownership_generation|history_pair"
        "renderer subsystems must use core contracts and current subsystem generations"
    )

    set(DB_PP_IR_HISTORY_ALLOWED_FILES
        "${SOURCE_ROOT}/src/renderers/opengl_gl1_5_gles1_1/gl1_internal.h"
        "${SOURCE_ROOT}/src/renderers/opengl_gl1_5_gles1_1/gl1_replay.c")
    db_pp_check_forbidden_outside_allowlist(
        DB_PP_RENDERER_FILES
        DB_PP_IR_HISTORY_ALLOWED_FILES
        "db_render_ir_(snapshot|owned_store)_t|db_render_ir_clone_replayable"
        "renderer IR history is permitted only in the bounded GL1 direct-window replay subsystem"
    )
    db_pp_check_forbidden_in_files(
        DB_PP_IR_HISTORY_ALLOWED_FILES
        "realloc[ \t\r\n]*[(]"
        "GL1 replay storage must be preallocated and must never grow at frame time"
    )

    db_pp_collect_files(
        DB_PP_GL1_FILES "${SOURCE_ROOT}/src/renderers/opengl_gl1_5_gles1_1/*.c"
        "${SOURCE_ROOT}/src/renderers/opengl_gl1_5_gles1_1/*.h")
    db_pp_check_forbidden_in_files(
        DB_PP_GL1_FILES
        "begin_full_upload_target|present_full_upload_target|PRESERVE_RING_COHERENT|slot_matches_shadow"
        "GL1 must use one authoritative CPU backing and transient partial-upload streams"
    )

    db_pp_collect_files(
        DB_PP_DISPLAY_FILES "${SOURCE_ROOT}/src/displays/*.c"
        "${SOURCE_ROOT}/src/displays/*.h" "${SOURCE_ROOT}/src/displays/*/*.c"
        "${SOURCE_ROOT}/src/displays/*/*.h")
    db_pp_check_forbidden_in_files(
        DB_PP_DISPLAY_FILES
        "#include[ \t]+\"[^\"]*renderers/[^\"]*internal\\.h\""
        "displays must not include renderer-internal headers")

elseif(RULE_SET STREQUAL "logging_policy")
    db_pp_collect_files(
        DB_PP_ALL_LOG_SOURCE_FILES
        "${SOURCE_ROOT}/src/*.c"
        "${SOURCE_ROOT}/src/*.h"
        "${SOURCE_ROOT}/src/*/*.c"
        "${SOURCE_ROOT}/src/*/*.h"
        "${SOURCE_ROOT}/src/*/*/*.c"
        "${SOURCE_ROOT}/src/*/*/*.h")
    set(DB_PP_VK_PRESENTATION_FILES
        "${SOURCE_ROOT}/src/renderers/vulkan_1_2_multi_gpu/vk_runtime_frame.c")
    db_pp_check_forbidden_in_files(
        DB_PP_VK_PRESENTATION_FILES "vkCmd(CopyImage|BlitImage)\\("
        "Vulkan final presentation must use the sampled fullscreen pipeline")
    set(DB_PP_ALLOWED_STDIO_LOG_FILES
        "${SOURCE_ROOT}/src/core/db_log.c" "${SOURCE_ROOT}/src/core/db_core.c"
        "${SOURCE_ROOT}/src/driverbench_cli.c")
    db_pp_check_forbidden_outside_allowlist(
        DB_PP_ALL_LOG_SOURCE_FILES
        DB_PP_ALLOWED_STDIO_LOG_FILES
        "(^|[^A-Za-z0-9_])(fputs|fprintf|printf|puts)[ \\t\\r\\n]*\\("
        "project output must use the structured logger; only core serialization and CLI help may write directly"
    )
    set(DB_PP_ALLOWED_LOG_PREFIX_FILES "${SOURCE_ROOT}/src/core/db_log.c")
    db_pp_check_forbidden_outside_allowlist(
        DB_PP_ALL_LOG_SOURCE_FILES DB_PP_ALLOWED_LOG_PREFIX_FILES
        "\"\\[[A-Za-z0-9_.-]+\\]\\[(info|error)\\]"
        "project log prefixes must be emitted only by the structured logger")

    set(DB_PP_PROGRESS_FILES "${SOURCE_ROOT}/src/core/db_core.c")
    db_pp_check_forbidden_in_files(
        DB_PP_PROGRESS_FILES "benchmark \\(%s\\): mode=%s"
        "periodic benchmark progress logs must not include static mode text")

    set(DB_PP_PRESENT_LOG_FILES
        "${SOURCE_ROOT}/src/renderers/gl_shadow_present.c")
    db_pp_check_forbidden_in_files(
        DB_PP_PRESENT_LOG_FILES
        "effective_full_present_upload=%s, partial_present_upload=%s"
        "shadow present decision logs must not append duplicated effective-mode text"
    )

    db_pp_collect_files(
        DB_PP_LOG_SCAN_FILES
        "${SOURCE_ROOT}/src/displays/*.c"
        "${SOURCE_ROOT}/src/displays/*.h"
        "${SOURCE_ROOT}/src/displays/*/*.c"
        "${SOURCE_ROOT}/src/displays/*/*.h"
        "${SOURCE_ROOT}/src/renderers/*.c"
        "${SOURCE_ROOT}/src/renderers/*.h"
        "${SOURCE_ROOT}/src/renderers/*/*.c"
        "${SOURCE_ROOT}/src/renderers/*/*.h")

    set(DB_PP_NO_RUNTIME_WRAPPER_ALLOWLIST "")
    db_pp_check_forbidden_outside_allowlist(
        DB_PP_LOG_SCAN_FILES
        DB_PP_NO_RUNTIME_WRAPPER_ALLOWLIST
        "(^|[^A-Za-z0-9_])(db_infof|db_errorf|db_failf|infof|failf|die)[ \t\r\n]*\\("
        "display and renderer runtime code must emit named typed log events")

    db_pp_check_forbidden_in_files(
        DB_PP_LOG_SCAN_FILES
        "schema=1|DB_LOG_(TOKEN|STRING)[ \t\r\n]*\\([ \t\r\n]*\"mode\"|db_log_renderer_capability_mode"
        "runtime logging must use schema 2 semantic fields instead of generic mode strings"
    )

    set(DB_PP_ALLOWED_FINAL_SUMMARY_FILES
        "${SOURCE_ROOT}/src/core/db_core.c" "${SOURCE_ROOT}/src/core/db_core.h"
        "${SOURCE_ROOT}/src/displays/display_runtime_config_common.h")
    db_pp_check_forbidden_outside_allowlist(
        DB_PP_LOG_SCAN_FILES
        DB_PP_ALLOWED_FINAL_SUMMARY_FILES
        "db_benchmark_log_final\\("
        "final benchmark summaries must be emitted only by the shared benchmark/final-summary helpers"
    )

    set(DB_PP_ALLOWED_DRAW_STATS_FILES
        "${SOURCE_ROOT}/src/displays/display_runtime_config_common.h")
    db_pp_check_forbidden_outside_allowlist(
        DB_PP_LOG_SCAN_FILES DB_PP_ALLOWED_DRAW_STATS_FILES
        "db_display_log_draw_stats_with_fn\\("
        "draw stats must be emitted only by the shared final-summary helper")

    set(DB_PP_ALLOWED_SCHEDULER_STATS_FILES
        "${SOURCE_ROOT}/src/renderers/vulkan_1_2_multi_gpu/vk_runtime.c")
    db_pp_check_forbidden_outside_allowlist(
        DB_PP_LOG_SCAN_FILES
        DB_PP_ALLOWED_SCHEDULER_STATS_FILES
        "scheduler stats:"
        "scheduler stats logs must be emitted only by the Vulkan final runtime summary"
    )

    set(DB_PP_SNAKE_RECOVERY_FILES
        "${SOURCE_ROOT}/src/renderers/opengl_gl1_5_gles1_1/gl1_frame.c")
    db_pp_check_forbidden_in_files(
        DB_PP_SNAKE_RECOVERY_FILES
        "Grid fast-path on invalid backbuffer"
        "snake-grid invalid backbuffer recovery must not use the old renderer-local clear-and-redraw workaround"
    )

    set(DB_PP_GL1_HISTORY_RECOVERY_FILES
        "${SOURCE_ROOT}/src/renderers/opengl_gl1_5_gles1_1/gl1_frame.c")
    db_pp_check_forbidden_in_files(
        DB_PP_GL1_HISTORY_RECOVERY_FILES
        "db_history_should_seed_full_on_invalid\\("
        "GL1 history-backed recovery must consume the shared recovery action instead of local invalid-backbuffer seed logic"
    )

    set(DB_PP_GL1_REPLAY_POLICY_FILES
        "${SOURCE_ROOT}/src/renderers/opengl_gl1_5_gles1_1/gl1_frame.c")
    db_pp_check_forbidden_in_files(
        DB_PP_GL1_REPLAY_POLICY_FILES
        "db_history_can_replay_previous_damage\\("
        "GL1 snake replay must use the shared preserved-backbuffer replay readiness helper instead of raw preserved-count replay checks"
    )

    set(DB_PP_GL1_SEED_DRAW_POLICY_FILES
        "${SOURCE_ROOT}/src/renderers/opengl_gl1_5_gles1_1/gl1_frame.c")
    db_pp_check_forbidden_in_files(
        DB_PP_GL1_SEED_DRAW_POLICY_FILES
        "snake_plan = &snake_frame->expanded_plan|retroactive_frames"
        "GL1 snake current draw must not use retroactive expanded replay plans during seed/recovery"
    )

    set(DB_PP_GL1_SHAPE_FALLBACK_POLICY_FILES
        "${SOURCE_ROOT}/src/renderers/opengl_gl1_5_gles1_1/gl1_frame.c")
    db_pp_check_forbidden_in_files(
        DB_PP_GL1_SHAPE_FALLBACK_POLICY_FILES
        "prefer_shadow_composite"
        "GL1 snake_shapes dirty replay must not force steady-state shadow fallback"
    )

    set(DB_PP_GL1_SINGLE_FRAME_REPLAY_FILES
        "${SOURCE_ROOT}/src/renderers/opengl_gl1_5_gles1_1/gl1_frame.c"
        "${SOURCE_ROOT}/src/renderers/opengl_gl1_5_gles1_1/gl1_internal.h")
    db_pp_check_forbidden_in_files(
        DB_PP_GL1_SINGLE_FRAME_REPLAY_FILES
        "prev_draw_blocks|prev_draw_block_count"
        "GL1 snake dirty replay must not regress to the single-frame prev_draw_blocks replay model"
    )

elseif(RULE_SET STREQUAL "bool_normalization_policy")
    execute_process(
        COMMAND
            "${CMAKE_COMMAND}" -DSOURCE_ROOT=${SOURCE_ROOT}
            -DRULE_SET=architecture_boundaries -P "${CMAKE_CURRENT_LIST_FILE}"
        RESULT_VARIABLE DB_PP_ARCHITECTURE_RESULT)
    if(NOT DB_PP_ARCHITECTURE_RESULT EQUAL 0)
        message(FATAL_ERROR "Architecture boundary policy failed")
    endif()
    db_pp_collect_files(
        DB_PP_SCAN_FILES
        "${SOURCE_ROOT}/src/*.c"
        "${SOURCE_ROOT}/src/*.h"
        "${SOURCE_ROOT}/src/*/*.c"
        "${SOURCE_ROOT}/src/*/*.h"
        "${SOURCE_ROOT}/tests/*.c"
        "${SOURCE_ROOT}/tests/*.h"
        "${SOURCE_ROOT}/tests/*/*.c"
        "${SOURCE_ROOT}/tests/*/*.h")

    set(DB_PP_ALLOWED_FILES "${SOURCE_ROOT}/src/core/db_numeric.h")

    db_pp_check_forbidden_outside_allowlist(
        DB_PP_SCAN_FILES
        DB_PP_ALLOWED_FILES
        "\\?[ \t\r\n]*1[ \t\r\n]*:[ \t\r\n]*0|\\?[ \t\r\n]*0[ \t\r\n]*:[ \t\r\n]*1|return[ \t]+[^;\n]*!=[ \t]*0[ \t]*;|(^|[ \t(])((const[ \t]+)?int)[ \t]+[A-Za-z_][A-Za-z0-9_]*[ \t]*=[ \t]*[^;\n]*!=[ \t]*0[ \t]*;"
        "boolean-normalization boundaries must use DB_BOOL(...) instead of raw != 0 or ? 1 : 0 / ? 0 : 1 patterns"
    )

elseif(RULE_SET STREQUAL "numeric_boundary_policy")
    db_pp_collect_files(
        DB_PP_SCAN_FILES
        "${SOURCE_ROOT}/src/*.c"
        "${SOURCE_ROOT}/src/*.h"
        "${SOURCE_ROOT}/src/*/*.c"
        "${SOURCE_ROOT}/src/*/*.h"
        "${SOURCE_ROOT}/src/*/*/*.c"
        "${SOURCE_ROOT}/src/*/*/*.h")

    db_pp_collect_files(
        DB_PP_ENUM_SCAN_FILES
        "${SOURCE_ROOT}/src/*.c"
        "${SOURCE_ROOT}/src/*.h"
        "${SOURCE_ROOT}/src/*/*.c"
        "${SOURCE_ROOT}/src/*/*.h"
        "${SOURCE_ROOT}/src/*/*/*.c"
        "${SOURCE_ROOT}/src/*/*/*.h"
        "${SOURCE_ROOT}/tests/*.c"
        "${SOURCE_ROOT}/tests/*.h"
        "${SOURCE_ROOT}/tests/*/*.c"
        "${SOURCE_ROOT}/tests/*/*.h")

    db_pp_check_forbidden_in_files(
        DB_PP_ENUM_SCAN_FILES
        "enum[^{;]*\\{[^}]*0[xX][89AaBbCcDdEeFf][0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f]"
        "ISO C enum values must fit in int; use an explicitly typed constant for high-bit data values"
    )

    set(DB_PP_ALLOWED_FLOAT_CAST_FILES "${SOURCE_ROOT}/src/core/db_numeric.h")
    db_pp_check_forbidden_outside_allowlist(
        DB_PP_SCAN_FILES
        DB_PP_ALLOWED_FLOAT_CAST_FILES
        "\\((float|double)\\)[ \\t\\r\\n]*[A-Za-z_(]"
        "scalar-to-float conversion must use DB_TO_F64 or the canonical narrowing helpers"
    )

    db_pp_check_forbidden_outside_allowlist(
        DB_PP_SCAN_FILES DB_PP_ALLOWED_FLOAT_CAST_FILES
        "(^|[^A-Za-z0-9_])(fmin|fmax)(f|l)?[ \\t\\r\\n]*\\("
        "floating-point extrema must use the canonical numeric policy helpers")

    db_pp_check_forbidden_outside_allowlist(
        DB_PP_SCAN_FILES
        DB_PP_ALLOWED_FLOAT_CAST_FILES
        "db_double_to_f16\\("
        "working-surface f16 storage must use the explicit f64-to-f32-to-f16 numeric policy helper"
    )

    db_pp_check_forbidden_in_files(
        DB_PP_SCAN_FILES
        "db_working_color_f64_to_f16|db_working_rgb_f64_to_f16_f32_rgb3|db_double_to_f32[ \\t\\r\\n]*\\([ \\t\\r\\n]*db_f16_to_double"
        "numeric conversion chains must use grouped helpers from db_numeric.h")

    db_pp_check_forbidden_in_files(
        DB_PP_SCAN_FILES
        "\\(int32_t\\)[ \\t\\r\\n]*pixel_block\\.|\\(uint32_t\\)[ \\t\\r\\n]*piece->[A-Za-z0-9_.]*offset"
        "pixel and Vulkan rectangle narrowing must use checked numeric boundary helpers"
    )

    set(DB_PP_ALLOWED_RAW_ALLOCATION_MATH "${SOURCE_ROOT}/src/core/db_core.h")
    db_pp_check_forbidden_outside_allowlist(
        DB_PP_SCAN_FILES
        DB_PP_ALLOWED_RAW_ALLOCATION_MATH
        "(malloc|realloc)[ \\t\\r\\n]*\\([^;\\n]*\\*[^;\\n]*\\)"
        "allocation byte counts must be checked before calling malloc or realloc"
    )

    db_pp_check_forbidden_in_files(
        DB_PP_SCAN_FILES
        "memcpy[ \\t\\r\\n]*\\([^;]*preserve_count[ \\t\\r\\n]*\\*[ \\t\\r\\n]*element_size"
        "array-preservation copy sizes must use checked multiplication")

    set(DB_PP_NUMERIC_PRIMITIVE_FILES "${SOURCE_ROOT}/src/core/db_core.c"
                                      "${SOURCE_ROOT}/src/core/db_core.h")
    db_pp_check_forbidden_outside_allowlist(
        DB_PP_SCAN_FILES
        DB_PP_NUMERIC_PRIMITIVE_FILES
        "(^|[^A-Za-z0-9_])(ckd_add|ckd_sub|ckd_mul|strtol|strtoul|strtod)[ \\t\\r\\n]*\\("
        "checked arithmetic and libc numeric parsing must remain behind core numeric boundaries"
    )

    db_pp_check_forbidden_in_files(
        DB_PP_SCAN_FILES
        "memcmp[ \\t\\r\\n]*\\([ \\t\\r\\n]*&"
        "memcmp must compare explicit byte arrays, not potentially padded object representations"
    )

    set(DB_PP_ALLOWED_IR_NARROWING_FILES
        "${SOURCE_ROOT}/src/core/db_render_ir.c"
        "${SOURCE_ROOT}/src/core/db_render_ir_query.c"
        "${SOURCE_ROOT}/src/core/db_render_ir_validate.c")
    db_pp_check_forbidden_outside_allowlist(
        DB_PP_SCAN_FILES
        DB_PP_ALLOWED_IR_NARROWING_FILES
        "\\(uint32_t\\)[ \\t\\r\\n]*(band|span)\\."
        "IR region coordinates must cross through db_render_ir_rect_to_grid_block before unsigned use"
    )

elseif(RULE_SET STREQUAL "sorting_policy")
    db_pp_collect_files(
        DB_PP_SCAN_FILES
        "${SOURCE_ROOT}/src/*.c"
        "${SOURCE_ROOT}/src/*.h"
        "${SOURCE_ROOT}/src/*/*.c"
        "${SOURCE_ROOT}/src/*/*.h"
        "${SOURCE_ROOT}/src/*/*/*.c"
        "${SOURCE_ROOT}/src/*/*/*.h")
    set(DB_PP_SORT_IMPLEMENTATION_FILES "${SOURCE_ROOT}/src/core/db_sort.c")
    db_pp_check_forbidden_outside_allowlist(
        DB_PP_SCAN_FILES
        DB_PP_SORT_IMPLEMENTATION_FILES
        "(^|[^A-Za-z0-9_])(qsort|heapsort|mergesort)[ \\t\\r\\n]*\\(|insertion[_A-Za-z0-9]*sort|sort[_A-Za-z0-9]*doubles"
        "sorting must use the canonical db_sort policy instead of local or direct libc implementations"
    )
    db_pp_check_forbidden_in_files(
        DB_PP_SCAN_FILES
        "\\(void\\)[ \\t\\r\\n]*db_sort_[A-Za-z0-9_]*[ \\t\\r\\n]*\\("
        "sort outcomes must be propagated rather than discarded")

else()
    message(FATAL_ERROR "Unknown RULE_SET: ${RULE_SET}")
endif()

if(NOT DB_FAILURES STREQUAL "")
    message(FATAL_ERROR "${DB_FAILURES}")
endif()
