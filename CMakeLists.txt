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

set(DB_PLATFORM_IS_APPLE OFF)
if(APPLE)
  set(DB_PLATFORM_IS_APPLE ON)
endif()

set(DB_PLATFORM_IS_NON_APPLE_UNIX OFF)
if(UNIX AND NOT APPLE)
  set(DB_PLATFORM_IS_NON_APPLE_UNIX ON)
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
if(DB_EFFECTIVE_LTO AND DB_COMPILER_IS_CLANG)
  if(DB_PLATFORM_IS_APPLE)
    message(STATUS
      "Apple build with LTO: keeping the platform linker; skipping ld.lld "
      "to avoid known ld64.lld crashes on macOS arm64."
    )
  else()
    find_program(DB_LLD_PROGRAM NAMES ld.lld lld)
    if(DB_LLD_PROGRAM)
      set(DB_USE_LLD ON)
    endif()
  endif()
endif()

set(DB_CROSS_COMPILE_FLAGS "")
set(DB_CROSS_LINK_FLAGS "")

if(DB_TARGET_LINUX_MUSL)
  if(NOT DB_PLATFORM_IS_NON_APPLE_UNIX)
    message(FATAL_ERROR
      "DB_TARGET_LINUX_MUSL is only supported for non-Apple UNIX builds."
    )
  endif()

  if(DB_TARGET_LINUX_MUSL_SYSROOT STREQUAL "")
    set(DB_AUTO_MUSL_SYSROOT "/usr/${DB_TARGET_LINUX_MUSL_TRIPLE}")
    if(EXISTS "${DB_AUTO_MUSL_SYSROOT}")
      set(DB_TARGET_LINUX_MUSL_SYSROOT "${DB_AUTO_MUSL_SYSROOT}" CACHE PATH
        "Optional sysroot path used when DB_TARGET_LINUX_MUSL is ON"
        FORCE
      )
      message(STATUS
        "DB_TARGET_LINUX_MUSL=ON: auto-detected musl sysroot at "
        "${DB_TARGET_LINUX_MUSL_SYSROOT}"
      )
    endif()
  endif()

  if(DB_COMPILER_IS_CLANG)
    list(APPEND DB_CROSS_COMPILE_FLAGS
      "--target=${DB_TARGET_LINUX_MUSL_TRIPLE}"
    )
    list(APPEND DB_CROSS_LINK_FLAGS
      "--target=${DB_TARGET_LINUX_MUSL_TRIPLE}"
    )
    if(NOT DB_TARGET_LINUX_MUSL_SYSROOT STREQUAL "")
      list(APPEND DB_CROSS_COMPILE_FLAGS
        "--sysroot=${DB_TARGET_LINUX_MUSL_SYSROOT}"
      )
      list(APPEND DB_CROSS_LINK_FLAGS
        "--sysroot=${DB_TARGET_LINUX_MUSL_SYSROOT}"
      )
    endif()
  elseif(DB_COMPILER_IS_GNU)
    message(STATUS
      "DB_TARGET_LINUX_MUSL is ON with GCC; ensure a musl-targeted GCC toolchain "
      "(e.g. musl-gcc or <triple>-gcc) is selected."
    )
    if(NOT DB_TARGET_LINUX_MUSL_SYSROOT STREQUAL "")
      list(APPEND DB_CROSS_COMPILE_FLAGS
        "--sysroot=${DB_TARGET_LINUX_MUSL_SYSROOT}"
      )
      list(APPEND DB_CROSS_LINK_FLAGS
        "--sysroot=${DB_TARGET_LINUX_MUSL_SYSROOT}"
      )
    endif()
  else()
    message(FATAL_ERROR
      "DB_TARGET_LINUX_MUSL is unsupported for compiler '${CMAKE_C_COMPILER_ID}'."
    )
  endif()
  # Musl target binaries should be fully static by default.
  list(APPEND DB_CROSS_LINK_FLAGS -static)
  message(STATUS
    "DB_TARGET_LINUX_MUSL=ON: enabling static linking (-static). Ensure all "
    "required third-party libraries are available as static archives."
  )

  if(NOT DB_TARGET_LINUX_MUSL_SYSROOT STREQUAL "")
    # Make CMake package/library discovery prefer the musl sysroot over host
    # glibc locations so static musl dependencies can be resolved correctly.
    list(PREPEND CMAKE_FIND_ROOT_PATH "${DB_TARGET_LINUX_MUSL_SYSROOT}")
    list(PREPEND CMAKE_PREFIX_PATH
      "${DB_TARGET_LINUX_MUSL_SYSROOT}"
      "${DB_TARGET_LINUX_MUSL_SYSROOT}/usr"
    )
    set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
    set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
    set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
    set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
    set(CMAKE_FIND_USE_SYSTEM_ENVIRONMENT_PATH OFF)
    set(CMAKE_FIND_USE_PACKAGE_REGISTRY OFF)
    set(CMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY OFF)

    # Force pkg-config lookup into the musl sysroot only.
    set(ENV{PKG_CONFIG_DIR} "")
    set(ENV{PKG_CONFIG_PATH} "")
    set(DB_MUSL_PKGCONFIG_LIBDIRS "")
    foreach(DB_PKG_DIR
      "${DB_TARGET_LINUX_MUSL_SYSROOT}/lib/pkgconfig"
      "${DB_TARGET_LINUX_MUSL_SYSROOT}/share/pkgconfig"
      "${DB_TARGET_LINUX_MUSL_SYSROOT}/usr/lib/pkgconfig"
      "${DB_TARGET_LINUX_MUSL_SYSROOT}/usr/share/pkgconfig")
      if(EXISTS "${DB_PKG_DIR}")
        list(APPEND DB_MUSL_PKGCONFIG_LIBDIRS "${DB_PKG_DIR}")
      endif()
    endforeach()
    if(DB_MUSL_PKGCONFIG_LIBDIRS)
      list(JOIN DB_MUSL_PKGCONFIG_LIBDIRS ":" DB_MUSL_PKGCONFIG_LIBDIR)
      set(ENV{PKG_CONFIG_LIBDIR} "${DB_MUSL_PKGCONFIG_LIBDIR}")
      set(ENV{PKG_CONFIG_SYSROOT_DIR} "${DB_TARGET_LINUX_MUSL_SYSROOT}")
    endif()
  endif()
