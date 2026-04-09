cmake_minimum_required(VERSION 3.21)

# Prefer Clang for better default LTO support when no compiler is specified.
if(NOT DEFINED CMAKE_C_COMPILER AND "$ENV{CC}" STREQUAL "")
  find_program(DB_DEFAULT_CLANG NAMES clang)
  if(DB_DEFAULT_CLANG)
    set(CMAKE_C_COMPILER "${DB_DEFAULT_CLANG}" CACHE FILEPATH
      "C compiler selected by DriverBench default preference (clang)"
    )
    message(STATUS "No C compiler specified; defaulting to clang: ${DB_DEFAULT_CLANG}")
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
option(DB_BUILD_LINUX_KMS_ATOMIC_DISPLAY
  "Build Linux KMS atomic display backend" ON)
option(DB_ENABLE_AGGRESSIVE_OPT "Enable aggressive compile optimization flags" ON)
option(DB_ENABLE_LOOP_HINTS "Enable loop optimization hint flags" ON)
option(DB_ENABLE_LTO "Enable link-time optimization in release-like builds" ON)
option(DB_ENABLE_LTO_RELWITHDEBINFO
  "Enable LTO specifically for RelWithDebInfo builds" OFF)
option(DB_ENABLE_SANITIZERS "Enable sanitizers for debug builds" ON)
option(DB_ENABLE_DSYM "Generate dSYM bundles for Apple debug-like builds" ON)
option(DB_TARGET_LINUX_MUSL
  "Enable Linux musl target flags for cross-compilation" OFF)
set(DB_TARGET_LINUX_MUSL_TRIPLE "x86_64-linux-musl" CACHE STRING
  "Target triple used when DB_TARGET_LINUX_MUSL is ON (for clang-style --target)"
)
set(DB_TARGET_LINUX_MUSL_SYSROOT "" CACHE PATH
  "Optional sysroot path used when DB_TARGET_LINUX_MUSL is ON"
)
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
if(DB_IS_RELEASE_LIKE AND DB_ENABLE_LTO)
  if(CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo" AND
     NOT DB_ENABLE_LTO_RELWITHDEBINFO)
    message(STATUS
      "RelWithDebInfo build: disabling LTO by default to preserve profiler symbols "
      "(set DB_ENABLE_LTO_RELWITHDEBINFO=ON to override)."
    )
  elseif(DB_COMPILER_IS_CLANG)
    set(DB_EFFECTIVE_LTO ON)
  else()
    message(WARNING
      "DB_ENABLE_LTO is ON, but compiler '${CMAKE_C_COMPILER_ID}' is not Clang; "
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
    target_compile_options(${target_name} PRIVATE -flto)
    target_link_options(${target_name} PRIVATE -flto)
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
