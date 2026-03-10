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

include(CheckIncludeFile)
include(CheckSymbolExists)

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
      -Wunreachable-code
      -Wunused-function
      -Wunused-parameter
      -Wunused-variable
      -Wshadow
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

set(DB_GLFW_TARGET "")
set(DB_GLFW_LINK_LIB "")
set(DB_GLFW_EXTRA_INCLUDE_DIRS "")
if(DB_BUILD_GLFW_WINDOW_DISPLAY)
  find_package(glfw3 QUIET)
  if(TARGET glfw)
    set(DB_GLFW_TARGET glfw)
    set(DB_GLFW_LINK_LIB glfw)
  elseif(TARGET glfw3::glfw)
    set(DB_GLFW_TARGET glfw3::glfw)
    set(DB_GLFW_LINK_LIB glfw3::glfw)
  endif()
  if((DB_GLFW_LINK_LIB STREQUAL "") AND (NOT DB_TARGET_LINUX_MUSL))
    find_package(PkgConfig QUIET)
    if(PKG_CONFIG_FOUND)
      pkg_check_modules(GLFW3 QUIET glfw3)
      if(GLFW3_FOUND)
        if(GLFW3_INCLUDE_DIRS)
          list(APPEND DB_GLFW_EXTRA_INCLUDE_DIRS ${GLFW3_INCLUDE_DIRS})
        endif()
        find_library(DB_GLFW_PKG_LIB
          NAMES glfw glfw3
          PATHS ${GLFW3_LIBRARY_DIRS}
          NO_DEFAULT_PATH
        )
        if(DB_GLFW_PKG_LIB)
          set(DB_GLFW_LINK_LIB "${DB_GLFW_PKG_LIB}")
        else()
          pkg_check_modules(GLFW3_IMPORTED QUIET IMPORTED_TARGET glfw3)
          if(TARGET PkgConfig::GLFW3_IMPORTED)
            set(DB_GLFW_LINK_LIB PkgConfig::GLFW3_IMPORTED)
          endif()
        endif()
      endif()
    endif()
  endif()
  if(DB_TARGET_LINUX_32BIT AND (NOT DB_TARGET_LINUX_MUSL))
    find_library(DB_GLFW_32BIT_LIB
      NAMES glfw glfw3
      PATHS
        /usr/lib32
        /usr/i686-pc-linux-gnu/usr/lib
      NO_DEFAULT_PATH
    )
    if(DB_GLFW_32BIT_LIB)
      set(DB_GLFW_LINK_LIB "${DB_GLFW_32BIT_LIB}")
    endif()
    find_package(PkgConfig QUIET)
    if(PKG_CONFIG_FOUND)
      pkg_check_modules(GLFW3_32 QUIET glfw3)
      if(GLFW3_32_FOUND AND GLFW3_32_INCLUDE_DIRS)
        list(APPEND DB_GLFW_EXTRA_INCLUDE_DIRS ${GLFW3_32_INCLUDE_DIRS})
      endif()
    endif()
  endif()
  if(DB_TARGET_LINUX_MUSL AND DB_GLFW_LINK_LIB)
    set(DB_GLFW_LOCATION "")
    get_target_property(DB_GLFW_IMPORTED_LOCATION ${DB_GLFW_TARGET} IMPORTED_LOCATION)
    if(DB_GLFW_IMPORTED_LOCATION)
      set(DB_GLFW_LOCATION "${DB_GLFW_IMPORTED_LOCATION}")
    else()
      get_target_property(DB_GLFW_IMPORTED_CONFIGS ${DB_GLFW_TARGET} IMPORTED_CONFIGURATIONS)
      foreach(DB_GLFW_CFG IN LISTS DB_GLFW_IMPORTED_CONFIGS)
        string(TOUPPER "${DB_GLFW_CFG}" DB_GLFW_CFG_UPPER)
        get_target_property(DB_GLFW_IMPORTED_LOCATION_CFG
          ${DB_GLFW_TARGET}
          IMPORTED_LOCATION_${DB_GLFW_CFG_UPPER}
        )
        if(DB_GLFW_IMPORTED_LOCATION_CFG)
          set(DB_GLFW_LOCATION "${DB_GLFW_IMPORTED_LOCATION_CFG}")
          break()
        endif()
      endforeach()
    endif()
    if(DB_GLFW_LOCATION MATCHES "\\.so(\\.|$)")
      set(DB_SAVED_FIND_LIBRARY_SUFFIXES "${CMAKE_FIND_LIBRARY_SUFFIXES}")
      set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
      find_library(DB_GLFW_STATIC_LIB NAMES glfw glfw3)
      set(CMAKE_FIND_LIBRARY_SUFFIXES "${DB_SAVED_FIND_LIBRARY_SUFFIXES}")
      if(DB_GLFW_STATIC_LIB)
        set(DB_GLFW_LINK_LIB "${DB_GLFW_STATIC_LIB}")
        message(STATUS
          "DB_TARGET_LINUX_MUSL=ON: using static GLFW archive "
          "'${DB_GLFW_STATIC_LIB}'"
        )
      else()
        message(WARNING
          "DB_TARGET_LINUX_MUSL=ON with static link: GLFW target resolves to "
          "shared library '${DB_GLFW_LOCATION}' and no static libglfw archive "
          "was found. Disabling GLFW display backend for this build."
        )
        set(DB_GLFW_TARGET "")
        set(DB_GLFW_LINK_LIB "")
      endif()
    endif()
  endif()
