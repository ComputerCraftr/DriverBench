include_guard(GLOBAL)

set(DB_WARNING_OPTIONS_COMMON
    -Wall
    -Wextra
    -Wpedantic
    -Werror
    -Wformat=2
    -Wcast-align
    -Wcast-qual
    -Wconversion
    -Wsign-conversion
    -Wdouble-promotion
    -Wfloat-equal
    -Wimplicit-fallthrough
    -Wmissing-format-attribute
    -Wmissing-prototypes
    -Wstrict-prototypes
    -Wnull-dereference
    -Wswitch-enum
    -Wundef
    -Wunreachable-code
    -Wunused-const-variable
    -Wunused-function
    -Wunused-label
    -Wunused-macros
    -Wunused-parameter
    -Wunused-result
    -Wunused-value
    -Wunused-variable
    -Wunused-but-set-variable
    -Wunused-but-set-parameter
    -Wvla
    -Wwrite-strings
    -Wpointer-arith
    -Wmissing-declarations
    -Wbad-function-cast)

set(DB_WARNING_OPTIONS_CLANG
    -Wnewline-eof -Wshadow-all -Wunneeded-internal-declaration
    -Wunreachable-code-break -Wunreachable-code-return -Wextra-semi-stmt)

set(DB_WARNING_OPTIONS_GNU
    -Wshadow=local
    -Wduplicated-cond
    -Wduplicated-branches
    -Wlogical-op
    -Wformat-overflow=2
    -Wformat-truncation=2
    -Wformat-signedness
    -Wstringop-overflow=4
    -Wstringop-truncation
    -Walloc-zero
    -Wmissing-attributes)

function(db_apply_warning_options target_name)
    if(NOT TARGET ${target_name} OR NOT DB_COMPILER_IS_GNU_LIKE)
        return()
    endif()

    target_compile_options(${target_name} PRIVATE ${DB_WARNING_OPTIONS_COMMON})
    if(DB_COMPILER_IS_CLANG)
        target_compile_options(${target_name}
                               PRIVATE ${DB_WARNING_OPTIONS_CLANG})
    elseif(DB_COMPILER_IS_GNU)
        target_compile_options(${target_name} PRIVATE ${DB_WARNING_OPTIONS_GNU})
    endif()
endfunction()
