if(NOT DEFINED CHECKER)
    message(FATAL_ERROR "CHECKER is required")
endif()

set(DB_LINE_FIXTURE_ROOT "${CMAKE_CURRENT_BINARY_DIR}/line-limit-fixture")
file(REMOVE_RECURSE "${DB_LINE_FIXTURE_ROOT}")
file(MAKE_DIRECTORY "${DB_LINE_FIXTURE_ROOT}/src")
file(WRITE "${DB_LINE_FIXTURE_ROOT}/CMakeLists.txt" "project(line_fixture)\n")

string(REPEAT "boundary\n" 1000 DB_BOUNDARY_CONTENT)
file(WRITE "${DB_LINE_FIXTURE_ROOT}/src/boundary.c" "${DB_BOUNDARY_CONTENT}")

string(REPEAT "hard-a\n" 1001 DB_HARD_A_CONTENT)
string(REPEAT "hard-b\n" 1002 DB_HARD_B_CONTENT)
file(WRITE "${DB_LINE_FIXTURE_ROOT}/src/hard_a.c" "${DB_HARD_A_CONTENT}")
file(WRITE "${DB_LINE_FIXTURE_ROOT}/src/hard_b.h" "${DB_HARD_B_CONTENT}")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -DSOURCE_ROOT=${DB_LINE_FIXTURE_ROOT}
            -DLINE_LIMIT=1000 -P "${CHECKER}"
    RESULT_VARIABLE DB_HARD_RESULT
    OUTPUT_VARIABLE DB_HARD_OUTPUT
    ERROR_VARIABLE DB_HARD_ERROR)
if(DB_HARD_RESULT EQUAL 0)
    message(FATAL_ERROR "hard line violations unexpectedly passed")
endif()
set(DB_HARD_LOG "${DB_HARD_OUTPUT}${DB_HARD_ERROR}")
foreach(DB_EXPECTED IN ITEMS "src/hard_a.c: 1001" "src/hard_b.h: 1002")
    if(NOT DB_HARD_LOG MATCHES "${DB_EXPECTED}")
        message(FATAL_ERROR "combined line report omitted ${DB_EXPECTED}")
    endif()
endforeach()
if(DB_HARD_LOG MATCHES "src/boundary.c")
    message(FATAL_ERROR "the 1000-line boundary file was rejected")
endif()

file(REMOVE_RECURSE "${DB_LINE_FIXTURE_ROOT}")
