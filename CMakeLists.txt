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
include(CTest)

include(CheckIncludeFile)
include(CheckSymbolExists)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(DB_CORE_SOURCES
  src/core/db_buffer_convert.c
  src/core/db_core.c
  src/core/db_hash.c
)

option(DB_BUILD_VULKAN "Build Vulkan benchmark backend" ON)
option(DB_BUILD_OPENGL "Build OpenGL benchmark backend" ON)
option(DB_FORCE_GLES_ONLY
  "Force OpenGL backend to compile in GLES-only mode (for build validation/testing)"
  OFF)
option(DB_BUILD_GLFW_WINDOW_DISPLAY "Build GLFW window display backend" ON)
option(DB_BUILD_LINUX_KMS_ATOMIC_DISPLAY
  "Build Linux KMS atomic display backend" ON)
option(DB_ENABLE_GLFW_OFFSCREEN_TESTS
  "Enable deterministic offscreen GLFW hash tests (requires working GLFW display stack)"
  OFF)
option(DB_ENABLE_AGGRESSIVE_OPT "Enable aggressive compile optimization flags" ON)
option(DB_ENABLE_LOOP_HINTS "Enable loop optimization hint flags" ON)
option(DB_ENABLE_LTO "Enable link-time optimization in release-like builds" ON)
option(DB_ENABLE_LTO_RELWITHDEBINFO
  "Enable LTO specifically for RelWithDebInfo builds" OFF)
option(DB_ENABLE_SANITIZERS "Enable sanitizers for debug builds" ON)
option(DB_ENABLE_DSYM "Generate dSYM bundles for Apple debug-like builds" ON)

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
  elseif(CMAKE_C_COMPILER_ID MATCHES "Clang")
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

  if(UNIX AND NOT APPLE)
    target_compile_definitions(${target_name} PRIVATE _POSIX_C_SOURCE=200809L)
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

  if(APPLE AND DB_ENABLE_DSYM)
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
if(DB_BUILD_GLFW_WINDOW_DISPLAY)
  find_package(glfw3 QUIET)
  if(TARGET glfw)
    set(DB_GLFW_TARGET glfw)
  elseif(TARGET glfw3::glfw)
    set(DB_GLFW_TARGET glfw3::glfw)
  endif()
endif()

set(DB_DRIVERBENCH_SOURCES
  src/driverbench_cli.c
  src/driverbench_main.c
  src/displays/display_dispatch.c
  src/displays/offscreen/display_offscreen.c
  src/renderers/cpu_renderer/renderer_cpu_renderer.c
  ${DB_CORE_SOURCES}
)
set(DB_DRIVERBENCH_LIBS m)
set(DB_DRIVERBENCH_DEFS "")

if(DB_BUILD_GLFW_WINDOW_DISPLAY AND DB_GLFW_TARGET)
  list(APPEND DB_DRIVERBENCH_SOURCES
    src/displays/glfw_window/display_glfw_window.c
    src/displays/glfw_window/display_glfw_window_common.c
  )
  list(APPEND DB_DRIVERBENCH_LIBS ${DB_GLFW_TARGET})
  list(APPEND DB_DRIVERBENCH_DEFS DB_HAS_GLFW=1)
endif()

