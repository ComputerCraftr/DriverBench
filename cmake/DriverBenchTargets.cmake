set(DB_GL_RENDERER_SOURCES
    src/renderers/opengl_gl1_5_gles1_1/gl1_renderer.c
    src/renderers/opengl_gl1_5_gles1_1/gl1_frame.c
    src/renderers/opengl_gl1_5_gles1_1/gl1_backing.c
    src/renderers/opengl_gl1_5_gles1_1/gl1_runtime.c
    src/renderers/opengl_gl3_3/gl3_renderer.c
    src/renderers/gl_buffer.c
    src/renderers/gl_common.c
    src/renderers/gl_probe.c
    src/renderers/gl_proc.c
    src/renderers/gl_state.c
    src/renderers/gl_shadow_present.c
    src/renderers/gl_shadow_present_upload.c
    src/renderers/gl_shadow_present_frame.c
    src/renderers/gl_shadow_present_hdr.c
    src/renderers/gl_upload.c
    src/renderers/gl_upload_stream.c
    src/renderers/gl_upload_probe.c
    src/renderers/gl_wrappers.c
    src/renderers/gl_program_wrappers.c)

set(DB_RENDER_CORE_SOURCES
    src/renderers/damage_trace.c src/renderers/gl_runtime.c
    src/renderers/gl_runtime_capabilities.c src/renderers/gl_trace.c)

set(DB_CLI_CORE_SOURCES src/cli/cli_parse.c src/cli/cli_validation.c)

set(DB_APP_SOURCES
    src/driverbench_cli.c
    src/displays/gl_display_runtime.c
    src/displays/display_presentation_policy.c
    src/displays/offscreen/offscreen_display.c
    src/renderers/cpu_renderer/cpu_renderer.c
    ${DB_GL_RENDERER_SOURCES})
set(DB_DRIVERBENCH_LIBS m)
set(DB_DRIVERBENCH_DEFS "")

function(db_set_source_include_directories include_dirs)
    if("${include_dirs}" STREQUAL "" OR ARGC LESS 2)
        return()
    endif()
    set(source_files ${ARGN})
    set_source_files_properties(${source_files} PROPERTIES INCLUDE_DIRECTORIES
                                                           "${include_dirs}")
endfunction()

function(db_set_source_compile_options compile_options)
    if("${compile_options}" STREQUAL "" OR ARGC LESS 2)
        return()
    endif()
    set(source_files ${ARGN})
    set_source_files_properties(${source_files} PROPERTIES COMPILE_OPTIONS
                                                           "${compile_options}")
endfunction()

