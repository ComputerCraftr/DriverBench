include("${CMAKE_CURRENT_LIST_DIR}/TestRunnerCommon.cmake")

db_test_require_defined(TEST_BIN)
set(common_args
    "--api vulkan --display glfw_window --glfw-hidden-window 1 --benchmark-mode snake_grid --frame-limit 1"
)
db_test_run_command(
    level1_output level1_skip level1_status "${common_args} --trace-vulkan 1"
    "Vulkan trace-level-1 command failed")
if(NOT "${level1_skip}" STREQUAL "")
    message(STATUS "Vulkan trace-level test skipped: ${level1_skip}")
    return()
endif()
if(level1_output
   MATCHES
   " event=(vk_memory_type_candidate|vk_memory_plane_layout|vk_memory_transport_attempt|vk_calibration_pair|vk_split_search_sample) "
)
    message(
        FATAL_ERROR "Vulkan level 1 emitted level-2 detail:\n${level1_output}")
endif()
if(NOT level1_output MATCHES " event=vk_execution_plan ")
    message(FATAL_ERROR "Vulkan level 1 emitted no execution-plan summary")
endif()

db_test_run_command(
    level2_output level2_skip level2_status "${common_args} --trace-vulkan 2"
    "Vulkan trace-level-2 command failed")
if(NOT "${level2_skip}" STREQUAL "")
    message(STATUS "Vulkan level-2 detail unavailable: ${level2_skip}")
    return()
endif()
if(NOT
   level2_output
   MATCHES
   " event=(vk_memory_type_candidate|vk_memory_plane_layout|vk_memory_transport_attempt|vk_calibration_pair|vk_split_search_sample) "
)
    message(STATUS "Vulkan level 2 had no capability-conditional detail")
    return()
endif()
message(STATUS "Vulkan trace levels are correctly bounded")
