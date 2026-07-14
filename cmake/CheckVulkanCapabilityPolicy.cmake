if(NOT DEFINED SOURCE_ROOT)
    message(FATAL_ERROR "SOURCE_ROOT is required")
endif()

set(db_selection_file
    "${SOURCE_ROOT}/src/renderers/vulkan_1_2_multi_gpu/vk_init_selection.c")
file(READ "${db_selection_file}" db_selection_source)

set(db_violations "")
if(db_selection_source
   MATCHES
   "db_vk_probe_external_image_interop[^;]+R16G16B16A16[^;]+db_vk_probe_external_image_interop[^;]+R8G8B8A8"
)
    list(
        APPEND db_violations
        "external-image eligibility must probe only the resolved working format"
    )
endif()
if(db_selection_source MATCHES
   "db_vk_find_common_drm_modifier[^;]+VK_FORMAT_R16G16B16A16_SFLOAT")
    list(APPEND db_violations
         "DRM modifier selection must use the resolved working format")
endif()

set(db_capability_file
    "${SOURCE_ROOT}/src/renderers/vulkan_1_2_multi_gpu/vk_init_capabilities.c")
file(READ "${db_capability_file}" db_capability_source)
if(db_capability_source
   MATCHES
   "VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT[ \t\r\n]*\\|[ \t\r\n]*VK_IMAGE_USAGE_SAMPLED_BIT"
)
    list(
        APPEND
        db_violations
        "external-image probes must use separate worker-export and primary-import usage contracts"
    )
endif()

if(db_violations)
    list(JOIN db_violations "\n  " db_violation_text)
    message(
        FATAL_ERROR
            "Vulkan capability policy violations:\n  ${db_violation_text}")
endif()
