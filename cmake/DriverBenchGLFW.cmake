set(DB_GLFW_TARGET "")
set(DB_GLFW_LINK_LIB "")
set(DB_GLFW_EXTRA_INCLUDE_DIRS "")
set(DB_GLFW_PROVIDER_RESOLVED
    "off"
    CACHE INTERNAL "Resolved GLFW provider for this configure")
set(DB_GLFW_AVAILABLE
    OFF
    CACHE INTERNAL "Whether GLFW support is available for this configure")
set(DB_GLFW_UNAVAILABLE_REASON
    ""
    CACHE INTERNAL "Reason GLFW support is unavailable for this configure")

function(db_glfw_mark_unavailable reason_text)
    if(DB_GLFW_REQUIRED)
        message(FATAL_ERROR "${reason_text}")
    endif()
    message(WARNING "${reason_text}")
    set(DB_GLFW_TARGET
        ""
        PARENT_SCOPE)
    set(DB_GLFW_LINK_LIB
        ""
        PARENT_SCOPE)
    set(DB_GLFW_EXTRA_INCLUDE_DIRS
        ""
        PARENT_SCOPE)
    set(DB_GLFW_PROVIDER_RESOLVED
        "off"
        CACHE INTERNAL "Resolved GLFW provider for this configure" FORCE)
    set(DB_GLFW_AVAILABLE
        OFF
        CACHE INTERNAL "Whether GLFW support is available for this configure"
              FORCE)
    set(DB_GLFW_UNAVAILABLE_REASON
        "${reason_text}"
        CACHE INTERNAL "Reason GLFW support is unavailable for this configure"
              FORCE)
endfunction()

function(db_glfw_set_available)
    set(DB_GLFW_UNAVAILABLE_REASON
        ""
        CACHE INTERNAL "Reason GLFW support is unavailable for this configure"
              FORCE)
endfunction()

function(db_glfw_probe_threads out_ok out_reason)
    find_package(Threads QUIET)
    if(Threads_FOUND)
        set(${out_ok}
            ON
            PARENT_SCOPE)
        set(${out_reason}
            ""
            PARENT_SCOPE)
        return()
    endif()

    if(DB_TARGET_LINUX_MUSL)
        include(CheckCSourceCompiles)
        set(DB_GLFW_THREADS_SAVED_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS}")
        set(DB_GLFW_THREADS_SAVED_REQUIRED_LINK_OPTIONS
            "${CMAKE_REQUIRED_LINK_OPTIONS}")
        set(DB_GLFW_THREADS_SAVED_REQUIRED_LIBRARIES
            "${CMAKE_REQUIRED_LIBRARIES}")

        list(JOIN DB_CROSS_COMPILE_FLAGS " " DB_GLFW_THREADS_REQUIRED_FLAGS)
        set(CMAKE_REQUIRED_FLAGS "${DB_GLFW_THREADS_REQUIRED_FLAGS}")
        set(CMAKE_REQUIRED_LINK_OPTIONS ${DB_CROSS_LINK_FLAGS})
        set(CMAKE_REQUIRED_LIBRARIES "-pthread")
        check_c_source_compiles(
            "#include <pthread.h>\nint main(void) { pthread_t t; return (int)(sizeof(t) == 0U); }\n"
            DB_GLFW_MUSL_PTHREAD_WORKS)

        set(CMAKE_REQUIRED_FLAGS "${DB_GLFW_THREADS_SAVED_REQUIRED_FLAGS}")
        set(CMAKE_REQUIRED_LINK_OPTIONS
            "${DB_GLFW_THREADS_SAVED_REQUIRED_LINK_OPTIONS}")
        set(CMAKE_REQUIRED_LIBRARIES
            "${DB_GLFW_THREADS_SAVED_REQUIRED_LIBRARIES}")

        if(DB_GLFW_MUSL_PTHREAD_WORKS)
            set(Threads_FOUND TRUE)
            set(CMAKE_THREAD_LIBS_INIT
                "-pthread"
                CACHE STRING "" FORCE)
            set(CMAKE_USE_PTHREADS_INIT
                1
                CACHE BOOL "" FORCE)
            set(THREADS_PREFER_PTHREAD_FLAG
                TRUE
                CACHE BOOL "" FORCE)
            set(${out_ok}
                ON
                PARENT_SCOPE)
            set(${out_reason}
                ""
                PARENT_SCOPE)
            return()
        endif()
    endif()

    set(${out_ok}
        OFF
        PARENT_SCOPE)
    set(${out_reason}
        "Threads"
        PARENT_SCOPE)
endfunction()