endif()

set(DB_DRIVERBENCH_SOURCES
  src/driverbench_cli.c
  src/driverbench_main.c
  src/displays/display_dispatch.c
  src/displays/display_gl_runtime_common.c
  src/displays/offscreen/display_offscreen.c
  src/renderers/cpu_renderer/renderer_cpu_renderer.c
  ${DB_CORE_SOURCES}
)
set(DB_DRIVERBENCH_LIBS m)
set(DB_DRIVERBENCH_DEFS "")

if(DB_BUILD_GLFW_WINDOW_DISPLAY AND DB_GLFW_LINK_LIB)
  set(DB_GLFW_EFFECTIVE_LIB "${DB_GLFW_LINK_LIB}")
  if(DB_TARGET_LINUX_32BIT AND (NOT DB_TARGET_LINUX_MUSL))
    if(EXISTS "/usr/lib32/libglfw.so")
      set(DB_GLFW_EFFECTIVE_LIB "/usr/lib32/libglfw.so")
    endif()
  endif()
  list(APPEND DB_DRIVERBENCH_SOURCES
    src/displays/glfw_window/display_glfw_window.c
    src/displays/glfw_window/display_glfw_window_common.c
  )
  if(DB_GLFW_EXTRA_INCLUDE_DIRS)
    set_source_files_properties(
      src/displays/glfw_window/display_glfw_window.c
      src/displays/glfw_window/display_glfw_window_common.c
      PROPERTIES
      INCLUDE_DIRECTORIES "${DB_GLFW_EXTRA_INCLUDE_DIRS}"
    )
  endif()
  if(DB_TARGET_LINUX_32BIT AND (NOT DB_TARGET_LINUX_MUSL) AND
     DB_COMPILER_IS_CLANG)
    set_source_files_properties(
      src/displays/glfw_window/display_glfw_window.c
      src/displays/glfw_window/display_glfw_window_common.c
      PROPERTIES
      COMPILE_OPTIONS "-Wno-macro-redefined"
    )
  endif()
  list(APPEND DB_DRIVERBENCH_LIBS ${DB_GLFW_EFFECTIVE_LIB})
  list(APPEND DB_DRIVERBENCH_DEFS DB_HAS_GLFW=1)
endif()

list(APPEND DB_DRIVERBENCH_SOURCES
  src/renderers/opengl_gl1_5_gles1_1/renderer_opengl_gl1_5_gles1_1.c
  src/renderers/opengl_gl1_5_gles1_1/renderer_opengl_gl1_5_gles1_1_damage.c
  src/renderers/opengl_gl1_5_gles1_1/renderer_opengl_gl1_5_gles1_1_util.c
  src/renderers/opengl_gl3_3/renderer_opengl_gl3_3.c
  src/renderers/renderer_gl_common.c
  src/renderers/renderer_gl_setup.c
  src/renderers/renderer_gl_upload.c
)

