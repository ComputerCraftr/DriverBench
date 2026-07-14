if(NOT DEFINED SOURCE_ROOT OR "${SOURCE_ROOT}" STREQUAL "")
    message(FATAL_ERROR "SOURCE_ROOT is required")
endif()

file(GLOB_RECURSE DB_IO_POLICY_FILES "${SOURCE_ROOT}/src/*.c"
     "${SOURCE_ROOT}/src/*.h" "${SOURCE_ROOT}/tests/*.c"
     "${SOURCE_ROOT}/tests/*.h")

set(DB_IO_POLICY_FAILURES "")
foreach(DB_IO_POLICY_FILE IN LISTS DB_IO_POLICY_FILES)
    file(READ "${DB_IO_POLICY_FILE}" DB_IO_POLICY_CONTENT)
    if(DB_IO_POLICY_CONTENT MATCHES
       "\\(void\\)[ \t\r\n]*(read|write|fread|fwrite)[ \t\r\n]*\\(")
        string(
            APPEND
            DB_IO_POLICY_FAILURES
            "${DB_IO_POLICY_FILE}: meaningful byte-I/O results must be checked\n"
        )
    endif()
endforeach()

if(NOT DB_IO_POLICY_FAILURES STREQUAL "")
    message(FATAL_ERROR "${DB_IO_POLICY_FAILURES}")
endif()
