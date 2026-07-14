include("${CMAKE_CURRENT_LIST_DIR}/TestRunnerCommon.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/DriverBenchCanonicalGoldens.cmake")

db_test_require_defined(TEST_BIN)
db_test_require_defined(TEST_BENCHMARK)
db_test_default_string(TEST_GLFW_ENABLED "OFF")
db_test_default_string(TEST_VULKAN_ENABLED "OFF")
db_test_default_string(TEST_CAPTURE_GOLDEN "OFF")

if(NOT DB_CANONICAL_GOLDEN_SCHEMA EQUAL 1)
    message(FATAL_ERROR "Unsupported canonical golden schema")
endif()
if(NOT DEFINED DB_CANONICAL_GOLDEN_${TEST_BENCHMARK}_LOW_STATE)
    message(FATAL_ERROR "No canonical golden for '${TEST_BENCHMARK}'")
endif()

function(db_matrix_extract_hash output kind key out_hash)
    string(REPLACE "\r" "" normalized "${output}")
    string(REPLACE "\n" ";" lines "${normalized}")
    set(matches "")
    foreach(line IN LISTS lines)
        if(line MATCHES "(^| )event=hash_result( |$)"
           AND line MATCHES "(^| )kind=${kind}( |$)")
            list(APPEND matches "${line}")
        endif()
    endforeach()
    list(LENGTH matches match_count)
    if(NOT match_count EQUAL 1)
        message(
            FATAL_ERROR
                "Expected exactly one ${kind} hash event, found ${match_count}\n${output}"
        )
    endif()
    list(GET matches 0 hash_line)
    if(NOT hash_line MATCHES "(^| )${key}=(0x[0-9a-fA-F]+)( |$)")
        message(FATAL_ERROR "Malformed ${kind} hash event:\n${hash_line}")
    endif()
    string(TOLOWER "${CMAKE_MATCH_2}" hash_value)
    set(${out_hash}
        "${hash_value}"
        PARENT_SCOPE)
endfunction()

function(
    db_matrix_run
    checkpoint
    scenario
    scenario_args
    speed
    frames
    expected_state
    expected_framebuffer)
    set(common_args
        "--benchmark-mode ${TEST_BENCHMARK} --random-seed ${DB_CANONICAL_GOLDEN_SEED} --working-format ${DB_CANONICAL_GOLDEN_WORKING_FORMAT} --output-format ${DB_CANONICAL_GOLDEN_OUTPUT_FORMAT} --bench-speed ${speed} --frame-limit ${frames} --fps-cap 0 --hash both --hash-report final"
    )
    db_test_run_command(
        output skip_reason run_status "${scenario_args} ${common_args}"
        "Canonical matrix scenario '${scenario}' failed")
    if(NOT "${skip_reason}" STREQUAL "")
        set_property(GLOBAL PROPERTY DB_MATRIX_CAPABILITY_SKIPPED TRUE)
        return()
    endif()
    db_matrix_extract_hash("${output}" state_hash state_hash_final actual_state)
    db_matrix_extract_hash("${output}" framebuffer_hash framebuffer_hash_final
                           actual_framebuffer)
    if(TEST_CAPTURE_GOLDEN)
        string(TOUPPER "${checkpoint}" checkpoint_key)
        get_property(reference_state GLOBAL
                     PROPERTY DB_MATRIX_${checkpoint_key}_STATE)
        get_property(reference_framebuffer GLOBAL
                     PROPERTY DB_MATRIX_${checkpoint_key}_FRAMEBUFFER)
        if("${reference_state}" STREQUAL "")
            set_property(GLOBAL PROPERTY DB_MATRIX_${checkpoint_key}_STATE
                                         "${actual_state}")
            set_property(GLOBAL PROPERTY DB_MATRIX_${checkpoint_key}_FRAMEBUFFER
                                         "${actual_framebuffer}")
            return()
        endif()
        set(expected_state "${reference_state}")
        set(expected_framebuffer "${reference_framebuffer}")
    endif()
    string(TOLOWER "${expected_state}" expected_state_normalized)
    string(TOLOWER "${expected_framebuffer}" expected_framebuffer_normalized)
    if(NOT actual_state STREQUAL expected_state_normalized
       OR NOT actual_framebuffer STREQUAL expected_framebuffer_normalized)
        message(
            FATAL_ERROR
                "Canonical determinism divergence: benchmark=${TEST_BENCHMARK} scenario=${scenario} speed=${speed} frames=${frames}\n"
                "state expected=${expected_state_normalized} actual=${actual_state}\n"
                "framebuffer expected=${expected_framebuffer_normalized} actual=${actual_framebuffer}\n${output}"
        )
    endif()
