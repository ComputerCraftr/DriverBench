if(NOT DEFINED SOURCE_ROOT OR "${SOURCE_ROOT}" STREQUAL "")
    message(FATAL_ERROR "SOURCE_ROOT is required")
endif()

if(NOT DEFINED RULE_SET OR "${RULE_SET}" STREQUAL "")
    message(FATAL_ERROR "RULE_SET is required")
endif()

set(DB_PP_SOURCE_ROOT "${SOURCE_ROOT}/src")
set(DB_PP_CORE_ROOT "${DB_PP_SOURCE_ROOT}/core")
set(DB_PP_BENCHMARK_ROOT "${DB_PP_SOURCE_ROOT}/benchmarks")
set(DB_PP_DISPLAY_ROOT "${DB_PP_SOURCE_ROOT}/displays")
set(DB_PP_RENDERER_ROOT "${DB_PP_SOURCE_ROOT}/renderers")
set(DB_PP_TEST_ROOT "${SOURCE_ROOT}/tests")
set(DB_PP_CORE_IMPLEMENTATION "${DB_PP_CORE_ROOT}/db_core.c")
set(DB_PP_CORE_HEADER "${DB_PP_CORE_ROOT}/db_core.h")
set(DB_PP_NUMERIC_HEADER "${DB_PP_CORE_ROOT}/db_numeric.h")
set(DB_PP_GL1_ROOT "${DB_PP_RENDERER_ROOT}/opengl_gl1_5_gles1_1")
set(DB_PP_GL1_FRAME_FILE "${DB_PP_GL1_ROOT}/gl1_frame.c")
set(DB_PP_GL1_INTERNAL_FILE "${DB_PP_GL1_ROOT}/gl1_internal.h")
set(DB_PP_GL_DISPLAY_RUNTIME_FILES "${DB_PP_DISPLAY_ROOT}/gl_display_runtime.c"
                                   "${DB_PP_DISPLAY_ROOT}/gl_display_runtime.h")
set(DB_PP_GLFW_ROOT "${DB_PP_DISPLAY_ROOT}/glfw_window")
set(DB_PP_GLFW_IMPLEMENTATION "${DB_PP_GLFW_ROOT}/glfw_window.c")
set(DB_PP_GLFW_COMMON_IMPLEMENTATION "${DB_PP_GLFW_ROOT}/glfw_window_common.c")
set(DB_PP_GLFW_COMMON_FILES "${DB_PP_GLFW_COMMON_IMPLEMENTATION}"
                            "${DB_PP_GLFW_ROOT}/glfw_window_common.h")
set(DB_PP_GL_PROC_FILES "${DB_PP_RENDERER_ROOT}/gl_proc.c"
                        "${DB_PP_RENDERER_ROOT}/gl_proc_runtime.h")
set(DB_PP_LOG_IMPLEMENTATION "${DB_PP_CORE_ROOT}/db_log.c")
set(DB_PP_DISPLAY_RUNTIME_CONFIG
    "${DB_PP_DISPLAY_ROOT}/display_runtime_config_common.h")
set(DB_PP_RENDERER_GLOBS
    "${DB_PP_RENDERER_ROOT}/*.c" "${DB_PP_RENDERER_ROOT}/*.h"
    "${DB_PP_RENDERER_ROOT}/*/*.c" "${DB_PP_RENDERER_ROOT}/*/*.h")
set(DB_PP_DISPLAY_GLOBS
    "${DB_PP_DISPLAY_ROOT}/*.c" "${DB_PP_DISPLAY_ROOT}/*.h"
    "${DB_PP_DISPLAY_ROOT}/*/*.c" "${DB_PP_DISPLAY_ROOT}/*/*.h")
set(DB_PP_CORE_BENCHMARK_GLOBS
    "${DB_PP_CORE_ROOT}/*.c" "${DB_PP_CORE_ROOT}/*.h"
    "${DB_PP_BENCHMARK_ROOT}/*.c" "${DB_PP_BENCHMARK_ROOT}/*.h")
