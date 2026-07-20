function(db_register_source_policy_tests)
    add_test(
        NAME source_file_line_limit
        COMMAND
            ${CMAKE_COMMAND} -DSOURCE_ROOT=${CMAKE_SOURCE_DIR}
            -DLINE_LIMIT=${DB_SOURCE_FILE_LINE_LIMIT}
            -DSOFT_LINE_LIMIT=${DB_SOURCE_FILE_SOFT_LINE_LIMIT} -P
            ${CMAKE_SOURCE_DIR}/cmake/CheckSourceFileLineLimit.cmake)
    set_tests_properties(source_file_line_limit PROPERTIES LABELS "regression")
    add_test(
        NAME source_file_line_limit_contract
        COMMAND
            ${CMAKE_COMMAND}
            -DCHECKER=${CMAKE_SOURCE_DIR}/cmake/CheckSourceFileLineLimit.cmake
            -P ${CMAKE_SOURCE_DIR}/cmake/TestSourceFileLineLimit.cmake)
    set_tests_properties(source_file_line_limit_contract
                         PROPERTIES LABELS "regression")

    add_test(NAME source_repository_hygiene
             COMMAND ${CMAKE_COMMAND} -DSOURCE_ROOT=${CMAKE_SOURCE_DIR} -P
                     ${CMAKE_SOURCE_DIR}/cmake/CheckRepositoryHygiene.cmake)
    set_tests_properties(source_repository_hygiene PROPERTIES LABELS
                                                              "regression")

    add_test(NAME source_io_result_policy
             COMMAND ${CMAKE_COMMAND} -DSOURCE_ROOT=${CMAKE_SOURCE_DIR} -P
                     ${CMAKE_SOURCE_DIR}/cmake/CheckIoResultPolicy.cmake)
    set_tests_properties(source_io_result_policy PROPERTIES LABELS "regression")

    add_test(
        NAME source_structured_control_flow_policy
        COMMAND ${CMAKE_COMMAND} -DSOURCE_ROOT=${CMAKE_SOURCE_DIR} -P
                ${CMAKE_SOURCE_DIR}/cmake/CheckStructuredControlFlowPolicy.cmake
    )
    set_tests_properties(source_structured_control_flow_policy
                         PROPERTIES LABELS "regression")

    add_test(
        NAME source_structured_control_flow_policy_contract
        COMMAND
            ${CMAKE_COMMAND}
            -DCHECKER=${CMAKE_SOURCE_DIR}/cmake/CheckStructuredControlFlowPolicy.cmake
            -P ${CMAKE_SOURCE_DIR}/cmake/TestStructuredControlFlowPolicy.cmake)
    set_tests_properties(source_structured_control_flow_policy_contract
                         PROPERTIES LABELS "regression")

    add_test(NAME source_compiler_warning_policy
             COMMAND ${CMAKE_COMMAND} -DSOURCE_ROOT=${CMAKE_SOURCE_DIR} -P
                     ${CMAKE_SOURCE_DIR}/cmake/CheckCompilerWarningPolicy.cmake)
    set_tests_properties(source_compiler_warning_policy PROPERTIES LABELS
                                                                   "regression")

    find_program(DB_SOURCE_POLICY_PYTHON NAMES python3)
    if(DB_SOURCE_POLICY_PYTHON)
        add_test(
            NAME source_numeric_syntax_policy
            COMMAND
                ${DB_SOURCE_POLICY_PYTHON}
                ${CMAKE_SOURCE_DIR}/scripts/check_numeric_policy.py
                --source-root ${CMAKE_SOURCE_DIR})
        set_tests_properties(source_numeric_syntax_policy
                             PROPERTIES LABELS "regression")

        add_test(
            NAME source_numeric_syntax_policy_contract
            COMMAND
                ${DB_SOURCE_POLICY_PYTHON}
                ${CMAKE_SOURCE_DIR}/tests/test_numeric_policy.py
                ${CMAKE_SOURCE_DIR}/scripts/check_numeric_policy.py)
        set_tests_properties(source_numeric_syntax_policy_contract
                             PROPERTIES LABELS "regression")

        add_test(
            NAME source_memory_syntax_policy
            COMMAND
                ${DB_SOURCE_POLICY_PYTHON}
                ${CMAKE_SOURCE_DIR}/scripts/check_memory_policy.py
                --source-root ${CMAKE_SOURCE_DIR})
        set_tests_properties(source_memory_syntax_policy
                             PROPERTIES LABELS "regression")

        add_test(
            NAME source_memory_syntax_policy_contract
            COMMAND
                ${DB_SOURCE_POLICY_PYTHON}
                ${CMAKE_SOURCE_DIR}/tests/test_memory_policy.py
                ${CMAKE_SOURCE_DIR}/scripts/check_memory_policy.py)
        set_tests_properties(source_memory_syntax_policy_contract
                             PROPERTIES LABELS "regression")

        add_test(
            NAME source_clang_tidy_runner_contract
            COMMAND
                ${DB_SOURCE_POLICY_PYTHON}
                ${CMAKE_SOURCE_DIR}/tests/test_run_clang_tidy.py
                ${CMAKE_COMMAND} ${CMAKE_SOURCE_DIR}/cmake/RunClangTidy.cmake)
        set_tests_properties(source_clang_tidy_runner_contract
                             PROPERTIES LABELS "regression")
        add_test(
            NAME source_header_clang_tidy_runner_contract
            COMMAND
                ${DB_SOURCE_POLICY_PYTHON}
                ${CMAKE_SOURCE_DIR}/tests/test_header_clang_tidy.py
                ${CMAKE_SOURCE_DIR}/scripts/run_header_clang_tidy.py)
        set_tests_properties(source_header_clang_tidy_runner_contract
                             PROPERTIES LABELS "regression")
    endif()

    find_program(DB_HEADER_CLANG_TIDY_EXECUTABLE NAMES clang-tidy-22 clang-tidy)
    find_program(DB_HEADER_CLANG_TIDY_PYTHON NAMES python3)
    if(CMAKE_C_COMPILER_ID MATCHES "Clang"
       AND DB_HEADER_CLANG_TIDY_EXECUTABLE
       AND DB_HEADER_CLANG_TIDY_PYTHON)
        add_test(
            NAME source_header_clang_tidy
            COMMAND
                ${DB_HEADER_CLANG_TIDY_PYTHON}
                ${CMAKE_SOURCE_DIR}/scripts/run_header_clang_tidy.py
                --source-root ${CMAKE_SOURCE_DIR} --build-dir
                ${CMAKE_BINARY_DIR} --clang-tidy
                ${DB_HEADER_CLANG_TIDY_EXECUTABLE})
        db_test_timeout_seconds(db_header_lint_timeout lint)
        set_tests_properties(
            source_header_clang_tidy PROPERTIES LABELS "regression" TIMEOUT
                                                "${db_header_lint_timeout}")
    else()
        message(
            STATUS
                "Header clang-tidy CTest disabled: a Clang configuration, clang-tidy, and python3 are required"
        )
    endif()

    add_test(
        NAME source_platform_policy_cmake
        COMMAND
            ${CMAKE_COMMAND} -DSOURCE_ROOT=${CMAKE_SOURCE_DIR}
            -DRULE_SET=cmake_platform_policy -P
            ${CMAKE_SOURCE_DIR}/cmake/CheckPlatformPolicyBoundaries.cmake)
    set_tests_properties(source_platform_policy_cmake PROPERTIES LABELS
                                                                 "regression")

    add_test(
        NAME source_platform_policy_display_glfw
        COMMAND
            ${CMAKE_COMMAND} -DSOURCE_ROOT=${CMAKE_SOURCE_DIR}
            -DRULE_SET=display_glfw_policy -P
            ${CMAKE_SOURCE_DIR}/cmake/CheckPlatformPolicyBoundaries.cmake)
    set_tests_properties(source_platform_policy_display_glfw
                         PROPERTIES LABELS "regression")

    add_test(
        NAME source_platform_policy_renderer_gl_upload
        COMMAND
            ${CMAKE_COMMAND} -DSOURCE_ROOT=${CMAKE_SOURCE_DIR}
            -DRULE_SET=renderer_gl_upload_policy -P
            ${CMAKE_SOURCE_DIR}/cmake/CheckPlatformPolicyBoundaries.cmake)
    set_tests_properties(source_platform_policy_renderer_gl_upload
                         PROPERTIES LABELS "regression")

    add_test(
        NAME source_renderer_ir_consumption_policy
        COMMAND
            ${CMAKE_COMMAND} -DSOURCE_ROOT=${CMAKE_SOURCE_DIR}
            -DRULE_SET=renderer_ir_policy -P
            ${CMAKE_SOURCE_DIR}/cmake/CheckPlatformPolicyBoundaries.cmake)
    set_tests_properties(source_renderer_ir_consumption_policy
                         PROPERTIES LABELS "regression")

    add_test(
        NAME source_platform_policy_proc_loading
        COMMAND
            ${CMAKE_COMMAND} -DSOURCE_ROOT=${CMAKE_SOURCE_DIR}
            -DRULE_SET=platform_proc_loading -P
            ${CMAKE_SOURCE_DIR}/cmake/CheckPlatformPolicyBoundaries.cmake)
    set_tests_properties(source_platform_policy_proc_loading
                         PROPERTIES LABELS "regression")

    add_test(
        NAME source_logging_policy
        COMMAND
            ${CMAKE_COMMAND} -DSOURCE_ROOT=${CMAKE_SOURCE_DIR}
            -DRULE_SET=logging_policy -P
            ${CMAKE_SOURCE_DIR}/cmake/CheckPlatformPolicyBoundaries.cmake)
    set_tests_properties(source_logging_policy PROPERTIES LABELS "regression")

    add_test(
        NAME source_cmake_test_registration_policy
        COMMAND ${CMAKE_COMMAND} -DSOURCE_ROOT=${CMAKE_SOURCE_DIR} -P
                ${CMAKE_SOURCE_DIR}/cmake/CheckCMakeTestRegistrationPolicy.cmake
    )
    set_tests_properties(source_cmake_test_registration_policy
                         PROPERTIES LABELS "regression")

    add_test(
        NAME source_bool_normalization_policy
        COMMAND
            ${CMAKE_COMMAND} -DSOURCE_ROOT=${CMAKE_SOURCE_DIR}
            -DRULE_SET=bool_normalization_policy -P
            ${CMAKE_SOURCE_DIR}/cmake/CheckPlatformPolicyBoundaries.cmake)
    set_tests_properties(source_bool_normalization_policy
                         PROPERTIES LABELS "regression")

    add_test(
        NAME source_numeric_boundary_policy
        COMMAND
            ${CMAKE_COMMAND} -DSOURCE_ROOT=${CMAKE_SOURCE_DIR}
            -DRULE_SET=numeric_boundary_policy -P
            ${CMAKE_SOURCE_DIR}/cmake/CheckPlatformPolicyBoundaries.cmake)
    set_tests_properties(source_numeric_boundary_policy PROPERTIES LABELS
                                                                   "regression")

    add_test(
        NAME source_sorting_policy
        COMMAND
            ${CMAKE_COMMAND} -DSOURCE_ROOT=${CMAKE_SOURCE_DIR}
            -DRULE_SET=sorting_policy -P
            ${CMAKE_SOURCE_DIR}/cmake/CheckPlatformPolicyBoundaries.cmake)
    set_tests_properties(source_sorting_policy PROPERTIES LABELS "regression")

    add_test(
        NAME source_sorting_policy_contract
        COMMAND
            ${DB_SOURCE_POLICY_PYTHON}
            ${CMAKE_SOURCE_DIR}/tests/test_sorting_policy.py
            ${CMAKE_SOURCE_DIR}/cmake/CheckPlatformPolicyBoundaries.cmake)
    set_tests_properties(source_sorting_policy_contract PROPERTIES LABELS
                                                                   "regression")

    add_test(NAME source_hash_tree_policy
             COMMAND ${CMAKE_COMMAND} -DSOURCE_ROOT=${CMAKE_SOURCE_DIR} -P
                     ${CMAKE_SOURCE_DIR}/cmake/CheckHashTreePolicy.cmake)
    set_tests_properties(source_hash_tree_policy PROPERTIES LABELS
                                                            "regression;hash")

    add_test(NAME source_vulkan_multi_gpu_policy
             COMMAND ${CMAKE_COMMAND} -DSOURCE_ROOT=${CMAKE_SOURCE_DIR} -P
                     ${CMAKE_SOURCE_DIR}/cmake/CheckVulkanMultiGpuPolicy.cmake)
    set_tests_properties(source_vulkan_multi_gpu_policy PROPERTIES LABELS
                                                                   "regression")

    add_test(NAME source_semantic_ir_policy
             COMMAND ${CMAKE_COMMAND} -DSOURCE_ROOT=${CMAKE_SOURCE_DIR} -P
                     ${CMAKE_SOURCE_DIR}/cmake/CheckSemanticIRPolicy.cmake)
    set_tests_properties(source_semantic_ir_policy PROPERTIES LABELS
                                                              "regression;ir")

    add_test(
        NAME config_glfw_provider_default_vendored
        COMMAND
            ${CMAKE_COMMAND} -DSOURCE_ROOT=${CMAKE_SOURCE_DIR}
            -DTEST_BINARY_DIR=${CMAKE_BINARY_DIR}/ctest_glfw_provider_vendored
            -DTEST_PROVIDER=vendored -DEXPECT_GLFW_AVAILABLE=ON
            -DEXPECT_PROVIDER_RESOLVED=vendored
            -DTEST_GENERATOR=${CMAKE_GENERATOR}
            -DTEST_BUILD_TYPE=${CMAKE_BUILD_TYPE}
            -DTEST_C_COMPILER=${CMAKE_C_COMPILER}
            -DTEST_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM} -P
            ${CMAKE_SOURCE_DIR}/cmake/CheckGLFWProviderConfig.cmake)
    set_tests_properties(config_glfw_provider_default_vendored
                         PROPERTIES LABELS "regression")

    add_test(
        NAME config_glfw_provider_off
        COMMAND
            ${CMAKE_COMMAND} -DSOURCE_ROOT=${CMAKE_SOURCE_DIR}
            -DTEST_BINARY_DIR=${CMAKE_BINARY_DIR}/ctest_glfw_provider_off
            -DTEST_PROVIDER=off -DEXPECT_GLFW_AVAILABLE=OFF
            -DEXPECT_PROVIDER_RESOLVED=off -DTEST_GENERATOR=${CMAKE_GENERATOR}
            -DTEST_BUILD_TYPE=${CMAKE_BUILD_TYPE}
            -DTEST_C_COMPILER=${CMAKE_C_COMPILER}
            -DTEST_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM} -P
            ${CMAKE_SOURCE_DIR}/cmake/CheckGLFWProviderConfig.cmake)
    set_tests_properties(config_glfw_provider_off PROPERTIES LABELS
                                                             "regression")

    add_test(NAME regression_render_wait_policy_bounded
             COMMAND ${CMAKE_COMMAND} -DSOURCE_ROOT=${CMAKE_SOURCE_DIR} -P
                     ${CMAKE_SOURCE_DIR}/cmake/CheckRenderWaitPolicy.cmake)
    set_tests_properties(regression_render_wait_policy_bounded
                         PROPERTIES LABELS "regression")

    add_test(NAME source_serialization_policy
             COMMAND ${CMAKE_COMMAND} -DSOURCE_ROOT=${CMAKE_SOURCE_DIR} -P
                     ${CMAKE_SOURCE_DIR}/cmake/CheckSerializationPolicy.cmake)
    set_tests_properties(source_serialization_policy PROPERTIES LABELS
                                                                "regression")

    add_test(
        NAME source_vulkan_capability_policy
        COMMAND ${CMAKE_COMMAND} -DSOURCE_ROOT=${CMAKE_SOURCE_DIR} -P
                ${CMAKE_SOURCE_DIR}/cmake/CheckVulkanCapabilityPolicy.cmake)
    set_tests_properties(source_vulkan_capability_policy
                         PROPERTIES LABELS "regression")

    add_test(NAME source_kms_presentation_policy
             COMMAND ${CMAKE_COMMAND} -DSOURCE_ROOT=${CMAKE_SOURCE_DIR} -P
                     ${CMAKE_SOURCE_DIR}/cmake/CheckKMSPresentationPolicy.cmake)
    set_tests_properties(source_kms_presentation_policy PROPERTIES LABELS
                                                                   "regression")

    add_test(
        NAME source_frame_lifecycle_migration
        COMMAND ${CMAKE_COMMAND} -DSOURCE_ROOT=${CMAKE_SOURCE_DIR} -P
                ${CMAKE_SOURCE_DIR}/cmake/CheckFrameLifecycleMigration.cmake)
    set_tests_properties(source_frame_lifecycle_migration
                         PROPERTIES LABELS "regression")

endfunction()