function(db_glfw_probe_linux_x11_dependencies out_ok out_reason)
    set(db_glfw_missing_libs "")
    set(db_glfw_active_root_pattern "^${DB_LINUX_EFFECTIVE_ROOT}(/|$)")
    set(db_glfw_saved_find_library_suffixes "${CMAKE_FIND_LIBRARY_SUFFIXES}")
    set(db_glfw_required_headers
        "X11/Xlib.h"
        "X11/extensions/Xrandr.h"
        "X11/extensions/Xinerama.h"
        "X11/Xcursor/Xcursor.h"
        "X11/extensions/XInput2.h"
        "X11/extensions/Xrender.h"
        "GL/glx.h")

    if(DB_LINUX_REQUIRE_STATIC_ARCHIVES)
        set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
    endif()

    db_glfw_probe_threads(DB_GLFW_THREADS_OK DB_GLFW_THREADS_REASON)
    if(NOT DB_GLFW_THREADS_OK)
        list(APPEND db_glfw_missing_libs "${DB_GLFW_THREADS_REASON}")
    endif()

    foreach(
        db_glfw_lib_name
        X11
        Xrandr
        Xinerama
        Xcursor
        Xi
        Xext
        GL)
        unset(db_glfw_probe_lib CACHE)
        find_library(db_glfw_probe_lib NAMES ${db_glfw_lib_name}
                                             lib${db_glfw_lib_name}.a)
        if(NOT db_glfw_probe_lib)
            list(APPEND db_glfw_missing_libs "${db_glfw_lib_name}")
        elseif((NOT DB_LINUX_EFFECTIVE_ROOT STREQUAL "")
               AND (NOT db_glfw_probe_lib MATCHES
                    "${db_glfw_active_root_pattern}"))
            list(APPEND db_glfw_missing_libs
                 "${db_glfw_lib_name} (outside active root)")
        elseif(DB_LINUX_REQUIRE_STATIC_ARCHIVES AND (NOT db_glfw_probe_lib
                                                     MATCHES "\\.a$"))
            list(APPEND db_glfw_missing_libs
                 "${db_glfw_lib_name} (static archive)")
        endif()
    endforeach()

    foreach(db_glfw_header IN LISTS db_glfw_required_headers)
        unset(db_glfw_probe_include CACHE)
        find_path(db_glfw_probe_include NAMES "${db_glfw_header}")
        if(NOT db_glfw_probe_include)
            list(APPEND db_glfw_missing_libs "${db_glfw_header}")
        elseif((NOT DB_LINUX_EFFECTIVE_ROOT STREQUAL "")
               AND (NOT db_glfw_probe_include MATCHES
                    "${db_glfw_active_root_pattern}"))
            list(APPEND db_glfw_missing_libs
                 "${db_glfw_header} (outside active root)")
        endif()
    endforeach()

    set(CMAKE_FIND_LIBRARY_SUFFIXES "${db_glfw_saved_find_library_suffixes}")

    if(db_glfw_missing_libs)
        list(JOIN db_glfw_missing_libs ", " db_glfw_missing_text)
        set(${out_ok}
            OFF
            PARENT_SCOPE)
        set(${out_reason}
            "DB_GLFW_PROVIDER=vendored requires Linux X11/OpenGL dependencies for this configure, but the following libraries were not found in the active toolchain/root: ${db_glfw_missing_text}"
            PARENT_SCOPE)
    else()
        set(${out_ok}
            ON
            PARENT_SCOPE)
        set(${out_reason}
            ""
            PARENT_SCOPE)
    endif()
endfunction()

