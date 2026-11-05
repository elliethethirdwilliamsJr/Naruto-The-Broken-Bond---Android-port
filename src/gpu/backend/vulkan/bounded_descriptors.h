#pragma once
#include <vulkan/vulkan_core.h>
#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

namespace gpu::vulkan {
inline constexpr uint32_t kBoundedSlots = 64;
using BoundedImages = std::array<std::array<VkImageView, kBoundedSlots>, 3>;
using BoundedSamplers = std::array<VkSampler, kBoundedSlots>;

// Guest registers 0..15 are pixel textures; 16..19 are vertex textures.
// Slot zero is always a dimension-correct fallback. Register+1 is stable within
// a draw, independent of the session's image/sampler IDs.
inline bool RemapBoundedRegister(std::array<uint8_t, 512>& shared, uint32_t reg, bool cube) {
  if (reg >= 20) return false;
  const uint32_t slot = reg + 1;
  const uint32_t image_offset = reg < 16 ? (cube ? 128 : 0) + reg * 4 : 288 + (reg - 16) * 4;
  const uint32_t sampler_offset = reg < 16 ? 192 + reg * 4 : 304 + (reg - 16) * 4;
  std::memcpy(shared.data() + image_offset, &slot, 4);
  std::memcpy(shared.data() + sampler_offset, &slot, 4);
  return true;
}

inline VkResult CreateBoundedLayouts(VkDevice device, std::array<VkDescriptorSetLayout, 4>& layouts) {
  for (uint32_t set = 0; set < 4; ++set) {
    VkDescriptorSetLayoutBinding binding{0, set == 3 ? VK_DESCRIPTOR_TYPE_SAMPLER : VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
        kBoundedSlots, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    info.bindingCount = 1; info.pBindings = &binding;
    const auto result = vkCreateDescriptorSetLayout(device, &info, nullptr, &layouts[set]);
    if (result != VK_SUCCESS) {
      for (auto& layout : layouts) { if (layout) vkDestroyDescriptorSetLayout(device, layout, nullptr); layout = VK_NULL_HANDLE; }
      return result;
    }
  }
  return VK_SUCCESS;
}

// Each allocation is a new, fully populated immutable snapshot. The owner must
// retain this pool, views, samplers and images through GPU completion. Recycle is
// called only after the owning batch fence completes; no update-after-bind,
// partially-bound descriptors or nullDescriptor feature is used.
class BoundedDescriptorPool {
 public:
  explicit BoundedDescriptorPool(VkDevice device) : device_(device) {}
  BoundedDescriptorPool(const BoundedDescriptorPool&) = delete;
  BoundedDescriptorPool& operator=(const BoundedDescriptorPool&) = delete;
  ~BoundedDescriptorPool() { Recycle(); }
  void Recycle() {
    for (auto pool : pools_) vkDestroyDescriptorPool(device_, pool, nullptr);
    pools_.clear(); used_ = kDrawsPerPool;
  }
  VkResult Allocate(const std::array<VkDescriptorSetLayout, 4>& layouts, const BoundedImages& images,
                    const BoundedSamplers& samplers, std::array<VkDescriptorSet, 4>& sets) {
    for (const auto& dimension : images) for (auto view : dimension)
      if (!view) return VK_ERROR_INITIALIZATION_FAILED;
    for (auto sampler : samplers) if (!sampler) return VK_ERROR_INITIALIZATION_FAILED;
    if (used_ == kDrawsPerPool) {
      const VkDescriptorPoolSize sizes[] = {{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 3 * kBoundedSlots * kDrawsPerPool},
                                           {VK_DESCRIPTOR_TYPE_SAMPLER, kBoundedSlots * kDrawsPerPool}};
      VkDescriptorPoolCreateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
      info.maxSets = 4 * kDrawsPerPool; info.poolSizeCount = 2; info.pPoolSizes = sizes;
      VkDescriptorPool pool{}; const auto result = vkCreateDescriptorPool(device_, &info, nullptr, &pool);
      if (result != VK_SUCCESS) return result;
      pools_.push_back(pool); used_ = 0;
    }
    VkDescriptorSetAllocateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    info.descriptorPool = pools_.back(); info.descriptorSetCount = 4; info.pSetLayouts = layouts.data();
    const auto result = vkAllocateDescriptorSets(device_, &info, sets.data());
    if (result != VK_SUCCESS) return result;
    ++used_;
    std::array<std::array<VkDescriptorImageInfo, kBoundedSlots>, 4> descriptors{};
    std::array<VkWriteDescriptorSet, 4> writes{};
    for (uint32_t set = 0; set < 4; ++set) {
      for (uint32_t slot = 0; slot < kBoundedSlots; ++slot) {
        if (set == 3) descriptors[set][slot].sampler = samplers[slot];
        else { descriptors[set][slot].imageView = images[set][slot];
               descriptors[set][slot].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; }
      }
      writes[set] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
      writes[set].dstSet = sets[set]; writes[set].descriptorCount = kBoundedSlots;
      writes[set].descriptorType = set == 3 ? VK_DESCRIPTOR_TYPE_SAMPLER : VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
      writes[set].pImageInfo = descriptors[set].data();
    }
    vkUpdateDescriptorSets(device_, 4, writes.data(), 0, nullptr);
    return VK_SUCCESS;
  }
 private:
  static constexpr uint32_t kDrawsPerPool = 16;
  VkDevice device_;
  std::vector<VkDescriptorPool> pools_;
  uint32_t used_ = kDrawsPerPool;
};
}  // namespace gpu::vulkan
