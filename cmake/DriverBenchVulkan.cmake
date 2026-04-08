if(DB_BUILD_VULKAN)
  find_package(Vulkan QUIET)
  find_program(GLSLC glslc HINTS ENV VULKAN_SDK PATH_SUFFIXES Bin bin)

  set(DB_VULKAN_LIB "")
  if(TARGET Vulkan::Vulkan)
    set(DB_VULKAN_LIB Vulkan::Vulkan)
  elseif(Vulkan_FOUND AND Vulkan_LIBRARY)
    set(DB_VULKAN_LIB "${Vulkan_LIBRARY}")
  endif()

  if(DB_TARGET_LINUX_MUSL AND DB_VULKAN_LIB)
    set(DB_VULKAN_LOCATION "")
    if(TARGET Vulkan::Vulkan)
      get_target_property(DB_VULKAN_IMPORTED_LOCATION
        Vulkan::Vulkan IMPORTED_LOCATION
      )
      if(DB_VULKAN_IMPORTED_LOCATION)
        set(DB_VULKAN_LOCATION "${DB_VULKAN_IMPORTED_LOCATION}")
      else()
        get_target_property(DB_VULKAN_IMPORTED_CONFIGS
          Vulkan::Vulkan IMPORTED_CONFIGURATIONS
        )
        foreach(DB_VK_CFG IN LISTS DB_VULKAN_IMPORTED_CONFIGS)
          string(TOUPPER "${DB_VK_CFG}" DB_VK_CFG_UPPER)
          get_target_property(DB_VULKAN_IMPORTED_LOCATION_CFG
            Vulkan::Vulkan
            IMPORTED_LOCATION_${DB_VK_CFG_UPPER}
          )
          if(DB_VULKAN_IMPORTED_LOCATION_CFG)
            set(DB_VULKAN_LOCATION "${DB_VULKAN_IMPORTED_LOCATION_CFG}")
            break()
          endif()
        endforeach()
      endif()
    else()
      set(DB_VULKAN_LOCATION "${DB_VULKAN_LIB}")
    endif()

    if(DB_VULKAN_LOCATION MATCHES "\\.so(\\.|$)")
      message(WARNING
        "DB_TARGET_LINUX_MUSL=ON with static link: Vulkan library resolves to "
        "shared library '${DB_VULKAN_LOCATION}'. Disabling Vulkan renderer "
        "for this build."
      )
      set(DB_VULKAN_LIB "")
    endif()
  endif()

  if(DB_BUILD_GLFW_WINDOW_DISPLAY AND DB_GLFW_LINK_LIB AND Vulkan_FOUND AND
     DB_VULKAN_LIB AND GLSLC)
    list(APPEND DB_DRIVERBENCH_SOURCES
      src/renderers/vulkan_1_2_multi_gpu/renderer_vulkan_1_2_multi_gpu.c
      src/renderers/vulkan_1_2_multi_gpu/renderer_vulkan_1_2_multi_gpu_frame.c
      src/renderers/vulkan_1_2_multi_gpu/renderer_vulkan_1_2_multi_gpu_init.c
      src/renderers/vulkan_1_2_multi_gpu/renderer_vulkan_1_2_multi_gpu_init_phases.c
      src/renderers/vulkan_1_2_multi_gpu/renderer_vulkan_1_2_multi_gpu_init_selection.c
      src/renderers/vulkan_1_2_multi_gpu/renderer_vulkan_1_2_multi_gpu_runtime.c
      src/renderers/vulkan_1_2_multi_gpu/renderer_vulkan_1_2_multi_gpu_runtime_frame.c
      src/renderers/vulkan_1_2_multi_gpu/renderer_vulkan_1_2_multi_gpu_runtime_metrics.c
      src/renderers/vulkan_1_2_multi_gpu/renderer_vulkan_1_2_multi_gpu_scheduler.c
      src/renderers/vulkan_1_2_multi_gpu/renderer_vulkan_1_2_multi_gpu_swapchain.c
    )
    list(APPEND DB_DRIVERBENCH_LIBS ${DB_VULKAN_LIB})
    list(APPEND DB_DRIVERBENCH_DEFS DB_HAS_VULKAN_API=1)

    set(DB_VK_VERT_SPV
      ${CMAKE_CURRENT_BINARY_DIR}/shader_vulkan_1_2_rect.vert.spv
    )
    set(DB_VK_FRAG_SPV
      ${CMAKE_CURRENT_BINARY_DIR}/shader_vulkan_1_2_rect.frag.spv
    )

    add_custom_command(
      OUTPUT ${DB_VK_VERT_SPV}
      COMMAND ${GLSLC}
              ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/shader_vulkan_1_2_rect.vert -o
              ${DB_VK_VERT_SPV}
      DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/shader_vulkan_1_2_rect.vert
      VERBATIM
    )

    add_custom_command(
      OUTPUT ${DB_VK_FRAG_SPV}
      COMMAND ${GLSLC}
              ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/shader_vulkan_1_2_rect.frag -o
              ${DB_VK_FRAG_SPV}
      DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/shader_vulkan_1_2_rect.frag
      VERBATIM
    )

    add_custom_target(driverbench_vulkan_shaders ALL
      DEPENDS ${DB_VK_VERT_SPV}
              ${DB_VK_FRAG_SPV}
    )
  endif()
endif()
