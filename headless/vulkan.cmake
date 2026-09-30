if(NOT PS5_NATIVE)
    message(FATAL_ERROR "EDEN_PS5_VULKAN requires PS5_NATIVE")
endif()
execute_process(COMMAND python3 "${PORT_ROOT}/tools/prepare-vulkan-port.py"
    "${PROJECT_SOURCE_DIR}" "${PORT_BUILD_DIR}" COMMAND_ERROR_IS_FATAL ANY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${PORT_ROOT}/tools/prepare-vulkan-port.py"
    "${EDEN_PORT_DIR}/vulkan_hud.vert" "${EDEN_PORT_DIR}/vulkan_hud.frag"
    "${EDEN_PORT_DIR}/vulkan_hud_prepare.inc" "${EDEN_PORT_DIR}/vulkan_hud_draw.inc"
    "${EDEN_PORT_DIR}/vulkan_loading.inc")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${EDEN_PORT_DIR}/vulkan_download_batch.inc"
    "${EDEN_PORT_DIR}/vulkan_gc_downloads.inc")
list(REMOVE_ITEM video_sources vulkan_common/vulkan_device.cpp renderer_vulkan/vk_scheduler.cpp)
list(APPEND video_sources "${PORT_BUILD_DIR}/vulkan_device.cpp" "${PORT_BUILD_DIR}/vulkan_scheduler.cpp")
list(REMOVE_ITEM video_sources vulkan_common/vulkan_instance.cpp vulkan_common/vulkan_wrapper.cpp
    vulkan_common/vulkan_library.cpp vulkan_common/vulkan_surface.cpp
    renderer_vulkan/vk_rasterizer.cpp renderer_vulkan/vk_blit_screen.cpp
    renderer_vulkan/present/window_adapt_pass.cpp renderer_vulkan/renderer_vulkan.cpp
    renderer_vulkan/vk_present_manager.cpp renderer_vulkan/vk_swapchain.cpp
    renderer_vulkan/vk_query_cache.cpp)
list(APPEND video_sources "${PORT_BUILD_DIR}/vulkan_instance.cpp" "${PORT_BUILD_DIR}/vulkan_wrapper.cpp"
    "${PORT_BUILD_DIR}/vulkan_rasterizer.cpp" "${PORT_BUILD_DIR}/vulkan_blit_screen.cpp"
    "${PORT_BUILD_DIR}/vulkan_window_adapt_pass.cpp" "${PORT_BUILD_DIR}/vulkan_renderer.cpp"
    "${PORT_BUILD_DIR}/vulkan_library.cpp" "${EDEN_PORT_DIR}/vulkan_surface.cpp" "${EDEN_PORT_DIR}/vulkan_allocator.cpp"
    "${PORT_BUILD_DIR}/vulkan_present_manager.cpp" "${PORT_BUILD_DIR}/vulkan_swapchain.cpp"
    "${PORT_BUILD_DIR}/vulkan_query_cache.cpp")
foreach(name vk_fence_manager vk_buffer_cache vk_texture_cache vk_descriptor_buffer vk_multi_range_buffer vk_compute_pass vk_graphics_pipeline vk_compute_pipeline vk_pipeline_cache)
    list(REMOVE_ITEM video_sources renderer_vulkan/${name}.cpp)
    list(APPEND video_sources "${PORT_BUILD_DIR}/${name}_cost.cpp")
endforeach()

target_include_directories(video_core BEFORE PUBLIC "${PORT_BUILD_DIR}/include")
target_include_directories(video_core BEFORE PUBLIC "${PORT_BUILD_DIR}/vulkan-cache")
target_include_directories(video_core PRIVATE "${PORT_BUILD_DIR}" "${EDEN_PORT_DIR}")
# The descriptor-buffer writer override (null descriptors): rebuild its callers once,
# since existing depfiles still name the upstream header.
set_property(SOURCE
    "${PORT_BUILD_DIR}/vk_graphics_pipeline_cost.cpp" "${PORT_BUILD_DIR}/vk_compute_pipeline_cost.cpp"
    "${PORT_BUILD_DIR}/vk_pipeline_cache_cost.cpp" "${PORT_BUILD_DIR}/vulkan_rasterizer.cpp"
    TARGET_DIRECTORY video_core APPEND PROPERTY OBJECT_DEPENDS
    "${PORT_BUILD_DIR}/include/video_core/renderer_vulkan/pipeline_helper.h")
