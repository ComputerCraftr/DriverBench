function(db_register_unit_and_probe_tests)
    add_executable(
        driverbench_unit_tests
        tests/test_main.c
        tests/test_benchmark_checkpoint_transaction.c
        tests/test_benchmark_seeding.c
        tests/test_benchmark_emitters.c
        tests/test_cli.c
        tests/test_gradient_divergence.c
        tests/test_core_logging.c
        tests/test_damage_trace.c
        tests/test_display_gl_runtime.c
        tests/test_display_hdr.c
        tests/test_frame_coordinator.c
        tests/test_gl_shadow_present.c
        tests/test_gl1_replay.c
        tests/test_hash.c
        tests/test_metrics_policy.c
        tests/test_numeric.c
        tests/test_progress_policy.c)
    target_sources(
        driverbench_unit_tests
        PRIVATE tests/test_render_ir.c tests/test_render_ir_snapshot.c
                tests/test_run_session.c tests/test_sort.c)
    if(DB_BUILD_VULKAN AND DB_VULKAN_LIB)
        target_sources(driverbench_unit_tests PRIVATE tests/test_vk_scheduler.c)
    endif()
    db_apply_perf_options(driverbench_unit_tests)
    target_compile_definitions(driverbench_unit_tests
                               PRIVATE ${DB_DRIVERBENCH_DEFS})
    target_include_directories(
        driverbench_unit_tests PRIVATE ${CMAKE_SOURCE_DIR}/src
                                       ${CMAKE_SOURCE_DIR}/tests)
    target_link_libraries(
        driverbench_unit_tests
        PRIVATE driverbench_app driverbench_cli_core driverbench_render_core
                driverbench_core driverbench_app ${DB_DRIVERBENCH_LIBS})

    add_executable(driverbench_hash_conformance tests/hash_conformance.c)
    db_apply_perf_options(driverbench_hash_conformance)
    target_include_directories(driverbench_hash_conformance
                               PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(driverbench_hash_conformance PRIVATE driverbench_core)
    add_test(NAME hash_conformance_native_host
             COMMAND driverbench_hash_conformance auto)

    add_executable(driverbench_probe_process_tests tests/test_probe_process.c)
    db_apply_perf_options(driverbench_probe_process_tests)
    target_include_directories(driverbench_probe_process_tests
                               PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(driverbench_probe_process_tests
                          PRIVATE driverbench_core)
    foreach(
        db_probe_mode IN
        ITEMS none
              conforming
              count_failure
              nonconforming
              crash
              child_failure
              timeout
              postwrite_timeout
              malformed
              malformed_checksum
              malformed_repeat
              fragmented
              identity
              service)
        add_test(
            NAME probe_helper_${db_probe_mode}
            COMMAND driverbench_probe_process_tests
                    $<TARGET_FILE:driverbench_probe_helper> ${db_probe_mode})
        db_test_timeout_seconds(db_probe_process_timeout probe_process)
        set_tests_properties(
            probe_helper_${db_probe_mode}
            PROPERTIES LABELS "unit;probe" TIMEOUT
                       "${db_probe_process_timeout}")
    endforeach()
    set_tests_properties(hash_conformance_native_host PROPERTIES LABELS
                                                                 "unit;hash")

    find_program(DB_TEST_PYTHON_EXECUTABLE NAMES python3)
    if(DB_TEST_PYTHON_EXECUTABLE)
        add_test(
            NAME ctest_outcome_auditor
            COMMAND
                ${DB_TEST_PYTHON_EXECUTABLE}
                ${CMAKE_SOURCE_DIR}/tests/test_ctest_result_auditor.py
                ${CMAKE_SOURCE_DIR}/scripts/audit_ctest_results.py)
        set_tests_properties(ctest_outcome_auditor
                             PROPERTIES LABELS "unit;test_contract")
    else()
        message(FATAL_ERROR "CTest outcome auditing requires python3")
    endif()

    if(DB_ENABLE_SANITIZERS AND DB_TARGET_LINUX_32BIT)
        add_executable(driverbench_asan_activation
                       tests/sanitizer_asan_activation.c)
        add_executable(driverbench_ubsan_activation
                       tests/sanitizer_ubsan_activation.c)
        db_apply_perf_options(driverbench_asan_activation)
        db_apply_perf_options(driverbench_ubsan_activation)
        add_test(
            NAME i686_asan_runtime_smoke
            COMMAND
                ${CMAKE_COMMAND}
                -DTEST_BIN=$<TARGET_FILE:driverbench_asan_activation>
                -DTEST_PATTERN=AddressSanitizer -P
                ${CMAKE_SOURCE_DIR}/cmake/RunSanitizerActivationTest.cmake)
        add_test(
            NAME i686_ubsan_runtime_smoke
            COMMAND
                ${CMAKE_COMMAND}
                -DTEST_BIN=$<TARGET_FILE:driverbench_ubsan_activation>
                "-DTEST_PATTERN=runtime error: signed integer overflow" -P
                ${CMAKE_SOURCE_DIR}/cmake/RunSanitizerActivationTest.cmake)
        set_tests_properties(i686_asan_runtime_smoke
                             PROPERTIES LABELS "sanitizer;i686")
        set_tests_properties(i686_ubsan_runtime_smoke
                             PROPERTIES LABELS "sanitizer;i686")
        if(CMAKE_READELF)
            foreach(db_sanitizer_target IN ITEMS driverbench_asan_activation
                                                 driverbench_ubsan_activation)
                add_test(
                    NAME ${db_sanitizer_target}_elf32
                    COMMAND
                        ${CMAKE_COMMAND}
                        -DTEST_BIN=$<TARGET_FILE:${db_sanitizer_target}>
                        -DREADELF_BIN=${CMAKE_READELF} -P
                        ${CMAKE_SOURCE_DIR}/cmake/CheckElf32.cmake)
                set_tests_properties(${db_sanitizer_target}_elf32
                                     PROPERTIES LABELS "sanitizer;i686")
            endforeach()
        else()
            message(
                FATAL_ERROR
                    "i686 sanitizer validation requires a target readelf")
        endif()
    endif()

    db_test_timeout_seconds(db_qemu_hash_timeout qemu_hash)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64)$" AND CMAKE_SIZEOF_VOID_P
                                                             EQUAL 8)
        find_program(DB_QEMU_X86_64_EXECUTABLE NAMES qemu-x86_64)
        if(DB_QEMU_X86_64_EXECUTABLE)
            add_test(
                NAME hash_conformance_qemu_host_x86_64_sse2
                COMMAND
                    ${DB_QEMU_X86_64_EXECUTABLE} -cpu
                    qemu64,-sse3,-ssse3,-sse4.1,-sse4.2,-avx,-avx2
                    $<TARGET_FILE:driverbench_hash_conformance> sse2)
            add_test(
                NAME hash_conformance_qemu_host_x86_64_avx2
                COMMAND ${DB_QEMU_X86_64_EXECUTABLE} -cpu Haswell,-hle,-rtm
                        $<TARGET_FILE:driverbench_hash_conformance> avx2)
            set_tests_properties(
                hash_conformance_qemu_host_x86_64_sse2
                hash_conformance_qemu_host_x86_64_avx2
                PROPERTIES LABELS "unit;hash;qemu" TIMEOUT
                           "${db_qemu_hash_timeout}")
            if(DB_ENABLE_SANITIZERS AND CMAKE_BUILD_TYPE STREQUAL "Debug")
                # LeakSanitizer cannot inspect a process running under QEMU's
                # ptrace-like execution. ASan and UBSan remain enabled.
                set_tests_properties(
                    hash_conformance_qemu_host_x86_64_sse2
                    hash_conformance_qemu_host_x86_64_avx2
                    PROPERTIES
                        ENVIRONMENT
                        "ASAN_OPTIONS=detect_leaks=0;LSAN_OPTIONS=detect_leaks=0"
                )
            endif()
        else()
            message(
                STATUS
                    "QEMU hash SSE2/AVX2 tests skipped: qemu-x86_64 not found")
        endif()
    endif()

    find_program(DB_QEMU_HASH_PYTHON NAMES python3)
    if(DB_QEMU_HASH_PYTHON)
        foreach(db_qemu_hash_arch IN ITEMS aarch64 i686)
            if(db_qemu_hash_arch STREQUAL "aarch64")
                set(db_qemu_hash_test_name
                    hash_conformance_qemu_linux_aarch64_neon)
            else()
                set(db_qemu_hash_test_name
                    hash_conformance_qemu_linux_i686_sse2)
            endif()
            add_test(
                NAME ${db_qemu_hash_test_name}
                COMMAND
                    ${DB_QEMU_HASH_PYTHON}
                    ${CMAKE_SOURCE_DIR}/scripts/run_qemu_hash_conformance.py
                    --arch ${db_qemu_hash_arch} --source-root
                    ${CMAKE_SOURCE_DIR} --build-root
                    ${CMAKE_BINARY_DIR}/qemu-hash/${db_qemu_hash_arch})
            set_tests_properties(
                ${db_qemu_hash_test_name}
                PROPERTIES LABELS "unit;hash;qemu" SKIP_RETURN_CODE 77 TIMEOUT
                           "${db_qemu_hash_timeout}")
        endforeach()
    else()
        message(STATUS "QEMU hash cross tests skipped: python3 not found")
    endif()

endfunction()
