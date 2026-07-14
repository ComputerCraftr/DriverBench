include("${CMAKE_CURRENT_LIST_DIR}/TestRunnerCommon.cmake")

if(NOT DEFINED TEST_BIN OR NOT DEFINED TEST_KIND)
    message(
        FATAL_ERROR
            "automatic qualification test requires TEST_BIN and TEST_KIND")
endif()

set(db_common_args
    --benchmark-mode
    gradient_fill
    --random-seed
    123456
    --bench-speed
    1
    --frame-limit
    2
    --working-format
    rgba16f
    --hash
    pixel)
execute_process(
    COMMAND "${TEST_BIN}" --api cpu --display offscreen ${db_common_args}
    RESULT_VARIABLE db_reference_result
    OUTPUT_VARIABLE db_reference_stdout
    ERROR_VARIABLE db_reference_stderr)
set(db_reference "${db_reference_stdout}\n${db_reference_stderr}")
if(NOT db_reference_result EQUAL 0)
    message(FATAL_ERROR "CPU qualification reference failed:\n${db_reference}")
endif()

if(TEST_KIND STREQUAL "gl3_semantic")
    set(db_mode conforming)
    set(db_expected "gradient_path=gl3_semantic_gradient")
    set(db_args
        --api
        opengl
        --renderer
        gl3_3
        --display
        offscreen
        --rerun-conformance-probe
        1
        ${db_common_args})
elseif(TEST_KIND STREQUAL "vulkan_exact")
    set(db_mode topology_exact)
    set(db_expected "gradient_path=vulkan_exact_lookup")
    set(db_args
        --api
        vulkan
        --display
        glfw_window
        --glfw-hidden-window
        1
        --rerun-conformance-probe
        1
        ${db_common_args})
else()
    message(
        FATAL_ERROR "unknown automatic qualification test kind: ${TEST_KIND}")
endif()

execute_process(
    COMMAND
        ${CMAKE_COMMAND} -E env
        DRIVERBENCH_PROBE_CACHE_DIR=${CMAKE_CURRENT_BINARY_DIR}/qualification-cache-${TEST_KIND}
        DRIVERBENCH_PROBE_HELPER_TEST_MODE=${db_mode} "${TEST_BIN}" ${db_args}
    RESULT_VARIABLE db_result
    OUTPUT_VARIABLE db_stdout
    ERROR_VARIABLE db_stderr)
set(db_output "${db_stdout}\n${db_stderr}")
if(NOT db_result EQUAL 0)
    if(db_output MATCHES "${DB_TEST_GLFW_ENV_SKIP_REGEX}")
        db_test_report_skip("qualification_hardware_unavailable"
                            "automatic_qualification")
        return()
    endif()
    message(FATAL_ERROR "automatic qualification run failed:\n${db_output}")
endif()

foreach(db_field IN
        ITEMS "${db_expected}" "qualified=true" "diagnostic_forced=false"
              "qualification_source=helper")
    if(NOT db_output MATCHES "${db_field}")
        message(FATAL_ERROR "missing '${db_field}':\n${db_output}")
    endif()
endforeach()

string(REGEX MATCH "framebuffer_hash_final=0x[0-9a-fA-F]+" db_reference_hash
             "${db_reference}")
string(REGEX MATCH "framebuffer_hash_final=0x[0-9a-fA-F]+" db_actual_hash
             "${db_output}")
if(db_reference_hash STREQUAL ""
   OR db_actual_hash STREQUAL ""
   OR NOT db_reference_hash STREQUAL db_actual_hash)
    message(
        FATAL_ERROR
            "automatic qualification hash mismatch: reference=${db_reference_hash} actual=${db_actual_hash}\n${db_output}"
    )
endif()
