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
  list(APPEND DB_CROSS_LINK_FLAGS -static)
  message(STATUS
    "DB_TARGET_LINUX_MUSL=ON: enabling static linking (-static). Ensure all "
    "required third-party libraries are available as static archives."
  )

  if(NOT DB_TARGET_LINUX_MUSL_SYSROOT STREQUAL "")
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
