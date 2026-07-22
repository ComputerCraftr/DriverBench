if(NOT DEFINED CHECKER OR "${CHECKER}" STREQUAL "")
    message(FATAL_ERROR "CHECKER is required")
endif()

set(DB_TEST_ROOT
    "${CMAKE_CURRENT_BINARY_DIR}/structured_control_flow_policy_contract")
set(DB_CLEANUP_FIXTURE "${DB_TEST_ROOT}/src/cleanup.c")
set(DB_REJECTED_FIXTURE "${DB_TEST_ROOT}/src/rejected.c")
file(REMOVE_RECURSE "${DB_TEST_ROOT}")
file(MAKE_DIRECTORY "${DB_TEST_ROOT}/src")

file(
    WRITE "${DB_TEST_ROOT}/src/clean.c"
    "static int clean(int value) {\n    if (value == 0) {\n        return 1;\n    }\n    return 0;\n}\n"
)
execute_process(
    COMMAND "${CMAKE_COMMAND}" -DSOURCE_ROOT=${DB_TEST_ROOT} -P "${CHECKER}"
    RESULT_VARIABLE DB_CLEAN_RESULT
    OUTPUT_VARIABLE DB_CLEAN_OUTPUT
    ERROR_VARIABLE DB_CLEAN_ERROR)
if(NOT DB_CLEAN_RESULT EQUAL 0)
    message(
        FATAL_ERROR
            "structured control-flow policy rejected clean source:\n${DB_CLEAN_OUTPUT}${DB_CLEAN_ERROR}"
    )
endif()

file(
    WRITE "${DB_CLEANUP_FIXTURE}"
    "static int cleanup_ok(int value) {\n    int result = 1;\n    if (value != 0) {\n        result = 0;\n        goto cleanup; /* DB_CLEANUP_GOTO */\n    }\ncleanup:\n    return result;\n}\n"
)
execute_process(
    COMMAND "${CMAKE_COMMAND}" -DSOURCE_ROOT=${DB_TEST_ROOT} -P "${CHECKER}"
    RESULT_VARIABLE DB_CLEANUP_RESULT
    OUTPUT_VARIABLE DB_CLEANUP_OUTPUT
    ERROR_VARIABLE DB_CLEANUP_ERROR)
if(NOT DB_CLEANUP_RESULT EQUAL 0)
    message(
        FATAL_ERROR
            "structured control-flow policy rejected annotated cleanup:\n${DB_CLEANUP_OUTPUT}${DB_CLEANUP_ERROR}"
    )
endif()
file(REMOVE "${DB_CLEANUP_FIXTURE}")

file(
    WRITE "${DB_REJECTED_FIXTURE}"
    "static int rejected(int value) {\n    if (value != 0) {\n        goto fail;\n    }\n    return 1;\nfail:\n    return 0;\n}\n"
)
execute_process(
    COMMAND "${CMAKE_COMMAND}" -DSOURCE_ROOT=${DB_TEST_ROOT} -P "${CHECKER}"
    RESULT_VARIABLE DB_REJECTED_RESULT
    OUTPUT_VARIABLE DB_REJECTED_OUTPUT
    ERROR_VARIABLE DB_REJECTED_ERROR)
if(DB_REJECTED_RESULT EQUAL 0)
    message(FATAL_ERROR "structured control-flow policy accepted goto")
endif()
if(NOT "${DB_REJECTED_OUTPUT}${DB_REJECTED_ERROR}" MATCHES "goto is restricted")
    message(
        FATAL_ERROR
            "structured control-flow policy failed without its stable diagnostic:\n${DB_REJECTED_OUTPUT}${DB_REJECTED_ERROR}"
    )
endif()
file(REMOVE "${DB_REJECTED_FIXTURE}")

foreach(DB_BAD_CASE IN ITEMS backward multi_label business_flow)
    if(DB_BAD_CASE STREQUAL "backward")
        set(DB_BAD_SOURCE
            "static int bad(int value) {\ncleanup:\n    value++;\n    if (value < 2) {\n        goto cleanup; /* DB_CLEANUP_GOTO */\n    }\n    return value;\n}\n"
        )
    elseif(DB_BAD_CASE STREQUAL "multi_label")
        set(DB_BAD_SOURCE
            "static int bad(int value) {\n    if (value != 0) {\n        goto cleanup; /* DB_CLEANUP_GOTO */\n    }\nother:\n    value++;\ncleanup:\n    return value;\n}\n"
        )
    else()
        set(DB_BAD_SOURCE
            "static int bad(int value) {\n    if (value != 0) {\n        goto retry;\n    }\nretry:\n    return value;\n}\n"
        )
    endif()
    set(DB_BAD_FIXTURE "${DB_TEST_ROOT}/src/${DB_BAD_CASE}.c")
    file(WRITE "${DB_BAD_FIXTURE}" "${DB_BAD_SOURCE}")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -DSOURCE_ROOT=${DB_TEST_ROOT} -P "${CHECKER}"
        RESULT_VARIABLE DB_BAD_RESULT
        OUTPUT_VARIABLE DB_BAD_OUTPUT
        ERROR_VARIABLE DB_BAD_ERROR)
    if(DB_BAD_RESULT EQUAL 0)
        message(
            FATAL_ERROR
                "structured control-flow policy accepted ${DB_BAD_CASE} goto")
    endif()
    file(REMOVE "${DB_BAD_FIXTURE}")
endforeach()

file(REMOVE_RECURSE "${DB_TEST_ROOT}")
