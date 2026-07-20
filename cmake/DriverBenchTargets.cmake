set(DB_GL_RENDERER_SOURCES
    src/renderers/opengl_gl1_5_gles1_1/gl1_renderer.c
    src/renderers/opengl_gl1_5_gles1_1/gl1_frame.c
    src/renderers/opengl_gl1_5_gles1_1/gl1_backing.c
    src/renderers/opengl_gl1_5_gles1_1/gl1_native.c
    src/renderers/opengl_gl1_5_gles1_1/gl1_replay.c
    src/renderers/opengl_gl1_5_gles1_1/gl1_runtime.c
    src/renderers/opengl_gl3_3/gl3_renderer.c
    src/renderers/opengl_gl3_3/gl3_exact_lookup.c
    src/renderers/opengl_gl3_3/gl3_execute.c
    src/renderers/opengl_gl3_3/gl3_target.c
    src/renderers/opengl_gl3_3/gl3_qualification.c
    src/renderers/gl_buffer.c
    src/renderers/gl_common.c
    src/renderers/gl_gradient_qualification.c
    src/renderers/gl_probe.c
    src/renderers/gl_proc.c
    src/renderers/gl_state.c
    src/renderers/gl_shadow_present.c
    src/renderers/gl_shadow_present_upload.c
    src/renderers/gl_shadow_present_frame.c
    src/renderers/gl_shadow_present_draw.c
    src/renderers/gl_shadow_present_repair.c
    src/renderers/gl_shadow_present_hdr.c
    src/renderers/gl_upload.c
    src/renderers/gl_upload_stream.c
    src/renderers/gl_upload_probe.c
    src/renderers/gl_wrappers.c
    src/renderers/gl_program_wrappers.c)

set(DB_RENDER_CORE_SOURCES
    src/renderers/damage_trace.c src/renderers/damage_trace_region.c
    src/renderers/gl_runtime.c src/renderers/gl_runtime_capabilities.c
    src/renderers/gl_trace.c)

set(DB_CLI_CORE_SOURCES src/cli/cli_parse.c src/cli/cli_runtime_options.c
                        src/cli/cli_validation.c)