function(db_glfw_resolve_system_provider)
    set(DB_GLFW_SYSTEM_TARGET "")
    set(DB_GLFW_SYSTEM_LINK_LIB "")
    set(DB_GLFW_SYSTEM_EXTRA_INCLUDE_DIRS "")

    find_package(glfw3 QUIET)
    if(TARGET glfw)
        set(DB_GLFW_SYSTEM_TARGET glfw)
    elseif(TARGET glfw3::glfw)
        set(DB_GLFW_SYSTEM_TARGET glfw3::glfw)
    endif()
    set(DB_GLFW_SYSTEM_LINK_LIB "${DB_GLFW_SYSTEM_TARGET}")

    if(DB_GLFW_SYSTEM_LINK_LIB STREQUAL "")
        find_package(PkgConfig QUIET)
        if(PKG_CONFIG_FOUND)
            pkg_check_modules(GLFW3 QUIET glfw3)
            if(GLFW3_FOUND)
                if(GLFW3_INCLUDE_DIRS)
                    list(APPEND DB_GLFW_SYSTEM_EXTRA_INCLUDE_DIRS
                         ${GLFW3_INCLUDE_DIRS})
                endif()
                find_library(
                    DB_GLFW_PKG_LIB
                    NAMES glfw glfw3
                    PATHS ${GLFW3_LIBRARY_DIRS}
                    NO_DEFAULT_PATH)
                if(DB_GLFW_PKG_LIB)
                    set(DB_GLFW_SYSTEM_LINK_LIB "${DB_GLFW_PKG_LIB}")
                else()
                    pkg_check_modules(GLFW3_IMPORTED QUIET IMPORTED_TARGET
                                      glfw3)
                    if(TARGET PkgConfig::GLFW3_IMPORTED)
                        set(DB_GLFW_SYSTEM_LINK_LIB PkgConfig::GLFW3_IMPORTED)
                    endif()
                endif()
            endif()
        endif()
    endif()

    set(DB_GLFW_TARGET
        "${DB_GLFW_SYSTEM_TARGET}"
        PARENT_SCOPE)
    set(DB_GLFW_LINK_LIB
        "${DB_GLFW_SYSTEM_LINK_LIB}"
        PARENT_SCOPE)
    set(DB_GLFW_EXTRA_INCLUDE_DIRS
        "${DB_GLFW_SYSTEM_EXTRA_INCLUDE_DIRS}"
        PARENT_SCOPE)
endfunction()

function(db_glfw_resolve_vendored_provider)
    set(DB_GLFW_VENDOR_DIR "${CMAKE_SOURCE_DIR}/third_party/glfw")
    if(NOT EXISTS "${DB_GLFW_VENDOR_DIR}/CMakeLists.txt")
        db_glfw_mark_unavailable(
            "DB_GLFW_PROVIDER=vendored but third_party/glfw is missing. Run 'git submodule update --init --recursive third_party/glfw' or set -DDB_GLFW_PROVIDER=system/off."
        )
        return()
    endif()

    set(DB_GLFW_VENDOR_BINARY_DIR "${CMAKE_BINARY_DIR}/third_party/glfw")

    set(DB_GLFW_SAVED_BUILD_SHARED_LIBS "${BUILD_SHARED_LIBS}")
    set(BUILD_SHARED_LIBS OFF)
    set(GLFW_BUILD_DOCS
        OFF
        CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS
        OFF
        CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES
        OFF
        CACHE BOOL "" FORCE)
    set(GLFW_INSTALL
        OFF
        CACHE BOOL "" FORCE)
    set(GLFW_LIBRARY_TYPE
        STATIC
        CACHE STRING "" FORCE)

    if(DB_PLATFORM_IS_APPLE)
        set(GLFW_BUILD_COCOA
            ON
            CACHE BOOL "" FORCE)
        set(GLFW_BUILD_X11
            OFF
            CACHE BOOL "" FORCE)
        set(GLFW_BUILD_WAYLAND
            OFF
            CACHE BOOL "" FORCE)
    elseif(DB_PLATFORM_IS_NON_APPLE_UNIX)
        set(GLFW_BUILD_COCOA
            OFF
            CACHE BOOL "" FORCE)
        set(GLFW_BUILD_X11
            ON
            CACHE BOOL "" FORCE)
        set(GLFW_BUILD_WAYLAND
            OFF
            CACHE BOOL "" FORCE)
    endif()

    if(DB_PLATFORM_IS_NON_APPLE_UNIX)
        db_glfw_probe_linux_x11_dependencies(DB_GLFW_VENDOR_DEPS_OK
                                             DB_GLFW_VENDOR_DEPS_REASON)
        if(NOT DB_GLFW_VENDOR_DEPS_OK)
            db_glfw_mark_unavailable("${DB_GLFW_VENDOR_DEPS_REASON}")
            return()
        endif()
    endif()

    if(NOT TARGET glfw)
        add_subdirectory("${DB_GLFW_VENDOR_DIR}" "${DB_GLFW_VENDOR_BINARY_DIR}"
                         EXCLUDE_FROM_ALL)
    endif()
    set(BUILD_SHARED_LIBS "${DB_GLFW_SAVED_BUILD_SHARED_LIBS}")

    if(TARGET glfw)
        set_property(TARGET glfw PROPERTY EXPORT_COMPILE_COMMANDS OFF)
        if(DB_CROSS_COMPILE_FLAGS)
            target_compile_options(glfw PRIVATE ${DB_CROSS_COMPILE_FLAGS})
        endif()
        if(DB_CROSS_LINK_FLAGS)
            target_link_options(glfw PRIVATE ${DB_CROSS_LINK_FLAGS})
        endif()
        set(DB_GLFW_TARGET
            glfw
            PARENT_SCOPE)
        set(DB_GLFW_LINK_LIB
            glfw
            PARENT_SCOPE)
        set(DB_GLFW_EXTRA_INCLUDE_DIRS
            "${DB_GLFW_EXTRA_INCLUDE_DIRS}"
            PARENT_SCOPE)
        db_glfw_set_available()
    else()
        db_glfw_mark_unavailable(
            "DB_GLFW_PROVIDER=vendored did not produce a glfw target. Disabling GLFW-dependent displays."
        )
    endif()
