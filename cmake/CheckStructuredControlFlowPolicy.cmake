if(NOT DEFINED SOURCE_ROOT OR "${SOURCE_ROOT}" STREQUAL "")
    message(FATAL_ERROR "SOURCE_ROOT is required")
endif()

file(
    GLOB_RECURSE DB_CONTROL_FLOW_FILES
    LIST_DIRECTORIES false
    "${SOURCE_ROOT}/src/*.c" "${SOURCE_ROOT}/src/*.h"
    "${SOURCE_ROOT}/tests/*.c" "${SOURCE_ROOT}/tests/*.h")

set(DB_CONTROL_FLOW_FAILURES "")
foreach(DB_CONTROL_FLOW_FILE IN LISTS DB_CONTROL_FLOW_FILES)
    file(READ "${DB_CONTROL_FLOW_FILE}" DB_CONTROL_FLOW_CONTENT)
    string(REGEX MATCHALL "(^|[\r\n])[^\r\n]*goto[ \t]+[A-Za-z_][A-Za-z0-9_]*"
                 DB_GOTO_LINES "${DB_CONTROL_FLOW_CONTENT}")
    list(LENGTH DB_GOTO_LINES DB_GOTO_COUNT)
    if(DB_GOTO_COUNT GREATER 0)
        string(FIND "${DB_CONTROL_FLOW_CONTENT}" "goto cleanup;" DB_GOTO_OFFSET)
        string(FIND "${DB_CONTROL_FLOW_CONTENT}"
                    "goto cleanup; /* DB_CLEANUP_GOTO */" DB_ANNOTATION_OFFSET)
        string(FIND "${DB_CONTROL_FLOW_CONTENT}" "cleanup:" DB_CLEANUP_OFFSET)
        string(REGEX MATCHALL "(^|[\n])[ \t]*cleanup[ \t]*:" DB_CLEANUP_LABELS
                     "${DB_CONTROL_FLOW_CONTENT}")
        list(LENGTH DB_CLEANUP_LABELS DB_CLEANUP_COUNT)
        string(REGEX MATCHALL "(^|[\n])[ \t]*[A-Za-z_][A-Za-z0-9_]*[ \t]*:"
                     DB_ALL_LABELS "${DB_CONTROL_FLOW_CONTENT}")
        list(LENGTH DB_ALL_LABELS DB_LABEL_COUNT)
        set(DB_CLEANUP_GOTO_VALID TRUE)
        if(NOT DB_GOTO_COUNT EQUAL 1
           OR NOT DB_CLEANUP_COUNT EQUAL 1
           OR NOT DB_LABEL_COUNT EQUAL 1
           OR DB_GOTO_OFFSET LESS 0
           OR DB_ANNOTATION_OFFSET LESS 0
           OR DB_CLEANUP_OFFSET LESS DB_GOTO_OFFSET)
            set(DB_CLEANUP_GOTO_VALID FALSE)
        endif()
    endif()
    if(DB_GOTO_COUNT GREATER 0 AND NOT DB_CLEANUP_GOTO_VALID)
        string(
            APPEND
            DB_CONTROL_FLOW_FAILURES
            "${DB_CONTROL_FLOW_FILE}: goto is restricted to one annotated forward 'goto cleanup; /* DB_CLEANUP_GOTO */' and one trailing cleanup label\n"
        )
    endif()
endforeach()

if(NOT DB_CONTROL_FLOW_FAILURES STREQUAL "")
    message(FATAL_ERROR "${DB_CONTROL_FLOW_FAILURES}")
endif()