if(DB_BUILD_LINUX_KMS_ATOMIC_DISPLAY AND DB_PLATFORM_IS_NON_APPLE_UNIX)
  find_package(PkgConfig QUIET)
  if(PKG_CONFIG_FOUND)
    pkg_check_modules(GBM QUIET gbm)
    pkg_check_modules(EGL QUIET egl)
    pkg_check_modules(DRM QUIET libdrm)
    if(GBM_FOUND AND EGL_FOUND AND DRM_FOUND)
      set(DB_ENABLE_KMS_BACKEND 1)
      set(DB_KMS_LINK_LIBS
        ${GBM_LIBRARIES}
        ${EGL_LIBRARIES}
        ${DRM_LIBRARIES}
      )
      set(DB_KMS_EXTRA_INCLUDE_DIRS
        ${GBM_INCLUDE_DIRS}
        ${EGL_INCLUDE_DIRS}
        ${DRM_INCLUDE_DIRS}
      )
      if(DB_TARGET_LINUX_MUSL)
        set(DB_SAVED_FIND_LIBRARY_SUFFIXES "${CMAKE_FIND_LIBRARY_SUFFIXES}")
        set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
        find_library(DB_GBM_STATIC_LIB gbm)
        find_library(DB_EGL_STATIC_LIB EGL)
        find_library(DB_DRM_STATIC_LIB drm)
        set(CMAKE_FIND_LIBRARY_SUFFIXES "${DB_SAVED_FIND_LIBRARY_SUFFIXES}")
        if(DB_GBM_STATIC_LIB AND DB_EGL_STATIC_LIB AND DB_DRM_STATIC_LIB)
          set(DB_KMS_LINK_LIBS
            ${DB_GBM_STATIC_LIB}
            ${DB_EGL_STATIC_LIB}
            ${DB_DRM_STATIC_LIB}
          )
        else()
          message(WARNING
            "DB_TARGET_LINUX_MUSL=ON with static link: missing static gbm/EGL/drm "
            "archives in musl root; disabling Linux KMS atomic backend for this build."
          )
          set(DB_ENABLE_KMS_BACKEND 0)
        endif()
      elseif(DB_TARGET_LINUX_32BIT)
        find_library(DB_GBM_32BIT_LIB
          NAMES gbm
          PATHS
            /usr/lib32
            /usr/i686-pc-linux-gnu/usr/lib
          NO_DEFAULT_PATH
          NO_CMAKE_FIND_ROOT_PATH
        )
        find_library(DB_EGL_32BIT_LIB
          NAMES EGL
          PATHS
            /usr/lib32
            /usr/i686-pc-linux-gnu/usr/lib
          NO_DEFAULT_PATH
          NO_CMAKE_FIND_ROOT_PATH
        )
        find_library(DB_DRM_32BIT_LIB
          NAMES drm
          PATHS
            /usr/lib32
            /usr/i686-pc-linux-gnu/usr/lib
          NO_DEFAULT_PATH
          NO_CMAKE_FIND_ROOT_PATH
        )
        if(DB_GBM_32BIT_LIB AND DB_EGL_32BIT_LIB AND DB_DRM_32BIT_LIB)
          set(DB_KMS_LINK_LIBS
            ${DB_GBM_32BIT_LIB}
            ${DB_EGL_32BIT_LIB}
            ${DB_DRM_32BIT_LIB}
          )
          if(EXISTS "/usr/include")
            list(APPEND DB_KMS_EXTRA_INCLUDE_DIRS "/usr/include")
          endif()
          if(EXISTS "/usr/include/libdrm")
            list(APPEND DB_KMS_EXTRA_INCLUDE_DIRS "/usr/include/libdrm")
          endif()
        else()
          message(WARNING
            "DB_TARGET_LINUX_32BIT=ON: missing 32-bit gbm/EGL/drm link "
            "libraries; disabling Linux KMS atomic backend for this build."
          )
          set(DB_ENABLE_KMS_BACKEND 0)
        endif()
      endif()
      if(DB_ENABLE_KMS_BACKEND)
        list(APPEND DB_DRIVERBENCH_SOURCES
          src/displays/linux_kms_atomic/display_linux_kms_atomic_common.c
          src/displays/linux_kms_atomic/display_linux_kms_atomic.c
        )
        if(DB_KMS_EXTRA_INCLUDE_DIRS)
          set_source_files_properties(
            src/displays/linux_kms_atomic/display_linux_kms_atomic_common.c
            src/displays/linux_kms_atomic/display_linux_kms_atomic.c
            PROPERTIES
            INCLUDE_DIRECTORIES "${DB_KMS_EXTRA_INCLUDE_DIRS}"
          )
        endif()
        if(DB_TARGET_LINUX_32BIT AND (NOT DB_TARGET_LINUX_MUSL) AND
           DB_COMPILER_IS_CLANG)
          set_source_files_properties(
            src/displays/linux_kms_atomic/display_linux_kms_atomic_common.c
            src/displays/linux_kms_atomic/display_linux_kms_atomic.c
            PROPERTIES
            COMPILE_OPTIONS "-Wno-macro-redefined"
          )
        endif()
        list(APPEND DB_DRIVERBENCH_DEFS DB_HAS_LINUX_KMS_ATOMIC=1)
        list(APPEND DB_DRIVERBENCH_LIBS ${DB_KMS_LINK_LIBS})
      endif()
    endif()
  endif()
