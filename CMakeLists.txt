cmake_minimum_required(VERSION 3.16)

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

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(DB_CORE_SOURCES src/core/db_core.c)
set(DB_GLFW_DISPLAY_SOURCES src/displays/glfw_window/display_glfw_window_common.c)

option(DB_BUILD_VULKAN "Build Vulkan benchmark backend" ON)
option(DB_BUILD_OPENGL "Build OpenGL benchmark backend" ON)
option(DB_ENABLE_AGGRESSIVE_OPT "Enable aggressive compile optimization flags" ON)
option(DB_ENABLE_LOOP_HINTS "Enable loop optimization hint flags" ON)
option(DB_ENABLE_LTO "Enable link-time optimization in release-like builds" ON)
option(DB_ENABLE_SANITIZERS "Enable sanitizers for debug builds" ON)

set(DB_OPENGL_RENDERER "auto" CACHE STRING
  "OpenGL renderer: auto, gl1_5_gles1_1, or gl3_3")
set_property(CACHE DB_OPENGL_RENDERER PROPERTY STRINGS auto gl1_5_gles1_1 gl3_3)

set(DB_OPENGL_DISPLAY "auto" CACHE STRING
  "OpenGL display: auto, glfw_window, or linux_kms_atomic")
set_property(CACHE DB_OPENGL_DISPLAY PROPERTY STRINGS
  auto glfw_window linux_kms_atomic
)

set(DB_VULKAN_DISPLAY "auto" CACHE STRING
  "Vulkan display: auto, glfw_window, or vk_khr_display")
set_property(CACHE DB_VULKAN_DISPLAY PROPERTY STRINGS
  auto glfw_window vk_khr_display
)

set(HAVE_BACKEND OFF)
set(DB_IS_RELEASE_LIKE OFF)
if(CMAKE_BUILD_TYPE MATCHES "^(Release|RelWithDebInfo|MinSizeRel)$")
  set(DB_IS_RELEASE_LIKE ON)
endif()

set(DB_EFFECTIVE_LTO OFF)
if(DB_IS_RELEASE_LIKE AND DB_ENABLE_LTO)
  if(CMAKE_C_COMPILER_ID MATCHES "Clang")
    set(DB_EFFECTIVE_LTO ON)
  else()
    message(WARNING
      "DB_ENABLE_LTO is ON, but compiler '${CMAKE_C_COMPILER_ID}' is not Clang; "
      "disabling LTO for this configure."
    )
  endif()
endif()

set(DB_USE_LLD OFF)
if(DB_EFFECTIVE_LTO AND CMAKE_C_COMPILER_ID STREQUAL "Clang")
  find_program(DB_LLD_PROGRAM NAMES ld.lld lld)
  if(DB_LLD_PROGRAM)
    set(DB_USE_LLD ON)
  endif()
endif()

function(db_apply_perf_options target_name)
  if(NOT TARGET ${target_name})
    return()
  endif()

  if(DB_ENABLE_SANITIZERS AND CMAKE_BUILD_TYPE STREQUAL "Debug")
    if(CMAKE_C_COMPILER_ID MATCHES "^(Clang|AppleClang|GNU)$")
      set(DB_SANITIZER_FLAGS -fsanitize=address,undefined)
      if(NOT APPLE)
        list(APPEND DB_SANITIZER_FLAGS -fsanitize=leak)
      endif()
      target_compile_options(${target_name} PRIVATE
        -fno-omit-frame-pointer
        ${DB_SANITIZER_FLAGS}
      )
      if(COMMAND target_link_options)
        target_link_options(${target_name} PRIVATE ${DB_SANITIZER_FLAGS})
      endif()
    endif()
  endif()

  if(DB_IS_RELEASE_LIKE AND DB_ENABLE_AGGRESSIVE_OPT)
    if(CMAKE_C_COMPILER_ID MATCHES "^(Clang|AppleClang|GNU)$")
      target_compile_options(${target_name} PRIVATE -O3)
    endif()
  endif()

  if(DB_IS_RELEASE_LIKE AND DB_ENABLE_LOOP_HINTS)
    if(CMAKE_C_COMPILER_ID MATCHES "^(Clang|AppleClang|GNU)$")
      target_compile_options(${target_name} PRIVATE -funroll-loops)
    endif()
  endif()

  if(DB_EFFECTIVE_LTO)
    target_compile_options(${target_name} PRIVATE -flto)
    if(COMMAND target_link_options)
      target_link_options(${target_name} PRIVATE -flto)
      if(DB_USE_LLD)
        target_link_options(${target_name} PRIVATE -fuse-ld=lld)
      endif()
    endif()
  endif()
