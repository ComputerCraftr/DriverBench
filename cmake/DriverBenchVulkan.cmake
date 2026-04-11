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
            src/renderers/vulkan_1_2_multi_gpu/vk_init_phases.c
            src/renderers/vulkan_1_2_multi_gpu/vk_init_capabilities.c
            src/renderers/vulkan_1_2_multi_gpu/vk_init_selection.c
            src/renderers/vulkan_1_2_multi_gpu/vk_interop.c
            src/renderers/vulkan_1_2_multi_gpu/vk_interop_memory.c
            src/renderers/vulkan_1_2_multi_gpu/vk_interop_execution.c
            src/renderers/vulkan_1_2_multi_gpu/vk_buffer_plan.c
            src/renderers/vulkan_1_2_multi_gpu/vk_buffer_transport.c
            src/renderers/vulkan_1_2_multi_gpu/vk_calibration.c
            src/renderers/vulkan_1_2_multi_gpu/vk_piece_plan.c
            src/renderers/vulkan_1_2_multi_gpu/vk_device_group.c
            src/renderers/vulkan_1_2_multi_gpu/vk_runtime.c
            src/renderers/vulkan_1_2_multi_gpu/vk_runtime_frame.c
            src/renderers/vulkan_1_2_multi_gpu/vk_runtime_metrics.c
            src/renderers/vulkan_1_2_multi_gpu/vk_wait.c
            src/renderers/vulkan_1_2_multi_gpu/vk_scheduler.c
            src/renderers/vulkan_1_2_multi_gpu/vk_swapchain.c)
        list(APPEND DB_DRIVERBENCH_LIBS ${DB_VULKAN_LIB})
        list(APPEND DB_DRIVERBENCH_DEFS DB_HAS_VULKAN_API=1)
        if(DB_VK_TEST_FORCE_BUFFER_TRANSPORT)
            list(APPEND DB_DRIVERBENCH_DEFS DB_VK_TEST_FORCE_BUFFER_TRANSPORT=1)
        endif()

        set(DB_VK_VERT_SPV ${CMAKE_CURRENT_BINARY_DIR}/vk_rect.vert.spv)
        set(DB_VK_FRAG_SPV ${CMAKE_CURRENT_BINARY_DIR}/vk_rect.frag.spv)
        set(DB_VK_PRESENT_VERT_SPV
            ${CMAKE_CURRENT_BINARY_DIR}/vk_present.vert.spv)
        set(DB_VK_PRESENT_FRAG_SPV
            ${CMAKE_CURRENT_BINARY_DIR}/vk_present.frag.spv)
        set(DB_VK_PACK_COMP_SPV ${CMAKE_CURRENT_BINARY_DIR}/vk_pack.comp.spv)
        set(DB_VK_UNPACK_FRAG_SPV
            ${CMAKE_CURRENT_BINARY_DIR}/vk_unpack.frag.spv)

        add_custom_command(
            OUTPUT ${DB_VK_VERT_SPV}
            COMMAND
                ${GLSLC} ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/vk_rect.vert -o
                ${DB_VK_VERT_SPV}
            DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/vk_rect.vert
            VERBATIM)

        add_custom_command(
            OUTPUT ${DB_VK_FRAG_SPV}
            COMMAND
                ${GLSLC} ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/vk_rect.frag -o
                ${DB_VK_FRAG_SPV}
            DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/vk_rect.frag
            VERBATIM)

        add_custom_command(
            OUTPUT ${DB_VK_PRESENT_VERT_SPV}
            COMMAND
                ${GLSLC} ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/vk_present.vert
                -o ${DB_VK_PRESENT_VERT_SPV}
            DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/vk_present.vert
            VERBATIM)

        add_custom_command(
            OUTPUT ${DB_VK_PRESENT_FRAG_SPV}
            COMMAND
                ${GLSLC} ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/vk_present.frag
                -o ${DB_VK_PRESENT_FRAG_SPV}
            DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/vk_present.frag
            VERBATIM)

        add_custom_command(
            OUTPUT ${DB_VK_PACK_COMP_SPV}
            COMMAND
                ${GLSLC} ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/vk_pack.comp -o
                ${DB_VK_PACK_COMP_SPV}
            DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/vk_pack.comp
            VERBATIM)

        add_custom_command(
            OUTPUT ${DB_VK_UNPACK_FRAG_SPV}
            COMMAND
                ${GLSLC} ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/vk_unpack.frag
                -o ${DB_VK_UNPACK_FRAG_SPV}
            DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/vk_unpack.frag
            VERBATIM)

        add_custom_target(
            driverbench_vulkan_shaders ALL
            DEPENDS ${DB_VK_VERT_SPV} ${DB_VK_FRAG_SPV}
                    ${DB_VK_PRESENT_VERT_SPV} ${DB_VK_PRESENT_FRAG_SPV}
                    ${DB_VK_PACK_COMP_SPV} ${DB_VK_UNPACK_FRAG_SPV})
    endif()
endif()
