if(NOT DEFINED SOURCE_ROOT OR "${SOURCE_ROOT}" STREQUAL "")
    message(FATAL_ERROR "SOURCE_ROOT is required")
endif()

include("${SOURCE_ROOT}/cmake/DriverBenchWarnings.cmake")

function(db_warning_require list_name)
    foreach(db_warning_flag IN LISTS ARGN)
        list(FIND ${list_name} "${db_warning_flag}" db_warning_index)
        if(db_warning_index EQUAL -1)
            message(
                FATAL_ERROR "${db_warning_flag} is missing from ${list_name}")
        endif()
    endforeach()
endfunction()

function(db_warning_reject list_name)
    foreach(db_warning_flag IN LISTS ARGN)
        list(FIND ${list_name} "${db_warning_flag}" db_warning_index)
        if(NOT db_warning_index EQUAL -1)
            message(
                FATAL_ERROR "${db_warning_flag} does not belong in ${list_name}"
            )
        endif()
    endforeach()
endfunction()

function(db_warning_require_unique list_name)
    set(db_warning_values "${${list_name}}")
    list(LENGTH db_warning_values db_warning_count)
    list(REMOVE_DUPLICATES db_warning_values)
    list(LENGTH db_warning_values db_warning_unique_count)
    if(NOT db_warning_count EQUAL db_warning_unique_count)
        message(FATAL_ERROR "${list_name} contains duplicate warning options")
    endif()
endfunction()

foreach(db_warning_list IN
        ITEMS DB_WARNING_OPTIONS_COMMON DB_WARNING_OPTIONS_CLANG
              DB_WARNING_OPTIONS_GNU)
    db_warning_require_unique(${db_warning_list})
endforeach()

db_warning_require(DB_WARNING_OPTIONS_COMMON -Werror -Wfloat-equal
                   -Wmissing-format-attribute -Wunused-result -Wunused-value)
db_warning_require(DB_WARNING_OPTIONS_CLANG -Wnewline-eof -Wshadow-all
                   -Wunreachable-code-break -Wunreachable-code-return)
db_warning_require(DB_WARNING_OPTIONS_GNU -Wduplicated-cond -Wformat-overflow=2
                   -Wformat-signedness -Wstringop-truncation)

db_warning_reject(
    DB_WARNING_OPTIONS_COMMON -Wshadow-all -Wunreachable-code-break
    -Wformat-overflow=2 -Wstringop-truncation)
db_warning_reject(DB_WARNING_OPTIONS_CLANG -Wduplicated-cond
                  -Wformat-overflow=2 -Wstringop-overflow=4)
db_warning_reject(DB_WARNING_OPTIONS_GNU -Wnewline-eof -Wshadow-all
                  -Wunreachable-code-break -Wunreachable-code-return)

foreach(db_warning_list IN
        ITEMS DB_WARNING_OPTIONS_COMMON DB_WARNING_OPTIONS_CLANG
              DB_WARNING_OPTIONS_GNU)
    db_warning_reject(${db_warning_list} -Wunused-lambda-capture
                      -Wunused-private-field)
endforeach()
