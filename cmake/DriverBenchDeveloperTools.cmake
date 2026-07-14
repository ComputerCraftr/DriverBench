function(db_find_developer_tool out_var)
    find_program(
        db_tool_path
        NAMES ${ARGN}
        HINTS "$ENV{HOME}/.local/bin")
    set(${out_var}
        "${db_tool_path}"
        PARENT_SCOPE)
    unset(db_tool_path CACHE)
endfunction()

function(db_add_developer_tool_targets)
    find_package(
        Python3
        COMPONENTS Interpreter
        QUIET)
    if(NOT Python3_Interpreter_FOUND)
        message(
            STATUS "Developer tooling targets disabled: Python 3 unavailable")
        return()
    endif()

    db_find_developer_tool(DB_CLANG_FORMAT_TOOL clang-format-22 clang-format)
    db_find_developer_tool(DB_CMAKE_FORMAT_TOOL cmake-format)
    db_find_developer_tool(DB_RUFF_TOOL ruff)
    db_find_developer_tool(DB_MYPY_TOOL mypy)
    db_find_developer_tool(DB_RUN_CLANG_TIDY_TOOL run-clang-tidy-22
                           run-clang-tidy)
    db_find_developer_tool(DB_CLANG_TIDY_TOOL clang-tidy-22 clang-tidy)

    set(db_developer_runner
        "${CMAKE_SOURCE_DIR}/scripts/run_developer_tools.py")

    add_custom_target(
        format-c-check
        COMMAND
            ${Python3_EXECUTABLE} ${db_developer_runner} --source-root
            ${CMAKE_SOURCE_DIR} c-format --tool ${DB_CLANG_FORMAT_TOOL} --check
        VERBATIM)
    add_custom_target(
        format-c
        COMMAND
            ${Python3_EXECUTABLE} ${db_developer_runner} --source-root
            ${CMAKE_SOURCE_DIR} c-format --tool ${DB_CLANG_FORMAT_TOOL} --fix
        VERBATIM)
    add_custom_target(
        format-cmake-check
        COMMAND
            ${Python3_EXECUTABLE} ${db_developer_runner} --source-root
            ${CMAKE_SOURCE_DIR} cmake-format --tool ${DB_CMAKE_FORMAT_TOOL}
            --check
        VERBATIM)
    add_custom_target(
        format-cmake
        COMMAND
            ${Python3_EXECUTABLE} ${db_developer_runner} --source-root
            ${CMAKE_SOURCE_DIR} cmake-format --tool ${DB_CMAKE_FORMAT_TOOL}
            --fix
        VERBATIM)
    add_custom_target(
        python-check
        COMMAND
            ${Python3_EXECUTABLE} ${db_developer_runner} --source-root
            ${CMAKE_SOURCE_DIR} python-check --ruff ${DB_RUFF_TOOL} --mypy
            ${DB_MYPY_TOOL}
        VERBATIM)
    add_custom_target(
        python-format
        COMMAND ${Python3_EXECUTABLE} ${db_developer_runner} --source-root
                ${CMAKE_SOURCE_DIR} python-format --ruff ${DB_RUFF_TOOL}
        VERBATIM)
    add_custom_target(format-check)
    add_dependencies(format-check format-c-check format-cmake-check
                     python-check)
    add_custom_target(format)
    add_dependencies(format format-c format-cmake python-format)

    add_custom_target(
        clang-tidy-check
        COMMAND
            ${CMAKE_COMMAND} -DTIDY_COMMAND=${DB_RUN_CLANG_TIDY_TOOL}
            -DBUILD_DIR=${CMAKE_BINARY_DIR} -DSOURCE_ROOT=${CMAKE_SOURCE_DIR}
            -DLOG_PATH=${CMAKE_BINARY_DIR}/clang-tidy.log -DJOBS=32 -P
            ${CMAKE_SOURCE_DIR}/cmake/RunClangTidy.cmake
        COMMAND
            ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/scripts/run_header_clang_tidy.py --source-root
            ${CMAKE_SOURCE_DIR} --build-dir ${CMAKE_BINARY_DIR} --clang-tidy
            ${DB_CLANG_TIDY_TOOL} --jobs 32
        VERBATIM)
endfunction()
