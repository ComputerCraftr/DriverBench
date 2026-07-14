include("${CMAKE_CURRENT_LIST_DIR}/TestRunnerCommon.cmake")

db_test_require_defined(TEST_BIN)

set(common_args
    "--random-seed 123456 --working-format rgba16f --output-format sdr --bench-speed 20 --frame-limit 2 --fps-cap 0 --hash both --hash-report final"
)

foreach(benchmark_mode IN ITEMS gradient_fill snake_rect)
    db_test_run_command(
        cpu_output
        cpu_skip
        cpu_status
        "--api cpu --display offscreen --benchmark-mode ${benchmark_mode} ${common_args}"
        "CPU persistent-geometry reference failed")
    db_test_extract_hash_or_fail(cpu_output state_hash_final expected_state)
    db_test_extract_hash_or_fail(cpu_output framebuffer_hash_final
                                 expected_framebuffer)

    foreach(repetition RANGE 1 5)
        db_test_run_command(
            gl3_output
            gl3_skip
            gl3_status
            "--api opengl --renderer gl3_3 --display offscreen --vsync 0 --backbuffer-draw-mode dirty --benchmark-mode ${benchmark_mode} ${common_args}"
            "GL3 persistent-geometry repetition ${repetition} failed")
        if(NOT "${gl3_skip}" STREQUAL "")
            return()
        endif()
        if(repetition EQUAL 1
           AND NOT gl3_output MATCHES
               "role=rectangle_geometry[^\r\n]*sync_enabled=true")
            message(
                FATAL_ERROR
                    "GL3 persistent geometry stream did not enable bounded reuse synchronization:\n${gl3_output}"
            )
        endif()
        db_test_extract_hash_or_fail(gl3_output state_hash_final actual_state)
        db_test_extract_hash_or_fail(gl3_output framebuffer_hash_final
                                     actual_framebuffer)
        if(NOT actual_state STREQUAL expected_state
           OR NOT actual_framebuffer STREQUAL expected_framebuffer)
            message(
                FATAL_ERROR
                    "GL3 persistent geometry reuse diverged: benchmark=${benchmark_mode} repetition=${repetition}\n"
                    "state expected=${expected_state} actual=${actual_state}\n"
                    "framebuffer expected=${expected_framebuffer} actual=${actual_framebuffer}\n${gl3_output}"
            )
        endif()
    endforeach()
endforeach()

message(STATUS "GL3 persistent geometry reuse remained deterministic")
