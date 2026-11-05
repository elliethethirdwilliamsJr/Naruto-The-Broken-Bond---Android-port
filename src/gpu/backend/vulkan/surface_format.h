#pragma once
#include <span>
#include <initializer_list>
#include <vulkan/vulkan_core.h>

namespace gpu::vulkan {
// Keep the desktop preference, with the RGBA8 format exposed by Android WSI.
inline VkFormat SelectSurfaceFormat(std::span<const VkSurfaceFormatKHR> formats) {
  if (formats.size() == 1 && formats[0].format == VK_FORMAT_UNDEFINED &&
      formats[0].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
    return VK_FORMAT_B8G8R8A8_UNORM;
  for (VkFormat preferred : {VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM})
    for (const auto& offered : formats)
      if (offered.format == preferred && offered.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        return preferred;
  return VK_FORMAT_UNDEFINED;
}
}  // namespace gpu::vulkan