function(db_finalize_driverbench_target)
    set(DB_GENERATED_INCLUDE_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
    set(DB_EMBEDDED_SHADERS_HEADER
        ${DB_GENERATED_INCLUDE_DIR}/db_embedded_shaders.h)
    set(DB_EMBEDDED_SHADER_ARGS
        -DOUT_HEADER=${DB_EMBEDDED_SHADERS_HEADER}
        -DGL3_VERT=${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/gl3_rect.vert
        -DGL3_FRAG=${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/gl3_rect.frag
        -DGL3_PRESENT_VERT=${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/gl3_present.vert
        -DGL3_PRESENT_FRAG=${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/gl3_present.frag
    )
    set(DB_EMBEDDED_SHADER_DEPS
        ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/gl3_rect.vert
        ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/gl3_rect.frag
        ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/gl3_present.vert
        ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/gl3_present.frag
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenerateEmbeddedShadersHeader.cmake)
    if(DEFINED DB_VK_VERT_SPV AND DEFINED DB_VK_FRAG_SPV)
        list(
            APPEND
            DB_EMBEDDED_SHADER_ARGS
            -DVK_VERT_SPV=${DB_VK_VERT_SPV}
            -DVK_FRAG_SPV=${DB_VK_FRAG_SPV}
            -DVK_PRESENT_VERT_SPV=${DB_VK_PRESENT_VERT_SPV}
            -DVK_PRESENT_FRAG_SPV=${DB_VK_PRESENT_FRAG_SPV})
        list(APPEND DB_EMBEDDED_SHADER_ARGS
             -DVK_PACK_COMP_SPV=${DB_VK_PACK_COMP_SPV}
             -DVK_UNPACK_FRAG_SPV=${DB_VK_UNPACK_FRAG_SPV})
        list(APPEND DB_EMBEDDED_SHADER_DEPS ${DB_VK_VERT_SPV} ${DB_VK_FRAG_SPV}
             ${DB_VK_PRESENT_VERT_SPV} ${DB_VK_PRESENT_FRAG_SPV})
        list(APPEND DB_EMBEDDED_SHADER_DEPS ${DB_VK_PACK_COMP_SPV}
             ${DB_VK_UNPACK_FRAG_SPV})
    endif()

    add_custom_command(
        OUTPUT ${DB_EMBEDDED_SHADERS_HEADER}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${DB_GENERATED_INCLUDE_DIR}
        COMMAND
            ${CMAKE_COMMAND} ${DB_EMBEDDED_SHADER_ARGS} -P
            ${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenerateEmbeddedShadersHeader.cmake
        DEPENDS ${DB_EMBEDDED_SHADER_DEPS}
        VERBATIM)
    add_custom_target(driverbench_embedded_shaders ALL
                      DEPENDS ${DB_EMBEDDED_SHADERS_HEADER})

    add_library(driverbench_core STATIC ${DB_CORE_SOURCES}
                                        src/displays/display_dispatch.c)
    db_apply_perf_options(driverbench_core)
    target_compile_definitions(driverbench_core PRIVATE ${DB_DRIVERBENCH_DEFS})
    target_link_libraries(driverbench_core PRIVATE ${DB_DRIVERBENCH_LIBS})
    target_include_directories(
        driverbench_core
        PUBLIC ${CMAKE_SOURCE_DIR}/src
        PRIVATE ${DB_GENERATED_INCLUDE_DIR})

    add_library(driverbench_render_core STATIC ${DB_RENDER_CORE_SOURCES})
    db_apply_perf_options(driverbench_render_core)
    target_compile_definitions(driverbench_render_core
                               PRIVATE ${DB_DRIVERBENCH_DEFS})
    target_link_libraries(driverbench_render_core
                          PRIVATE driverbench_core ${DB_DRIVERBENCH_LIBS})
    target_include_directories(
        driverbench_render_core
        PUBLIC ${CMAKE_SOURCE_DIR}/src
        PRIVATE ${DB_GENERATED_INCLUDE_DIR})

    add_library(driverbench_cli_core STATIC ${DB_CLI_CORE_SOURCES})
    db_apply_perf_options(driverbench_cli_core)
    target_compile_definitions(driverbench_cli_core
                               PRIVATE ${DB_DRIVERBENCH_DEFS})
    target_link_libraries(
        driverbench_cli_core PRIVATE driverbench_core driverbench_render_core
                                     ${DB_DRIVERBENCH_LIBS})
    target_include_directories(
        driverbench_cli_core
        PUBLIC ${CMAKE_SOURCE_DIR}/src
        PRIVATE ${DB_GENERATED_INCLUDE_DIR})

    add_library(driverbench_app STATIC ${DB_APP_SOURCES})
    db_apply_perf_options(driverbench_app)
    target_compile_definitions(driverbench_app PRIVATE ${DB_DRIVERBENCH_DEFS})
    target_link_libraries(
        driverbench_app PRIVATE driverbench_core driverbench_render_core
                                driverbench_cli_core ${DB_DRIVERBENCH_LIBS})
    target_include_directories(
        driverbench_app
        PUBLIC ${CMAKE_SOURCE_DIR}/src
        PRIVATE ${DB_GENERATED_INCLUDE_DIR})
    if(TARGET driverbench_vulkan_shaders)
        add_dependencies(driverbench_app driverbench_vulkan_shaders)
    endif()
    if(TARGET driverbench_embedded_shaders)
        add_dependencies(driverbench_app driverbench_embedded_shaders)
    endif()

    add_executable(${DB_UNIFIED_TARGET} src/driverbench_main.c)
    db_apply_perf_options(${DB_UNIFIED_TARGET})
    db_apply_platform_executable_postbuild_options(${DB_UNIFIED_TARGET})
    target_compile_definitions(${DB_UNIFIED_TARGET}
                               PRIVATE ${DB_DRIVERBENCH_DEFS})
    target_link_libraries(
        ${DB_UNIFIED_TARGET}
        PRIVATE driverbench_app driverbench_cli_core driverbench_render_core
                driverbench_core driverbench_app ${DB_DRIVERBENCH_LIBS})
    target_include_directories(
        ${DB_UNIFIED_TARGET} PRIVATE ${DB_GENERATED_INCLUDE_DIR}
                                     ${CMAKE_SOURCE_DIR}/src)
    if(TARGET driverbench_vulkan_shaders)
        add_dependencies(${DB_UNIFIED_TARGET} driverbench_vulkan_shaders)
    endif()
    if(TARGET driverbench_embedded_shaders)
        add_dependencies(${DB_UNIFIED_TARGET} driverbench_embedded_shaders)
    endif()

    if(CMAKE_EXPORT_COMPILE_COMMANDS)
        add_custom_target(
            db_sync_compile_commands ALL
            COMMAND
                ${CMAKE_COMMAND} -E copy_if_different
                ${CMAKE_BINARY_DIR}/compile_commands.json
                ${CMAKE_SOURCE_DIR}/compile_commands.json
            COMMENT "Sync compile_commands.json to source root for clangd"
            VERBATIM)
    endif()
endfunction()
