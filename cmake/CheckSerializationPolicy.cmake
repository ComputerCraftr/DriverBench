if(NOT DEFINED SOURCE_ROOT)
    message(FATAL_ERROR "SOURCE_ROOT is required")
endif()

set(DB_SERIALIZATION_FILES
    "src/core/db_probe_protocol.c"
    "src/core/db_conformance_cache.c"
    "src/core/db_conformance_service.c"
    "src/core/db_gradient_divergence.c"
    "src/probe_helper_main.c"
    "src/renderers/vulkan_1_2_multi_gpu/vk_init_selection.c")
set(DB_DUPLICATE_CODEC_PATTERNS
    "static[ \t\r\n]+void[ \t\r\n]+put_u(32|64)"
    "static[ \t\r\n]+void[ \t\r\n]+encode_u(32|64)_le"
    "static[ \t\r\n]+u?int(32|64)_t[ \t\r\n]+(get|decode)_u(32|64)"
    "static[ \t\r\n]+int[ \t\r\n]+(read|write)_complete"
    "0123456789abcdef")

set(db_violations "")
foreach(db_file IN LISTS DB_SERIALIZATION_FILES)
    set(db_path "${SOURCE_ROOT}/${db_file}")
    if(NOT EXISTS "${db_path}")
        list(APPEND db_violations "${db_file}: required policy input missing")
        continue()
    endif()
    file(READ "${db_path}" db_content)
    foreach(db_pattern IN LISTS DB_DUPLICATE_CODEC_PATTERNS)
        if(db_content MATCHES "${db_pattern}")
            list(APPEND db_violations
                 "${db_file}: duplicate byte/pixel codec '${db_pattern}'")
        endif()
    endforeach()
endforeach()

file(GLOB DB_VULKAN_POLICY_FILES
     "${SOURCE_ROOT}/src/renderers/vulkan_1_2_multi_gpu/*.c")
foreach(db_path IN LISTS DB_VULKAN_POLICY_FILES)
    file(RELATIVE_PATH db_file "${SOURCE_ROOT}" "${db_path}")
    file(READ "${db_path}" db_content)
    if(db_content
       MATCHES
       "DB_PIXEL_FORMAT_RGBA16F[^;\\n]*\\?[^;\\n]*(8U|2U)[^;\\n]*:[^;\\n]*(4U|1U)"
    )
        list(
            APPEND
            db_violations
            "${db_file}: pixel format sizing must use db_pixel_format_*_per_pixel"
        )
    endif()
endforeach()

if(db_violations)
    list(JOIN db_violations "\n  " db_violation_text)
    message(
        FATAL_ERROR
            "Serialization policy violations:\n  ${db_violation_text}\nUse src/core/db_byte_codec.h and db_rgba8_pixel_diff()."
    )
endif()
