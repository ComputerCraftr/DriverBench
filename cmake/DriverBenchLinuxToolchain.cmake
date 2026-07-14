set(DB_LINUX_TARGET_KIND "native")
set(DB_LINUX_EFFECTIVE_ROOT "")
set(DB_LINUX_ROOT_SOURCE "")
set(DB_LINUX_REQUIRE_STATIC_ARCHIVES OFF)

set(DB_LINUX_MULTILIB_LIB_DIRS "/usr/lib32" "/lib32" "/usr/lib/i386-linux-gnu"
                               "/lib/i386-linux-gnu")

function(db_linux_collect_pkgconfig_dirs out_var base_root)
    set(db_linux_pkg_dirs "")
    foreach(
        db_linux_pattern
        "${base_root}/lib/pkgconfig" "${base_root}/lib*/pkgconfig"
        "${base_root}/share/pkgconfig" "${base_root}/usr/lib/pkgconfig"
        "${base_root}/usr/lib*/pkgconfig" "${base_root}/usr/share/pkgconfig")
        file(
            GLOB db_linux_pkg_matches
            LIST_DIRECTORIES true
            "${db_linux_pattern}")
        foreach(db_linux_pkg_dir IN LISTS db_linux_pkg_matches)
            if(IS_DIRECTORY "${db_linux_pkg_dir}")
                list(APPEND db_linux_pkg_dirs "${db_linux_pkg_dir}")
            endif()
        endforeach()
    endforeach()
    list(REMOVE_DUPLICATES db_linux_pkg_dirs)
    set(${out_var}
        "${db_linux_pkg_dirs}"
        PARENT_SCOPE)
endfunction()

function(db_linux_append_root_flags root_path)
    set(CMAKE_SYSROOT
        "${root_path}"
        CACHE PATH "Resolved Linux cross root/sysroot path" FORCE)
    list(PREPEND CMAKE_FIND_ROOT_PATH "${root_path}")
    list(PREPEND CMAKE_PREFIX_PATH "${root_path}" "${root_path}/usr")
    set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
    set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
    set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
    set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
    set(CMAKE_FIND_USE_SYSTEM_ENVIRONMENT_PATH OFF)
    set(CMAKE_FIND_USE_PACKAGE_REGISTRY OFF)
    set(CMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY OFF)

    set(ENV{PKG_CONFIG_DIR} "")
    set(ENV{PKG_CONFIG_PATH} "")
    db_linux_collect_pkgconfig_dirs(DB_LINUX_PKGCONFIG_DIRS "${root_path}")
    if(DB_LINUX_PKGCONFIG_DIRS)
        list(JOIN DB_LINUX_PKGCONFIG_DIRS ":" DB_LINUX_PKGCONFIG_LIBDIR)
        set(ENV{PKG_CONFIG_LIBDIR} "${DB_LINUX_PKGCONFIG_LIBDIR}")
        set(ENV{PKG_CONFIG_SYSROOT_DIR} "${root_path}")
    endif()

    set(DB_CROSS_COMPILE_FLAGS
        "${DB_CROSS_COMPILE_FLAGS}"
        PARENT_SCOPE)
    set(DB_CROSS_LINK_FLAGS
        "${DB_CROSS_LINK_FLAGS}"
        PARENT_SCOPE)
endfunction()

if(NOT DB_TARGET_LINUX_ROOT STREQUAL "")
    set(DB_LINUX_EFFECTIVE_ROOT "${DB_TARGET_LINUX_ROOT}")
    set(DB_LINUX_ROOT_SOURCE "cache")
elseif(NOT "$ENV{DB_TARGET_LINUX_ROOT}" STREQUAL "")
    set(DB_LINUX_EFFECTIVE_ROOT "$ENV{DB_TARGET_LINUX_ROOT}")
    set(DB_LINUX_ROOT_SOURCE "env")
elseif(NOT "${CMAKE_SYSROOT}" STREQUAL "")
    set(DB_LINUX_EFFECTIVE_ROOT "${CMAKE_SYSROOT}")
    set(DB_LINUX_ROOT_SOURCE "cmake-sysroot")
elseif(DB_TARGET_LINUX_MUSL)
    set(DB_AUTO_MUSL_ROOT "/usr/${DB_TARGET_LINUX_MUSL_TRIPLE}")
    if(EXISTS "${DB_AUTO_MUSL_ROOT}")
        set(DB_LINUX_EFFECTIVE_ROOT "${DB_AUTO_MUSL_ROOT}")
        set(DB_LINUX_ROOT_SOURCE "fallback-musl-triple-root")
    endif()
endif()

if(NOT DB_LINUX_EFFECTIVE_ROOT STREQUAL "" AND NOT EXISTS
                                               "${DB_LINUX_EFFECTIVE_ROOT}")
    message(
        FATAL_ERROR
            "Linux cross root '${DB_LINUX_EFFECTIVE_ROOT}' does not exist. Set DB_TARGET_LINUX_ROOT to a valid path."
    )
