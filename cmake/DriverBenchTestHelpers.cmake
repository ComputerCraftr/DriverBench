# Generic low-level CTest registration helpers.
function(
    db_add_hash_difference_test
    test_name
    test_bin
    test_args_a
    test_args_b
    hash_key
    test_labels)
    add_test(
        NAME ${test_name}
        COMMAND
            ${CMAKE_COMMAND} -DTEST_BIN=${test_bin}
            -DTEST_ARGS_A=${test_args_a} -DTEST_ARGS_B=${test_args_b}
            -DTEST_HASH_KEY=${hash_key} -P
            ${CMAKE_SOURCE_DIR}/cmake/RunHashDifferenceTest.cmake)
    set_tests_properties(${test_name} PROPERTIES LABELS "${test_labels}")
endfunction()
function(
    db_add_output_expectation_test
    test_name
    test_bin
    test_args
    required_patterns
    forbidden_patterns
    test_labels)
    add_test(
        NAME ${test_name}
        COMMAND
            ${CMAKE_COMMAND} -DTEST_BIN=${test_bin} -DTEST_ARGS=${test_args}
            -DTEST_REQUIRED_PATTERNS=${required_patterns}
            -DTEST_FORBIDDEN_PATTERNS=${forbidden_patterns} -P
            ${CMAKE_SOURCE_DIR}/cmake/RunOutputExpectationTest.cmake)
    set_tests_properties(${test_name} PROPERTIES LABELS "${test_labels}")
endfunction()
function(
    db_add_trace_contract_test
    test_name
    test_bin
    test_args
    global_required
    global_forbidden
    trace_required
    trace_forbidden
    test_labels)
    add_test(
        NAME ${test_name}
        COMMAND
            ${CMAKE_COMMAND} -DTEST_BIN=${test_bin} -DTEST_ARGS=${test_args}
            -DTEST_GLOBAL_REQUIRED=${global_required}
            -DTEST_GLOBAL_FORBIDDEN=${global_forbidden}
            -DTEST_TRACE_REQUIRED=${trace_required}
            -DTEST_TRACE_FORBIDDEN=${trace_forbidden} -P
            ${CMAKE_SOURCE_DIR}/cmake/RunTraceContractTest.cmake)
    set_tests_properties(${test_name} PROPERTIES LABELS "${test_labels}")
endfunction()
function(db_add_trace_difference_test test_name test_bin test_args_a
         test_args_b test_labels)
    add_test(
        NAME ${test_name}
        COMMAND
            ${CMAKE_COMMAND} -DTEST_BIN=${test_bin}
            -DTEST_ARGS_A=${test_args_a} -DTEST_ARGS_B=${test_args_b} -P
            ${CMAKE_SOURCE_DIR}/cmake/RunTraceDifferenceTest.cmake)
    set_tests_properties(${test_name} PROPERTIES LABELS "${test_labels}")
endfunction()
function(db_add_damage_trace_invariant_test test_name test_bin test_args
         test_labels)
    add_test(
        NAME ${test_name}
        COMMAND
            ${CMAKE_COMMAND} -DTEST_BIN=${test_bin} -DTEST_ARGS=${test_args} -P
            ${CMAKE_SOURCE_DIR}/cmake/RunDamageTraceInvariantTest.cmake)
    set_tests_properties(${test_name} PROPERTIES LABELS "${test_labels}")
endfunction()
function(
    db_add_damage_trace_presence_test
    test_name
    test_bin
    test_args
    backend
    stages
    test_labels)
    add_test(
        NAME ${test_name}
        COMMAND
            ${CMAKE_COMMAND} -DTEST_BIN=${test_bin} -DTEST_ARGS=${test_args}
            -DTEST_DAMAGE_BACKEND=${backend} -DTEST_DAMAGE_STAGES=${stages} -P
            ${CMAKE_SOURCE_DIR}/cmake/RunDamageTracePresenceTest.cmake)
    set_tests_properties(${test_name} PROPERTIES LABELS "${test_labels}")
endfunction()
function(
    db_add_command_contract_test
    test_name
    test_bin
    test_args
    test_exit_code
    required_substrings
    forbidden_substrings
    required_fields
    forbidden_fields
    test_labels)
    add_test(
        NAME ${test_name}
        COMMAND
            ${CMAKE_COMMAND} -DTEST_BIN=${test_bin} -DTEST_ARGS=${test_args}
            -DTEST_EXIT_CODE=${test_exit_code}
            -DTEST_REQUIRED_SUBSTRINGS=${required_substrings}
            -DTEST_FORBIDDEN_SUBSTRINGS=${forbidden_substrings}
            -DTEST_REQUIRED_FIELDS=${required_fields}
            -DTEST_FORBIDDEN_FIELDS=${forbidden_fields} -P
            ${CMAKE_SOURCE_DIR}/cmake/RunCommandContractTest.cmake)
    set_tests_properties(${test_name} PROPERTIES LABELS "${test_labels}")
