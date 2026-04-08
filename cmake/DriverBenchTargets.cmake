set(DB_GL_RENDERER_SOURCES
  src/renderers/opengl_gl1_5_gles1_1/renderer_opengl_gl1_5_gles1_1.c
  src/renderers/opengl_gl1_5_gles1_1/renderer_opengl_gl1_5_gles1_1_damage.c
  src/renderers/opengl_gl1_5_gles1_1/renderer_opengl_gl1_5_gles1_1_draw.c
  src/renderers/opengl_gl1_5_gles1_1/renderer_opengl_gl1_5_gles1_1_frame.c
  src/renderers/opengl_gl1_5_gles1_1/renderer_opengl_gl1_5_gles1_1_snake.c
  src/renderers/opengl_gl1_5_gles1_1/renderer_opengl_gl1_5_gles1_1_util.c
  src/renderers/opengl_gl3_3/renderer_opengl_gl3_3.c
  src/renderers/renderer_gl_buffer.c
  src/renderers/renderer_gl_common.c
  src/renderers/renderer_gl_probe.c
  src/renderers/renderer_gl_proc.c
  src/renderers/renderer_gl_runtime.c
  src/renderers/renderer_gl_state.c
  src/renderers/renderer_gl_shadow_present.c
  src/renderers/renderer_gl_upload.c
  src/renderers/renderer_gl_upload_probe.c
  src/renderers/renderer_gl_wrappers.c
)

set(DB_DRIVERBENCH_SOURCES
  src/driverbench_cli.c
  src/driverbench_main.c
  src/displays/display_dispatch.c
  src/displays/display_gl_runtime_common.c
  src/displays/offscreen/display_offscreen.c
  src/renderers/cpu_renderer/renderer_cpu_renderer.c
  ${DB_CORE_SOURCES}
  ${DB_GL_RENDERER_SOURCES}
)
set(DB_DRIVERBENCH_LIBS m)
set(DB_DRIVERBENCH_DEFS "")

function(db_set_source_include_directories include_dirs)
  if("${include_dirs}" STREQUAL "" OR ARGC LESS 2)
    return()
  endif()
  set(source_files ${ARGN})
  set_source_files_properties(${source_files} PROPERTIES
    INCLUDE_DIRECTORIES "${include_dirs}")
endfunction()

function(db_set_source_compile_options compile_options)
  if("${compile_options}" STREQUAL "" OR ARGC LESS 2)
    return()
  endif()
  set(source_files ${ARGN})
  set_source_files_properties(${source_files} PROPERTIES
    COMPILE_OPTIONS "${compile_options}")
endfunction()

function(db_finalize_driverbench_target)
  set(DB_GENERATED_INCLUDE_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
  set(DB_EMBEDDED_SHADERS_HEADER
    ${DB_GENERATED_INCLUDE_DIR}/db_embedded_shaders.h
  )
  set(DB_EMBEDDED_SHADER_ARGS
    -DOUT_HEADER=${DB_EMBEDDED_SHADERS_HEADER}
    -DGL3_VERT=${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/shader_opengl_gl3_3_rect.vert
    -DGL3_FRAG=${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/shader_opengl_gl3_3_rect.frag
  )
  set(DB_EMBEDDED_SHADER_DEPS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/shader_opengl_gl3_3_rect.vert
    ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/shader_opengl_gl3_3_rect.frag
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenerateEmbeddedShadersHeader.cmake
  )
  if(DEFINED DB_VK_VERT_SPV AND DEFINED DB_VK_FRAG_SPV)
    list(APPEND DB_EMBEDDED_SHADER_ARGS
      -DVK_VERT_SPV=${DB_VK_VERT_SPV}
      -DVK_FRAG_SPV=${DB_VK_FRAG_SPV}
    )
    list(APPEND DB_EMBEDDED_SHADER_DEPS
      ${DB_VK_VERT_SPV}
      ${DB_VK_FRAG_SPV}
    )
  endif()

  add_custom_command(
    OUTPUT ${DB_EMBEDDED_SHADERS_HEADER}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${DB_GENERATED_INCLUDE_DIR}
    COMMAND ${CMAKE_COMMAND} ${DB_EMBEDDED_SHADER_ARGS}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenerateEmbeddedShadersHeader.cmake
    DEPENDS ${DB_EMBEDDED_SHADER_DEPS}
    VERBATIM
  )
  add_custom_target(driverbench_embedded_shaders ALL
    DEPENDS ${DB_EMBEDDED_SHADERS_HEADER}
  )

  add_executable(${DB_UNIFIED_TARGET}
    ${DB_DRIVERBENCH_SOURCES}
  )
  db_apply_perf_options(${DB_UNIFIED_TARGET})
  target_compile_definitions(${DB_UNIFIED_TARGET} PRIVATE ${DB_DRIVERBENCH_DEFS})
  target_link_libraries(${DB_UNIFIED_TARGET} PRIVATE ${DB_DRIVERBENCH_LIBS})
  target_include_directories(${DB_UNIFIED_TARGET} PRIVATE ${DB_GENERATED_INCLUDE_DIR})
  if(TARGET driverbench_vulkan_shaders)
    add_dependencies(${DB_UNIFIED_TARGET} driverbench_vulkan_shaders)
  endif()
  if(TARGET driverbench_embedded_shaders)
    add_dependencies(${DB_UNIFIED_TARGET} driverbench_embedded_shaders)
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
endfunction()