endif()

if(DB_BUILD_VULKAN)
  find_package(Vulkan QUIET)
  find_program(GLSLC glslc HINTS ENV VULKAN_SDK PATH_SUFFIXES Bin bin)

  set(DB_VULKAN_LIB "")
  if(TARGET Vulkan::Vulkan)
    set(DB_VULKAN_LIB Vulkan::Vulkan)
  elseif(Vulkan_FOUND AND Vulkan_LIBRARY)
    set(DB_VULKAN_LIB "${Vulkan_LIBRARY}")
  endif()

  if(DB_TARGET_LINUX_MUSL AND DB_VULKAN_LIB)
    set(DB_VULKAN_LOCATION "")
    if(TARGET Vulkan::Vulkan)
      get_target_property(DB_VULKAN_IMPORTED_LOCATION
        Vulkan::Vulkan IMPORTED_LOCATION
      )
      if(DB_VULKAN_IMPORTED_LOCATION)
        set(DB_VULKAN_LOCATION "${DB_VULKAN_IMPORTED_LOCATION}")
      else()
        get_target_property(DB_VULKAN_IMPORTED_CONFIGS
          Vulkan::Vulkan IMPORTED_CONFIGURATIONS
        )
        foreach(DB_VK_CFG IN LISTS DB_VULKAN_IMPORTED_CONFIGS)
          string(TOUPPER "${DB_VK_CFG}" DB_VK_CFG_UPPER)
          get_target_property(DB_VULKAN_IMPORTED_LOCATION_CFG
            Vulkan::Vulkan
            IMPORTED_LOCATION_${DB_VK_CFG_UPPER}
          )
          if(DB_VULKAN_IMPORTED_LOCATION_CFG)
            set(DB_VULKAN_LOCATION "${DB_VULKAN_IMPORTED_LOCATION_CFG}")
            break()
          endif()
        endforeach()
      endif()
    else()
      set(DB_VULKAN_LOCATION "${DB_VULKAN_LIB}")
    endif()

    if(DB_VULKAN_LOCATION MATCHES "\\.so(\\.|$)")
      message(WARNING
        "DB_TARGET_LINUX_MUSL=ON with static link: Vulkan library resolves to "
        "shared library '${DB_VULKAN_LOCATION}'. Disabling Vulkan renderer "
        "for this build."
      )
      set(DB_VULKAN_LIB "")
    endif()
  endif()

  if(DB_BUILD_GLFW_WINDOW_DISPLAY AND DB_GLFW_LINK_LIB AND Vulkan_FOUND AND
     DB_VULKAN_LIB AND GLSLC)
    list(APPEND DB_DRIVERBENCH_SOURCES
      src/renderers/vulkan_1_2_multi_gpu/renderer_vulkan_1_2_multi_gpu.c
      src/renderers/vulkan_1_2_multi_gpu/renderer_vulkan_1_2_multi_gpu_frame.c
      src/renderers/vulkan_1_2_multi_gpu/renderer_vulkan_1_2_multi_gpu_init.c
      src/renderers/vulkan_1_2_multi_gpu/renderer_vulkan_1_2_multi_gpu_runtime.c
      src/renderers/vulkan_1_2_multi_gpu/renderer_vulkan_1_2_multi_gpu_scheduler.c
      src/renderers/vulkan_1_2_multi_gpu/renderer_vulkan_1_2_multi_gpu_swapchain.c
    )
    list(APPEND DB_DRIVERBENCH_LIBS ${DB_VULKAN_LIB})
    list(APPEND DB_DRIVERBENCH_DEFS DB_HAS_VULKAN_API=1)

    set(DB_VK_VERT_SPV
      ${CMAKE_CURRENT_BINARY_DIR}/shader_vulkan_1_2_rect.vert.spv
    )
    set(DB_VK_FRAG_SPV
      ${CMAKE_CURRENT_BINARY_DIR}/shader_vulkan_1_2_rect.frag.spv
    )

    add_custom_command(
      OUTPUT ${DB_VK_VERT_SPV}
      COMMAND ${GLSLC}
              ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/shader_vulkan_1_2_rect.vert -o
              ${DB_VK_VERT_SPV}
      DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/shader_vulkan_1_2_rect.vert
      VERBATIM
    )

    add_custom_command(
      OUTPUT ${DB_VK_FRAG_SPV}
      COMMAND ${GLSLC}
              ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/shader_vulkan_1_2_rect.frag -o
              ${DB_VK_FRAG_SPV}
      DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/shader_vulkan_1_2_rect.frag
      VERBATIM
    )

    add_custom_target(driverbench_vulkan_shaders ALL
      DEPENDS ${DB_VK_VERT_SPV}
              ${DB_VK_FRAG_SPV}
    )
  endif()