set(DB_APP_SOURCES
    src/driverbench_cli.c
    src/displays/display_dispatch.c
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

function(db_add_contract_library target)
    add_library(${target} STATIC ${ARGN})
    db_apply_perf_options(${target})
    target_compile_definitions(${target} PRIVATE ${DB_DRIVERBENCH_DEFS})
    target_include_directories(
        ${target}
        PUBLIC ${CMAKE_SOURCE_DIR}/src
        PRIVATE ${DB_GENERATED_INCLUDE_DIR})
endfunction()

function(db_finalize_driverbench_target)
    set(DB_GENERATED_INCLUDE_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
    set(DB_EMBEDDED_SHADERS_HEADER
        ${DB_GENERATED_INCLUDE_DIR}/db_embedded_shaders.h)
    set(DB_EMBEDDED_SHADER_ARGS -DOUT_HEADER=${DB_EMBEDDED_SHADERS_HEADER})
    set(DB_EMBEDDED_SHADER_DEPS
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenerateEmbeddedShadersHeader.cmake)
    set(DB_GL3_SHADER_MANIFEST
        "IR_EXECUTE_VERT|ir_execute|vert"
        "IR_EXECUTE_FRAG|ir_execute|frag"
        "EXACT_LOOKUP_VERT|exact_lookup|vert"
        "EXACT_LOOKUP_RGBA8_FRAG|exact_lookup_rgba8|frag"
        "EXACT_LOOKUP_RGBA16F_FRAG|exact_lookup_rgba16f|frag"
        "PRESENTATION_VERT|presentation|vert"
        "PRESENTATION_FRAG|presentation|frag")
    set(DB_GL3_SHADER_KEYS "")
    set(DB_GL3_SHADER_ROLE_STAGES "")
    foreach(DB_GL3_SHADER_ENTRY IN LISTS DB_GL3_SHADER_MANIFEST)
        string(REPLACE "|" ";" DB_GL3_SHADER_FIELDS "${DB_GL3_SHADER_ENTRY}")
        list(LENGTH DB_GL3_SHADER_FIELDS DB_GL3_SHADER_FIELD_COUNT)
        if(NOT DB_GL3_SHADER_FIELD_COUNT EQUAL 3)
            message(
                FATAL_ERROR
                    "Invalid GL3 shader manifest entry: ${DB_GL3_SHADER_ENTRY}")
        endif()
        list(GET DB_GL3_SHADER_FIELDS 0 DB_GL3_SHADER_KEY)
        list(GET DB_GL3_SHADER_FIELDS 1 DB_GL3_SHADER_ROLE)
        list(GET DB_GL3_SHADER_FIELDS 2 DB_GL3_SHADER_STAGE)
        set(DB_GL3_SHADER_ROLE_STAGE
            "${DB_GL3_SHADER_ROLE}|${DB_GL3_SHADER_STAGE}")
        if(DB_GL3_SHADER_KEY IN_LIST DB_GL3_SHADER_KEYS
           OR DB_GL3_SHADER_ROLE_STAGE IN_LIST DB_GL3_SHADER_ROLE_STAGES)
            message(
                FATAL_ERROR
                    "Duplicate GL3 shader manifest entry: ${DB_GL3_SHADER_ENTRY}"
            )
        endif()
        list(APPEND DB_GL3_SHADER_KEYS "${DB_GL3_SHADER_KEY}")
        list(APPEND DB_GL3_SHADER_ROLE_STAGES "${DB_GL3_SHADER_ROLE_STAGE}")
        set(DB_GL3_SHADER_SOURCE
            ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/gl3/${DB_GL3_SHADER_ROLE}.${DB_GL3_SHADER_STAGE}
        )
        if(NOT EXISTS "${DB_GL3_SHADER_SOURCE}")
            message(
                FATAL_ERROR
                    "GL3 shader source is missing: ${DB_GL3_SHADER_SOURCE}")
        endif()
        list(APPEND DB_EMBEDDED_SHADER_ARGS
             -DGL3_${DB_GL3_SHADER_KEY}=${DB_GL3_SHADER_SOURCE})
        list(APPEND DB_EMBEDDED_SHADER_DEPS ${DB_GL3_SHADER_SOURCE})
    endforeach()
    if(DEFINED DB_VK_IR_EXECUTE_VERT_SPV AND DEFINED DB_VK_IR_EXECUTE_FRAG_SPV)
        list(
            APPEND
            DB_EMBEDDED_SHADER_ARGS
            -DVK_IR_EXECUTE_VERT_SPV=${DB_VK_IR_EXECUTE_VERT_SPV}
            -DVK_IR_EXECUTE_FRAG_SPV=${DB_VK_IR_EXECUTE_FRAG_SPV}
            -DVK_PRESENTATION_VERT_SPV=${DB_VK_PRESENTATION_VERT_SPV}
            -DVK_PRESENTATION_FRAG_SPV=${DB_VK_PRESENTATION_FRAG_SPV})
        list(APPEND DB_EMBEDDED_SHADER_ARGS
             -DVK_TRANSPORT_PACK_COMP_SPV=${DB_VK_TRANSPORT_PACK_COMP_SPV}
             -DVK_TRANSPORT_UNPACK_FRAG_SPV=${DB_VK_TRANSPORT_UNPACK_FRAG_SPV})
        list(APPEND DB_EMBEDDED_SHADER_DEPS ${DB_VK_IR_EXECUTE_VERT_SPV}
             ${DB_VK_IR_EXECUTE_FRAG_SPV} ${DB_VK_PRESENTATION_VERT_SPV}
             ${DB_VK_PRESENTATION_FRAG_SPV})
        list(APPEND DB_EMBEDDED_SHADER_DEPS ${DB_VK_TRANSPORT_PACK_COMP_SPV}
             ${DB_VK_TRANSPORT_UNPACK_FRAG_SPV})
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

    db_add_contract_library(db_foundation ${DB_FOUNDATION_SOURCES})
    target_link_libraries(db_foundation PRIVATE ${DB_DRIVERBENCH_LIBS})

    db_add_contract_library(db_render_ir ${DB_RENDER_IR_SOURCES})
    target_link_libraries(db_render_ir PUBLIC db_foundation)

    db_add_contract_library(db_qualification_contracts
                            ${DB_QUALIFICATION_CONTRACT_SOURCES})
    target_link_libraries(db_qualification_contracts PUBLIC db_foundation)

    db_add_contract_library(db_frame_contracts ${DB_FRAME_CONTRACT_SOURCES})
    target_link_libraries(db_frame_contracts PUBLIC db_foundation db_render_ir
                                                    db_qualification_contracts)

    db_add_contract_library(db_benchmark_model ${DB_BENCHMARK_MODEL_SOURCES})
    target_link_libraries(db_benchmark_model PUBLIC db_foundation db_render_ir)

    db_add_contract_library(db_platform_process ${DB_PLATFORM_PROCESS_SOURCES})
    target_link_libraries(db_platform_process PUBLIC db_foundation)

    db_add_contract_library(db_qualification_service
                            ${DB_QUALIFICATION_SERVICE_SOURCES})
    target_link_libraries(
        db_qualification_service PUBLIC db_foundation db_platform_process
                                        db_qualification_contracts db_render_ir)

    db_add_contract_library(db_frame_coordinator
                            ${DB_FRAME_COORDINATOR_SOURCES})
    target_link_libraries(
        db_frame_coordinator PUBLIC db_foundation db_frame_contracts
                                    db_benchmark_model)

    db_add_contract_library(db_run_session ${DB_RUN_SESSION_SOURCES})
    target_link_libraries(
        db_run_session
        PUBLIC db_foundation db_frame_coordinator db_frame_contracts
               db_benchmark_model db_qualification_service)

    add_library(driverbench_core INTERFACE)
    target_link_libraries(
        driverbench_core
        INTERFACE db_run_session
                  db_frame_coordinator
                  db_qualification_service
                  db_frame_contracts
                  db_benchmark_model
                  db_render_ir
                  db_qualification_contracts
                  db_platform_process
                  db_foundation)
    target_include_directories(driverbench_core
                               INTERFACE ${CMAKE_SOURCE_DIR}/src)

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

    add_executable(driverbench_probe_helper src/probe_helper_main.c)
    db_apply_perf_options(driverbench_probe_helper)
    target_link_libraries(driverbench_probe_helper
                          PRIVATE driverbench_core ${DB_DRIVERBENCH_LIBS})
    target_include_directories(driverbench_probe_helper
                               PRIVATE ${CMAKE_SOURCE_DIR}/src)

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
