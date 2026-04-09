if(NOT DEFINED SOURCE_ROOT OR "${SOURCE_ROOT}" STREQUAL "")
  message(FATAL_ERROR "SOURCE_ROOT is required")
endif()

if(NOT DEFINED RULE_SET OR "${RULE_SET}" STREQUAL "")
  message(FATAL_ERROR "RULE_SET is required")
endif()

function(db_pp_collect_files OUT_VAR)
  set(DB_PP_FILES "")
  foreach(DB_PP_GLOB IN LISTS ARGN)
    file(GLOB_RECURSE DB_PP_GLOB_MATCHES ${DB_PP_GLOB})
    list(APPEND DB_PP_FILES ${DB_PP_GLOB_MATCHES})
  endforeach()
  list(REMOVE_DUPLICATES DB_PP_FILES)
  set(${OUT_VAR} "${DB_PP_FILES}" PARENT_SCOPE)
endfunction()

function(db_pp_append_failure FILE MESSAGE_TEXT)
  set(DB_FAILURES "${DB_FAILURES}${FILE}: ${MESSAGE_TEXT}\n" PARENT_SCOPE)
endfunction()

function(db_pp_check_forbidden_outside_allowlist FILES_VAR ALLOWED_VAR
         FORBIDDEN_REGEX MESSAGE_TEXT)
  foreach(DB_PP_FILE IN LISTS ${FILES_VAR})
    list(FIND ${ALLOWED_VAR} "${DB_PP_FILE}" DB_PP_ALLOWED_INDEX)
    if(NOT DB_PP_ALLOWED_INDEX EQUAL -1)
      continue()
    endif()
    file(READ "${DB_PP_FILE}" DB_PP_FILE_CONTENT)
    if(DB_PP_FILE_CONTENT MATCHES "${FORBIDDEN_REGEX}")
      db_pp_append_failure("${DB_PP_FILE}" "${MESSAGE_TEXT}")
    endif()
  endforeach()
  set(DB_FAILURES "${DB_FAILURES}" PARENT_SCOPE)
endfunction()

function(db_pp_check_forbidden_in_files FILES_VAR FORBIDDEN_REGEX MESSAGE_TEXT)
  foreach(DB_PP_FILE IN LISTS ${FILES_VAR})
    file(READ "${DB_PP_FILE}" DB_PP_FILE_CONTENT)
    if(DB_PP_FILE_CONTENT MATCHES "${FORBIDDEN_REGEX}")
      db_pp_append_failure("${DB_PP_FILE}" "${MESSAGE_TEXT}")
    endif()
  endforeach()
  set(DB_FAILURES "${DB_FAILURES}" PARENT_SCOPE)
endfunction()

set(DB_FAILURES "")

if(RULE_SET STREQUAL "display_glfw_policy")
  set(DB_PP_SCAN_FILES
    "${SOURCE_ROOT}/src/displays/display_gl_runtime_common.c"
    "${SOURCE_ROOT}/src/displays/display_gl_runtime_common.h"
    "${SOURCE_ROOT}/src/displays/glfw_window/display_glfw_window.c"
    "${SOURCE_ROOT}/src/displays/glfw_window/display_glfw_window_common.c"
    "${SOURCE_ROOT}/src/displays/glfw_window/display_glfw_window_common.h")

  set(DB_PP_ALLOWED_FILES
    "${SOURCE_ROOT}/src/displays/display_gl_runtime_common.c"
    "${SOURCE_ROOT}/src/displays/display_gl_runtime_common.h"
    "${SOURCE_ROOT}/src/displays/glfw_window/display_glfw_window_common.c"
    "${SOURCE_ROOT}/src/displays/glfw_window/display_glfw_window_common.h")

  db_pp_check_forbidden_outside_allowlist(
    DB_PP_SCAN_FILES DB_PP_ALLOWED_FILES
    "#ifdef __linux__|#if defined\\(__linux__\\)|#ifdef __APPLE__|#if defined\\(__APPLE__\\)"
    "platform-specific GLFW/OpenGL policy branches must live only in approved display policy/helper files")

  set(DB_PP_WORDING_FILES
    "${SOURCE_ROOT}/src/displays/display_gl_runtime_common.c"
    "${SOURCE_ROOT}/src/displays/display_gl_runtime_common.h")
  db_pp_check_forbidden_in_files(
    DB_PP_WORDING_FILES
    "Linux GLFW|Apple GLFW|macOS GLFW"
    "shared OpenGL display policy must not contain platform-specific GLFW reason text")

