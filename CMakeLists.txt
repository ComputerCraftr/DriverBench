cmake_minimum_required(VERSION 3.21)

# Prefer musl-targeted GCC when a musl preset requests it and a matching cross
# compiler is available. Otherwise prefer Clang for better default LTO support.
if(NOT DEFINED CMAKE_C_COMPILER AND "$ENV{CC}" STREQUAL "")
  if(DEFINED DB_TARGET_LINUX_MUSL AND DB_TARGET_LINUX_MUSL AND
     DEFINED DB_TARGET_LINUX_MUSL_TRIPLE AND
     NOT "${DB_TARGET_LINUX_MUSL_TRIPLE}" STREQUAL "")
    find_program(DB_DEFAULT_MUSL_GCC
      NAMES "${DB_TARGET_LINUX_MUSL_TRIPLE}-gcc")
    if(DB_DEFAULT_MUSL_GCC)
      set(CMAKE_C_COMPILER "${DB_DEFAULT_MUSL_GCC}" CACHE FILEPATH
        "C compiler selected by DriverBench default preference (musl-targeted GCC)"
      )
      message(STATUS
        "No C compiler specified; defaulting to musl-targeted GCC: ${DB_DEFAULT_MUSL_GCC}")
    endif()
  endif()

  if(NOT DEFINED CMAKE_C_COMPILER)
    find_program(DB_DEFAULT_CLANG NAMES clang)
    if(DB_DEFAULT_CLANG)
      set(CMAKE_C_COMPILER "${DB_DEFAULT_CLANG}" CACHE FILEPATH
        "C compiler selected by DriverBench default preference (clang)"
      )
      message(STATUS
        "No C compiler specified; defaulting to clang: ${DB_DEFAULT_CLANG}")
    endif()
  endif()
endif()

project(DriverBench C)
include(CTest)
find_package(Python3 COMPONENTS Interpreter REQUIRED)

include(CheckIncludeFile)
include(CheckSymbolExists)
include(${CMAKE_SOURCE_DIR}/cmake/DriverBenchPlatform.cmake)

set(DB_SOURCE_FILE_LINE_LIMIT 1000)

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

set(DB_CORE_SOURCES
  src/config/runtime_options.c
  src/core/db_buffer_convert.c
  src/core/db_core.c
  src/core/db_hash.c
  src/core/db_hash_simd.c
)

option(DB_BUILD_VULKAN "Build Vulkan benchmark backend" ON)
option(DB_BUILD_GLFW_WINDOW_DISPLAY "Build GLFW window display backend" ON)
option(DB_GLFW_REQUIRED
  "Fail configure if a requested GLFW provider is unavailable instead of disabling GLFW-backed displays"
  OFF)
option(DB_BUILD_LINUX_KMS_ATOMIC_DISPLAY
  "Build Linux KMS atomic display backend" ON)
set(DB_GLFW_PROVIDER "vendored" CACHE STRING
  "GLFW provider: vendored, system, or off"
)
set_property(CACHE DB_GLFW_PROVIDER PROPERTY STRINGS vendored system off)
option(DB_ENABLE_AGGRESSIVE_OPT "Enable aggressive compile optimization flags" ON)
option(DB_ENABLE_LOOP_HINTS "Enable loop optimization hint flags" ON)
option(DB_ENABLE_LTO "Enable link-time optimization in release-like builds" OFF)
option(DB_ENABLE_LTO_RELWITHDEBINFO
  "Enable LTO specifically for RelWithDebInfo builds" OFF)
set(DB_LTO_MODE "full" CACHE STRING
  "LTO mode: full or thin"
)
set_property(CACHE DB_LTO_MODE PROPERTY STRINGS full thin)
option(DB_ENABLE_SANITIZERS "Enable sanitizers for debug builds" ON)
option(DB_ENABLE_DSYM "Generate dSYM bundles for Apple debug-like builds" ON)
set(DB_TEST_ALTERNATE_RELEASE_ROOTS "" CACHE STRING
  "Optional semicolon-separated alternate release suites in the form lane=build_dir"
)
option(DB_TARGET_LINUX_MUSL
  "Enable Linux musl target flags for cross-compilation" OFF)
set(DB_TARGET_LINUX_MUSL_TRIPLE "x86_64-linux-musl" CACHE STRING
  "Target triple used when DB_TARGET_LINUX_MUSL is ON (for clang-style --target)"
)
set(DB_TARGET_LINUX_ROOT "" CACHE PATH
  "Optional root/sysroot path used for Linux cross-compilation discovery"
)
set(DB_TARGET_LINUX_MUSL_SYSROOT "" CACHE PATH
  "Deprecated legacy musl sysroot path; prefer DB_TARGET_LINUX_ROOT"
)
option(DB_TARGET_LINUX_MUSL_STATIC_LINK
  "Enable static linking (-static) for Linux musl target builds" OFF)