endfunction()

function(db_matrix_run_strategies checkpoint expected_state
         expected_framebuffer schedule_speed schedule_frames)
    set(suffix "${checkpoint}_speed${schedule_speed}_frames${schedule_frames}")
    db_matrix_run(
        "${checkpoint}"
        "cpu_offscreen_${suffix}"
        "--api cpu --display offscreen"
        ${schedule_speed}
        ${schedule_frames}
        "${expected_state}"
        "${expected_framebuffer}")
    if(TEST_GLFW_ENABLED)
        db_matrix_run(
            "${checkpoint}"
            "cpu_glfw_${suffix}"
            "--api cpu --display glfw_window --glfw-hidden-window 1 --vsync 0"
            ${schedule_speed}
            ${schedule_frames}
            "${expected_state}"
            "${expected_framebuffer}")
        db_matrix_run(
            "${checkpoint}"
            "gl1_offscreen_dirty_${suffix}"
            "--api opengl --renderer gl1_5_gles1_1 --display offscreen --vsync 0 --backbuffer-draw-mode dirty"
            ${schedule_speed}
            ${schedule_frames}
            "${expected_state}"
            "${expected_framebuffer}")
        db_matrix_run(
            "${checkpoint}"
            "gl1_offscreen_full_${suffix}"
            "--api opengl --renderer gl1_5_gles1_1 --display offscreen --vsync 0 --backbuffer-draw-mode full"
            ${schedule_speed}
            ${schedule_frames}
            "${expected_state}"
            "${expected_framebuffer}")
        db_matrix_run(
            "${checkpoint}"
            "gl1_glfw_dirty_single_source_${suffix}"
            "--api opengl --renderer gl1_5_gles1_1 --display glfw_window --glfw-hidden-window 1 --vsync 0 --backbuffer-draw-mode dirty --present-buffer-mode single_source"
            ${schedule_speed}
            ${schedule_frames}
            "${expected_state}"
            "${expected_framebuffer}")
        db_matrix_run(
            "${checkpoint}"
            "gl1_glfw_dirty_ring_${suffix}"
            "--api opengl --renderer gl1_5_gles1_1 --display glfw_window --glfw-hidden-window 1 --vsync 0 --backbuffer-draw-mode dirty --present-buffer-mode ring"
            ${schedule_speed}
            ${schedule_frames}
            "${expected_state}"
            "${expected_framebuffer}")
        db_matrix_run(
            "${checkpoint}"
            "gl1_glfw_full_ring_${suffix}"
            "--api opengl --renderer gl1_5_gles1_1 --display glfw_window --glfw-hidden-window 1 --vsync 0 --backbuffer-draw-mode full --present-buffer-mode ring"
            ${schedule_speed}
            ${schedule_frames}
            "${expected_state}"
            "${expected_framebuffer}")
        foreach(gl3_display IN ITEMS offscreen glfw_window)
            set(gl3_visibility "")
            if(gl3_display STREQUAL "glfw_window")
                set(gl3_visibility "--glfw-hidden-window 1")
            endif()
            foreach(draw_mode IN ITEMS dirty full)
                db_matrix_run(
                    "${checkpoint}"
                    "gl3_${gl3_display}_${draw_mode}_${suffix}"
                    "--api opengl --renderer gl3_3 --display ${gl3_display} ${gl3_visibility} --vsync 0 --backbuffer-draw-mode ${draw_mode}"
                    ${schedule_speed}
                    ${schedule_frames}
                    "${expected_state}"
                    "${expected_framebuffer}")
            endforeach()
        endforeach()
        if(TEST_VULKAN_ENABLED)
            foreach(draw_mode IN ITEMS dirty full)
                db_matrix_run(
                    "${checkpoint}"
                    "vulkan_glfw_${draw_mode}_${suffix}"
                    "--api vulkan --display glfw_window --glfw-hidden-window 1 --vsync 0 --vk-multi-device-policy auto --backbuffer-draw-mode ${draw_mode}"
                    ${schedule_speed}
                    ${schedule_frames}
                    "${expected_state}"
                    "${expected_framebuffer}")
            endforeach()
        endif()
    endif()
