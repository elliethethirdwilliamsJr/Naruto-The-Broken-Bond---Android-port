// Exercise the production BC fallback and real Vulkan texture sampling on Android.
// This isolated compute test does not initialize the game's renderer or entry point.
#include "gpu/textures/upload_fallback.h"
#include "gpu/backend/vulkan/formats.h"
#include <vulkan/vulkan.h>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace gpu;
namespace {
void Require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void Check(VkResult result, const char* message) {
  if (result != VK_SUCCESS) throw std::runtime_error(std::string(message) + ": " + std::to_string(result));
}
struct Context {
  VkInstance instance{};
  VkPhysicalDevice physical{};
  VkDevice device{};
  VkQueue queue{};
  VkCommandPool pool{};
  VkDescriptorSetLayout set_layout{};
  VkPipelineLayout pipeline_layout{};
  VkPipeline pipeline{};
  VkDescriptorPool descriptors{};
  VkSampler sampler{};
  VkPhysicalDeviceMemoryProperties memory{};
  VkPhysicalDeviceProperties properties{};
  VkPhysicalDeviceFeatures features{};
  uint32_t family{};
  ~Context() {
    if (device) {
      if (sampler) vkDestroySampler(device, sampler, nullptr);
      if (descriptors) vkDestroyDescriptorPool(device, descriptors, nullptr);
      if (pipeline) vkDestroyPipeline(device, pipeline, nullptr);
      if (pipeline_layout) vkDestroyPipelineLayout(device, pipeline_layout, nullptr);
      if (set_layout) vkDestroyDescriptorSetLayout(device, set_layout, nullptr);
      if (pool) vkDestroyCommandPool(device, pool, nullptr);
      vkDestroyDevice(device, nullptr);
    }
    if (instance) vkDestroyInstance(instance, nullptr);
  }
  bool Supports(TextureFormat format) const {
    if (IsBlockCompressed(format) && !features.textureCompressionBC) return false;
    VkFormatProperties p{};
    vkGetPhysicalDeviceFormatProperties(physical, gpu::vulkan::ImageFormat(format), &p);
    constexpr auto needed = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_TRANSFER_SRC_BIT |
                            VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
    return (p.optimalTilingFeatures & needed) == needed;
  }
  uint32_t MemoryType(uint32_t bits, VkMemoryPropertyFlags flags) {
    for (uint32_t i = 0; i < memory.memoryTypeCount; ++i)
      if ((bits & (1u << i)) && (memory.memoryTypes[i].propertyFlags & flags) == flags) return i;
    throw std::runtime_error("required Vulkan memory type unavailable");
  }
  void Init(const std::filesystem::path& shader, bool dynamic_arrays = false) {
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "Broken Bond texture fallback diagnostic";
    app.apiVersion = VK_API_VERSION_1_1;
    VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO}; info.pApplicationInfo = &app;
    Check(vkCreateInstance(&info, nullptr, &instance), "create instance");
    uint32_t count = 0;
    Check(vkEnumeratePhysicalDevices(instance, &count, nullptr), "enumerate GPUs");
    Require(count != 0, "no Vulkan device");
    std::vector<VkPhysicalDevice> devices(count);
    Check(vkEnumeratePhysicalDevices(instance, &count, devices.data()), "enumerate GPUs");
    physical = devices[0];
    vkGetPhysicalDeviceProperties(physical, &properties);
    vkGetPhysicalDeviceFeatures(physical, &features);
    vkGetPhysicalDeviceMemoryProperties(physical, &memory);
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, nullptr);
    std::vector<VkQueueFamilyProperties> queues(count);
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, queues.data());
    family = count;
    for (uint32_t i = 0; i < count; ++i)
      if (queues[i].queueCount && (queues[i].queueFlags & VK_QUEUE_COMPUTE_BIT)) { family = i; break; }
    Require(family != count, "no compute queue");
    const float priority = 1;
    VkDeviceQueueCreateInfo queue_info{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queue_info.queueFamilyIndex = family; queue_info.queueCount = 1; queue_info.pQueuePriorities = &priority;
    VkDeviceCreateInfo device_info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    device_info.queueCreateInfoCount = 1; device_info.pQueueCreateInfos = &queue_info;
    VkPhysicalDeviceFeatures enabled{};
    if (dynamic_arrays) {
      Require(features.shaderSampledImageArrayDynamicIndexing, "sampled image dynamic indexing unavailable");
      enabled.shaderSampledImageArrayDynamicIndexing = VK_TRUE;
    }
    device_info.pEnabledFeatures = &enabled;
    // No optional desktop features, descriptor indexing or BC support enabled.
    Check(vkCreateDevice(physical, &device_info, nullptr, &device), "create device");
    vkGetDeviceQueue(device, family, 0, &queue);
    VkCommandPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pool_info.queueFamilyIndex = family; pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    Check(vkCreateCommandPool(device, &pool_info, nullptr, &pool), "create command pool");
    VkDescriptorSetLayoutBinding bindings[2] = {{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
        VK_SHADER_STAGE_COMPUTE_BIT, nullptr}, {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}};
    VkDescriptorSetLayoutCreateInfo set_info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    set_info.bindingCount = 2; set_info.pBindings = bindings;
    Check(vkCreateDescriptorSetLayout(device, &set_info, nullptr, &set_layout), "create descriptor layout");
    VkPushConstantRange push{VK_SHADER_STAGE_COMPUTE_BIT, 0, 16};
    VkPipelineLayoutCreateInfo layout_info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layout_info.setLayoutCount = 1; layout_info.pSetLayouts = &set_layout;
    layout_info.pushConstantRangeCount = 1; layout_info.pPushConstantRanges = &push;
    Check(vkCreatePipelineLayout(device, &layout_info, nullptr, &pipeline_layout), "create pipeline layout");
    std::ifstream file(shader, std::ios::binary | std::ios::ate);
    Require(bool(file), "open diagnostic SPIR-V");
    const auto size = file.tellg(); Require(size > 0 && size % 4 == 0, "invalid SPIR-V size");
    std::vector<uint32_t> code(size_t(size) / 4); file.seekg(0); file.read(reinterpret_cast<char*>(code.data()), size);
    Require(bool(file), "read diagnostic SPIR-V");
    VkShaderModuleCreateInfo module_info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    module_info.codeSize = size_t(size); module_info.pCode = code.data();
    VkShaderModule module{};
    Check(vkCreateShaderModule(device, &module_info, nullptr, &module), "create shader module");
    VkComputePipelineCreateInfo pipeline_info{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    pipeline_info.layout = pipeline_layout;
    pipeline_info.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    pipeline_info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pipeline_info.stage.module = module; pipeline_info.stage.pName = "main";
    const auto pipeline_result = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline);
    vkDestroyShaderModule(device, module, nullptr); Check(pipeline_result, "create compute pipeline");
    VkDescriptorPoolSize sizes[] = {{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1}, {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1}};
    VkDescriptorPoolCreateInfo desc_info{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    desc_info.maxSets = 1; desc_info.poolSizeCount = 2; desc_info.pPoolSizes = sizes;
    desc_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    Check(vkCreateDescriptorPool(device, &desc_info, nullptr, &descriptors), "create descriptor pool");
    VkSamplerCreateInfo sampler_info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sampler_info.magFilter = sampler_info.minFilter = VK_FILTER_LINEAR;
    sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler_info.addressModeU = sampler_info.addressModeV = sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.maxLod = 13;
    Check(vkCreateSampler(device, &sampler_info, nullptr, &sampler), "create sampler");
    std::cerr << "GPU: " << properties.deviceName << ", BC feature: " << features.textureCompressionBC << '\n';
  }
  VkCommandBuffer Begin() {
    Check(vkResetCommandPool(device, pool, 0), "reset command pool");
    VkCommandBufferAllocateInfo allocate{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocate.commandPool = pool; allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; allocate.commandBufferCount = 1;
    VkCommandBuffer command{}; Check(vkAllocateCommandBuffers(device, &allocate, &command), "allocate commands");
    VkCommandBufferBeginInfo info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    Check(vkBeginCommandBuffer(command, &info), "begin commands"); return command;
  }
  void Submit(VkCommandBuffer command) {
    Check(vkEndCommandBuffer(command), "end commands");
    VkFence fence{}; VkFenceCreateInfo create{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    Check(vkCreateFence(device, &create, nullptr, &fence), "create fence");
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO}; submit.commandBufferCount = 1; submit.pCommandBuffers = &command;
    Check(vkQueueSubmit(queue, 1, &submit, fence), "submit commands");
    const auto wait = vkWaitForFences(device, 1, &fence, VK_TRUE, 5'000'000'000ull);
    if (wait != VK_SUCCESS) {
      std::cerr << "GPU completion unproved: " << wait << "; ending diagnostic without freeing in-flight resources.\n";
      std::_Exit(2);
    }
    vkDestroyFence(device, fence, nullptr);
    vkFreeCommandBuffers(device, pool, 1, &command);
  }
};

struct Buffer {
  Context& c; VkBuffer handle{}; VkDeviceMemory memory{}; void* mapped{};
  Buffer(Context& context, VkDeviceSize size, VkBufferUsageFlags usage) : c(context) {
    VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO}; info.size = size; info.usage = usage;
    Check(vkCreateBuffer(c.device, &info, nullptr, &handle), "create buffer");
    VkMemoryRequirements requirements{}; vkGetBufferMemoryRequirements(c.device, handle, &requirements);
    VkMemoryAllocateInfo allocate{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; allocate.allocationSize = requirements.size;
    allocate.memoryTypeIndex = c.MemoryType(requirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    Check(vkAllocateMemory(c.device, &allocate, nullptr, &memory), "allocate buffer memory");
    Check(vkBindBufferMemory(c.device, handle, memory, 0), "bind buffer");
    Check(vkMapMemory(c.device, memory, 0, VK_WHOLE_SIZE, 0, &mapped), "map buffer");
  }
  ~Buffer() { if (mapped) vkUnmapMemory(c.device, memory); if (handle) vkDestroyBuffer(c.device, handle, nullptr);
              if (memory) vkFreeMemory(c.device, memory, nullptr); }
};
struct Image {
  Context& c; VkImage handle{}; VkDeviceMemory memory{}; VkImageView view{};
  Image(Context& context, const DrawTexture& source, TextureFormat format) : c(context) {
    VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO}; info.imageType = VK_IMAGE_TYPE_2D;
    info.format = gpu::vulkan::ImageFormat(format); info.extent = {source.width, source.height, 1};
    info.mipLevels = source.levels; info.arrayLayers = source.cube ? 6 : 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT; info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    info.flags = source.cube ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0;
    Check(vkCreateImage(c.device, &info, nullptr, &handle), "create fallback image");
    VkMemoryRequirements requirements{}; vkGetImageMemoryRequirements(c.device, handle, &requirements);
    VkMemoryAllocateInfo allocate{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; allocate.allocationSize = requirements.size;
    allocate.memoryTypeIndex = c.MemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    Check(vkAllocateMemory(c.device, &allocate, nullptr, &memory), "allocate image memory");
    Check(vkBindImageMemory(c.device, handle, memory, 0), "bind image");
    VkImageViewCreateInfo view_info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view_info.image = handle; view_info.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY; view_info.format = info.format;
    view_info.components = gpu::vulkan::ImageViewMapping(source.component_mapping);
    view_info.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, source.levels, 0, info.arrayLayers};
    Check(vkCreateImageView(c.device, &view_info, nullptr, &view), "create swizzled image view");
  }
  ~Image() { if (view) vkDestroyImageView(c.device, view, nullptr); if (handle) vkDestroyImage(c.device, handle, nullptr);
             if (memory) vkFreeMemory(c.device, memory, nullptr); }
};
using Pixel = std::array<uint8_t, 4>;
constexpr uint8_t alpha_palettes[2][8] = {{255, 0, 218, 182, 145, 109, 72, 36}, {0, 255, 51, 102, 153, 204, 0, 255}};
void Alpha(std::vector<uint8_t>& block, unsigned offset, unsigned mode, unsigned rotation) {
  block[offset] = mode ? 0 : 255; block[offset + 1] = mode ? 255 : 0;
  uint64_t bits = 0; for (unsigned p = 0; p < 16; ++p) bits |= uint64_t((p + rotation) % 8) << (3 * p);
  for (unsigned b = 0; b < 6; ++b) block[offset + 2 + b] = uint8_t(bits >> (8 * b));
}
Pixel MakeBlock(TextureFormat format, unsigned mode, unsigned rotation, std::vector<uint8_t>& block, unsigned p) {
  block.assign(BlockBytes(format), 0);
  const unsigned color_offset = format == TextureFormat::kBC3Unorm ? 8 : 0;
  Pixel color{};
  if (format == TextureFormat::kBC1Unorm || format == TextureFormat::kBC3Unorm) {
    const bool transparent = format == TextureFormat::kBC1Unorm && mode;
    const uint16_t first = transparent ? 0 : mode ? 0x07E0 : 0xF800;
    const uint16_t second = transparent ? 0xFFFF : mode ? 0xF800 : 0x07E0;
    block[color_offset] = uint8_t(first); block[color_offset + 1] = uint8_t(first >> 8);
    block[color_offset + 2] = uint8_t(second); block[color_offset + 3] = uint8_t(second >> 8);
    uint32_t indices = 0; for (unsigned i = 0; i < 16; ++i) indices |= ((i + rotation) % 4) << (2 * i);
    for (unsigned b = 0; b < 4; ++b) block[color_offset + 4 + b] = uint8_t(indices >> (8 * b));
    constexpr Pixel red_green[] = {{{255,0,0,255}}, {{0,255,0,255}}, {{170,85,0,255}}, {{85,170,0,255}}};
    constexpr Pixel green_red[] = {{{0,255,0,255}}, {{255,0,0,255}}, {{85,170,0,255}}, {{170,85,0,255}}};
    constexpr Pixel transparent_colors[] = {{{0,0,0,255}}, {{255,255,255,255}}, {{127,127,127,255}}, {{0,0,0,0}}};
    color = (transparent ? transparent_colors : mode ? green_red : red_green)[(p + rotation) % 4];
  }
  if (format != TextureFormat::kBC1Unorm) {
    Alpha(block, 0, mode, rotation);
    const uint8_t value = alpha_palettes[mode][(p + rotation) % 8];
    if (format == TextureFormat::kBC3Unorm) color[3] = value;
    else color = {value, 0, 0, 255};
  }
  if (format == TextureFormat::kBC5Unorm) {
    Alpha(block, 8, 1 - mode, rotation + 1);
    color[1] = alpha_palettes[1 - mode][(p + rotation + 1) % 8];
  }
  return color;
}
DrawTexture MakeTexture(TextureFormat format, bool cube, std::vector<std::vector<Pixel>>& expected) {
  DrawTexture t; t.format = format; t.width = cube ? 8 : 7; t.height = cube ? 8 : 5;
  t.levels = cube ? 4 : 3; t.cube = cube; t.key = 42; t.version = 7; t.layout = 123;
  if (format == TextureFormat::kBC4Unorm) t.component_mapping = {Swizzle::kR,Swizzle::kR,Swizzle::kR,Swizzle::kR};
  if (format == TextureFormat::kBC5Unorm) t.component_mapping = {Swizzle::kR,Swizzle::kG,Swizzle::kG,Swizzle::kG};
  if (format == TextureFormat::kBC3Unorm) t.component_mapping = {Swizzle::kB,Swizzle::kG,Swizzle::kR,Swizzle::kA};
  for (unsigned face = 0; face < (cube ? 6u : 1u); ++face) for (unsigned level = 0; level < t.levels; ++level) {
    const unsigned width = std::max(t.width >> level, 1u), height = std::max(t.height >> level, 1u);
    std::vector<uint8_t> data; std::vector<Pixel> pixels(width * height);
    for (unsigned by = 0; by < (height + 3) / 4; ++by) for (unsigned bx = 0; bx < (width + 3) / 4; ++bx) {
      const unsigned mode = (face + level + bx + by) & 1, rotation = (face * 3 + level * 5 + bx + by * 2) % 16;
      std::vector<uint8_t> block;
      for (unsigned y = 0; y < 4; ++y) for (unsigned x = 0; x < 4; ++x) {
        const auto pixel = MakeBlock(format, mode, rotation, block, y * 4 + x);
        if (bx * 4 + x < width && by * 4 + y < height) pixels[(by * 4 + y) * width + bx * 4 + x] = pixel;
      }
      data.insert(data.end(), block.begin(), block.end());
    }
    t.data.push_back(std::move(data)); expected.push_back(std::move(pixels));
  }
  return t;
}

uint64_t TestTexture(Context& c, TextureFormat source_format, bool cube) {
  std::vector<std::vector<Pixel>> expected;
  const auto t = MakeTexture(source_format, cube, expected);
  const auto original = t.data;
  const auto selected = SelectSampledFormat(t.format, [&](TextureFormat f) { return c.Supports(f); });
  Require(selected == DecodedSampledFormat(t.format), "this diagnostic requires the device's BC fallback path");
  VkFormatProperties properties{};
  vkGetPhysicalDeviceFormatProperties(c.physical, gpu::vulkan::ImageFormat(selected), &properties);
  Require(properties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT, "fallback lacks linear filtering");
  std::vector<std::vector<uint8_t>> decoded; std::string reason;
  Require(DecodeSampledData(t, selected, decoded, reason), "production decoder failed");
  Require(t.data == original && t.format == source_format && t.layout == 123 && t.version == 7, "input modified");
  const unsigned components = BlockBytes(selected);
  VkDeviceSize bytes = 0; std::vector<VkBufferImageCopy> copies;
  for (unsigned i = 0; i < decoded.size(); ++i) {
    Require(decoded[i].size() == expected[i].size() * components, "decoded size");
    for (unsigned p = 0; p < expected[i].size(); ++p) for (unsigned component = 0; component < components; ++component)
      Require(decoded[i][p * components + component] == expected[i][p][component], "CPU decoder differs from golden pixels");
    VkBufferImageCopy copy{}; copy.bufferOffset = bytes;
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, i % t.levels, i / t.levels, 1};
    copy.imageExtent = {std::max(t.width >> (i % t.levels), 1u), std::max(t.height >> (i % t.levels), 1u), 1};
    copies.push_back(copy); bytes += (decoded[i].size() + 15) & ~VkDeviceSize(15);
  }
  Image image(c, t, selected); Buffer upload(c, bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
  Buffer output(c, size_t(t.width) * t.height * 8 * sizeof(float), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
  for (unsigned i = 0; i < decoded.size(); ++i)
    std::memcpy(static_cast<uint8_t*>(upload.mapped) + copies[i].bufferOffset, decoded[i].data(), decoded[i].size());
  auto command = c.Begin();
  VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
  barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = image.handle; barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, t.levels, 0, cube ? 6u : 1u};
  barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0,nullptr,0,nullptr,1,&barrier);
  vkCmdCopyBufferToImage(command, upload.handle, image.handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                         uint32_t(copies.size()), copies.data());
  barrier.oldLayout = barrier.newLayout; barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
  vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,0,nullptr,0,nullptr,1,&barrier);
  c.Submit(command);
  VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
  allocate.descriptorPool = c.descriptors; allocate.descriptorSetCount = 1; allocate.pSetLayouts = &c.set_layout;
  VkDescriptorSet set{}; Check(vkAllocateDescriptorSets(c.device, &allocate, &set), "allocate descriptor set");
  VkDescriptorImageInfo sampled{c.sampler, image.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
  VkDescriptorBufferInfo buffer{output.handle, 0, VK_WHOLE_SIZE};
  VkWriteDescriptorSet writes[2]{};
  for (unsigned i = 0; i < 2; ++i) { writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      writes[i].dstSet = set; writes[i].dstBinding = i; writes[i].descriptorCount = 1; }
  writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; writes[0].pImageInfo = &sampled;
  writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; writes[1].pBufferInfo = &buffer;
  vkUpdateDescriptorSets(c.device, 2, writes, 0, nullptr);
  uint64_t checked = 0;
  for (unsigned i = 0; i < decoded.size(); ++i) {
    std::memset(output.mapped, 0xCD, size_t(t.width) * t.height * 8 * sizeof(float));
    command = c.Begin();
    VkBufferMemoryBarrier output_barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
    output_barrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT; output_barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    output_barrier.srcQueueFamilyIndex = output_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    output_barrier.buffer = output.handle; output_barrier.size = VK_WHOLE_SIZE;
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,0,nullptr,1,&output_barrier,0,nullptr);
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, c.pipeline);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, c.pipeline_layout, 0,1,&set,0,nullptr);
    const uint32_t settings[] = {copies[i].imageExtent.width, copies[i].imageExtent.height, i % t.levels, i / t.levels};
    vkCmdPushConstants(command, c.pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(settings), settings);
    vkCmdDispatch(command, settings[0], settings[1], 1);
    output_barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT; output_barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0,0,nullptr,1,&output_barrier,0,nullptr);
    c.Submit(command);
    const auto* pixels = static_cast<const float*>(output.mapped);
    for (unsigned p = 0; p < expected[i].size(); ++p) for (unsigned method = 0; method < 2; ++method)
      for (unsigned component = 0; component < 4; ++component) {
        const unsigned channel = unsigned(t.component_mapping[component]);
        Require(channel < 4, "probe mapping");
        const float want = expected[i][p][channel] / 255.0f;
        const float got = pixels[p * 8 + method * 4 + component];
        Require(std::isfinite(got) && std::abs(got - want) < 0.0001f, "GPU sampled value differs from golden pixels");
        ++checked;
      }
  }
  Check(vkFreeDescriptorSets(c.device, c.descriptors, 1, &set), "free descriptor set");
  std::cerr << "BC " << uint32_t(source_format) << " -> " << uint32_t(selected) << (cube ? " cube" : " odd 2D")
            << ": " << decoded.size() << " subresources passed.\n";
  return checked;
}