endif()

set(DB_GENERATED_INCLUDE_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
set(DB_EMBEDDED_SHADERS_HEADER
  ${DB_GENERATED_INCLUDE_DIR}/db_embedded_shaders.h
)
set(DB_EMBEDDED_SHADER_ARGS
  -DOUT_HEADER=${DB_EMBEDDED_SHADERS_HEADER}
  -DGL3_VERT=${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/shader_opengl_gl3_3_rect.vert
  -DGL3_FRAG=${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/shader_opengl_gl3_3_rect.frag
)
set(DB_EMBEDDED_SHADER_DEPS
  ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/shader_opengl_gl3_3_rect.vert
  ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/shader_opengl_gl3_3_rect.frag
  ${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenerateEmbeddedShadersHeader.cmake
)
if(DEFINED DB_VK_VERT_SPV AND DEFINED DB_VK_FRAG_SPV)
  list(APPEND DB_EMBEDDED_SHADER_ARGS
    -DVK_VERT_SPV=${DB_VK_VERT_SPV}
    -DVK_FRAG_SPV=${DB_VK_FRAG_SPV}
  )
  list(APPEND DB_EMBEDDED_SHADER_DEPS
    ${DB_VK_VERT_SPV}
    ${DB_VK_FRAG_SPV}
  )
endif()
add_custom_command(
  OUTPUT ${DB_EMBEDDED_SHADERS_HEADER}
  COMMAND ${CMAKE_COMMAND} -E make_directory ${DB_GENERATED_INCLUDE_DIR}
  COMMAND ${CMAKE_COMMAND} ${DB_EMBEDDED_SHADER_ARGS}
          -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenerateEmbeddedShadersHeader.cmake
  DEPENDS ${DB_EMBEDDED_SHADER_DEPS}
  VERBATIM
)
add_custom_target(driverbench_embedded_shaders ALL
  DEPENDS ${DB_EMBEDDED_SHADERS_HEADER}
)

set(DB_UNIFIED_TARGET driverbench)
add_executable(${DB_UNIFIED_TARGET}
  ${DB_DRIVERBENCH_SOURCES}
)
db_apply_perf_options(${DB_UNIFIED_TARGET})
target_compile_definitions(${DB_UNIFIED_TARGET} PRIVATE ${DB_DRIVERBENCH_DEFS})
target_link_libraries(${DB_UNIFIED_TARGET} PRIVATE ${DB_DRIVERBENCH_LIBS})
target_include_directories(${DB_UNIFIED_TARGET} PRIVATE ${DB_GENERATED_INCLUDE_DIR})
if(TARGET driverbench_vulkan_shaders)
  add_dependencies(${DB_UNIFIED_TARGET} driverbench_vulkan_shaders)
endif()
if(TARGET driverbench_embedded_shaders)
  add_dependencies(${DB_UNIFIED_TARGET} driverbench_embedded_shaders)
endif()

if(CMAKE_EXPORT_COMPILE_COMMANDS)
  add_custom_target(db_sync_compile_commands ALL
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      ${CMAKE_BINARY_DIR}/compile_commands.json
      ${CMAKE_SOURCE_DIR}/compile_commands.json
    COMMENT "Sync compile_commands.json to source root for clangd"
    VERBATIM
  )
endif()

if(BUILD_TESTING)
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
    "state_hash_aggregate=0xa84f5f681a12809d,framebuffer_hash_aggregate=0xcfdc8a8fbe23ed20"
  )

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
    db_add_cross_renderer_snake_state_triplet(
      determinism_cross_renderer_snake_grid_state_triplet
      snake_grid
      0xa84f5f681a12809d
    )
    db_add_cross_renderer_snake_state_triplet(
      determinism_cross_renderer_snake_rect_state_triplet
      snake_rect
      0x61cd6f247ff98d89
    )
    db_add_cross_renderer_snake_state_triplet(
      determinism_cross_renderer_snake_shapes_state_triplet
      snake_shapes
      0x90f208d3f4e2b13f
    )
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
      db_add_determinism_test(
        determinism_offscreen_gl1_5
        "${DB_DETERMINISM_GL1_OFFSCREEN_PREFIX} --benchmark-mode snake_grid ${DB_DETERMINISM_COMMON_ARGS} ${DB_DETERMINISM_HASH} ${DB_DETERMINISM_HASH_REPORT} ${DB_DETERMINISM_SNAKE_ARGS} --backbuffer-draw-mode full"
        "${DB_DETERMINISM_GL1_OFFSCREEN_FULL_GOLDEN_HASH_CHECKS}"
      )
    else()
      db_add_determinism_test(
        determinism_offscreen_gl1_5
        "${DB_DETERMINISM_GL1_OFFSCREEN_PREFIX} --benchmark-mode snake_grid ${DB_DETERMINISM_COMMON_ARGS} ${DB_DETERMINISM_HASH} ${DB_DETERMINISM_HASH_REPORT} ${DB_DETERMINISM_SNAKE_ARGS}"
        "${DB_DETERMINISM_GL1_OFFSCREEN_DIRTY_GOLDEN_HASH_CHECKS}"
      )
    endif()
    db_add_determinism_test(
      determinism_offscreen_gl3_3_fbo
      "${DB_DETERMINISM_GL3_OFFSCREEN_PREFIX} --benchmark-mode snake_grid ${DB_DETERMINISM_COMMON_ARGS} ${DB_DETERMINISM_HASH} ${DB_DETERMINISM_HASH_REPORT} ${DB_DETERMINISM_SNAKE_ARGS}"
      "state_hash_aggregate=0xa84f5f681a12809d,fbo_hash16f_aggregate=0xbdf6bad5fa808825"
    )
    if(APPLE)
      db_add_determinism_test(
        determinism_offscreen_gl1_5_dirty_macos
        "${DB_DETERMINISM_GL1_OFFSCREEN_PREFIX} --benchmark-mode snake_grid ${DB_DETERMINISM_COMMON_ARGS} ${DB_DETERMINISM_HASH} ${DB_DETERMINISM_HASH_REPORT} ${DB_DETERMINISM_SNAKE_ARGS} --backbuffer-draw-mode dirty"
        "${DB_DETERMINISM_GL1_OFFSCREEN_DIRTY_GOLDEN_HASH_CHECKS}"
      )
    endif()
  endif()

endif()