endfunction()
function(
    db_add_command_expectation_test
    test_name
    test_bin
    test_args
    test_exit_code
    required_patterns
    forbidden_patterns
    test_labels)
    add_test(
        NAME ${test_name}
        COMMAND
            ${CMAKE_COMMAND} -DTEST_BIN=${test_bin} -DTEST_ARGS=${test_args}
            -DTEST_EXIT_CODE=${test_exit_code}
            -DTEST_REQUIRED_PATTERNS=${required_patterns}
            -DTEST_FORBIDDEN_PATTERNS=${forbidden_patterns} -P
            ${CMAKE_SOURCE_DIR}/cmake/RunCommandExpectationTest.cmake)
    set_tests_properties(${test_name} PROPERTIES LABELS "${test_labels}")
endfunction()
function(db_add_binary_pattern_check_test test_name test_bin forbidden_patterns
         test_labels)
    add_test(
        NAME ${test_name}
        COMMAND
            ${CMAKE_COMMAND} -DTEST_BIN=${test_bin} -DNM_BIN=${CMAKE_NM}
            -DTEST_FORBIDDEN_PATTERNS=${forbidden_patterns} -P
            ${CMAKE_SOURCE_DIR}/cmake/RunBinaryPatternCheck.cmake)
    set_tests_properties(${test_name} PROPERTIES LABELS "${test_labels}")
endfunction()
function(db_add_unit_binary_test test_name test_bin test_labels)
    add_test(
        NAME ${test_name}
        COMMAND
            ${CMAKE_COMMAND} -DTEST_BIN=${test_bin} -DTEST_ARGS=
            -DTEST_EXIT_CODE=0 -DTEST_REQUIRED_PATTERNS=
            -DTEST_FORBIDDEN_PATTERNS= -P
            ${CMAKE_SOURCE_DIR}/cmake/RunCommandExpectationTest.cmake)
    set_tests_properties(${test_name} PROPERTIES LABELS "${test_labels}")
endfunction()
function(db_test_apply_skip_regex test_name skip_regex)
    set_tests_properties(
        ${test_name} PROPERTIES SKIP_REGULAR_EXPRESSION
                                "${DB_TEST_CANONICAL_SKIP_REGEX}")
endfunction()
function(db_test_apply_timeout test_name timeout_profile)
    db_test_timeout_seconds(db_timeout_seconds "${timeout_profile}")
    set_tests_properties(${test_name} PROPERTIES TIMEOUT
                                                 "${db_timeout_seconds}")
endfunction()
function(db_test_finalize_skip_contract)
    get_property(
        db_registered_tests
        DIRECTORY
        PROPERTY TESTS)
    foreach(db_registered_test IN LISTS db_registered_tests)
        set_property(
            TEST ${db_registered_test}
            PROPERTY SKIP_REGULAR_EXPRESSION "${DB_TEST_CANONICAL_SKIP_REGEX}")
    endforeach()
endfunction()
function(db_suite_make_test_name out_var test_prefix base_name)
    if("${test_prefix}" STREQUAL "")
        set(${out_var}
            "${base_name}"
            PARENT_SCOPE)
    else()
        set(${out_var}
            "${test_prefix}${base_name}"
            PARENT_SCOPE)
    endif()
endfunction()
function(db_suite_compose_labels out_var base_labels extra_labels)
    if("${extra_labels}" STREQUAL "")
        set(${out_var}
            "${base_labels}"
            PARENT_SCOPE)
    else()
        set(${out_var}
            "${base_labels};${extra_labels}"
            PARENT_SCOPE)
    endif()
endfunction()
function(db_register_cross_renderer_damage_trace_contract test_prefix test_bin
         test_labels benchmark_mode)
    db_suite_make_test_name(
        db_test_name "${test_prefix}"
        "regression_cross_renderer_${benchmark_mode}_logical_damage_equivalence"
    )
    set(common_args
        "--benchmark-mode ${benchmark_mode} ${DB_DETERMINISM_COMMON_ARGS} --trace-damage 2 --frame-limit 5"
    )
    db_add_trace_difference_test(
        "${db_test_name}"
        "${test_bin}"
        "${DB_DETERMINISM_CPU_OFFSCREEN_PREFIX} ${common_args}"
        "${DB_DETERMINISM_GL1_OFFSCREEN_PREFIX} ${common_args}"
        "${test_labels}")
endfunction()
