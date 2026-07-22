if(NOT DEFINED SOURCE_ROOT OR "${SOURCE_ROOT}" STREQUAL "")
    message(FATAL_ERROR "SOURCE_ROOT is required")
endif()

file(GLOB_RECURSE DB_DISPLAY_FILES "${SOURCE_ROOT}/src/displays/*.c"
     "${SOURCE_ROOT}/src/displays/*.h")
file(GLOB_RECURSE DB_RENDERER_FILES "${SOURCE_ROOT}/src/renderers/*.c"
     "${SOURCE_ROOT}/src/renderers/*.h")
set(DB_FRAME_BOUNDARY_FILES ${DB_DISPLAY_FILES} ${DB_RENDERER_FILES})
foreach(DB_FRAME_BOUNDARY_FILE IN LISTS DB_FRAME_BOUNDARY_FILES)
    file(READ "${DB_FRAME_BOUNDARY_FILE}" DB_FRAME_BOUNDARY_SOURCE)
    if(DB_FRAME_BOUNDARY_SOURCE MATCHES
       "db_benchmark_(model|core)_(generate|commit|apply)")
        message(
            FATAL_ERROR
                "Display/renderer owns benchmark generation or commit: ${DB_FRAME_BOUNDARY_FILE}"
        )
    endif()
    if(DB_FRAME_BOUNDARY_SOURCE MATCHES "db_frame_source")
        message(
            FATAL_ERROR
                "Removed frame-source API remains reachable: ${DB_FRAME_BOUNDARY_FILE}"
        )
    endif()
endforeach()

foreach(DB_DISPLAY_FILE IN LISTS DB_DISPLAY_FILES)
    file(READ "${DB_DISPLAY_FILE}" DB_DISPLAY_SOURCE)
    if(DB_DISPLAY_SOURCE
       MATCHES
       "db_benchmark_model_(init|shutdown)|db_frame_coordinator_(init|run_frame|step)"
    )
        message(
            FATAL_ERROR
                "Display owns benchmark/coordinator lifecycle: ${DB_DISPLAY_FILE}"
        )
    endif()
    if(DB_DISPLAY_SOURCE MATCHES
       "\\.(target_strategy|gradient_path)[ \t\r\n]*=")
        message(
            FATAL_ERROR
                "Display owns renderer strategy resolution: ${DB_DISPLAY_FILE}")
    endif()
    if(DB_DISPLAY_SOURCE MATCHES
       "\\.(rebuild_required|rebuild_reason|force_rebuild)[ \t\r\n]*=")
        message(
            FATAL_ERROR
                "Display owns logical rebuild policy: ${DB_DISPLAY_FILE}")
    endif()
endforeach()

foreach(DB_RENDERER_FILE IN LISTS DB_RENDERER_FILES)
    file(READ "${DB_RENDERER_FILE}" DB_RENDERER_SOURCE)
    if(DB_RENDERER_SOURCE
       MATCHES
       "db_(conformance_qualify|qualification_service|conformance_cache_(read|write)|probe_process_run)"
    )
        message(
            FATAL_ERROR
                "Renderer owns qualification cache/helper/service policy: ${DB_RENDERER_FILE}"
        )
    endif()
    if(DB_RENDERER_SOURCE MATCHES "db_run_session")
        message(
            FATAL_ERROR
                "Renderer depends on run-session implementation: ${DB_RENDERER_FILE}"
        )
    endif()
    if(DB_RENDERER_SOURCE MATCHES "db_f64_sample_ring_t")
        message(
            FATAL_ERROR
                "Renderer owns forbidden long-run metric history: ${DB_RENDERER_FILE}"
        )
    endif()
    if(DB_RENDERER_SOURCE MATCHES "db_render_ir_(clone_replayable|snapshot)"
       AND NOT DB_RENDERER_FILE MATCHES
           "/opengl_gl1_5_gles1_1/gl1_(internal\\.h|replay\\.c)$")
        message(
            FATAL_ERROR
                "Renderer retains historical frame IR outside GL1 replay: ${DB_RENDERER_FILE}"
        )
    endif()
endforeach()
