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
      PATHS /usr/lib32 /usr/i686-pc-linux-gnu/usr/lib
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
    get_target_property(DB_GLFW_IMPORTED_LOCATION
      ${DB_GLFW_TARGET} IMPORTED_LOCATION)
    if(DB_GLFW_IMPORTED_LOCATION)
      set(DB_GLFW_LOCATION "${DB_GLFW_IMPORTED_LOCATION}")
    else()
      get_target_property(DB_GLFW_IMPORTED_CONFIGS
        ${DB_GLFW_TARGET} IMPORTED_CONFIGURATIONS)
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

if(DB_BUILD_GLFW_WINDOW_DISPLAY AND DB_GLFW_LINK_LIB)
  set(DB_GLFW_EFFECTIVE_LIB "${DB_GLFW_LINK_LIB}")
  if(DB_TARGET_LINUX_32BIT AND (NOT DB_TARGET_LINUX_MUSL) AND
     EXISTS "/usr/lib32/libglfw.so")
    set(DB_GLFW_EFFECTIVE_LIB "/usr/lib32/libglfw.so")
  endif()

  set(DB_GLFW_SOURCES
    src/displays/glfw_window/display_glfw_window.c
    src/displays/glfw_window/display_glfw_window_common.c
  )
  list(APPEND DB_APP_SOURCES ${DB_GLFW_SOURCES})
  db_set_source_include_directories("${DB_GLFW_EXTRA_INCLUDE_DIRS}" ${DB_GLFW_SOURCES})
  if(DB_TARGET_LINUX_32BIT AND (NOT DB_TARGET_LINUX_MUSL) AND
     DB_COMPILER_IS_CLANG)
    db_set_source_compile_options("-Wno-macro-redefined" ${DB_GLFW_SOURCES})
  endif()
  list(APPEND DB_DRIVERBENCH_LIBS ${DB_GLFW_EFFECTIVE_LIB})
  list(APPEND DB_DRIVERBENCH_DEFS DB_HAS_GLFW=1)
endif()