endif()

if(DB_TARGET_LINUX_MUSL)
    set(DB_LINUX_TARGET_KIND "musl")
elseif(DB_TARGET_LINUX_32BIT)
    set(DB_LINUX_TARGET_KIND "glibc32")
endif()

if(DB_TARGET_LINUX_MUSL)
    if(NOT DB_PLATFORM_IS_NON_APPLE_UNIX)
        message(
            FATAL_ERROR
                "DB_TARGET_LINUX_MUSL is only supported for non-Apple UNIX builds."
        )
    endif()

    if(DB_COMPILER_IS_CLANG)
        if(CMAKE_C_COMPILER_TARGET STREQUAL "")
            list(APPEND DB_CROSS_COMPILE_FLAGS
                 "--target=${DB_TARGET_LINUX_MUSL_TRIPLE}")
            list(APPEND DB_CROSS_LINK_FLAGS
                 "--target=${DB_TARGET_LINUX_MUSL_TRIPLE}")
        elseif(NOT CMAKE_C_COMPILER_TARGET STREQUAL DB_TARGET_LINUX_MUSL_TRIPLE)
            message(
                FATAL_ERROR
                    "Clang target '${CMAKE_C_COMPILER_TARGET}' does not match requested musl target '${DB_TARGET_LINUX_MUSL_TRIPLE}'."
            )
        endif()
    elseif(DB_COMPILER_IS_GNU)
        message(
            STATUS
                "DB_TARGET_LINUX_MUSL is ON with GCC; ensure a musl-targeted GCC toolchain "
                "(e.g. musl-gcc or <triple>-gcc) is selected.")
    else()
        message(
            FATAL_ERROR
                "DB_TARGET_LINUX_MUSL is unsupported for compiler '${CMAKE_C_COMPILER_ID}'."
        )
    endif()

    if(DB_TARGET_LINUX_MUSL_STATIC_LINK)
        list(APPEND DB_CROSS_LINK_FLAGS -static)
        set(DB_LINUX_REQUIRE_STATIC_ARCHIVES ON)
        message(
            STATUS
                "DB_TARGET_LINUX_MUSL_STATIC_LINK=ON: enabling static linking (-static). "
                "Ensure required third-party libraries are available as static archives."
        )
    endif()
endif()

if(DB_TARGET_LINUX_32BIT)
    if(NOT DB_PLATFORM_IS_NON_APPLE_UNIX)
        message(
            FATAL_ERROR
                "DB_TARGET_LINUX_32BIT is only supported for non-Apple UNIX builds."
        )
    endif()
    set(DB_LINUX_EXPLICIT_32BIT_TARGET OFF)
    if(CMAKE_C_COMPILER_TARGET MATCHES "^i[3-6]86-"
       OR DB_TARGET_LINUX_MUSL_TRIPLE MATCHES "^i[3-6]86-")
        set(DB_LINUX_EXPLICIT_32BIT_TARGET ON)
    endif()
    if(NOT DB_LINUX_EXPLICIT_32BIT_TARGET)
        list(APPEND DB_CROSS_COMPILE_FLAGS -m32)
        list(APPEND DB_CROSS_LINK_FLAGS -m32)
    endif()
    if(DB_LINUX_EFFECTIVE_ROOT STREQUAL "")
        list(JOIN DB_LINUX_MULTILIB_LIB_DIRS ":" DB_LINUX_MULTILIB_RPATH_LINK)
        list(APPEND DB_CROSS_LINK_FLAGS
             "-Wl,-rpath-link=${DB_LINUX_MULTILIB_RPATH_LINK}")
    endif()
    list(
        APPEND
        DB_CROSS_COMPILE_FLAGS
        -march=pentium4
        -mtune=generic
        -msse2
        -mfpmath=sse
        -mno-sse3
        -mno-ssse3
        -mno-sse4.1
        -mno-sse4.2
        -mno-avx
        -mno-avx2)
    message(
        STATUS
            "DB_TARGET_LINUX_32BIT=ON: enforcing SSE2 baseline code generation."
    )

    if(DB_USE_LLD)
        set(DB_USE_LLD OFF)
        message(
            STATUS
                "DB_TARGET_LINUX_32BIT=ON: disabling ld.lld after target resolution to "
                "avoid common multilib runtime archive lookup failures.")
    endif()

    if(DB_LINUX_EFFECTIVE_ROOT STREQUAL "")
        set_property(GLOBAL PROPERTY FIND_LIBRARY_USE_LIB32_PATHS TRUE)
    endif()
endif()