endfunction()

set(DB_GLFW_TARGET "")
find_package(glfw3 QUIET)
if(TARGET glfw)
  set(DB_GLFW_TARGET glfw)
elseif(TARGET glfw3::glfw)
  set(DB_GLFW_TARGET glfw3::glfw)
endif()

if(NOT DB_OPENGL_RENDERER MATCHES "^(auto|gl1_5_gles1_1|gl3_3)$")
  message(FATAL_ERROR
    "Invalid DB_OPENGL_RENDERER='${DB_OPENGL_RENDERER}'. "
    "Use: auto, gl1_5_gles1_1, or gl3_3."
  )
endif()

if(NOT DB_OPENGL_DISPLAY MATCHES "^(auto|glfw_window|linux_kms_atomic)$")
  message(FATAL_ERROR
    "Invalid DB_OPENGL_DISPLAY='${DB_OPENGL_DISPLAY}'. "
    "Use: auto, glfw_window, or linux_kms_atomic."
  )
endif()

if(NOT DB_VULKAN_DISPLAY MATCHES "^(auto|glfw_window|vk_khr_display)$")
  message(FATAL_ERROR
    "Invalid DB_VULKAN_DISPLAY='${DB_VULKAN_DISPLAY}'. "
    "Use: auto, glfw_window, or vk_khr_display."
  )
endif()

if(DB_BUILD_VULKAN)
  message(STATUS "Vulkan selection: renderer=vulkan_1_2_multi_gpu display=${DB_VULKAN_DISPLAY}")
  find_package(Vulkan QUIET)
  find_program(GLSLC glslc HINTS ENV VULKAN_SDK PATH_SUFFIXES Bin bin)

  set(DB_VULKAN_DISPLAY_SELECTED "${DB_VULKAN_DISPLAY}")
  if(DB_VULKAN_DISPLAY_SELECTED STREQUAL "auto")
    set(DB_VULKAN_DISPLAY_SELECTED "glfw_window")
  endif()

  if(DB_VULKAN_DISPLAY_SELECTED STREQUAL "vk_khr_display")
    message(WARNING
      "Skipping Vulkan backend: DB_VULKAN_DISPLAY=vk_khr_display is not "
      "implemented yet (select glfw_window)."
    )
  else()
    set(DB_VULKAN_SOURCE "src/displays/glfw_window/display_glfw_window_vulkan_1_2_multi_gpu.c")
  endif()

  set(DB_VULKAN_LIB "")
  if(TARGET Vulkan::Vulkan)
    set(DB_VULKAN_LIB Vulkan::Vulkan)
  elseif(Vulkan_FOUND AND Vulkan_LIBRARY)
    set(DB_VULKAN_LIB "${Vulkan_LIBRARY}")
  endif()

  if(DB_VULKAN_SOURCE AND Vulkan_FOUND AND DB_VULKAN_LIB AND DB_GLFW_TARGET AND GLSLC)
    set(DB_VULKAN_TARGET driverbench_glfw_window_vulkan_1_2_multi_gpu)
    add_executable(${DB_VULKAN_TARGET}
      ${DB_VULKAN_SOURCE}
      src/renderers/vulkan_1_2_multi_gpu/renderer_vulkan_1_2_multi_gpu.c
      ${DB_GLFW_DISPLAY_SOURCES}
      ${DB_CORE_SOURCES}
    )
    db_apply_perf_options(${DB_VULKAN_TARGET})
    target_include_directories(${DB_VULKAN_TARGET} PRIVATE ${Vulkan_INCLUDE_DIRS})
    target_link_libraries(${DB_VULKAN_TARGET} PRIVATE ${DB_VULKAN_LIB} ${DB_GLFW_TARGET} m)

    add_custom_command(
      OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/shader_vulkan_1_2_rect.vert.spv
      COMMAND ${GLSLC}
              ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/shader_vulkan_1_2_rect.vert -o
              ${CMAKE_CURRENT_BINARY_DIR}/shader_vulkan_1_2_rect.vert.spv
      DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/shader_vulkan_1_2_rect.vert
      VERBATIM
    )

    add_custom_command(
      OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/shader_vulkan_1_2_rect.frag.spv
      COMMAND ${GLSLC}
              ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/shader_vulkan_1_2_rect.frag -o
              ${CMAKE_CURRENT_BINARY_DIR}/shader_vulkan_1_2_rect.frag.spv
      DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/shader_vulkan_1_2_rect.frag
      VERBATIM
    )

    add_custom_target(driverbench_vulkan_1_2_multi_gpu_shaders ALL
      DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/shader_vulkan_1_2_rect.vert.spv
              ${CMAKE_CURRENT_BINARY_DIR}/shader_vulkan_1_2_rect.frag.spv
    )
    add_dependencies(${DB_VULKAN_TARGET} driverbench_vulkan_1_2_multi_gpu_shaders)

    target_compile_definitions(${DB_VULKAN_TARGET} PRIVATE
      VERT_SPV_PATH="${CMAKE_CURRENT_BINARY_DIR}/shader_vulkan_1_2_rect.vert.spv"
      FRAG_SPV_PATH="${CMAKE_CURRENT_BINARY_DIR}/shader_vulkan_1_2_rect.frag.spv"
    )
    set(HAVE_BACKEND ON)
  else()
    set(DB_VULKAN_MISSING_DEPS "")
    if(NOT DB_VULKAN_SOURCE)
      list(APPEND DB_VULKAN_MISSING_DEPS "display backend")
    endif()
    if(NOT Vulkan_FOUND OR NOT DB_VULKAN_LIB)
      list(APPEND DB_VULKAN_MISSING_DEPS "Vulkan SDK/loader")
    endif()
    if(NOT DB_GLFW_TARGET)
      list(APPEND DB_VULKAN_MISSING_DEPS "glfw3")
    endif()
    if(NOT GLSLC)
      list(APPEND DB_VULKAN_MISSING_DEPS "glslc")
    endif()
    if(DB_VULKAN_MISSING_DEPS)
      list(JOIN DB_VULKAN_MISSING_DEPS ", " DB_VULKAN_MISSING_DEPS_STR)
    else()
      set(DB_VULKAN_MISSING_DEPS_STR "unknown")
    endif()
    message(WARNING
      "Skipping Vulkan backend: missing ${DB_VULKAN_MISSING_DEPS_STR}."
    )
  endif()