elseif(RULE_SET STREQUAL "renderer_gl_upload_policy")
  db_pp_collect_files(DB_PP_SCAN_FILES
    "${SOURCE_ROOT}/src/renderers/*.c"
    "${SOURCE_ROOT}/src/renderers/*.h"
    "${SOURCE_ROOT}/src/renderers/*/*.c"
    "${SOURCE_ROOT}/src/renderers/*/*.h")

  set(DB_PP_ALLOWED_FILES
    "${SOURCE_ROOT}/src/renderers/renderer_gl_runtime.c"
    "${SOURCE_ROOT}/src/renderers/renderer_gl_proc.c"
    "${SOURCE_ROOT}/src/renderers/renderer_gl_proc_runtime_internal.h")

  db_pp_check_forbidden_outside_allowlist(
    DB_PP_SCAN_FILES DB_PP_ALLOWED_FILES
    "#ifdef __APPLE__|#if defined\\(__APPLE__\\)"
    "Apple-specific OpenGL/buffer policy must live only in the approved GL resolver/proc files")

  db_pp_check_forbidden_outside_allowlist(
    DB_PP_SCAN_FILES DB_PP_ALLOWED_FILES
    "Apple GLFW|macOS GLFW|Apple M[0-9]|MacOSX|macOS"
    "Apple-specific OpenGL/buffer policy text must live only in the approved GL resolver/proc files")

elseif(RULE_SET STREQUAL "platform_proc_loading")
  db_pp_collect_files(DB_PP_SCAN_FILES
    "${SOURCE_ROOT}/src/renderers/*.c"
    "${SOURCE_ROOT}/src/renderers/*.h"
    "${SOURCE_ROOT}/src/renderers/*/*.c"
    "${SOURCE_ROOT}/src/renderers/*/*.h")

  set(DB_PP_ALLOWED_FILES
    "${SOURCE_ROOT}/src/renderers/renderer_gl_proc.c"
    "${SOURCE_ROOT}/src/renderers/renderer_gl_proc_runtime_internal.h")

  db_pp_check_forbidden_outside_allowlist(
    DB_PP_SCAN_FILES DB_PP_ALLOWED_FILES
    "<dlfcn\\.h>|dlsym\\(|#if defined\\(__APPLE__\\) \\|\\| defined\\(__linux__\\)|#if defined\\(__linux__\\) \\|\\| defined\\(__APPLE__\\)"
    "platform-specific GL proc-loading logic must live only in the approved proc-runtime files")

elseif(RULE_SET STREQUAL "cmake_platform_policy")
  set(DB_PP_SCAN_FILES "${SOURCE_ROOT}/CMakeLists.txt")
  file(GLOB DB_PP_CMAKE_FILES "${SOURCE_ROOT}/cmake/*.cmake")
  list(APPEND DB_PP_SCAN_FILES ${DB_PP_CMAKE_FILES})

  set(DB_PP_ALLOWED_FILES
    "${SOURCE_ROOT}/cmake/DriverBenchPlatform.cmake"
    "${SOURCE_ROOT}/cmake/DriverBenchLinuxToolchain.cmake"
    "${SOURCE_ROOT}/cmake/DriverBenchTestPlatform.cmake"
    "${SOURCE_ROOT}/cmake/CheckPlatformPolicyBoundaries.cmake")

  db_pp_check_forbidden_outside_allowlist(
    DB_PP_SCAN_FILES DB_PP_ALLOWED_FILES
    "(^|[^A-Za-z0-9_])(APPLE|CMAKE_SYSTEM_NAME)([^A-Za-z0-9_]|$)"
    "direct platform branching must live only in approved platform/test-policy CMake modules")

