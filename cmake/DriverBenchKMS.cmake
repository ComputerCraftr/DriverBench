if(DB_BUILD_LINUX_KMS_ATOMIC_DISPLAY AND DB_PLATFORM_IS_NON_APPLE_UNIX)
    set(DB_ENABLE_KMS_BACKEND 1)
    set(DB_KMS_EXTRA_INCLUDE_DIRS "")
    set(DB_KMS_LINK_LIBS "")
    set(DB_KMS_MISSING_DEPS "")

    set(DB_KMS_SAVED_FIND_LIBRARY_SUFFIXES "${CMAKE_FIND_LIBRARY_SUFFIXES}")
    if(DB_LINUX_REQUIRE_STATIC_ARCHIVES)
        set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
    endif()

    find_library(DB_KMS_GBM_LIB NAMES gbm)
    find_library(DB_KMS_EGL_LIB NAMES EGL)
    find_library(DB_KMS_DRM_LIB NAMES drm)
    set(CMAKE_FIND_LIBRARY_SUFFIXES "${DB_KMS_SAVED_FIND_LIBRARY_SUFFIXES}")

    find_path(DB_KMS_GBM_INCLUDE_DIR NAMES gbm.h)
    find_path(DB_KMS_EGL_INCLUDE_DIR NAMES EGL/egl.h)
    find_path(
        DB_KMS_DRM_INCLUDE_DIR
        NAMES xf86drm.h
        PATH_SUFFIXES libdrm)
    find_path(
        DB_KMS_DRM_SHARED_INCLUDE_DIR
        NAMES drm.h
        PATH_SUFFIXES libdrm drm)

    foreach(
        DB_KMS_DEP
        DB_KMS_GBM_LIB
        DB_KMS_EGL_LIB
        DB_KMS_DRM_LIB
        DB_KMS_GBM_INCLUDE_DIR
        DB_KMS_EGL_INCLUDE_DIR
        DB_KMS_DRM_INCLUDE_DIR
        DB_KMS_DRM_SHARED_INCLUDE_DIR)
        if(NOT ${DB_KMS_DEP})
            list(APPEND DB_KMS_MISSING_DEPS "${DB_KMS_DEP}")
        endif()
    endforeach()

    if(DB_KMS_MISSING_DEPS)
        list(JOIN DB_KMS_MISSING_DEPS ", " DB_KMS_MISSING_TEXT)
        message(
            WARNING
                "Linux KMS atomic backend dependencies were not found in the active toolchain/root "
                "(${DB_KMS_MISSING_TEXT}); disabling Linux KMS atomic backend for this build."
        )
        set(DB_ENABLE_KMS_BACKEND 0)
    else()
        set(DB_KMS_LINK_LIBS ${DB_KMS_GBM_LIB} ${DB_KMS_EGL_LIB}
                             ${DB_KMS_DRM_LIB})
        set(DB_KMS_EXTRA_INCLUDE_DIRS
            ${DB_KMS_GBM_INCLUDE_DIR} ${DB_KMS_EGL_INCLUDE_DIR}
            ${DB_KMS_DRM_INCLUDE_DIR} ${DB_KMS_DRM_SHARED_INCLUDE_DIR})
    endif()

    if(DB_ENABLE_KMS_BACKEND)
        set(DB_KMS_SOURCES
            src/displays/linux_kms_atomic/kms_runner.c
            src/displays/linux_kms_atomic/kms_core.c
            src/displays/linux_kms_atomic/kms_connector.c
            src/displays/linux_kms_atomic/kms_diagnostics.c
            src/displays/linux_kms_atomic/kms_framebuffer.c
            src/displays/linux_kms_atomic/kms_page_flip.c
            src/displays/linux_kms_atomic/kms_egl.c
            src/displays/linux_kms_atomic/kms_cpu.c
            src/displays/linux_kms_atomic/kms_display.c)
        list(APPEND DB_APP_SOURCES ${DB_KMS_SOURCES})
        db_set_source_include_directories("${DB_KMS_EXTRA_INCLUDE_DIRS}"
                                          ${DB_KMS_SOURCES})
        if(DB_TARGET_LINUX_32BIT
           AND (NOT DB_TARGET_LINUX_MUSL)
           AND DB_COMPILER_IS_CLANG)
            db_set_source_compile_options("-Wno-macro-redefined"
                                          ${DB_KMS_SOURCES})
        endif()
        list(APPEND DB_DRIVERBENCH_DEFS DB_HAS_LINUX_KMS_ATOMIC=1)
        list(APPEND DB_DRIVERBENCH_LIBS ${DB_KMS_LINK_LIBS})
    endif()
endif()
