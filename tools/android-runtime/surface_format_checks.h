#pragma once
#include "gpu/backend/vulkan/surface_format.h"
#include <vector>

inline bool CheckSurfaceFormatPolicy() {
  using namespace gpu::vulkan;
  const VkSurfaceFormatKHR rgba{VK_FORMAT_R8G8B8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
  const VkSurfaceFormatKHR bgra{VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
  const VkSurfaceFormatKHR srgb{VK_FORMAT_B8G8R8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
  const VkSurfaceFormatKHR wrong_space{VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_DISPLAY_P3_NONLINEAR_EXT};
  const VkSurfaceFormatKHR unrestricted{VK_FORMAT_UNDEFINED, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
  struct Case { std::vector<VkSurfaceFormatKHR> offered; VkFormat expected; };
  const Case cases[]{
      {{}, VK_FORMAT_UNDEFINED}, {{rgba}, rgba.format}, {{bgra}, bgra.format},
      {{rgba, bgra}, bgra.format}, {{bgra, rgba}, bgra.format},
      {{wrong_space, rgba}, rgba.format}, {{srgb, wrong_space}, VK_FORMAT_UNDEFINED},
      {{unrestricted}, bgra.format}};
  for (const auto& fixture : cases)
    if (SelectSurfaceFormat(fixture.offered) != fixture.expected) return false;
  return true;
}