set(DB_PP_SOURCE_GLOBS
    "${DB_PP_SOURCE_ROOT}/*.c" "${DB_PP_SOURCE_ROOT}/*.h"
    "${DB_PP_SOURCE_ROOT}/*/*.c" "${DB_PP_SOURCE_ROOT}/*/*.h"
    "${DB_PP_SOURCE_ROOT}/*/*/*.c" "${DB_PP_SOURCE_ROOT}/*/*/*.h")
set(DB_PP_TEST_GLOBS "${DB_PP_TEST_ROOT}/*.c" "${DB_PP_TEST_ROOT}/*.h"
                     "${DB_PP_TEST_ROOT}/*/*.c" "${DB_PP_TEST_ROOT}/*/*.h")

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
        ${DB_PP_GL_DISPLAY_RUNTIME_FILES} "${DB_PP_GLFW_IMPLEMENTATION}"
        ${DB_PP_GLFW_COMMON_FILES})

    set(DB_PP_ALLOWED_FILES ${DB_PP_GL_DISPLAY_RUNTIME_FILES}
                            ${DB_PP_GLFW_COMMON_FILES})

    db_pp_check_forbidden_outside_allowlist(
        DB_PP_SCAN_FILES
        DB_PP_ALLOWED_FILES
        "#ifdef __linux__|#if defined\\(__linux__\\)|#ifdef __APPLE__|#if defined\\(__APPLE__\\)"
        "platform-specific GLFW/OpenGL policy branches must live only in approved display policy/helper files"
    )

    db_pp_check_forbidden_in_files(
        DB_PP_GL_DISPLAY_RUNTIME_FILES
        "Linux GLFW|Apple GLFW|macOS GLFW"
        "shared OpenGL display policy must not contain platform-specific GLFW reason text"
    )

    set(DB_PP_GLFW_HINT_FILES "${DB_PP_GLFW_IMPLEMENTATION}"
                              "${DB_PP_GLFW_COMMON_IMPLEMENTATION}")
    db_pp_check_forbidden_in_files(
        DB_PP_GLFW_HINT_FILES "GLFW_(RED|GREEN|BLUE|ALPHA)_BITS"
        "GLFW native color bits must come from the resolved output contract")

    set(DB_PP_KMS_FORMAT_FILES
        "${SOURCE_ROOT}/src/displays/linux_kms_atomic/kms_cpu.c"
        "${SOURCE_ROOT}/src/displays/linux_kms_atomic/kms_core.c"
        "${SOURCE_ROOT}/src/displays/linux_kms_atomic/kms_gl_frame.c")
    db_pp_check_forbidden_in_files(
        DB_PP_KMS_FORMAT_FILES "GBM_FORMAT_(XRGB8888|XRGB2101010)"
        "KMS producers must consume the centralized native output format")

elseif(RULE_SET STREQUAL "renderer_gl_upload_policy")
    db_pp_collect_files(DB_PP_SCAN_FILES ${DB_PP_RENDERER_GLOBS})

    set(DB_PP_ALLOWED_FILES "${DB_PP_RENDERER_ROOT}/gl_runtime.c"
                            ${DB_PP_GL_PROC_FILES})

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

    set(DB_PP_HDR_UPLOAD_FILE
        "${SOURCE_ROOT}/src/renderers/gl_shadow_present_hdr.c")
    file(READ "${DB_PP_HDR_UPLOAD_FILE}" DB_PP_HDR_UPLOAD_CONTENT)
    string(FIND "${DB_PP_HDR_UPLOAD_CONTENT}"
                "void db_gl_shadow_present_upload_hdr_damage_blocks"
                DB_PP_HDR_UPLOAD_START)
    if(DB_PP_HDR_UPLOAD_START EQUAL -1)
        db_pp_append_failure("${DB_PP_HDR_UPLOAD_FILE}"
                             "missing bounded HDR damage upload implementation")
    else()
        string(SUBSTRING "${DB_PP_HDR_UPLOAD_CONTENT}"
                         ${DB_PP_HDR_UPLOAD_START} -1 DB_PP_HDR_UPLOAD_BODY)
        if(DB_PP_HDR_UPLOAD_BODY MATCHES "(malloc|calloc|realloc)[ \t\r\n]*[(]")
            db_pp_append_failure(
                "${DB_PP_HDR_UPLOAD_FILE}"
                "HDR damage upload must use provisioned workspace and cannot allocate in the frame hot path"
            )
        endif()
    endif()

