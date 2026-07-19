if(NOT DEFINED SOURCE_ROOT OR "${SOURCE_ROOT}" STREQUAL "")
    message(FATAL_ERROR "SOURCE_ROOT is required")
endif()

set(DB_FRAME_MIGRATION_MANIFEST
    "${SOURCE_ROOT}/config/frame_lifecycle_migration.json")
if(NOT EXISTS "${DB_FRAME_MIGRATION_MANIFEST}")
    message(FATAL_ERROR "Missing frame lifecycle migration manifest")
endif()

file(READ "${DB_FRAME_MIGRATION_MANIFEST}" DB_FRAME_MIGRATION_JSON)
string(JSON DB_FRAME_MIGRATION_SCHEMA GET "${DB_FRAME_MIGRATION_JSON}" schema)
if(NOT DB_FRAME_MIGRATION_SCHEMA EQUAL 2)
    message(FATAL_ERROR "Unsupported frame lifecycle migration schema")
endif()

set(DB_FRAME_MIGRATION_CATEGORIES
    production_frame_lifecycle
    display_generation_commit_decisions
    display_benchmark_coordinator_ownership
    display_renderer_strategy_decisions
    backend_qualification_policy
    backend_cache_helper_service_calls
    backend_qualification_retry_loops
    backend_topology_reducers
    renderer_benchmark_internal_dependencies
    display_backend_frame_retry_sessions
    renderer_long_run_metric_histories
    historical_frame_ir_owners
    temporary_migration_adapters)

foreach(DB_FRAME_CATEGORY IN LISTS DB_FRAME_MIGRATION_CATEGORIES)
    string(JSON DB_FRAME_SITE_COUNT LENGTH "${DB_FRAME_MIGRATION_JSON}"
                                           categories "${DB_FRAME_CATEGORY}")
    if(DB_FRAME_SITE_COUNT EQUAL 0)
        continue()
    endif()
    math(EXPR DB_FRAME_SITE_LAST "${DB_FRAME_SITE_COUNT} - 1")
    foreach(DB_FRAME_SITE_INDEX RANGE 0 ${DB_FRAME_SITE_LAST})
        string(
            JSON
            DB_FRAME_SITE_PATH
            GET
            "${DB_FRAME_MIGRATION_JSON}"
            categories
            "${DB_FRAME_CATEGORY}"
            ${DB_FRAME_SITE_INDEX}
            path)
        string(
            JSON
            DB_FRAME_SITE_SYMBOL
            GET
            "${DB_FRAME_MIGRATION_JSON}"
            categories
            "${DB_FRAME_CATEGORY}"
            ${DB_FRAME_SITE_INDEX}
            symbol)
        set(DB_FRAME_SITE_FILE "${SOURCE_ROOT}/${DB_FRAME_SITE_PATH}")
        if(NOT EXISTS "${DB_FRAME_SITE_FILE}")
            message(
                FATAL_ERROR "Stale frame migration site: ${DB_FRAME_SITE_PATH}")
        endif()
        file(READ "${DB_FRAME_SITE_FILE}" DB_FRAME_SITE_SOURCE)
        string(FIND "${DB_FRAME_SITE_SOURCE}" "${DB_FRAME_SITE_SYMBOL}"
                    DB_FRAME_SYMBOL_OFFSET)
        if(DB_FRAME_SYMBOL_OFFSET EQUAL -1)
            message(
                FATAL_ERROR
                    "Stale frame migration symbol: ${DB_FRAME_SITE_PATH}:${DB_FRAME_SITE_SYMBOL}"
            )
        endif()
    endforeach()
endforeach()

file(GLOB_RECURSE DB_FRAME_BOUNDARY_FILES "${SOURCE_ROOT}/src/displays/*.c"
     "${SOURCE_ROOT}/src/displays/*.h" "${SOURCE_ROOT}/src/renderers/*.c"
     "${SOURCE_ROOT}/src/renderers/*.h")
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
    if(DB_FRAME_BOUNDARY_FILE MATCHES "/src/displays/"
       AND DB_FRAME_BOUNDARY_SOURCE
           MATCHES
           "db_benchmark_model_(init|shutdown)|db_frame_coordinator_(init|run_frame|step)"
    )
        message(
            FATAL_ERROR
                "Display owns benchmark/coordinator lifecycle: ${DB_FRAME_BOUNDARY_FILE}"
        )
    endif()
    if(DB_FRAME_BOUNDARY_FILE MATCHES "/src/displays/"
       AND DB_FRAME_BOUNDARY_SOURCE MATCHES
           "\\.(target_strategy|gradient_path)[ \t\r\n]*=")
        message(
            FATAL_ERROR
                "Display owns renderer strategy resolution: ${DB_FRAME_BOUNDARY_FILE}"
        )
    endif()
    if(DB_FRAME_BOUNDARY_FILE MATCHES "/src/renderers/"
       AND DB_FRAME_BOUNDARY_SOURCE
           MATCHES
           "db_(conformance_qualify|qualification_service|conformance_cache_(read|write)|probe_process_run)"
    )
        message(
            FATAL_ERROR
                "Renderer owns qualification cache/helper/service policy: ${DB_FRAME_BOUNDARY_FILE}"
        )
    endif()
    if(DB_FRAME_BOUNDARY_FILE MATCHES "/src/renderers/"
       AND DB_FRAME_BOUNDARY_SOURCE MATCHES "db_run_session")
        message(
            FATAL_ERROR
                "Renderer depends on run-session implementation: ${DB_FRAME_BOUNDARY_FILE}"
        )
    endif()
    if(DB_FRAME_BOUNDARY_FILE MATCHES "/src/renderers/"
       AND DB_FRAME_BOUNDARY_SOURCE MATCHES "db_f64_sample_ring_t")
        message(
            FATAL_ERROR
                "Renderer owns forbidden long-run metric history: ${DB_FRAME_BOUNDARY_FILE}"
        )
    endif()
endforeach()

string(JSON DB_FRAME_LOC_BASELINE GET "${DB_FRAME_MIGRATION_JSON}"
       scoped_production_loc_baseline)
file(GLOB_RECURSE DB_FRAME_PRODUCTION_FILES "${SOURCE_ROOT}/src/*.c"
     "${SOURCE_ROOT}/src/*.h")
set(DB_FRAME_PRODUCTION_LOC 0)
foreach(DB_FRAME_PRODUCTION_FILE IN LISTS DB_FRAME_PRODUCTION_FILES)
    file(STRINGS "${DB_FRAME_PRODUCTION_FILE}" DB_FRAME_PRODUCTION_LINES)
    list(LENGTH DB_FRAME_PRODUCTION_LINES DB_FRAME_FILE_LOC)
    math(EXPR DB_FRAME_PRODUCTION_LOC
         "${DB_FRAME_PRODUCTION_LOC} + ${DB_FRAME_FILE_LOC}")
endforeach()
if(DB_FRAME_PRODUCTION_LOC GREATER DB_FRAME_LOC_BASELINE)
    message(
        FATAL_ERROR
            "Frame lifecycle production LOC increased: ${DB_FRAME_PRODUCTION_LOC} > ${DB_FRAME_LOC_BASELINE}"
    )
endif()
message(
    STATUS
        "Frame lifecycle production LOC: ${DB_FRAME_PRODUCTION_LOC}/${DB_FRAME_LOC_BASELINE}"
)
