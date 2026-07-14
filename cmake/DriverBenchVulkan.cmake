if(DB_BUILD_VULKAN)
    option(DB_VK_TEST_FORCE_BUFFER_TRANSPORT
           "Force DMA-BUF buffer transport in internal test builds" OFF)
    mark_as_advanced(DB_VK_TEST_FORCE_BUFFER_TRANSPORT)
    find_package(Vulkan QUIET)
    find_program(
        GLSLC glslc
        HINTS ENV VULKAN_SDK
        PATH_SUFFIXES Bin bin)

    set(DB_VULKAN_LIB "")
    if(TARGET Vulkan::Vulkan)
        set(DB_VULKAN_LIB Vulkan::Vulkan)
    elseif(Vulkan_FOUND AND Vulkan_LIBRARY)
        set(DB_VULKAN_LIB "${Vulkan_LIBRARY}")
    endif()

    if(DB_LINUX_REQUIRE_STATIC_ARCHIVES AND DB_VULKAN_LIB)
        set(DB_VULKAN_LOCATION "")
        if(TARGET Vulkan::Vulkan)
            get_target_property(DB_VULKAN_IMPORTED_LOCATION Vulkan::Vulkan
                                IMPORTED_LOCATION)
            if(DB_VULKAN_IMPORTED_LOCATION)
                set(DB_VULKAN_LOCATION "${DB_VULKAN_IMPORTED_LOCATION}")
            else()
                get_target_property(DB_VULKAN_IMPORTED_CONFIGS Vulkan::Vulkan
                                    IMPORTED_CONFIGURATIONS)
                foreach(DB_VK_CFG IN LISTS DB_VULKAN_IMPORTED_CONFIGS)
                    string(TOUPPER "${DB_VK_CFG}" DB_VK_CFG_UPPER)
                    get_target_property(
                        DB_VULKAN_IMPORTED_LOCATION_CFG Vulkan::Vulkan
                        IMPORTED_LOCATION_${DB_VK_CFG_UPPER})
                    if(DB_VULKAN_IMPORTED_LOCATION_CFG)
                        set(DB_VULKAN_LOCATION
                            "${DB_VULKAN_IMPORTED_LOCATION_CFG}")
                        break()
                    endif()
                endforeach()
            endif()
        else()
            set(DB_VULKAN_LOCATION "${DB_VULKAN_LIB}")
        endif()

        if(DB_VULKAN_LOCATION MATCHES "\\.so(\\.|$)")
            message(
                WARNING
                    "Linux static-link mode requires archive Vulkan linkage, but Vulkan resolves to "
                    "shared library '${DB_VULKAN_LOCATION}'. Disabling Vulkan renderer "
                    "for this build.")
            set(DB_VULKAN_LIB "")
        endif()
    endif()

    if(DB_BUILD_GLFW_WINDOW_DISPLAY
       AND DB_GLFW_LINK_LIB
       AND Vulkan_FOUND
       AND DB_VULKAN_LIB
       AND GLSLC)
        list(
            APPEND
            DB_APP_SOURCES
            src/renderers/vulkan_1_2_multi_gpu/vk_renderer.c
            src/renderers/vulkan_1_2_multi_gpu/vk_frame.c
            src/renderers/vulkan_1_2_multi_gpu/vk_init.c
            src/renderers/vulkan_1_2_multi_gpu/vk_init_device.c
            src/renderers/vulkan_1_2_multi_gpu/vk_init_phases.c
            src/renderers/vulkan_1_2_multi_gpu/vk_init_capabilities.c
            src/renderers/vulkan_1_2_multi_gpu/vk_init_selection.c
            src/renderers/vulkan_1_2_multi_gpu/vk_selection_diagnostics.c
            src/renderers/vulkan_1_2_multi_gpu/vk_interop.c
            src/renderers/vulkan_1_2_multi_gpu/vk_interop_memory.c
            src/renderers/vulkan_1_2_multi_gpu/vk_interop_execution.c
            src/renderers/vulkan_1_2_multi_gpu/vk_buffer_plan.c
            src/renderers/vulkan_1_2_multi_gpu/vk_buffer_transport.c
            src/renderers/vulkan_1_2_multi_gpu/vk_calibration.c
            src/renderers/vulkan_1_2_multi_gpu/vk_piece_plan.c
            src/renderers/vulkan_1_2_multi_gpu/vk_qualification.c
            src/renderers/vulkan_1_2_multi_gpu/vk_device_group.c
            src/renderers/vulkan_1_2_multi_gpu/vk_runtime.c
            src/renderers/vulkan_1_2_multi_gpu/vk_runtime_frame.c
            src/renderers/vulkan_1_2_multi_gpu/vk_frame_finalize.c
            src/renderers/vulkan_1_2_multi_gpu/vk_runtime_metrics.c
            src/renderers/vulkan_1_2_multi_gpu/vk_wait.c
            src/renderers/vulkan_1_2_multi_gpu/vk_scheduler.c
            src/renderers/vulkan_1_2_multi_gpu/vk_swapchain.c)
        list(APPEND DB_DRIVERBENCH_LIBS ${DB_VULKAN_LIB})
        list(APPEND DB_DRIVERBENCH_DEFS DB_HAS_VULKAN_API=1)
        if(DB_VK_TEST_FORCE_BUFFER_TRANSPORT)
            list(APPEND DB_DRIVERBENCH_DEFS DB_VK_TEST_FORCE_BUFFER_TRANSPORT=1)
        endif()

        set(DB_VK_SHADER_MANIFEST
            "IR_EXECUTE_VERT|ir_execute|vert"
            "IR_EXECUTE_FRAG|ir_execute|frag"
            "PRESENTATION_VERT|presentation|vert"
            "PRESENTATION_FRAG|presentation|frag"
            "TRANSPORT_PACK_COMP|transport_pack|comp"
            "TRANSPORT_UNPACK_FRAG|transport_unpack|frag")
        set(DB_VK_SHADER_KEYS "")
        set(DB_VK_SHADER_ROLE_STAGES "")
        foreach(DB_VK_SHADER_ENTRY IN LISTS DB_VK_SHADER_MANIFEST)
            string(REPLACE "|" ";" DB_VK_SHADER_FIELDS "${DB_VK_SHADER_ENTRY}")
            list(LENGTH DB_VK_SHADER_FIELDS DB_VK_SHADER_FIELD_COUNT)
            if(NOT DB_VK_SHADER_FIELD_COUNT EQUAL 3)
                message(
                    FATAL_ERROR
                        "Invalid Vulkan shader manifest entry: ${DB_VK_SHADER_ENTRY}"
                )
            endif()
            list(GET DB_VK_SHADER_FIELDS 0 DB_VK_SHADER_KEY)
            list(GET DB_VK_SHADER_FIELDS 1 DB_VK_SHADER_ROLE)
            list(GET DB_VK_SHADER_FIELDS 2 DB_VK_SHADER_STAGE)
            set(DB_VK_SHADER_ROLE_STAGE
                "${DB_VK_SHADER_ROLE}|${DB_VK_SHADER_STAGE}")
            if(DB_VK_SHADER_KEY IN_LIST DB_VK_SHADER_KEYS
               OR DB_VK_SHADER_ROLE_STAGE IN_LIST DB_VK_SHADER_ROLE_STAGES)
                message(
                    FATAL_ERROR
                        "Duplicate Vulkan shader manifest entry: ${DB_VK_SHADER_ENTRY}"
                )
            endif()
            list(APPEND DB_VK_SHADER_KEYS "${DB_VK_SHADER_KEY}")
            list(APPEND DB_VK_SHADER_ROLE_STAGES "${DB_VK_SHADER_ROLE_STAGE}")
            set(DB_VK_SHADER_SOURCE
                ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/vulkan/${DB_VK_SHADER_ROLE}.${DB_VK_SHADER_STAGE}
            )
            if(NOT EXISTS "${DB_VK_SHADER_SOURCE}")
                message(
                    FATAL_ERROR
                        "Vulkan shader source is missing: ${DB_VK_SHADER_SOURCE}"
                )
            endif()
            set(DB_VK_SHADER_OUTPUT
                ${CMAKE_CURRENT_BINARY_DIR}/vk_${DB_VK_SHADER_ROLE}.${DB_VK_SHADER_STAGE}.spv
            )
            set(DB_VK_${DB_VK_SHADER_KEY}_SPV ${DB_VK_SHADER_OUTPUT})
            list(APPEND DB_VK_SHADER_OUTPUTS ${DB_VK_SHADER_OUTPUT})
            add_custom_command(
                OUTPUT ${DB_VK_SHADER_OUTPUT}
                COMMAND ${GLSLC} ${DB_VK_SHADER_SOURCE} -o
                        ${DB_VK_SHADER_OUTPUT}
                DEPENDS ${DB_VK_SHADER_SOURCE}
                VERBATIM)
        endforeach()

        add_custom_target(driverbench_vulkan_shaders ALL
                          DEPENDS ${DB_VK_SHADER_OUTPUTS})
    endif()
endif()