if(DB_TARGET_LINUX_MUSL
   AND DB_TARGET_LINUX_32BIT
   AND DB_TARGET_LINUX_MUSL_TRIPLE MATCHES "^x86_64-")
    message(
        WARNING
            "DB_TARGET_LINUX_32BIT is ON while DB_TARGET_LINUX_MUSL_TRIPLE is "
            "'${DB_TARGET_LINUX_MUSL_TRIPLE}'. For 32-bit musl target consider "
            "setting DB_TARGET_LINUX_MUSL_TRIPLE=i686-linux-musl.")
endif()

if(DB_TARGET_LINUX_MUSL AND DB_TARGET_LINUX_32BIT)
    if(DB_COMPILER_IS_CLANG AND DB_LINUX_EFFECTIVE_ROOT STREQUAL "")
        message(
            FATAL_ERROR
                "32-bit musl target requires a musl i686 root/sysroot when using clang. "
                "Set DB_TARGET_LINUX_ROOT to your i686 musl root, or use an i686 musl GCC cross compiler."
        )
    endif()
endif()

if(NOT DB_LINUX_EFFECTIVE_ROOT STREQUAL "")
    if(DB_LINUX_ROOT_SOURCE STREQUAL "fallback-musl-triple-root")
        message(
            STATUS
                "Resolved Linux cross root from conventional musl path: ${DB_LINUX_EFFECTIVE_ROOT}"
        )
    else()
        message(
            STATUS
                "Resolved Linux cross root (${DB_LINUX_ROOT_SOURCE}): ${DB_LINUX_EFFECTIVE_ROOT}"
        )
    endif()
    db_linux_append_root_flags("${DB_LINUX_EFFECTIVE_ROOT}")
endif()

if(DB_TARGET_LINUX_32BIT)
    include(CheckCSourceCompiles)
    set(DB_LINUX_32BIT_SAVED_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS}")
    set(DB_LINUX_32BIT_SAVED_REQUIRED_LINK_OPTIONS
        "${CMAKE_REQUIRED_LINK_OPTIONS}")
    set(DB_LINUX_32BIT_REQUIRED_COMPILE_FLAGS ${DB_CROSS_COMPILE_FLAGS})
    set(DB_LINUX_32BIT_REQUIRED_LINK_OPTIONS ${DB_CROSS_LINK_FLAGS})
    if(DB_EFFECTIVE_LTO)
        list(APPEND DB_LINUX_32BIT_REQUIRED_COMPILE_FLAGS
             "${DB_EFFECTIVE_LTO_FLAG}")
        list(APPEND DB_LINUX_32BIT_REQUIRED_LINK_OPTIONS
             "${DB_EFFECTIVE_LTO_FLAG}")
    endif()
    if(DB_USE_LLD)
        list(APPEND DB_LINUX_32BIT_REQUIRED_LINK_OPTIONS -fuse-ld=lld)
    endif()
    list(JOIN DB_LINUX_32BIT_REQUIRED_COMPILE_FLAGS " "
         DB_LINUX_32BIT_REQUIRED_FLAGS)
    set(CMAKE_REQUIRED_FLAGS "${DB_LINUX_32BIT_REQUIRED_FLAGS}")
    set(CMAKE_REQUIRED_LINK_OPTIONS ${DB_LINUX_32BIT_REQUIRED_LINK_OPTIONS})
    check_c_source_compiles("int main(void) { return 0; }"
                            DB_LINUX_32BIT_LINK_WORKS)
    set(CMAKE_REQUIRED_FLAGS "${DB_LINUX_32BIT_SAVED_REQUIRED_FLAGS}")
    set(CMAKE_REQUIRED_LINK_OPTIONS
        "${DB_LINUX_32BIT_SAVED_REQUIRED_LINK_OPTIONS}")

    if(NOT DB_LINUX_32BIT_LINK_WORKS)
        message(
            FATAL_ERROR
                "The active 32-bit Linux target toolchain cannot link a minimal binary with the current root/multilib setup. "
                "Provide a working DB_TARGET_LINUX_ROOT or install a complete host 32-bit multilib toolchain."
        )
    endif()
endif()

set(DB_LINUX_TARGET_KIND
    "${DB_LINUX_TARGET_KIND}"
    CACHE INTERNAL "Resolved Linux cross target kind" FORCE)
set(DB_LINUX_EFFECTIVE_ROOT
    "${DB_LINUX_EFFECTIVE_ROOT}"
    CACHE INTERNAL "Resolved Linux cross root/sysroot path" FORCE)
set(DB_LINUX_ROOT_SOURCE
    "${DB_LINUX_ROOT_SOURCE}"
    CACHE INTERNAL "Resolved Linux cross root source" FORCE)
set(DB_LINUX_REQUIRE_STATIC_ARCHIVES
    ${DB_LINUX_REQUIRE_STATIC_ARCHIVES}
    CACHE INTERNAL
          "Whether Linux cross dependency discovery requires static archives"
          FORCE)