option(DB_TARGET_LINUX_32BIT
  "Enable 32-bit target flags (-m32) for Linux cross-compilation" OFF)

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING
    "Build type (defaulted to Release for renderer validation performance)"
    FORCE
  )
endif()

set(DB_IS_RELEASE_LIKE OFF)
if(CMAKE_BUILD_TYPE MATCHES "^(Release|RelWithDebInfo|MinSizeRel)$")
  set(DB_IS_RELEASE_LIKE ON)
endif()

set(DB_EFFECTIVE_LTO OFF)
set(DB_EFFECTIVE_LTO_FLAG "")
if(DB_IS_RELEASE_LIKE AND DB_ENABLE_LTO)
  if(CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo" AND
     NOT DB_ENABLE_LTO_RELWITHDEBINFO)
    message(STATUS
      "RelWithDebInfo build: disabling LTO by default to preserve profiler symbols "
      "(set DB_ENABLE_LTO_RELWITHDEBINFO=ON to override)."
    )
  elseif((NOT DB_LTO_MODE STREQUAL "full") AND
         (NOT DB_LTO_MODE STREQUAL "thin"))
    message(FATAL_ERROR
      "Invalid DB_LTO_MODE='${DB_LTO_MODE}'. Expected full or thin."
    )
  elseif(DB_COMPILER_IS_CLANG)
    set(DB_EFFECTIVE_LTO ON)
    if(DB_LTO_MODE STREQUAL "thin")
      set(DB_EFFECTIVE_LTO_FLAG "-flto=thin")
    else()
      set(DB_EFFECTIVE_LTO_FLAG "-flto")
    endif()
  elseif(DB_COMPILER_IS_GNU)
    if(DB_LTO_MODE STREQUAL "thin")
      message(FATAL_ERROR
        "DB_LTO_MODE=thin requested for GCC, but GCC does not support ThinLTO. "
        "Use -DDB_LTO_MODE=full for GCC."
      )
    endif()
    set(DB_EFFECTIVE_LTO ON)
    set(DB_EFFECTIVE_LTO_FLAG "-flto")
  else()
    message(WARNING
      "DB_ENABLE_LTO is ON, but compiler '${CMAKE_C_COMPILER_ID}' does not "
      "have configured LTO support in this build; "
      "disabling LTO for this configure."
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

function(db_apply_perf_options target_name)
  if(NOT TARGET ${target_name})
    return()
  endif()

  if(DB_COMPILER_IS_CLANG)
    target_compile_options(${target_name} PRIVATE
      -Wall
      -Wextra
      -Wpedantic
      -Werror
      -Wformat=2
      -Wcast-align
      -Wcast-qual
      -Wconversion
      -Wsign-conversion
      -Wdouble-promotion
      -Wfloat-equal
      -Wimplicit-fallthrough
      -Wmissing-prototypes
      -Wstrict-prototypes
      -Wnewline-eof
      -Wnull-dereference
      -Wshadow-all
      -Wswitch-enum
      -Wundef
      -Wunreachable-code
      -Wunused-function
      -Wunused-parameter
      -Wunused-variable
      -Wvla
    )
  endif()

  if(DB_CROSS_COMPILE_FLAGS)
    target_compile_options(${target_name} PRIVATE ${DB_CROSS_COMPILE_FLAGS})
  endif()
  if(DB_CROSS_LINK_FLAGS)
    target_link_options(${target_name} PRIVATE ${DB_CROSS_LINK_FLAGS})
  endif()

  if(DB_IS_RELEASE_LIKE AND DB_ENABLE_AGGRESSIVE_OPT)
    if(DB_COMPILER_IS_GNU_LIKE)
      target_compile_options(${target_name} PRIVATE -O3)
    endif()
  endif()

  if(DB_IS_RELEASE_LIKE AND DB_ENABLE_LOOP_HINTS)
    if(DB_COMPILER_IS_GNU_LIKE)
      target_compile_options(${target_name} PRIVATE -funroll-loops)
    endif()
  endif()

  if(DB_EFFECTIVE_LTO)
    target_compile_options(${target_name} PRIVATE ${DB_EFFECTIVE_LTO_FLAG})
    target_link_options(${target_name} PRIVATE ${DB_EFFECTIVE_LTO_FLAG})
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
