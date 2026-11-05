#pragma once
#include <plume_vulkan.h>
#include <filesystem>
#include <fstream>
#include <vector>
#include <mutex>
#include <cstring>
#include <fmt/format.h>

namespace gpu::vulkan {
// Android development cache. One mutex provides the external synchronization
// required when several pipeline workers share a VkPipelineCache.
class MobilePipelineCache {
 public:
  MobilePipelineCache(VkDevice device, const VkPhysicalDeviceProperties& properties, std::filesystem::path path)
      : device_(device), path_(std::move(path)) {
    std::vector<uint8_t> bytes;
    std::error_code error;
    const auto size = std::filesystem::file_size(path_, error);
    if (!error && size >= sizeof(VkPipelineCacheHeaderVersionOne) && size <= kMaximumBytes) {
      bytes.resize(size);
      std::ifstream file(path_, std::ios::binary);
      if (!file.read(reinterpret_cast<char*>(bytes.data()), bytes.size())) bytes.clear();
    }
    if (!bytes.empty()) {
      VkPipelineCacheHeaderVersionOne header{};
      std::memcpy(&header, bytes.data(), sizeof(header));
      if (header.headerSize < sizeof(header) || header.headerSize > bytes.size() ||
          header.headerVersion != VK_PIPELINE_CACHE_HEADER_VERSION_ONE ||
          header.vendorID != properties.vendorID || header.deviceID != properties.deviceID ||
          std::memcmp(header.pipelineCacheUUID, properties.pipelineCacheUUID, VK_UUID_SIZE)) bytes.clear();
    }
    VkPipelineCacheCreateInfo info{VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
    info.initialDataSize = bytes.size(); info.pInitialData = bytes.data();
    if (vkCreatePipelineCache(device_, &info, nullptr, &cache_) == VK_SUCCESS) loaded_bytes_ = bytes.size();
    else {
      cache_ = VK_NULL_HANDLE;
      if (!bytes.empty()) {
        info.initialDataSize = 0; info.pInitialData = nullptr;
        if (vkCreatePipelineCache(device_, &info, nullptr, &cache_) != VK_SUCCESS) cache_ = VK_NULL_HANDLE;
      }
    }
  }
  ~MobilePipelineCache() {
    std::lock_guard lock(mutex_);
    SaveLocked();
    if (cache_) vkDestroyPipelineCache(device_, cache_, nullptr);
  }
  VkResult Create(const VkGraphicsPipelineCreateInfo& info, VkPipeline* pipeline) {
    std::lock_guard lock(mutex_);
    const auto result = vkCreateGraphicsPipelines(device_, cache_, 1, &info, nullptr, pipeline);
    // Checkpoint during loading too, so force-closing does not discard all
    // compilation work. Never substitute a cached file for a real pipeline.
    if (result == VK_SUCCESS && ++created_ % 8 == 0) SaveLocked();
    return result;
  }
  std::string Summary() const {
    std::lock_guard lock(mutex_);
    return fmt::format("{{\"enabled\":{},\"loaded_bytes\":{},\"saved_bytes\":{},\"created\":{}}}",
                       cache_ != VK_NULL_HANDLE, loaded_bytes_, saved_bytes_, created_);
  }
 private:
  void SaveLocked() {
    if (!cache_ || !created_) return;
    size_t size = 0;
    if (vkGetPipelineCacheData(device_, cache_, &size, nullptr) != VK_SUCCESS || !size || size > kMaximumBytes) return;
    std::vector<uint8_t> bytes(size);
    if (vkGetPipelineCacheData(device_, cache_, &size, bytes.data()) != VK_SUCCESS) return;
    std::error_code error;
    std::filesystem::create_directories(path_.parent_path(), error);
    if (error) return;
    auto temporary = path_; temporary += ".tmp";
    {
      std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
      if (!file.write(reinterpret_cast<const char*>(bytes.data()), size)) return;
      file.close();
      if (!file) return;
    }
    std::filesystem::rename(temporary, path_, error);
    if (!error) saved_bytes_ = size;
  }
  static constexpr size_t kMaximumBytes = 128 * 1024 * 1024;
  VkDevice device_;
  VkPipelineCache cache_ = VK_NULL_HANDLE;
  std::filesystem::path path_;
  mutable std::mutex mutex_;
  size_t loaded_bytes_ = 0, saved_bytes_ = 0;
  uint64_t created_ = 0;
};
}