elseif(RULE_SET STREQUAL "renderer_ir_policy")
    db_pp_collect_files(DB_PP_SCAN_FILES ${DB_PP_RENDERER_GLOBS}
                        ${DB_PP_DISPLAY_GLOBS})

    db_pp_check_forbidden_in_files(
        DB_PP_SCAN_FILES
        "db_colored_f64_block_t|db_geometry_execution_t|db_frame_plan_draw_fill|geometry\\.current_blocks|rebuild_seed\\.geometry"
        "renderers and displays must consume canonical render IR instead of legacy colored-block storage"
    )
    db_pp_check_forbidden_in_files(
        DB_PP_SCAN_FILES
        "db_render_ir_command_range_rect_(count|at)"
        "renderers must expand each IR command range once through the bounded range-copy contract"
    )
    db_pp_check_forbidden_in_files(
        DB_PP_SCAN_FILES
        "db_render_ir_rect_(count|at)|db_vk_frame_rect_at"
        "renderers must use stateful flattened-IR iterators instead of repeated indexed scans"
    )

    db_pp_check_forbidden_in_files(
        DB_PP_SCAN_FILES "static[ \t]+int[ \t]+cached_result"
        "live backend probe results must not use unkeyed process-global caches")

    db_pp_collect_files(DB_PP_CORE_FILES ${DB_PP_CORE_BENCHMARK_GLOBS})
    db_pp_check_forbidden_in_files(
        DB_PP_CORE_FILES
        "#[ \t]*include[ \t]*[<\"][^>\"]*(renderers|GLFW|vulkan|OpenGL|GL/)"
        "benchmark and IR core modules must not include renderer or native graphics headers"
    )

elseif(RULE_SET STREQUAL "platform_proc_loading")
    db_pp_collect_files(DB_PP_SCAN_FILES ${DB_PP_RENDERER_GLOBS})

    set(DB_PP_ALLOWED_FILES ${DB_PP_GL_PROC_FILES})

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
    db_pp_collect_files(DB_PP_CORE_BENCHMARK_FILES
                        ${DB_PP_CORE_BENCHMARK_GLOBS})
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

    db_pp_collect_files(DB_PP_RENDER_DISPLAY_FILES ${DB_PP_RENDERER_GLOBS}
                        ${DB_PP_DISPLAY_GLOBS})
    db_pp_check_forbidden_in_files(
        DB_PP_RENDER_DISPLAY_FILES
        "DB_PATTERN_|is_snake|snake\\.|renderer_snake_|db_benchmark_core"
        "renderers and displays must consume generic frame contracts, not benchmark state"
    )

    db_pp_collect_files(DB_PP_RENDERER_FILES ${DB_PP_RENDERER_GLOBS})
    db_pp_check_forbidden_in_files(
        DB_PP_RENDERER_FILES "#include[ \t]+\"[^\"]*benchmarks/"
        "renderers must not include benchmark implementation headers")
    db_pp_check_forbidden_in_files(
        DB_PP_RENDERER_FILES
        "#include[ \t]+\"[^\"]*displays/|ownership_generation|history_pair"
        "renderer subsystems must use core contracts and current subsystem generations"
    )

    set(DB_PP_IR_HISTORY_ALLOWED_FILES "${DB_PP_GL1_INTERNAL_FILE}"
                                       "${DB_PP_GL1_ROOT}/gl1_replay.c")
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

    db_pp_collect_files(DB_PP_GL1_FILES "${DB_PP_GL1_ROOT}/*.c"
                        "${DB_PP_GL1_ROOT}/*.h")
    db_pp_check_forbidden_in_files(
        DB_PP_GL1_FILES
        "begin_full_upload_target|present_full_upload_target|PRESERVE_RING_COHERENT|slot_matches_shadow"
        "GL1 must use one authoritative CPU backing and transient partial-upload streams"
    )

    db_pp_collect_files(DB_PP_DISPLAY_FILES ${DB_PP_DISPLAY_GLOBS})
    db_pp_check_forbidden_in_files(
        DB_PP_DISPLAY_FILES
        "#include[ \t]+\"[^\"]*renderers/[^\"]*internal\\.h\""
        "displays must not include renderer-internal headers")