unsigned CheckRejections() {
  std::vector<std::vector<Pixel>> expected;
  auto valid = MakeTexture(TextureFormat::kBC1Unorm, true, expected);
  std::vector<std::vector<uint8_t>> output; std::string reason;
  auto reject = [&](DrawTexture t, TextureFormat destination = TextureFormat::kR8G8B8A8Unorm) {
    output = {{1,2,3}};
    Require(!DecodeSampledData(t, destination, output, reason) && output.empty() && !reason.empty(), "malformed upload accepted");
  };
  auto t = valid; t.data.back().pop_back(); reject(t);
  t = valid; t.data.pop_back(); reject(t);
  t = valid; t.levels = 0; reject(t);
  t = valid; t.levels = 13; reject(t);
  t = valid; t.width = 7; reject(t);
  t = valid; t.resolved = true; reject(t);
  reject(valid, TextureFormat::kR8Unorm);
  for (auto format : {TextureFormat::kBC1Unorm, TextureFormat::kBC3Unorm, TextureFormat::kBC4Unorm, TextureFormat::kBC5Unorm}) {
    Require(SelectSampledFormat(format, [](auto) { return true; }) == format, "native format path changed");
    Require(SelectSampledFormat(format, [](auto) { return false; }) == TextureFormat::kUnknown, "unsupported fallback accepted");
  }
  Require(SelectSampledFormat(TextureFormat::kUnknown, [](auto) { return true; }) == TextureFormat::kUnknown, "unknown format accepted");
  return 16;
}
}  // namespace

