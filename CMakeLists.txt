cmake_minimum_required(VERSION 3.24)

project(DriverBench C)
include(CTest)

include(CheckIncludeFile)
include(CheckSymbolExists)
include(${CMAKE_SOURCE_DIR}/cmake/DriverBenchPlatform.cmake)

set(DB_SOURCE_FILE_LINE_LIMIT 1000)
set(DB_SOURCE_FILE_SOFT_LINE_LIMIT 800)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(DB_COMPILER_IS_CLANG OFF)
if(CMAKE_C_COMPILER_ID MATCHES "^(Clang|AppleClang)$")
    set(DB_COMPILER_IS_CLANG ON)
endif()

set(DB_COMPILER_IS_GNU OFF)
if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    set(DB_COMPILER_IS_GNU ON)
endif()

set(DB_COMPILER_IS_GNU_LIKE OFF)
if(DB_COMPILER_IS_CLANG OR DB_COMPILER_IS_GNU)
    set(DB_COMPILER_IS_GNU_LIKE ON)
endif()
include(${CMAKE_SOURCE_DIR}/cmake/DriverBenchWarnings.cmake)

set(DB_FOUNDATION_SOURCES
    src/config/runtime_options.c
    src/core/db_buffer_convert.c
    src/core/db_core.c
    src/core/db_log.c
    src/core/db_metrics_policy.c
    src/core/db_progress_policy.c
    src/core/db_renderer_diagnostics.c
    src/core/db_sort.c
    src/core/db_hash.c
    src/core/db_hash_simd.c
    src/core/db_trace.c)

set(DB_RENDER_IR_SOURCES
    src/core/db_frame_plan.c
    src/core/db_render_ir.c
    src/core/db_render_ir_query.c
    src/core/db_render_ir_ranges.c
    src/core/db_render_ir_lowering.c
    src/core/db_render_ir_optimizer.c
    src/core/db_render_ir_region.c
    src/core/db_render_ir_snapshot.c
    src/core/db_render_ir_surface.c
    src/core/db_render_ir_validate.c
    src/core/db_replay_policy.c)

set(DB_PLATFORM_PROCESS_SOURCES
    src/core/db_process_environment.c src/core/db_probe_process.c
    src/core/db_probe_protocol.c)

set(DB_QUALIFICATION_CONTRACT_SOURCES src/core/db_conformance.c
                                      src/core/db_qualification_contracts.c)

set(DB_QUALIFICATION_SERVICE_SOURCES
    src/core/db_conformance_cache.c src/core/db_conformance_service.c
    src/core/db_gradient_divergence.c)

set(DB_FRAME_CONTRACT_SOURCES src/core/db_frame_contracts.c)

set(DB_BENCHMARK_MODEL_SOURCES
    src/benchmarks/db_benchmark_core.c
    src/benchmarks/db_benchmark_workspace.c
    src/benchmarks/db_benchmark_requirements.c
    src/benchmarks/db_snake_fast_forward.c
    src/benchmarks/db_benchmark_checkpoint.c
    src/benchmarks/db_benchmark_emitters.c
    src/benchmarks/db_benchmark_model.c)

set(DB_FRAME_COORDINATOR_SOURCES src/core/db_frame_coordinator.c)
set(DB_RUN_SESSION_SOURCES src/core/db_run_session.c)

if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|i[3-6]86)$")
    list(APPEND DB_FOUNDATION_SOURCES src/core/db_hash_simd_x86.c)
endif()

option(DB_BUILD_VULKAN "Build Vulkan benchmark backend" ON)
option(DB_BUILD_GLFW_WINDOW_DISPLAY "Build GLFW window display backend" ON)
option(
    DB_GLFW_REQUIRED
    "Fail configure if a requested GLFW provider is unavailable instead of disabling GLFW-backed displays"
    OFF)
option(DB_BUILD_LINUX_KMS_ATOMIC_DISPLAY
       "Build Linux KMS atomic display backend" ON)
set(DB_GLFW_PROVIDER
    "vendored"
    CACHE STRING "GLFW provider: vendored, system, or off")
set_property(CACHE DB_GLFW_PROVIDER PROPERTY STRINGS vendored system off)
option(DB_ENABLE_LOOP_HINTS "Enable loop optimization hint flags" ON)
option(DB_ENABLE_LTO "Enable link-time optimization in release-like builds" OFF)
option(DB_ENABLE_SANITIZERS "Enable sanitizers for debug builds" ON)
option(DB_REQUIRE_SANITIZER_RUNTIME
       "Fail configuration when requested sanitizer runtimes cannot link" OFF)
option(DB_ENABLE_DSYM "Generate dSYM bundles for Apple debug-like builds" ON)
option(
    DB_TEST_HEADLESS_ONLY
    "Register only CTests that do not create native windows or require a graphics runtime"
    OFF)
set(DB_TEST_ALTERNATE_RELEASE_ROOTS
    ""
    CACHE
        STRING
        "Optional semicolon-separated alternate release suites in the form lane=build_dir"
)
option(DB_TARGET_LINUX_MUSL
       "Enable Linux musl target flags for cross-compilation" OFF)
set(DB_TARGET_LINUX_MUSL_TRIPLE
    "x86_64-linux-musl"
    CACHE
        STRING
        "Target triple used when DB_TARGET_LINUX_MUSL is ON (for clang-style --target)"
)
set(DB_TARGET_LINUX_ROOT
    ""
    CACHE
        PATH
        "Optional root/sysroot path used for Linux cross-compilation discovery")