if(DB_BUILD_OPENGL)
  find_package(OpenGL QUIET)
  set(DB_OPENGL_RUNTIME_ENABLED OFF)
  set(DB_OPENGL_DESKTOP_AVAILABLE OFF)

  if(NOT DB_FORCE_GLES_ONLY)
    if(APPLE)
      set(DB_OPENGL_DESKTOP_AVAILABLE ON)
    elseif(TARGET OpenGL::OpenGL OR TARGET OpenGL::GL)
      set(DB_OPENGL_DESKTOP_AVAILABLE ON)
    endif()
  else()
    message(STATUS
      "DB_FORCE_GLES_ONLY=ON: skipping desktop OpenGL sources/libraries for OpenGL backend")
  endif()

  if(DB_BUILD_GLFW_WINDOW_DISPLAY AND DB_GLFW_TARGET)
    set(DB_OPENGL_RUNTIME_ENABLED ON)
  endif()

  if(DB_BUILD_LINUX_KMS_ATOMIC_DISPLAY AND UNIX AND NOT APPLE)
    find_package(PkgConfig QUIET)
    if(PKG_CONFIG_FOUND)
      pkg_check_modules(GBM QUIET gbm)
      pkg_check_modules(EGL QUIET egl)
      pkg_check_modules(GLES QUIET glesv1_cm)
      pkg_check_modules(DRM QUIET libdrm)
      if(GBM_FOUND AND EGL_FOUND AND DRM_FOUND AND GLES_FOUND)
        list(APPEND DB_DRIVERBENCH_SOURCES
          src/displays/linux_kms_atomic/display_linux_kms_atomic_common.c
          src/displays/linux_kms_atomic/display_linux_kms_atomic.c
        )
        list(APPEND DB_DRIVERBENCH_DEFS DB_HAS_LINUX_KMS_ATOMIC=1)
        list(APPEND DB_DRIVERBENCH_LIBS
          ${GBM_LIBRARIES}
          ${EGL_LIBRARIES}
          ${GLES_LIBRARIES}
          ${DRM_LIBRARIES}
        )
        set(DB_OPENGL_RUNTIME_ENABLED ON)
      endif()
    endif()
  endif()

  if(DB_OPENGL_RUNTIME_ENABLED)
    list(APPEND DB_DRIVERBENCH_SOURCES
      src/renderers/opengl_gl1_5_gles1_1/renderer_opengl_gl1_5_gles1_1.c
      src/renderers/renderer_gl_common.c
    )
    list(APPEND DB_DRIVERBENCH_DEFS DB_HAS_OPENGL_API=1)

    if(DB_OPENGL_DESKTOP_AVAILABLE)
      list(APPEND DB_DRIVERBENCH_SOURCES
        src/renderers/opengl_gl3_3/renderer_opengl_gl3_3.c
      )
      list(APPEND DB_DRIVERBENCH_DEFS
        DB_HAS_OPENGL_DESKTOP=1
        OPENGL_GL3_3_VERT_SHADER_PATH="${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/shader_opengl_gl3_3_rect.vert"
        OPENGL_GL3_3_FRAG_SHADER_PATH="${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/shader_opengl_gl3_3_rect.frag"
      )
      if(APPLE)
        list(APPEND DB_DRIVERBENCH_LIBS "-framework OpenGL")
        list(APPEND DB_DRIVERBENCH_DEFS GL_SILENCE_DEPRECATION=1)
      else()
        if(TARGET OpenGL::OpenGL)
          list(APPEND DB_DRIVERBENCH_LIBS OpenGL::OpenGL)
        elseif(TARGET OpenGL::GL)
          list(APPEND DB_DRIVERBENCH_LIBS OpenGL::GL)
        else()
          list(APPEND DB_DRIVERBENCH_LIBS GL)
        endif()
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

  if(DB_BUILD_GLFW_WINDOW_DISPLAY AND DB_GLFW_TARGET AND Vulkan_FOUND AND
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

    add_custom_target(driverbench_vulkan_shaders ALL
      DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/shader_vulkan_1_2_rect.vert.spv
              ${CMAKE_CURRENT_BINARY_DIR}/shader_vulkan_1_2_rect.frag.spv
    )
    list(APPEND DB_DRIVERBENCH_DEFS
      VERT_SPV_PATH="${CMAKE_CURRENT_BINARY_DIR}/shader_vulkan_1_2_rect.vert.spv"
      FRAG_SPV_PATH="${CMAKE_CURRENT_BINARY_DIR}/shader_vulkan_1_2_rect.frag.spv"
    )
  endif()
endif()

set(DB_UNIFIED_TARGET driverbench)
add_executable(${DB_UNIFIED_TARGET}
  ${DB_DRIVERBENCH_SOURCES}
)
db_apply_perf_options(${DB_UNIFIED_TARGET})
target_compile_definitions(${DB_UNIFIED_TARGET} PRIVATE ${DB_DRIVERBENCH_DEFS})
if(GBM_FOUND OR EGL_FOUND OR DRM_FOUND OR GLES_FOUND)
  target_include_directories(${DB_UNIFIED_TARGET} PRIVATE
    ${GBM_INCLUDE_DIRS}
    ${EGL_INCLUDE_DIRS}
    ${DRM_INCLUDE_DIRS}
    ${GLES_INCLUDE_DIRS}
  )
endif()
target_link_libraries(${DB_UNIFIED_TARGET} PRIVATE ${DB_DRIVERBENCH_LIBS})
if(TARGET driverbench_vulkan_shaders)
  add_dependencies(${DB_UNIFIED_TARGET} driverbench_vulkan_shaders)
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

  function(db_add_hash_equivalence_test test_name test_args_a test_args_b hash_checks)
    if(NOT TARGET ${DB_UNIFIED_TARGET})
      return()
    endif()
    add_test(
      NAME ${test_name}
      COMMAND ${CMAKE_COMMAND}
        -DTEST_BIN=$<TARGET_FILE:${DB_UNIFIED_TARGET}>
        -DTEST_ARGS=${test_args_a}
        -DTEST_ARGS_B=${test_args_b}
        -DTEST_HASH_CHECKS=${hash_checks}
        -P ${CMAKE_SOURCE_DIR}/cmake/RunDeterminismTest.cmake
    )
  endfunction()

  set(DB_DETERMINISM_COMMON_ARGS "--random-seed 12345 --fps-cap 0")
  set(DB_DETERMINISM_HASH "--hash both")
  set(DB_DETERMINISM_HASH_REPORT "--hash-report aggregate")
  set(DB_DETERMINISM_FRAME_LIMIT "--frame-limit 600")
  set(DB_DETERMINISM_GLFW_FRAMES "--offscreen 1 --frame-limit 600 --vsync 0")

  db_add_determinism_test(
    determinism_cpu_renderer_snake
    "--api cpu --display offscreen --benchmark-mode snake_grid ${DB_DETERMINISM_COMMON_ARGS} ${DB_DETERMINISM_HASH} ${DB_DETERMINISM_HASH_REPORT} ${DB_DETERMINISM_FRAME_LIMIT}"
    "state_hash_aggregate=0xe2647e06105e3581,bo_hash_aggregate=0x824f612ef514b922"
  )
  db_add_determinism_test(
    determinism_cpu_renderer_gradient_fill
    "--api cpu --display offscreen --benchmark-mode gradient_fill ${DB_DETERMINISM_COMMON_ARGS} ${DB_DETERMINISM_HASH} ${DB_DETERMINISM_HASH_REPORT} ${DB_DETERMINISM_FRAME_LIMIT}"
    "state_hash_aggregate=0x74c03956a8e5feca,bo_hash_aggregate=0xbd0b012b6fc09934"
  )
  db_add_determinism_test(
    determinism_cpu_renderer_snake_shapes
    "--api cpu --display offscreen --benchmark-mode snake_shapes ${DB_DETERMINISM_COMMON_ARGS} ${DB_DETERMINISM_HASH} ${DB_DETERMINISM_HASH_REPORT} ${DB_DETERMINISM_FRAME_LIMIT}"
    "state_hash_aggregate=0xa4179206a93eea79,bo_hash_aggregate=0x3348240f3dc329c3"
  )
  db_add_determinism_test(
    determinism_cpu_renderer_gradient_sweep
    "--api cpu --display offscreen --benchmark-mode gradient_sweep ${DB_DETERMINISM_COMMON_ARGS} ${DB_DETERMINISM_HASH} ${DB_DETERMINISM_HASH_REPORT} ${DB_DETERMINISM_FRAME_LIMIT}"
    "state_hash_aggregate=0x3ba477bddd8f7c72,bo_hash_aggregate=0xbd0b012b6fc09934"
  )
  db_add_determinism_test(
    determinism_cpu_renderer_bands
    "--api cpu --display offscreen --benchmark-mode bands ${DB_DETERMINISM_COMMON_ARGS} ${DB_DETERMINISM_HASH} ${DB_DETERMINISM_HASH_REPORT} ${DB_DETERMINISM_FRAME_LIMIT}"
    "state_hash_aggregate=0xf775acec086459cd,bo_hash_aggregate=0xcd3155ad5295b2c4"
  )

  db_add_hash_equivalence_test(
    determinism_cpu_gradient_fill_speed_equivalence
    "--api cpu --display offscreen --benchmark-mode gradient_fill ${DB_DETERMINISM_COMMON_ARGS} --hash pixel --hash-report both --bench-speed 1 --frame-limit 600"
    "--api cpu --display offscreen --benchmark-mode gradient_fill ${DB_DETERMINISM_COMMON_ARGS} --hash pixel --hash-report both --bench-speed 2 --frame-limit 300"
    "bo_hash_final=0xf06657d3d2fafd93"
  )

  if(DB_ENABLE_GLFW_OFFSCREEN_TESTS)
    db_add_determinism_test(
      determinism_glfw_gl1_5_offscreen
      "--api opengl --renderer gl1_5_gles1_1 --display glfw_window --benchmark-mode snake_grid ${DB_DETERMINISM_COMMON_ARGS} ${DB_DETERMINISM_HASH} ${DB_DETERMINISM_HASH_REPORT} ${DB_DETERMINISM_GLFW_FRAMES}"
      "state_hash_final,framebuffer_hash_final"
    )
    db_add_determinism_test(
      determinism_glfw_gl3_3_offscreen
      "--api opengl --renderer gl3_3 --display glfw_window --benchmark-mode snake_grid ${DB_DETERMINISM_COMMON_ARGS} ${DB_DETERMINISM_HASH} ${DB_DETERMINISM_HASH_REPORT} ${DB_DETERMINISM_GLFW_FRAMES}"
      "state_hash_final,framebuffer_hash_final"
    )
  endif()
endif()