#ifndef BROKEN_BOND_TEXTURE_SUPPORT_ONLY
int main(int argc, char** argv) {
  try {
    Require(argc == 2, "usage: android_texture_probe <diagnostic directory>");
    const std::filesystem::path root(argv[1]);
    const unsigned policy_checks = CheckRejections();
    Context c; c.Init(root / "texture_probe.spv");
    uint64_t values = 0;
    for (auto format : {TextureFormat::kBC1Unorm, TextureFormat::kBC3Unorm, TextureFormat::kBC4Unorm, TextureFormat::kBC5Unorm})
      for (bool cube : {false, true}) values += TestTexture(c, format, cube);
    std::ofstream result(root / "texture-result.json");
    result << "{\"gpu\":\"" << c.properties.deviceName << "\",\"bc_feature\":" << c.features.textureCompressionBC
           << ",\"formats_tested\":4,\"images_tested\":8,\"subresources_tested\":108,\"sampled_values_checked\":" << values
           << ",\"policy_and_rejection_checks\":" << policy_checks
           << ",\"sampling_methods\":[\"texelFetch\",\"linear-textureLod\"],\"game_renderer_started\":false,\"guest_entry_executed\":false}\n";
    result.close(); Require(bool(result), "write texture result");
    std::cout << "Production texture fallback and Vulkan sampling checks passed.\n";
    return 0;
  } catch (const std::exception& error) { std::cerr << "texture probe: " << error.what() << '\n'; return 1; }
}
#endif
