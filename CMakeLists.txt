cmake_minimum_required(VERSION 3.16)
project(DriverBench C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

option(DB_BUILD_VULKAN "Build Vulkan benchmark backend" ON)
option(DB_BUILD_OPENGL "Build OpenGL benchmark backend" ON)

set(HAVE_BACKEND OFF)

if(DB_BUILD_VULKAN)
  find_package(Vulkan QUIET)
  find_package(glfw3 QUIET)
  find_program(GLSLC glslc HINTS ENV VULKAN_SDK PATH_SUFFIXES Bin bin)

  set(DB_GLFW_TARGET "")
  if(TARGET glfw)
    set(DB_GLFW_TARGET glfw)
  elseif(TARGET glfw3::glfw)
    set(DB_GLFW_TARGET glfw3::glfw)
  endif()

  set(DB_VULKAN_LIB "")
  if(TARGET Vulkan::Vulkan)
    set(DB_VULKAN_LIB Vulkan::Vulkan)
  elseif(Vulkan_FOUND AND Vulkan_LIBRARY)
    set(DB_VULKAN_LIB "${Vulkan_LIBRARY}")
  endif()

  if(Vulkan_FOUND AND DB_VULKAN_LIB AND DB_GLFW_TARGET AND GLSLC)
    add_executable(driverbench_vulkan src/vulkan1.2multi.c)
    target_include_directories(driverbench_vulkan PRIVATE ${Vulkan_INCLUDE_DIRS})
    target_link_libraries(driverbench_vulkan PRIVATE ${DB_VULKAN_LIB} ${DB_GLFW_TARGET} m)

    add_custom_command(
      OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/rect.vert.spv
      COMMAND ${GLSLC} ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/rect.vert -o
              ${CMAKE_CURRENT_BINARY_DIR}/rect.vert.spv
      DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/rect.vert
      VERBATIM
    )

    add_custom_command(
      OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/rect.frag.spv
      COMMAND ${GLSLC} ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/rect.frag -o
              ${CMAKE_CURRENT_BINARY_DIR}/rect.frag.spv
      DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/rect.frag
      VERBATIM
    )

    add_custom_target(driverbench_vulkan_shaders ALL
      DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/rect.vert.spv
              ${CMAKE_CURRENT_BINARY_DIR}/rect.frag.spv
    )
    add_dependencies(driverbench_vulkan driverbench_vulkan_shaders)

    target_compile_definitions(driverbench_vulkan PRIVATE
      VERT_SPV_PATH="${CMAKE_CURRENT_BINARY_DIR}/rect.vert.spv"
      FRAG_SPV_PATH="${CMAKE_CURRENT_BINARY_DIR}/rect.frag.spv"
    )

    set(HAVE_BACKEND ON)
  else()
    message(WARNING
      "Skipping Vulkan backend: requires Vulkan SDK, glfw, and glslc."
    )
  endif()
endif()

if(DB_BUILD_OPENGL)
  find_package(PkgConfig QUIET)
  if(PkgConfig_FOUND)
    pkg_check_modules(GBM QUIET gbm)
    pkg_check_modules(EGL QUIET egl)
    pkg_check_modules(GLES QUIET glesv1_cm)
    pkg_check_modules(DRM QUIET libdrm)
  endif()

  if(PkgConfig_FOUND AND GBM_FOUND AND EGL_FOUND AND GLES_FOUND AND DRM_FOUND)
    add_executable(driverbench_opengl src/gl1.5gles1.1.c)

    target_include_directories(driverbench_opengl PRIVATE
      ${GBM_INCLUDE_DIRS}
      ${EGL_INCLUDE_DIRS}
      ${GLES_INCLUDE_DIRS}
      ${DRM_INCLUDE_DIRS}
    )
    target_compile_options(driverbench_opengl PRIVATE
      ${GBM_CFLAGS_OTHER}
      ${EGL_CFLAGS_OTHER}
      ${GLES_CFLAGS_OTHER}
      ${DRM_CFLAGS_OTHER}
    )
    target_link_directories(driverbench_opengl PRIVATE
      ${GBM_LIBRARY_DIRS}
      ${EGL_LIBRARY_DIRS}
      ${GLES_LIBRARY_DIRS}
      ${DRM_LIBRARY_DIRS}
    )
    target_link_libraries(driverbench_opengl PRIVATE
      ${GBM_LIBRARIES}
      ${EGL_LIBRARIES}
      ${GLES_LIBRARIES}
      ${DRM_LIBRARIES}
      m
    )

    set(HAVE_BACKEND ON)
  else()
    message(WARNING
      "Skipping OpenGL backend: requires pkg-config with gbm, egl, glesv1_cm, and libdrm."
    )
  endif()
endif()

if(NOT HAVE_BACKEND)
  message(WARNING
    "No backends were built. Install Vulkan/glfw/glslc and/or gbm/egl/glesv1_cm/libdrm to enable targets."
  )
endif()