# PresentManager is embedded by value in RendererVulkan. Rebuild consumers whose
# old depfiles still name the upstream header when adding the failure field.
set_property(SOURCE
    "${PROJECT_SOURCE_DIR}/src/video_core/video_core.cpp"
    "${PROJECT_SOURCE_DIR}/src/video_core/renderer_vulkan/vk_turbo_mode.cpp"
    "${PROJECT_SOURCE_DIR}/src/video_core/renderer_vulkan/present/frame_gen.cpp"
    "${PORT_BUILD_DIR}/vulkan_renderer.cpp" "${PORT_BUILD_DIR}/vulkan_blit_screen.cpp"
    "${PORT_BUILD_DIR}/vulkan_rasterizer.cpp" "${PORT_BUILD_DIR}/vulkan_window_adapt_pass.cpp"
    TARGET_DIRECTORY video_core APPEND PROPERTY OBJECT_DEPENDS
    "${PORT_BUILD_DIR}/include/video_core/renderer_vulkan/vk_present_manager.h")
# Existing depfiles name the original header; rebuild all filter consumers once
# when introducing the override so the shared struct layout stays consistent.
set_property(SOURCE
    "${PROJECT_SOURCE_DIR}/src/video_core/renderer_opengl/present/layer.cpp"
    "${PORT_BUILD_DIR}/renderer_opengl.cpp"
    "${PROJECT_SOURCE_DIR}/src/video_core/renderer_opengl/gl_blit_screen.cpp"
    "${PROJECT_SOURCE_DIR}/src/video_core/renderer_vulkan/present/layer.cpp"
    TARGET_DIRECTORY video_core APPEND PROPERTY OBJECT_DEPENDS
    "${PORT_BUILD_DIR}/include/video_core/present.h")
# BlitScreen stores WindowAdaptPass by value through unique_ptr; all consumers
# must see the extended resource layout, including cached objects predating it.
set_property(SOURCE
    "${PROJECT_SOURCE_DIR}/src/video_core/renderer_vulkan/present/filters.cpp"
    TARGET_DIRECTORY video_core APPEND PROPERTY OBJECT_DEPENDS
    "${PORT_BUILD_DIR}/include/video_core/renderer_vulkan/present/window_adapt_pass.h")
# A newly generated override is not in old depfiles until the sources rebuild.
# Track it explicitly for the API wrappers and allocator/device setup callers.
set_property(SOURCE
    "${PROJECT_SOURCE_DIR}/src/video_core/vulkan_common/vulkan_wrapper.cpp"
    "${PROJECT_SOURCE_DIR}/src/video_core/vulkan_common/vulkan_device.cpp"
    TARGET_DIRECTORY video_core APPEND PROPERTY OBJECT_DEPENDS
    "${PORT_BUILD_DIR}/include/video_core/vulkan_common/vulkan_wrapper.h")
target_compile_definitions(video_core PRIVATE PS5_NATIVE=1)
# Keep incompatible Mesa/PSBC globals private to the Vulkan archives.
if(EDEN_VULKAN_DRIVER STREQUAL "RADV")
    set(radv_archive "${PORT_ROOT}/build/radv-isolated/libvulkan_radeon.ps5.a")
    if(NOT EXISTS "${radv_archive}")
        message(FATAL_ERROR "Build and isolate the pinned RADV release archive first")
    endif()
    target_link_libraries(video_core PRIVATE "${radv_archive}"
        "${PORT_ROOT}/../ps5-vulkan-eden/dist-sdk/lib/libSceAgcDriver.so"
        "${sdk}/target/lib/libSceSysmodule.so")
else()
set(vk_isolated "${PORT_ROOT}/build/vulkan-isolated")
foreach(archive libps5vk.a libpsbc.a)
    if(NOT EXISTS "${vk_isolated}/${archive}")
        message(FATAL_ERROR "Stage isolated Vulkan compiler archives before building")
    endif()
endforeach()
target_link_libraries(video_core PRIVATE "${vk_isolated}/libps5vk.a" "${vk_isolated}/libpsbc.a"
    "${PORT_ROOT}/../ps5-vulkan-eden/dist-sdk/lib/libSceAgcDriver.so"
    "${sdk}/target/lib/libSceSysmodule.so")
endif()
