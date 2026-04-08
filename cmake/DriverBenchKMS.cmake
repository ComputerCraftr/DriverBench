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
          PATHS /usr/lib32 /usr/i686-pc-linux-gnu/usr/lib
          NO_DEFAULT_PATH
          NO_CMAKE_FIND_ROOT_PATH
        )
        find_library(DB_EGL_32BIT_LIB
          NAMES EGL
          PATHS /usr/lib32 /usr/i686-pc-linux-gnu/usr/lib
          NO_DEFAULT_PATH
          NO_CMAKE_FIND_ROOT_PATH
        )
        find_library(DB_DRM_32BIT_LIB
          NAMES drm
          PATHS /usr/lib32 /usr/i686-pc-linux-gnu/usr/lib
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
        set(DB_KMS_SOURCES
          src/displays/linux_kms_atomic/display_linux_kms_atomic_runner.c
          src/displays/linux_kms_atomic/display_linux_kms_atomic_core.c
          src/displays/linux_kms_atomic/display_linux_kms_atomic_cpu.c
          src/displays/linux_kms_atomic/display_linux_kms_atomic.c
        )
        list(APPEND DB_DRIVERBENCH_SOURCES ${DB_KMS_SOURCES})
        db_set_source_include_directories("${DB_KMS_EXTRA_INCLUDE_DIRS}" ${DB_KMS_SOURCES})
        if(DB_TARGET_LINUX_32BIT AND (NOT DB_TARGET_LINUX_MUSL) AND
           DB_COMPILER_IS_CLANG)
          db_set_source_compile_options("-Wno-macro-redefined" ${DB_KMS_SOURCES})
        endif()
        list(APPEND DB_DRIVERBENCH_DEFS DB_HAS_LINUX_KMS_ATOMIC=1)
        list(APPEND DB_DRIVERBENCH_LIBS ${DB_KMS_LINK_LIBS})
      endif()
    endif()
  endif()
endif()
