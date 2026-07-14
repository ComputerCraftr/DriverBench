if(NOT DEFINED SOURCE_ROOT OR SOURCE_ROOT STREQUAL "")
    message(FATAL_ERROR "SOURCE_ROOT is required")
endif()

find_program(DB_HYGIENE_GIT_EXECUTABLE NAMES git REQUIRED)
execute_process(
    COMMAND "${DB_HYGIENE_GIT_EXECUTABLE}" -C "${SOURCE_ROOT}" ls-files
    RESULT_VARIABLE db_git_status
    OUTPUT_VARIABLE db_tracked_output
    ERROR_VARIABLE db_git_error
    OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT db_git_status EQUAL 0)
    message(FATAL_ERROR "git ls-files failed: ${db_git_error}")
endif()

string(REPLACE "\n" ";" db_tracked_files "${db_tracked_output}")
set(db_forbidden_pattern
    "(^|/)(__pycache__|\\.mypy_cache|\\.ruff_cache|\\.pytest_cache)(/|$)|\\.py[co]$|(^|/)clang-tidy(\\.[^/]*)?\\.log$|(^|/)ctest-results[^/]*\\.xml$"
)
set(db_violations "")
foreach(db_file IN LISTS db_tracked_files)
    if(db_file MATCHES "${db_forbidden_pattern}")
        string(APPEND db_violations "${db_file}\n")
    endif()
endforeach()

if(NOT db_violations STREQUAL "")
    message(
        FATAL_ERROR
            "Generated tooling artifacts must not be tracked:\n${db_violations}"
    )
endif()

message(STATUS "Repository hygiene policy passed.")