endfunction()

if((NOT DB_GLFW_PROVIDER STREQUAL "vendored")
   AND (NOT DB_GLFW_PROVIDER STREQUAL "system")
   AND (NOT DB_GLFW_PROVIDER STREQUAL "off"))
    message(
        FATAL_ERROR
            "Invalid DB_GLFW_PROVIDER='${DB_GLFW_PROVIDER}'. Expected vendored, system, or off."
    )
endif()

if(DB_BUILD_GLFW_WINDOW_DISPLAY)
    if(DB_GLFW_REQUIRED)
        message(
            STATUS
                "DB_GLFW_REQUIRED=ON: this configure requires the requested GLFW provider and dependency stack."
        )
    endif()
    if(DB_GLFW_PROVIDER STREQUAL "vendored")
        message(
            STATUS
                "DB_GLFW_PROVIDER=vendored: using in-repo static GLFW only; system GLFW discovery is disabled for this configure."
        )
        db_glfw_resolve_vendored_provider()
    elseif(DB_GLFW_PROVIDER STREQUAL "system")
        message(
            STATUS
                "DB_GLFW_PROVIDER=system: enabling explicit system GLFW discovery."
        )
        db_glfw_resolve_system_provider()
        if(DB_GLFW_LINK_LIB STREQUAL "")
            db_glfw_mark_unavailable(
                "DB_GLFW_PROVIDER=system but no compatible system GLFW was found. Disabling GLFW-dependent displays."
            )
        else()
            db_glfw_set_available()
        endif()
    else()
        message(
            STATUS
                "DB_GLFW_PROVIDER=off: disabling GLFW-dependent displays and offscreen OpenGL routes."
        )
    endif()
else()
    message(
        STATUS
            "DB_BUILD_GLFW_WINDOW_DISPLAY=OFF: disabling GLFW-dependent displays and offscreen OpenGL routes."
    )
endif()

if(DB_BUILD_GLFW_WINDOW_DISPLAY AND NOT (DB_GLFW_LINK_LIB STREQUAL ""))
    set(DB_GLFW_PROVIDER_RESOLVED
        "${DB_GLFW_PROVIDER}"
        CACHE INTERNAL "Resolved GLFW provider for this configure" FORCE)
    set(DB_GLFW_AVAILABLE
        ON
        CACHE INTERNAL "Whether GLFW support is available for this configure"
              FORCE)
    set(DB_GLFW_UNAVAILABLE_REASON
        ""
        CACHE INTERNAL "Reason GLFW support is unavailable for this configure"
              FORCE)
    message(
        STATUS
            "GLFW provider resolved to '${DB_GLFW_PROVIDER_RESOLVED}' (${DB_GLFW_LINK_LIB})"
    )

    set(DB_GLFW_SOURCES
        src/displays/glfw_window/glfw_window.c
        src/displays/glfw_window/glfw_opengl.c
        src/displays/glfw_window/glfw_window_common.c)
    list(APPEND DB_APP_SOURCES ${DB_GLFW_SOURCES})
    db_set_source_include_directories("${DB_GLFW_EXTRA_INCLUDE_DIRS}"
                                      ${DB_GLFW_SOURCES})
    list(APPEND DB_DRIVERBENCH_LIBS ${DB_GLFW_LINK_LIB})
    list(APPEND DB_DRIVERBENCH_DEFS DB_HAS_GLFW=1)
else()
    set(DB_GLFW_PROVIDER_RESOLVED
        "off"
        CACHE INTERNAL "Resolved GLFW provider for this configure" FORCE)
    set(DB_GLFW_AVAILABLE
        OFF
        CACHE INTERNAL "Whether GLFW support is available for this configure"
              FORCE)
    if(DB_GLFW_UNAVAILABLE_REASON STREQUAL "")
        set(DB_GLFW_UNAVAILABLE_REASON
            "GLFW-backed displays are disabled by build policy for this configure"
            CACHE INTERNAL
                  "Reason GLFW support is unavailable for this configure" FORCE)
    endif()
endif()

list(APPEND DB_DRIVERBENCH_DEFS
     DB_GLFW_PROVIDER_TEXT=\"${DB_GLFW_PROVIDER_RESOLVED}\")