elseif(RULE_SET STREQUAL "logging_policy")
    db_pp_collect_files(DB_PP_ALL_LOG_SOURCE_FILES ${DB_PP_SOURCE_GLOBS})
    set(DB_PP_VK_PRESENTATION_FILES
        "${SOURCE_ROOT}/src/renderers/vulkan_1_2_multi_gpu/vk_runtime_frame.c")
    db_pp_check_forbidden_in_files(
        DB_PP_VK_PRESENTATION_FILES "vkCmd(CopyImage|BlitImage)\\("
        "Vulkan final presentation must use the sampled fullscreen pipeline")
    set(DB_PP_ALLOWED_STDIO_LOG_FILES
        "${DB_PP_LOG_IMPLEMENTATION}" "${DB_PP_CORE_IMPLEMENTATION}"
        "${DB_PP_SOURCE_ROOT}/driverbench_cli.c")
    db_pp_check_forbidden_outside_allowlist(
        DB_PP_ALL_LOG_SOURCE_FILES
        DB_PP_ALLOWED_STDIO_LOG_FILES
        "(^|[^A-Za-z0-9_])(fputs|fprintf|printf|puts)[ \\t\\r\\n]*\\("
        "project output must use the structured logger; only core serialization and CLI help may write directly"
    )
    set(DB_PP_ALLOWED_LOG_PREFIX_FILES "${DB_PP_LOG_IMPLEMENTATION}")
    db_pp_check_forbidden_outside_allowlist(
        DB_PP_ALL_LOG_SOURCE_FILES DB_PP_ALLOWED_LOG_PREFIX_FILES
        "\"\\[[A-Za-z0-9_.-]+\\]\\[(info|error)\\]"
        "project log prefixes must be emitted only by the structured logger")

    db_pp_collect_files(DB_PP_LOG_SCAN_FILES ${DB_PP_DISPLAY_GLOBS}
                        ${DB_PP_RENDERER_GLOBS})

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
        "${DB_PP_CORE_IMPLEMENTATION}" "${DB_PP_CORE_HEADER}"
        "${DB_PP_DISPLAY_RUNTIME_CONFIG}")
    db_pp_check_forbidden_outside_allowlist(
        DB_PP_LOG_SCAN_FILES
        DB_PP_ALLOWED_FINAL_SUMMARY_FILES
        "db_benchmark_log_final\\("
        "final benchmark summaries must be emitted only by the shared benchmark/final-summary helpers"
    )

    set(DB_PP_ALLOWED_DRAW_STATS_FILES "${DB_PP_DISPLAY_RUNTIME_CONFIG}")
    db_pp_check_forbidden_outside_allowlist(
        DB_PP_LOG_SCAN_FILES DB_PP_ALLOWED_DRAW_STATS_FILES
        "db_display_log_draw_stats_with_fn\\("
        "draw stats must be emitted only by the shared final-summary helper")