endif()

if(DB_TARGET_LINUX_32BIT)
  if(NOT DB_PLATFORM_IS_NON_APPLE_UNIX)
    message(FATAL_ERROR
      "DB_TARGET_LINUX_32BIT is only supported for non-Apple UNIX builds."
    )
  endif()
  list(APPEND DB_CROSS_COMPILE_FLAGS -m32)
  list(APPEND DB_CROSS_LINK_FLAGS -m32)
  # Enforce conservative x86 SSE2-only baseline for 32-bit targets.
  list(APPEND DB_CROSS_COMPILE_FLAGS
    -march=pentium4
    -mtune=generic
    -msse2
    -mfpmath=sse
    -mno-sse3
    -mno-ssse3
    -mno-sse4.1
    -mno-sse4.2
    -mno-avx
    -mno-avx2
  )
endif()

if(DB_TARGET_LINUX_MUSL AND DB_TARGET_LINUX_32BIT AND
   DB_TARGET_LINUX_MUSL_TRIPLE MATCHES "^x86_64-")
  message(WARNING
    "DB_TARGET_LINUX_32BIT is ON while DB_TARGET_LINUX_MUSL_TRIPLE is "
    "'${DB_TARGET_LINUX_MUSL_TRIPLE}'. For 32-bit musl target consider "
    "setting DB_TARGET_LINUX_MUSL_TRIPLE=i686-linux-musl."
  )
endif()

if(DB_TARGET_LINUX_MUSL AND DB_TARGET_LINUX_32BIT)
  if(DB_COMPILER_IS_CLANG AND
     DB_TARGET_LINUX_MUSL_SYSROOT STREQUAL "")
    message(FATAL_ERROR
      "32-bit musl target requires a musl i686 sysroot when using clang. "
      "Set DB_TARGET_LINUX_MUSL_SYSROOT to your i686 musl sysroot path, "
      "or use an i686 musl GCC cross compiler (e.g. i686-linux-musl-gcc)."
    )
  endif()
endif()

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

  if(DB_PLATFORM_IS_NON_APPLE_UNIX)
    target_compile_definitions(${target_name} PRIVATE _POSIX_C_SOURCE=200809L)
  endif()

  if(DB_ENABLE_SANITIZERS AND CMAKE_BUILD_TYPE STREQUAL "Debug")
    if(DB_COMPILER_IS_GNU_LIKE)
      set(DB_SANITIZER_FLAGS -fsanitize=address,undefined)
      if(NOT DB_PLATFORM_IS_APPLE)
        list(APPEND DB_SANITIZER_FLAGS -fsanitize=leak)
      endif()
      target_compile_options(${target_name} PRIVATE
        -fno-omit-frame-pointer
        ${DB_SANITIZER_FLAGS}
      )
      target_link_options(${target_name} PRIVATE ${DB_SANITIZER_FLAGS})
    endif()
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

  if(DB_PLATFORM_IS_APPLE AND DB_ENABLE_DSYM)
    if(CMAKE_BUILD_TYPE MATCHES "^(Debug|RelWithDebInfo)$")
      find_program(DB_DSYMUTIL_PROGRAM NAMES dsymutil xcrun)
      if(DB_DSYMUTIL_PROGRAM)
        if(DB_DSYMUTIL_PROGRAM MATCHES ".*/xcrun$")
          add_custom_command(TARGET ${target_name} POST_BUILD
            COMMAND ${DB_DSYMUTIL_PROGRAM} dsymutil
                    $<TARGET_FILE:${target_name}>
                    -o $<TARGET_FILE:${target_name}>.dSYM
            COMMENT "Generating dSYM for ${target_name}"
            VERBATIM
          )
        else()
          add_custom_command(TARGET ${target_name} POST_BUILD
            COMMAND ${DB_DSYMUTIL_PROGRAM}
                    $<TARGET_FILE:${target_name}>
                    -o $<TARGET_FILE:${target_name}>.dSYM
            COMMENT "Generating dSYM for ${target_name}"
            VERBATIM
          )
        endif()
      endif()
    endif()
  endif()
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
