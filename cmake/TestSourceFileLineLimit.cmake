if(NOT DEFINED CHECKER)
    message(FATAL_ERROR "CHECKER is required")
endif()

set(DB_LINE_FIXTURE_ROOT "${CMAKE_CURRENT_BINARY_DIR}/line-limit-fixture")
file(REMOVE_RECURSE "${DB_LINE_FIXTURE_ROOT}")
file(MAKE_DIRECTORY "${DB_LINE_FIXTURE_ROOT}/src")
file(WRITE "${DB_LINE_FIXTURE_ROOT}/CMakeLists.txt" "project(line_fixture)\n")

string(REPEAT "soft\n" 801 DB_SOFT_CONTENT)
file(WRITE "${DB_LINE_FIXTURE_ROOT}/src/soft.c" "${DB_SOFT_CONTENT}")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -DSOURCE_ROOT=${DB_LINE_FIXTURE_ROOT}
            -DLINE_LIMIT=1000 -DSOFT_LINE_LIMIT=800 -P "${CHECKER}"
    RESULT_VARIABLE DB_SOFT_RESULT
    OUTPUT_VARIABLE DB_SOFT_OUTPUT
    ERROR_VARIABLE DB_SOFT_ERROR)
if(NOT DB_SOFT_RESULT EQUAL 0)
    message(FATAL_ERROR "soft line warning unexpectedly failed")
endif()
set(DB_SOFT_LOG "${DB_SOFT_OUTPUT}${DB_SOFT_ERROR}")
if(NOT DB_SOFT_LOG MATCHES "src/soft.c: 801")
    message(FATAL_ERROR "soft line warning did not report its file")
endif()

string(REPEAT "hard-a\n" 1001 DB_HARD_A_CONTENT)
string(REPEAT "hard-b\n" 1002 DB_HARD_B_CONTENT)
file(WRITE "${DB_LINE_FIXTURE_ROOT}/src/hard_a.c" "${DB_HARD_A_CONTENT}")
file(WRITE "${DB_LINE_FIXTURE_ROOT}/src/hard_b.h" "${DB_HARD_B_CONTENT}")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -DSOURCE_ROOT=${DB_LINE_FIXTURE_ROOT}
            -DLINE_LIMIT=1000 -DSOFT_LINE_LIMIT=800 -P "${CHECKER}"
    RESULT_VARIABLE DB_HARD_RESULT
    OUTPUT_VARIABLE DB_HARD_OUTPUT
    ERROR_VARIABLE DB_HARD_ERROR)
if(DB_HARD_RESULT EQUAL 0)
    message(FATAL_ERROR "hard line violations unexpectedly passed")
endif()
set(DB_HARD_LOG "${DB_HARD_OUTPUT}${DB_HARD_ERROR}")
foreach(DB_EXPECTED IN ITEMS "src/hard_a.c: 1001" "src/hard_b.h: 1002"
                             "src/soft.c: 801")
    if(NOT DB_HARD_LOG MATCHES "${DB_EXPECTED}")
        message(FATAL_ERROR "combined line report omitted ${DB_EXPECTED}")
    endif()
endforeach()

file(REMOVE_RECURSE "${DB_LINE_FIXTURE_ROOT}")