elseif(RULE_SET STREQUAL "bool_normalization_policy")
    execute_process(
        COMMAND
            "${CMAKE_COMMAND}" -DSOURCE_ROOT=${SOURCE_ROOT}
            -DRULE_SET=architecture_boundaries -P "${CMAKE_CURRENT_LIST_FILE}"
        RESULT_VARIABLE DB_PP_ARCHITECTURE_RESULT)
    if(NOT DB_PP_ARCHITECTURE_RESULT EQUAL 0)
        message(FATAL_ERROR "Architecture boundary policy failed")
    endif()
    db_pp_collect_files(DB_PP_SCAN_FILES ${DB_PP_SOURCE_GLOBS}
                        ${DB_PP_TEST_GLOBS})

    set(DB_PP_ALLOWED_FILES "${DB_PP_NUMERIC_HEADER}")

    db_pp_check_forbidden_outside_allowlist(
        DB_PP_SCAN_FILES
        DB_PP_ALLOWED_FILES
        "\\?[ \t\r\n]*1[ \t\r\n]*:[ \t\r\n]*0|\\?[ \t\r\n]*0[ \t\r\n]*:[ \t\r\n]*1|return[ \t]+[^;\n]*!=[ \t]*0[ \t]*;|(^|[ \t(])((const[ \t]+)?int)[ \t]+[A-Za-z_][A-Za-z0-9_]*[ \t]*=[ \t]*[^;\n]*!=[ \t]*0[ \t]*;"
        "boolean-normalization boundaries must use DB_BOOL(...) instead of raw != 0 or ? 1 : 0 / ? 0 : 1 patterns"
    )

elseif(RULE_SET STREQUAL "numeric_boundary_policy")
    db_pp_collect_files(DB_PP_SCAN_FILES ${DB_PP_SOURCE_GLOBS})

    db_pp_collect_files(DB_PP_ENUM_SCAN_FILES ${DB_PP_SOURCE_GLOBS}
                        ${DB_PP_TEST_GLOBS})

    db_pp_check_forbidden_in_files(
        DB_PP_ENUM_SCAN_FILES
        "enum[^{;]*\\{[^}]*0[xX][89AaBbCcDdEeFf][0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f]"
        "ISO C enum values must fit in int; use an explicitly typed constant for high-bit data values"
    )

    set(DB_PP_ALLOWED_FLOAT_CAST_FILES "${DB_PP_NUMERIC_HEADER}")
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

    set(DB_PP_ALLOWED_RAW_ALLOCATION_MATH "${DB_PP_CORE_HEADER}")
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

    set(DB_PP_NUMERIC_PRIMITIVE_FILES "${DB_PP_CORE_IMPLEMENTATION}"
                                      "${DB_PP_CORE_HEADER}")
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
    db_pp_collect_files(DB_PP_SCAN_FILES ${DB_PP_SOURCE_GLOBS})
    set(DB_PP_SORT_IMPLEMENTATION_FILES "${DB_PP_CORE_ROOT}/db_sort.c")
    db_pp_check_forbidden_outside_allowlist(
        DB_PP_SCAN_FILES
        DB_PP_SORT_IMPLEMENTATION_FILES
        "(^|[^A-Za-z0-9_])(qsort|heapsort|mergesort)[ \\t\\r\\n]*\\(|insertion[_A-Za-z0-9]*sort|sort[_A-Za-z0-9]*doubles|(^|[^A-Za-z0-9_])stable_sort_|[A-Za-z0-9_]*(heap|merge)[A-Za-z0-9_]*sort[A-Za-z0-9_]*[ \\t\\r\\n]*\\("
        "sorting must use the canonical db_sort policy instead of local or direct libc implementations"
    )
    db_pp_check_forbidden_outside_allowlist(
        DB_PP_SCAN_FILES
        DB_PP_SORT_IMPLEMENTATION_FILES
        "\\[[A-Za-z_][A-Za-z0-9_]*[ \\t]*-[ \\t]*1U\\][^\\}]*[A-Za-z_][A-Za-z0-9_]*--"
        "adjacent-shift insertion sorting must use the canonical db_sort policy"
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