option(DB_TARGET_LINUX_MUSL_STATIC_LINK
       "Enable static linking (-static) for Linux musl target builds" OFF)
option(DB_TARGET_LINUX_32BIT
       "Enable 32-bit target flags (-m32) for Linux cross-compilation" OFF)

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE
        Release
        CACHE
            STRING
            "Build type (defaulted to Release for renderer validation performance)"
            FORCE)
endif()

set(DB_IS_RELEASE_LIKE OFF)
if(CMAKE_BUILD_TYPE MATCHES "^(Release|RelWithDebInfo|MinSizeRel)$")
    set(DB_IS_RELEASE_LIKE ON)
endif()

set(DB_EFFECTIVE_LTO OFF)
if(DB_IS_RELEASE_LIKE AND DB_ENABLE_LTO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT DB_IPO_SUPPORTED OUTPUT DB_IPO_OUTPUT)
    if(DB_IPO_SUPPORTED)
        set(DB_EFFECTIVE_LTO ON)
    else()
        message(
            WARNING
                "LTO (Interprocedural Optimization) is not supported by the compiler: ${DB_IPO_OUTPUT}"
        )
    endif()
endif()

# C23 checked integer arithmetic support (stdckdint.h + ckd_add)
set(CMAKE_REQUIRED_QUIET TRUE)
check_include_file(stdckdint.h DB_HAVE_STDCKDINT_H)
set(DB_HAVE_STDCKDINT 0)
if(DB_HAVE_STDCKDINT_H)
    check_symbol_exists(ckd_add stdckdint.h DB_HAVE_STDCKDINT_CKD_ADD)
    if(DB_HAVE_STDCKDINT_CKD_ADD)
        set(DB_HAVE_STDCKDINT 1)
    endif()
endif()
add_compile_definitions(DB_HAVE_STDCKDINT=${DB_HAVE_STDCKDINT})
message(STATUS "C23 stdckdint support: ${DB_HAVE_STDCKDINT}")

set(DB_USE_LLD OFF)
db_configure_platform_linker_preference()

set(DB_CROSS_COMPILE_FLAGS "")
set(DB_CROSS_LINK_FLAGS "")
include(${CMAKE_SOURCE_DIR}/cmake/DriverBenchLinuxToolchain.cmake)

if(DB_ENABLE_SANITIZERS
   AND DB_REQUIRE_SANITIZER_RUNTIME
   AND CMAKE_BUILD_TYPE STREQUAL "Debug")
    include(CheckCSourceCompiles)
    set(DB_SAVED_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS}")
    set(DB_SAVED_REQUIRED_LINK_OPTIONS "${CMAKE_REQUIRED_LINK_OPTIONS}")
    string(APPEND CMAKE_REQUIRED_FLAGS " -fsanitize=address,undefined")
    list(APPEND CMAKE_REQUIRED_LINK_OPTIONS -fsanitize=address,undefined)
    check_c_source_compiles(
        "#include <stddef.h>\nint main(void) { volatile size_t n = 1U; return (int)n - 1; }"
        DB_SANITIZER_RUNTIME_LINKS)
    set(CMAKE_REQUIRED_FLAGS "${DB_SAVED_REQUIRED_FLAGS}")
    set(CMAKE_REQUIRED_LINK_OPTIONS "${DB_SAVED_REQUIRED_LINK_OPTIONS}")
    if(NOT DB_SANITIZER_RUNTIME_LINKS)
        message(
            FATAL_ERROR
                "ASan/UBSan runtimes do not compile and link for the selected target. Install matching target runtimes or disable DB_REQUIRE_SANITIZER_RUNTIME."
        )
    endif()
endif()

function(db_apply_perf_options target_name)
    if(NOT TARGET ${target_name})
        return()
    endif()

    target_compile_definitions(${target_name} PRIVATE _GNU_SOURCE)

    db_apply_warning_options(${target_name})

    if(DB_CROSS_COMPILE_FLAGS)
        target_compile_options(${target_name} PRIVATE ${DB_CROSS_COMPILE_FLAGS})
    endif()
    if(DB_CROSS_LINK_FLAGS)
        target_link_options(${target_name} PRIVATE ${DB_CROSS_LINK_FLAGS})
    endif()

    if(DB_IS_RELEASE_LIKE AND DB_ENABLE_LOOP_HINTS)
        if(DB_COMPILER_IS_GNU_LIKE)
            target_compile_options(${target_name} PRIVATE -funroll-loops)
        endif()
    endif()

    if(DB_EFFECTIVE_LTO)
        set_target_properties(${target_name}
                              PROPERTIES INTERPROCEDURAL_OPTIMIZATION TRUE)
        if(DB_USE_LLD)
            target_link_options(${target_name} PRIVATE -fuse-ld=lld)
        endif()
    endif()

    db_apply_platform_target_options(${target_name})
endfunction()

set(DB_UNIFIED_TARGET driverbench)
include(${CMAKE_SOURCE_DIR}/cmake/DriverBenchTargets.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/DriverBenchGLFW.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/DriverBenchKMS.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/DriverBenchVulkan.cmake)

db_finalize_driverbench_target()

if(BUILD_TESTING)
    include(${CMAKE_SOURCE_DIR}/cmake/DriverBenchTests.cmake)
    db_register_driverbench_tests()
endif()

include(${CMAKE_SOURCE_DIR}/cmake/DriverBenchDeveloperTools.cmake)
db_add_developer_tool_targets()