elseif(RULE_SET STREQUAL "logging_policy")
  set(DB_PP_PROGRESS_FILES
    "${SOURCE_ROOT}/src/core/db_core.c")
  db_pp_check_forbidden_in_files(
    DB_PP_PROGRESS_FILES
    "benchmark \\(%s\\): mode=%s"
    "periodic benchmark progress logs must not include static mode text")

  set(DB_PP_PRESENT_LOG_FILES
    "${SOURCE_ROOT}/src/renderers/renderer_gl_shadow_present.c")
  db_pp_check_forbidden_in_files(
    DB_PP_PRESENT_LOG_FILES
    "effective_full_present_upload=%s, partial_present_upload=%s"
    "shadow present decision logs must not append duplicated effective-mode text")

  db_pp_collect_files(DB_PP_LOG_SCAN_FILES
    "${SOURCE_ROOT}/src/displays/*.c"
    "${SOURCE_ROOT}/src/displays/*.h"
    "${SOURCE_ROOT}/src/displays/*/*.c"
    "${SOURCE_ROOT}/src/displays/*/*.h"
    "${SOURCE_ROOT}/src/renderers/*.c"
    "${SOURCE_ROOT}/src/renderers/*.h"
    "${SOURCE_ROOT}/src/renderers/*/*.c"
    "${SOURCE_ROOT}/src/renderers/*/*.h")

  set(DB_PP_ALLOWED_FINAL_SUMMARY_FILES
    "${SOURCE_ROOT}/src/core/db_core.c"
    "${SOURCE_ROOT}/src/core/db_core.h"
    "${SOURCE_ROOT}/src/displays/display_runtime_config_common.h")
  db_pp_check_forbidden_outside_allowlist(
    DB_PP_LOG_SCAN_FILES DB_PP_ALLOWED_FINAL_SUMMARY_FILES
    "db_benchmark_log_final\\("
    "final benchmark summaries must be emitted only by the shared benchmark/final-summary helpers")

  set(DB_PP_ALLOWED_DRAW_STATS_FILES
    "${SOURCE_ROOT}/src/displays/display_runtime_config_common.h")
  db_pp_check_forbidden_outside_allowlist(
    DB_PP_LOG_SCAN_FILES DB_PP_ALLOWED_DRAW_STATS_FILES
    "db_display_log_draw_stats_with_fn\\("
    "draw stats must be emitted only by the shared final-summary helper")

  set(DB_PP_ALLOWED_SCHEDULER_STATS_FILES
    "${SOURCE_ROOT}/src/renderers/vulkan_1_2_multi_gpu/renderer_vulkan_1_2_multi_gpu_runtime.c")
  db_pp_check_forbidden_outside_allowlist(
    DB_PP_LOG_SCAN_FILES DB_PP_ALLOWED_SCHEDULER_STATS_FILES
    "scheduler stats:"
    "scheduler stats logs must be emitted only by the Vulkan final runtime summary")

  set(DB_PP_SNAKE_RECOVERY_FILES
    "${SOURCE_ROOT}/src/renderers/opengl_gl1_5_gles1_1/renderer_opengl_gl1_5_gles1_1_frame.c")
  db_pp_check_forbidden_in_files(
    DB_PP_SNAKE_RECOVERY_FILES
    "Grid fast-path on invalid backbuffer"
    "snake-grid invalid backbuffer recovery must not use the old renderer-local clear-and-redraw workaround")

  set(DB_PP_GL1_HISTORY_RECOVERY_FILES
    "${SOURCE_ROOT}/src/renderers/opengl_gl1_5_gles1_1/renderer_opengl_gl1_5_gles1_1_frame.c")
  db_pp_check_forbidden_in_files(
    DB_PP_GL1_HISTORY_RECOVERY_FILES
    "db_history_should_seed_full_on_invalid\\("
    "GL1 history-backed recovery must consume the shared recovery action instead of local invalid-backbuffer seed logic")

else()
  message(FATAL_ERROR "Unknown RULE_SET: ${RULE_SET}")
endif()

if(NOT DB_FAILURES STREQUAL "")
  message(FATAL_ERROR "${DB_FAILURES}")
endif()