endfunction()

set(low_state "${DB_CANONICAL_GOLDEN_${TEST_BENCHMARK}_LOW_STATE}")
set(low_framebuffer "${DB_CANONICAL_GOLDEN_${TEST_BENCHMARK}_LOW_FRAMEBUFFER}")

# Speed associativity belongs to benchmark planning, so exercise all equal-work
# schedules once through the canonical CPU surface instead of multiplying them
# by every renderer and presentation strategy.
db_matrix_run(
    low
    cpu_offscreen_low_speed1_frames40
    "--api cpu --display offscreen"
    1
    40
    "${low_state}"
    "${low_framebuffer}")
db_matrix_run(
    low
    cpu_offscreen_low_speed20_frames2
    "--api cpu --display offscreen"
    20
    2
    "${low_state}"
    "${low_framebuffer}")
db_matrix_run(
    low
    cpu_offscreen_low_speed40_frames1
    "--api cpu --display offscreen"
    40
    1
    "${low_state}"
    "${low_framebuffer}")

# Every strategy still validates startup/dirty history at a low-frame checkpoint
# and validates the benchmark's long canonical checkpoint, without a redundant
# speed-by-backend cross product.
db_matrix_run_strategies(low "${low_state}" "${low_framebuffer}" 20 2)
db_matrix_run_strategies(
    long
    "${DB_CANONICAL_GOLDEN_${TEST_BENCHMARK}_LONG_STATE}"
    "${DB_CANONICAL_GOLDEN_${TEST_BENCHMARK}_LONG_FRAMEBUFFER}"
    "${DB_CANONICAL_GOLDEN_${TEST_BENCHMARK}_LONG_SPEED}"
    "${DB_CANONICAL_GOLDEN_${TEST_BENCHMARK}_LONG_FRAMES}")

get_property(db_matrix_capability_skipped GLOBAL
             PROPERTY DB_MATRIX_CAPABILITY_SKIPPED)
if(db_matrix_capability_skipped)
    db_test_report_skip("matrix_capability_unavailable" "cross_renderer_matrix")
    return()
endif()

if(TEST_CAPTURE_GOLDEN)
    get_property(low_state GLOBAL PROPERTY DB_MATRIX_LOW_STATE)
    get_property(low_framebuffer GLOBAL PROPERTY DB_MATRIX_LOW_FRAMEBUFFER)
    get_property(long_state GLOBAL PROPERTY DB_MATRIX_LONG_STATE)
    get_property(long_framebuffer GLOBAL PROPERTY DB_MATRIX_LONG_FRAMEBUFFER)
    message(
        STATUS
            "canonical_golden benchmark=${TEST_BENCHMARK} low_state=${low_state} low_framebuffer=${low_framebuffer} long_state=${long_state} long_framebuffer=${long_framebuffer}"
    )
endif()

message(STATUS "Canonical determinism matrix passed: ${TEST_BENCHMARK}")
