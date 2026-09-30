// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/frontend/emu_window.h"
#include "video_core/vulkan_common/vulkan_surface.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"
#include <vector>
#include <cstdio>

namespace Vulkan {
vk::SurfaceKHR CreateSurface(const vk::Instance& instance,
                            const Core::Frontend::EmuWindow::WindowSystemInfo& window_info) {
    if (window_info.type != Core::Frontend::WindowSystemType::PS5)
        throw vk::Exception(VK_ERROR_INITIALIZATION_FAILED);
    std::fprintf(stderr, "[ProsperoEden] Vulkan instance ready; creating native surface\n");
    const auto& dispatch = instance.Dispatch();
    const auto proc = [&](const char* name) {
        auto function = dispatch.vkGetInstanceProcAddr(*instance, name);
        if (!function) throw vk::Exception(VK_ERROR_EXTENSION_NOT_PRESENT);
        return function;
    };
    const auto check = [](VkResult result, int line = __builtin_LINE()) {
        std::fprintf(stderr, "[ProsperoEden] Vulkan surface line=%d result=%d\n", line, static_cast<int>(result));
        if (result != VK_SUCCESS) throw vk::Exception(result);
    };
    // ponytail: the native driver exposes one GPU/display/plane; enumerate all
    // and select explicitly if a future driver supports multiple outputs.
    uint32_t count = 1;
    VkPhysicalDevice physical{};
    check(reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(proc("vkEnumeratePhysicalDevices"))(
        *instance, &count, &physical));
    if (count != 1) {
        std::fprintf(stderr, "[ProsperoEden] Vulkan surface enumeration count=%u\n", count);
        throw vk::Exception(VK_ERROR_INITIALIZATION_FAILED);
    }
    VkDisplayPropertiesKHR display{};
    count = 1;
    check(reinterpret_cast<PFN_vkGetPhysicalDeviceDisplayPropertiesKHR>(
        proc("vkGetPhysicalDeviceDisplayPropertiesKHR"))(physical, &count, &display));
    if (count != 1) {
        std::fprintf(stderr, "[ProsperoEden] Vulkan surface enumeration count=%u\n", count);
        throw vk::Exception(VK_ERROR_INITIALIZATION_FAILED);
    }
    const auto get_modes = reinterpret_cast<PFN_vkGetDisplayModePropertiesKHR>(
        proc("vkGetDisplayModePropertiesKHR"));
    std::vector<VkDisplayModePropertiesKHR> modes;
    VkResult result;
    do {
        count = 0;
        check(get_modes(physical, display.display, &count, nullptr));
        if (!count) throw vk::Exception(VK_ERROR_INITIALIZATION_FAILED);
        modes.resize(count);
        result = get_modes(physical, display.display, &count, modes.data());
    } while (result == VK_INCOMPLETE);
    check(result);
    if (!count) throw vk::Exception(VK_ERROR_INITIALIZATION_FAILED);
    modes.resize(count);
    // Prefer the existing 60 Hz output even if RADV lists 120 Hz first.
    auto mode = modes.front();
    for (const auto& candidate : modes) {
        if (candidate.parameters.refreshRate >= 59000 &&
            candidate.parameters.refreshRate <= 61000) {
            mode = candidate;
            break;
        }
    }
    const VkDisplaySurfaceCreateInfoKHR info{
        .sType = VK_STRUCTURE_TYPE_DISPLAY_SURFACE_CREATE_INFO_KHR,
        .displayMode = mode.displayMode,
        .planeIndex = 0,
        .planeStackIndex = 0,
        .transform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
        .globalAlpha = 1.0f,
        .alphaMode = VK_DISPLAY_PLANE_ALPHA_OPAQUE_BIT_KHR,
        .imageExtent = mode.parameters.visibleRegion,
    };
    VkSurfaceKHR surface{};
    check(reinterpret_cast<PFN_vkCreateDisplayPlaneSurfaceKHR>(proc("vkCreateDisplayPlaneSurfaceKHR"))(
        *instance, &info, nullptr, &surface));
    return vk::SurfaceKHR(surface, *instance, dispatch);
}
} // namespace Vulkan
