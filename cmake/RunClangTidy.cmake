if(NOT DEFINED TIDY_COMMAND OR TIDY_COMMAND STREQUAL "")
    message(FATAL_ERROR "TIDY_COMMAND is required")
endif()
if(NOT DEFINED BUILD_DIR OR BUILD_DIR STREQUAL "")
    message(FATAL_ERROR "BUILD_DIR is required")
endif()
if(NOT DEFINED SOURCE_ROOT OR SOURCE_ROOT STREQUAL "")
    message(FATAL_ERROR "SOURCE_ROOT is required")
endif()
if(NOT DEFINED LOG_PATH OR LOG_PATH STREQUAL "")
    message(FATAL_ERROR "LOG_PATH is required")
endif()
if(NOT DEFINED JOBS)
    set(JOBS 32)
endif()

string(
    RANDOM
    LENGTH 16
    ALPHABET 0123456789abcdef db_log_suffix)
set(db_temporary_log "${LOG_PATH}.${db_log_suffix}.tmp")

execute_process(
    COMMAND "${TIDY_COMMAND}" -p "${BUILD_DIR}" -j "${JOBS}" .
    WORKING_DIRECTORY "${SOURCE_ROOT}"
    RESULT_VARIABLE db_tidy_status
    OUTPUT_VARIABLE db_tidy_stdout
    ERROR_VARIABLE db_tidy_stderr)
file(WRITE "${db_temporary_log}" "${db_tidy_stdout}${db_tidy_stderr}")
file(RENAME "${db_temporary_log}" "${LOG_PATH}")

file(STRINGS "${LOG_PATH}" db_tidy_diagnostics
     REGEX "(^|[^A-Za-z])(warning|error):")
if(NOT db_tidy_status EQUAL 0 OR db_tidy_diagnostics)
    if(db_tidy_diagnostics)
        list(JOIN db_tidy_diagnostics "\n" db_tidy_diagnostic_text)
        message(STATUS "clang-tidy diagnostics:\n${db_tidy_diagnostic_text}")
    endif()
    message(
        FATAL_ERROR
            "run-clang-tidy failed: status=${db_tidy_status}, log=${LOG_PATH}")
endif()

message(STATUS "clang-tidy completed without diagnostics: ${LOG_PATH}")
