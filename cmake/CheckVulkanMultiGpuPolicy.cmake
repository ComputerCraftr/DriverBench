if(NOT DEFINED SOURCE_ROOT)
    message(FATAL_ERROR "SOURCE_ROOT is required")
endif()

set(db_vk_root "${SOURCE_ROOT}/src/renderers/vulkan_1_2_multi_gpu")
file(GLOB db_vk_sources "${db_vk_root}/*.c" "${db_vk_root}/*.h")
foreach(db_source IN LISTS db_vk_sources)
    file(READ "${db_source}" db_text)
    foreach(
        db_forbidden IN
        ITEMS db_vk_compute_lane_bands
              db_vk_clip_block_to_lane_band
              opaque_fd_device_uuid_mismatch
              VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT
              "db_vk_multi_gpu_phase_t multi_gpu_phase;")
        string(FIND "${db_text}" "${db_forbidden}" db_match)
        if(NOT db_match EQUAL -1)
            message(
                FATAL_ERROR
                    "Forbidden Vulkan multi-GPU pattern '${db_forbidden}' in ${db_source}"
            )
        endif()
    endforeach()
endforeach()

set(db_interop "")
foreach(db_source IN LISTS db_vk_sources)
    get_filename_component(db_name "${db_source}" NAME)
    if(db_name MATCHES "^vk_(interop|buffer_transport)")
        file(READ "${db_source}" db_interop_part)
        string(APPEND db_interop "\n${db_interop_part}")
    endif()
endforeach()
foreach(
    db_required IN
    ITEMS VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT
          VK_SEMAPHORE_IMPORT_TEMPORARY_BIT
          VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT)
    string(FIND "${db_interop}" "${db_required}" db_match)
    if(db_match EQUAL -1)
        message(
            FATAL_ERROR
                "Required asynchronous Linux interop contract '${db_required}' is missing"
        )
    endif()
endforeach()

file(READ "${db_vk_root}/vk_device_group.c" db_device_group)
string(FIND "${db_device_group}" "VK_PEER_MEMORY_FEATURE_GENERIC_SRC_BIT"
            db_peer_read)
if(db_peer_read EQUAL -1)
    message(FATAL_ERROR "Device-group targets must validate primary peer reads")
endif()

file(READ "${db_vk_root}/vk_runtime_frame.c" db_runtime)
string(FIND "${db_runtime}" "frame_index & 1U" db_alternating_calibration)
if(NOT db_alternating_calibration EQUAL -1)
    message(
        FATAL_ERROR
            "Calibration must use paired same-plan samples, not alternating frames"
    )
endif()
