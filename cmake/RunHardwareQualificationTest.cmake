include("${CMAKE_CURRENT_LIST_DIR}/TestRunnerCommon.cmake")

db_test_require_defined(TEST_BIN)
db_test_require_defined(TEST_KIND)

set(common_args
    "--benchmark-mode gradient_fill --random-seed 123456 --output-format sdr --fps-cap 0 --hash pixel"
)
if(TEST_KIND STREQUAL "gl1_direct_window")
    set(args
        "--api opengl --renderer gl1_5_gles1_1 --display glfw_window --glfw-hidden-window 1 --working-format rgba8 --gl1-target auto --frame-limit 24 ${common_args}"
    )
    set(required
        "target_strategy=gl1_direct_window;qualified=true;diagnostic_forced=false"
    )
    set(capability "gl1_direct_window")
elseif(TEST_KIND STREQUAL "gl3_native")
    set(args
        "--api opengl --renderer gl3_3 --display offscreen --working-format rgba16f --gl3-gradient auto --frame-limit 24 ${common_args}"
    )
    set(required "qualified=true;diagnostic_forced=false")
    set(path_regex "gradient_path=gl3_(semantic_gradient|exact_lookup)")
    set(capability "gl3_native")
elseif(TEST_KIND STREQUAL "vulkan_single")
    set(args
        "--api vulkan --display glfw_window --glfw-hidden-window 1 --working-format rgba16f --vk-multi-device-policy group_only --vk-gradient auto --frame-limit 24 ${common_args}"
    )
    set(required "qualified=true;diagnostic_forced=false")
    set(path_regex
        "gradient_path=vulkan_(semantic_gradient|exact_lookup|row_instances)")
    set(capability "vulkan_single_device")
elseif(TEST_KIND STREQUAL "vulkan_device_group")
    set(args
        "--api vulkan --display glfw_window --glfw-hidden-window 1 --working-format rgba16f --vk-multi-device-policy group_only --vk-gradient auto --frame-limit 80 ${common_args}"
    )
    set(required "qualified=true;diagnostic_forced=false")
    set(path_regex "execution_mode=device_group")
    set(calibration_regex
        "event=vk_multi_gpu_phase[^\r\n]*to=(validated|active)")
    set(capability "vulkan_device_group")
elseif(TEST_KIND STREQUAL "vulkan_independent")
    set(args
        "--api vulkan --display glfw_window --glfw-hidden-window 1 --working-format rgba16f --vk-multi-device-policy independent_ok --vk-gradient auto --frame-limit 80 ${common_args}"
    )
    set(required "qualified=true;diagnostic_forced=false")
    set(path_regex "independent_topology_available=true")
    set(calibration_regex
        "event=vk_multi_gpu_phase[^\r\n]*to=(validated|active)")
    set(capability "vulkan_independent")
else()
    message(FATAL_ERROR "Unknown hardware qualification kind: ${TEST_KIND}")
endif()

db_test_run_command(output skip_reason status "${args}"
                    "Hardware qualification run failed")
if(NOT "${skip_reason}" STREQUAL "")
    return()
endif()

set(path_selected TRUE)
foreach(field IN LISTS required)
    if(NOT output MATCHES "${field}")
        set(path_selected FALSE)
    endif()
endforeach()
if(DEFINED path_regex AND NOT output MATCHES "${path_regex}")
    set(path_selected FALSE)
endif()
if(DEFINED calibration_regex AND NOT output MATCHES "${calibration_regex}")
    set(path_selected FALSE)
endif()
if(NOT path_selected)
    if(TEST_KIND STREQUAL "vulkan_device_group" OR TEST_KIND STREQUAL
                                                   "vulkan_independent")
        if(TEST_KIND STREQUAL "vulkan_independent"
           AND output MATCHES "reason=measured_no_benefit"
           AND output MATCHES "independent_topology_available=true"
           AND output MATCHES "qualified=true"
           AND output MATCHES "diagnostic_forced=false"
           AND output MATCHES "event=vk_calibration_pair[^\r\n]*match=true"
           AND NOT output MATCHES
               "event=vk_calibration_pair[^\r\n]*match=false")
            message(
                STATUS
                    "Independent topology calibrated correctly and was rejected by automatic performance policy"
            )
            return()
        endif()
        if(TEST_KIND STREQUAL "vulkan_device_group"
           AND output MATCHES "device_group_available=false")
            db_test_report_skip("device_group_unavailable" "${capability}")
            return()
        endif()
        if(TEST_KIND STREQUAL "vulkan_independent"
           AND output MATCHES "independent_topology_available=false")
            db_test_report_skip("independent_topology_unavailable"
                                "${capability}")
            return()
        endif()
    endif()
    message(
        FATAL_ERROR
            "Required implementation path was not selected for capability=${capability}.\nObserved output:\n${output}"
    )
endif()

message(STATUS "Required hardware path executed: ${capability}")