endif()

if(DB_BUILD_OPENGL)
  message(STATUS
    "OpenGL selection: renderer=${DB_OPENGL_RENDERER} display=${DB_OPENGL_DISPLAY}"
  )

  set(DB_OPENGL_RENDERER_LIST "")
  if(DB_OPENGL_RENDERER STREQUAL "auto")
    list(APPEND DB_OPENGL_RENDERER_LIST gl1_5_gles1_1 gl3_3)
  else()
    list(APPEND DB_OPENGL_RENDERER_LIST ${DB_OPENGL_RENDERER})
  endif()

  set(DB_OPENGL_DISPLAY_LIST "")
  if(DB_OPENGL_DISPLAY STREQUAL "auto")
    list(APPEND DB_OPENGL_DISPLAY_LIST glfw_window)
    if(NOT APPLE)
      list(APPEND DB_OPENGL_DISPLAY_LIST linux_kms_atomic)
    endif()
  else()
    list(APPEND DB_OPENGL_DISPLAY_LIST ${DB_OPENGL_DISPLAY})
  endif()

  list(FIND DB_OPENGL_DISPLAY_LIST "glfw_window" DB_HAS_GLFW_DISPLAY_INDEX)
  if(NOT DB_HAS_GLFW_DISPLAY_INDEX EQUAL -1)
    if(NOT DB_GLFW_TARGET)
      message(WARNING
        "Skipping OpenGL backend: DB_OPENGL_DISPLAY includes glfw_window but glfw was not found."
      )
    else()
      foreach(DB_RENDERER IN LISTS DB_OPENGL_RENDERER_LIST)
        if(DB_RENDERER STREQUAL "gl1_5_gles1_1")
          set(DB_OPENGL_TARGET driverbench_glfw_window_opengl_gl1_5_gles1_1)
          set(DB_OPENGL_SOURCE src/displays/glfw_window/display_glfw_window_opengl_gl1_5_gles1_1.c)
          set(DB_OPENGL_RENDERER_SOURCE src/renderers/opengl_gl1_5_gles1_1/renderer_opengl_gl1_5_gles1_1.c)
        elseif(DB_RENDERER STREQUAL "gl3_3")
          set(DB_OPENGL_TARGET driverbench_glfw_window_opengl_gl3_3)
          set(DB_OPENGL_SOURCE src/displays/glfw_window/display_glfw_window_opengl_gl3_3.c)
          set(DB_OPENGL_RENDERER_SOURCE src/renderers/opengl_gl3_3/renderer_opengl_gl3_3.c)
        else()
          continue()
        endif()

        add_executable(${DB_OPENGL_TARGET}
          ${DB_OPENGL_SOURCE}
          ${DB_OPENGL_RENDERER_SOURCE}
          ${DB_GLFW_DISPLAY_SOURCES}
          ${DB_CORE_SOURCES}
        )
        db_apply_perf_options(${DB_OPENGL_TARGET})
        if(DB_RENDERER STREQUAL "gl3_3")
          target_compile_definitions(${DB_OPENGL_TARGET} PRIVATE
            OPENGL_GL3_3_VERT_SHADER_PATH="${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/shader_opengl_gl3_3_rect.vert"
            OPENGL_GL3_3_FRAG_SHADER_PATH="${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/shader_opengl_gl3_3_rect.frag"
          )
        endif()
        if(APPLE)
          target_compile_definitions(${DB_OPENGL_TARGET} PRIVATE GL_SILENCE_DEPRECATION)
          target_link_libraries(${DB_OPENGL_TARGET} PRIVATE
            ${DB_GLFW_TARGET}
            "-framework OpenGL"
            m
          )
        else()
          find_package(OpenGL QUIET)
          if(TARGET OpenGL::GL)
            target_link_libraries(${DB_OPENGL_TARGET} PRIVATE
              ${DB_GLFW_TARGET}
              OpenGL::GL
              m
            )
          else()
            target_link_libraries(${DB_OPENGL_TARGET} PRIVATE
              ${DB_GLFW_TARGET}
              GL
              m
            )
          endif()
        endif()
        set(HAVE_BACKEND ON)
      endforeach()
    endif()
  endif()

  list(FIND DB_OPENGL_DISPLAY_LIST "linux_kms_atomic" DB_HAS_KMS_DISPLAY_INDEX)
  if(NOT DB_HAS_KMS_DISPLAY_INDEX EQUAL -1)
    if(APPLE)
      message(WARNING
        "Skipping OpenGL backend: linux_kms_atomic display is Linux-only."
      )
    else()
      find_package(PkgConfig QUIET)
      if(PkgConfig_FOUND)
        pkg_check_modules(GBM QUIET gbm)
        pkg_check_modules(EGL QUIET egl)
        pkg_check_modules(GLES QUIET glesv1_cm)
        pkg_check_modules(DRM QUIET libdrm)
      endif()

      if(PkgConfig_FOUND AND GBM_FOUND AND EGL_FOUND AND GLES_FOUND AND DRM_FOUND)
        list(FIND DB_OPENGL_RENDERER_LIST "gl1_5_gles1_1" DB_HAS_GL15_RENDERER_INDEX)
        if(NOT DB_HAS_GL15_RENDERER_INDEX EQUAL -1)
          set(DB_OPENGL_TARGET driverbench_linux_kms_atomic_opengl_gl1_5_gles1_1)
          add_executable(${DB_OPENGL_TARGET}
            src/displays/linux_kms_atomic/display_linux_kms_atomic_opengl_gl1_5_gles1_1.c
            ${DB_CORE_SOURCES}
          )
          db_apply_perf_options(${DB_OPENGL_TARGET})

          target_include_directories(${DB_OPENGL_TARGET} PRIVATE
            ${GBM_INCLUDE_DIRS}
            ${EGL_INCLUDE_DIRS}
            ${GLES_INCLUDE_DIRS}
            ${DRM_INCLUDE_DIRS}
          )
          target_compile_options(${DB_OPENGL_TARGET} PRIVATE
            ${GBM_CFLAGS_OTHER}
            ${EGL_CFLAGS_OTHER}
            ${GLES_CFLAGS_OTHER}
            ${DRM_CFLAGS_OTHER}
          )
          target_link_directories(${DB_OPENGL_TARGET} PRIVATE
            ${GBM_LIBRARY_DIRS}
            ${EGL_LIBRARY_DIRS}
            ${GLES_LIBRARY_DIRS}
            ${DRM_LIBRARY_DIRS}
          )
          target_link_libraries(${DB_OPENGL_TARGET} PRIVATE
            ${GBM_LIBRARIES}
            ${EGL_LIBRARIES}
            ${GLES_LIBRARIES}
            ${DRM_LIBRARIES}
            m
          )
          set(HAVE_BACKEND ON)
        endif()
      else()
        message(WARNING
          "Skipping OpenGL backend on Linux: requires pkg-config with gbm, egl, glesv1_cm, and libdrm."
        )
      endif()
    endif()
  endif()
endif()

if(NOT HAVE_BACKEND)
  message(WARNING
    "No backends were built. Install Vulkan/glfw/glslc and/or gbm/egl/glesv1_cm/libdrm to enable targets."
  )
endif()
