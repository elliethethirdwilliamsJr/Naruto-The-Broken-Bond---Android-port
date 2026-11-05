// Compare the production geometry clipper with independent double-precision CPU
// polygon clipping, using actual rasterization, depth testing and MSAA on Android.
#include <vulkan/vulkan.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
constexpr uint32_t kWidth=32, kHeight=24, kPixelBytes=kWidth*kHeight*4;
struct Vertex { float position[4]; float values[18][4]; float distances[6]; };
static_assert(sizeof(Vertex)==82*sizeof(float));
void Require(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
void Check(VkResult result,const char* message) {
  if(result!=VK_SUCCESS) throw std::runtime_error(std::string(message)+": "+std::to_string(result));
}
struct Context {
  VkInstance instance{}; VkPhysicalDevice physical{}; VkDevice device{}; VkQueue queue{};
  VkCommandPool pool{}; VkPipelineLayout layout{}; VkPipelineCache cache{};
  VkShaderModule vs{},gs{},ps{};
  VkPhysicalDeviceMemoryProperties memory{}; VkPhysicalDeviceProperties properties{};
  VkPhysicalDeviceFeatures features{}; uint32_t family{};
  ~Context() {
    if(device) {
      if(cache) vkDestroyPipelineCache(device,cache,nullptr);
      if(layout) vkDestroyPipelineLayout(device,layout,nullptr);
      for(auto module:{vs,gs,ps}) if(module) vkDestroyShaderModule(device,module,nullptr);
      if(pool) vkDestroyCommandPool(device,pool,nullptr);
      vkDestroyDevice(device,nullptr);
    }
    if(instance) vkDestroyInstance(instance,nullptr);
  }
  VkShaderModule Shader(const std::filesystem::path& path) {
    std::ifstream file(path,std::ios::binary|std::ios::ate);
    Require(bool(file),"open probe shader"); auto bytes=file.tellg();
    Require(bytes>0 && bytes%4==0,"probe shader size");
    std::vector<uint32_t> code(size_t(bytes)/4); file.seekg(0);
    file.read(reinterpret_cast<char*>(code.data()),bytes); Require(bool(file),"read probe shader");
    VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    info.codeSize=size_t(bytes); info.pCode=code.data(); VkShaderModule module{};
    Check(vkCreateShaderModule(device,&info,nullptr,&module),"create probe shader"); return module;
  }
  void Init(const std::filesystem::path& root) {
    VkApplicationInfo application{VK_STRUCTURE_TYPE_APPLICATION_INFO}; application.apiVersion=VK_API_VERSION_1_2;
    VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO}; info.pApplicationInfo=&application;
    Check(vkCreateInstance(&info,nullptr,&instance),"create Vulkan instance");
    uint32_t count=0; Check(vkEnumeratePhysicalDevices(instance,&count,nullptr),"enumerate GPU");
    Require(count!=0,"no Vulkan GPU"); std::vector<VkPhysicalDevice> devices(count);
    Check(vkEnumeratePhysicalDevices(instance,&count,devices.data()),"enumerate GPU"); physical=devices[0];
    vkGetPhysicalDeviceProperties(physical,&properties); vkGetPhysicalDeviceFeatures(physical,&features);
    vkGetPhysicalDeviceMemoryProperties(physical,&memory);
    const auto& l=properties.limits;
    Require(features.geometryShader && !features.shaderClipDistance,"expected geometry-capable GPU without clip distances");
    Require(l.maxVertexOutputComponents>=82 && l.maxGeometryInputComponents>=82 && l.maxGeometryOutputComponents>=76 &&
            l.maxGeometryOutputVertices>=9 && l.maxGeometryTotalOutputComponents>=684,"clip geometry limits");
    vkGetPhysicalDeviceQueueFamilyProperties(physical,&count,nullptr);
    std::vector<VkQueueFamilyProperties> queues(count); vkGetPhysicalDeviceQueueFamilyProperties(physical,&count,queues.data());
    family=count; for(uint32_t i=0;i<count;++i) if(queues[i].queueCount && (queues[i].queueFlags&VK_QUEUE_GRAPHICS_BIT)) {family=i;break;}
    Require(family<count,"no graphics queue");
    float priority=1; VkDeviceQueueCreateInfo queue_info{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queue_info.queueFamilyIndex=family; queue_info.queueCount=1; queue_info.pQueuePriorities=&priority;
    VkPhysicalDeviceFeatures enabled{}; enabled.geometryShader=VK_TRUE;
    // Native clip distances remain disabled, even if another device has them.
    VkDeviceCreateInfo device_info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    device_info.queueCreateInfoCount=1; device_info.pQueueCreateInfos=&queue_info; device_info.pEnabledFeatures=&enabled;
    Check(vkCreateDevice(physical,&device_info,nullptr,&device),"create geometry device"); vkGetDeviceQueue(device,family,0,&queue);
    VkCommandPoolCreateInfo command_info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    command_info.queueFamilyIndex=family; command_info.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    Check(vkCreateCommandPool(device,&command_info,nullptr,&pool),"create command pool");
    VkPushConstantRange push{VK_SHADER_STAGE_FRAGMENT_BIT,0,4};
    VkPipelineLayoutCreateInfo layout_info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layout_info.pushConstantRangeCount=1; layout_info.pPushConstantRanges=&push;
    Check(vkCreatePipelineLayout(device,&layout_info,nullptr,&layout),"create pipeline layout");
    VkPipelineCacheCreateInfo cache_info{VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
    Check(vkCreatePipelineCache(device,&cache_info,nullptr,&cache),"create pipeline cache");
    vs=Shader(root/"clip_probe.vert.spv"); gs=Shader(root/"clip_planes_gs.spv"); ps=Shader(root/"clip_probe.frag.spv");
    std::cerr<<"GPU: "<<properties.deviceName<<", geometry="<<features.geometryShader<<", native clip="<<features.shaderClipDistance<<'\n';
  }
  uint32_t MemoryType(uint32_t bits,VkMemoryPropertyFlags flags) {
    for(uint32_t i=0;i<memory.memoryTypeCount;++i)
      if((bits&(1u<<i)) && (memory.memoryTypes[i].propertyFlags&flags)==flags) return i;
    throw std::runtime_error("Vulkan memory type unavailable");
  }
  VkCommandBuffer Begin() {
    Check(vkResetCommandPool(device,pool,0),"reset command pool");
    VkCommandBufferAllocateInfo allocate{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocate.commandPool=pool; allocate.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY; allocate.commandBufferCount=1;
    VkCommandBuffer command{}; Check(vkAllocateCommandBuffers(device,&allocate,&command),"allocate commands");
    VkCommandBufferBeginInfo info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; info.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    Check(vkBeginCommandBuffer(command,&info),"begin commands"); return command;
  }
  void Submit(VkCommandBuffer command) {
    Check(vkEndCommandBuffer(command),"end commands"); VkFence fence{};
    VkFenceCreateInfo create{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO}; Check(vkCreateFence(device,&create,nullptr,&fence),"create fence");
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO}; submit.commandBufferCount=1; submit.pCommandBuffers=&command;
    Check(vkQueueSubmit(queue,1,&submit,fence),"submit draw");
    auto wait=vkWaitForFences(device,1,&fence,VK_TRUE,5'000'000'000ull);
    if(wait!=VK_SUCCESS) { std::cerr<<"Unproved GPU completion: "<<wait<<'\n';std::_Exit(2); }
    vkDestroyFence(device,fence,nullptr); vkFreeCommandBuffers(device,pool,1,&command);
  }
};
struct Buffer {
  Context& c; VkBuffer handle{}; VkDeviceMemory memory{}; void* mapped{};
  Buffer(Context& context,VkDeviceSize size,VkBufferUsageFlags usage):c(context) {
    VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO}; info.size=size; info.usage=usage;
    Check(vkCreateBuffer(c.device,&info,nullptr,&handle),"create buffer");
    VkMemoryRequirements req{}; vkGetBufferMemoryRequirements(c.device,handle,&req);
    VkMemoryAllocateInfo allocate{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; allocate.allocationSize=req.size;
    allocate.memoryTypeIndex=c.MemoryType(req.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    Check(vkAllocateMemory(c.device,&allocate,nullptr,&memory),"allocate buffer memory");
    Check(vkBindBufferMemory(c.device,handle,memory,0),"bind buffer");
    Check(vkMapMemory(c.device,memory,0,VK_WHOLE_SIZE,0,&mapped),"map buffer");
  }
  ~Buffer() {if(mapped) vkUnmapMemory(c.device,memory);if(handle) vkDestroyBuffer(c.device,handle,nullptr);if(memory) vkFreeMemory(c.device,memory,nullptr);}
};
struct Image {
  Context& c; VkImage handle{}; VkDeviceMemory memory{}; VkImageView view{};
  Image(Context& context,VkFormat format,VkSampleCountFlagBits samples,VkImageUsageFlags usage,VkImageAspectFlags aspect):c(context) {
    VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO}; info.imageType=VK_IMAGE_TYPE_2D; info.format=format;
    info.extent={kWidth,kHeight,1}; info.mipLevels=info.arrayLayers=1; info.samples=samples;
    info.tiling=VK_IMAGE_TILING_OPTIMAL; info.usage=usage;
    Check(vkCreateImage(c.device,&info,nullptr,&handle),"create image");
    VkMemoryRequirements req{};vkGetImageMemoryRequirements(c.device,handle,&req);
    VkMemoryAllocateInfo allocate{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};allocate.allocationSize=req.size;
    allocate.memoryTypeIndex=c.MemoryType(req.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    Check(vkAllocateMemory(c.device,&allocate,nullptr,&memory),"allocate image memory");
    Check(vkBindImageMemory(c.device,handle,memory,0),"bind image");
    VkImageViewCreateInfo view_info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};view_info.image=handle;
    view_info.viewType=VK_IMAGE_VIEW_TYPE_2D;view_info.format=format;view_info.subresourceRange={aspect,0,1,0,1};
    Check(vkCreateImageView(c.device,&view_info,nullptr,&view),"create image view");
  }
  ~Image(){if(view) vkDestroyImageView(c.device,view,nullptr);if(handle) vkDestroyImage(c.device,handle,nullptr);if(memory) vkFreeMemory(c.device,memory,nullptr);}
};
struct Target {
  Context& c; VkSampleCountFlagBits samples; Image color,depth,resolve; Buffer readback;
  VkRenderPass pass{};VkFramebuffer framebuffer{};std::map<unsigned,VkPipeline> pipelines;
  Target(Context& context,VkSampleCountFlagBits count):c(context),samples(count),
      color(c,VK_FORMAT_R8G8B8A8_UNORM,count,VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT,VK_IMAGE_ASPECT_COLOR_BIT),
      depth(c,VK_FORMAT_D32_SFLOAT,count,VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,VK_IMAGE_ASPECT_DEPTH_BIT),
      resolve(c,VK_FORMAT_R8G8B8A8_UNORM,VK_SAMPLE_COUNT_1_BIT,VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT,VK_IMAGE_ASPECT_COLOR_BIT),
      readback(c,kPixelBytes,VK_BUFFER_USAGE_TRANSFER_DST_BIT) {
    VkAttachmentDescription attachments[3]{};
    for(auto& a:attachments){a.samples=VK_SAMPLE_COUNT_1_BIT;a.loadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE;a.storeOp=VK_ATTACHMENT_STORE_OP_STORE;
      a.stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE;a.stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;a.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;}
    attachments[0].format=attachments[2].format=VK_FORMAT_R8G8B8A8_UNORM;attachments[1].format=VK_FORMAT_D32_SFLOAT;
    attachments[0].samples=attachments[1].samples=samples;
    attachments[0].loadOp=attachments[1].loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[0].finalLayout=count==VK_SAMPLE_COUNT_1_BIT?VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    attachments[1].finalLayout=VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    attachments[2].finalLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    VkAttachmentReference color_ref{0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},depth_ref{1,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL},resolve_ref{2,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};subpass.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;subpass.colorAttachmentCount=1;
    subpass.pColorAttachments=&color_ref;subpass.pDepthStencilAttachment=&depth_ref;
    if(count!=VK_SAMPLE_COUNT_1_BIT)subpass.pResolveAttachments=&resolve_ref;
    VkSubpassDependency deps[2]{};deps[0]={VK_SUBPASS_EXTERNAL,0,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT|VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,0,
      VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,0};
    deps[1]={0,VK_SUBPASS_EXTERNAL,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT,0};
    VkRenderPassCreateInfo info{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};info.attachmentCount=count==VK_SAMPLE_COUNT_1_BIT?2:3;
    info.pAttachments=attachments;info.subpassCount=1;info.pSubpasses=&subpass;info.dependencyCount=2;info.pDependencies=deps;
    Check(vkCreateRenderPass(c.device,&info,nullptr,&pass),"create render pass");
    VkImageView views[]={color.view,depth.view,resolve.view};VkFramebufferCreateInfo frame{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    frame.renderPass=pass;frame.attachmentCount=info.attachmentCount;frame.pAttachments=views;frame.width=kWidth;frame.height=kHeight;frame.layers=1;
    Check(vkCreateFramebuffer(c.device,&frame,nullptr,&framebuffer),"create framebuffer");
  }
  ~Target(){for(auto [key,pipeline]:pipelines)vkDestroyPipeline(c.device,pipeline,nullptr);if(framebuffer)vkDestroyFramebuffer(c.device,framebuffer,nullptr);if(pass)vkDestroyRenderPass(c.device,pass,nullptr);}
  VkPipeline Pipeline(bool geometry,bool cull,bool strip) {
    const unsigned key=geometry|unsigned(cull)<<1|unsigned(strip)<<2;
    if(auto it=pipelines.find(key);it!=pipelines.end())return it->second;
    VkPipelineShaderStageCreateInfo stages[3]{};unsigned count=0;
    auto stage=[&](VkShaderModule module,VkShaderStageFlagBits type){auto& s=stages[count++];s={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};s.stage=type;s.module=module;s.pName="main";};
    stage(c.vs,VK_SHADER_STAGE_VERTEX_BIT);if(geometry)stage(c.gs,VK_SHADER_STAGE_GEOMETRY_BIT);stage(c.ps,VK_SHADER_STAGE_FRAGMENT_BIT);
    VkVertexInputBindingDescription binding{0,sizeof(Vertex),VK_VERTEX_INPUT_RATE_VERTEX};
    std::vector<VkVertexInputAttributeDescription> attrs={{0,0,VK_FORMAT_R32G32B32A32_SFLOAT,0}};
    for(unsigned i=0;i<18;++i)attrs.push_back({i+1,0,VK_FORMAT_R32G32B32A32_SFLOAT,uint32_t(offsetof(Vertex,values)+i*16)});
    attrs.push_back({19,0,VK_FORMAT_R32G32B32A32_SFLOAT,offsetof(Vertex,distances)});
    attrs.push_back({20,0,VK_FORMAT_R32G32_SFLOAT,offsetof(Vertex,distances)+16});
    VkPipelineVertexInputStateCreateInfo input{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    input.vertexBindingDescriptionCount=1;input.pVertexBindingDescriptions=&binding;input.vertexAttributeDescriptionCount=uint32_t(attrs.size());input.pVertexAttributeDescriptions=attrs.data();
    VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    assembly.topology=strip?VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP:VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkViewport viewport{0,0,float(kWidth),float(kHeight),0,1};VkRect2D scissor{{0,0},{kWidth,kHeight}};
    VkPipelineViewportStateCreateInfo view{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};view.viewportCount=view.scissorCount=1;view.pViewports=&viewport;view.pScissors=&scissor;
    VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode=VK_POLYGON_MODE_FILL;raster.cullMode=cull?VK_CULL_MODE_BACK_BIT:VK_CULL_MODE_NONE;raster.frontFace=VK_FRONT_FACE_COUNTER_CLOCKWISE;raster.lineWidth=1;
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};ms.rasterizationSamples=samples;
    VkPipelineDepthStencilStateCreateInfo depth_state{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};depth_state.depthTestEnable=depth_state.depthWriteEnable=VK_TRUE;depth_state.depthCompareOp=VK_COMPARE_OP_LESS_OR_EQUAL;
    VkPipelineColorBlendAttachmentState attachment{};attachment.colorWriteMask=15;
    VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};blend.attachmentCount=1;blend.pAttachments=&attachment;
    VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};info.stageCount=count;info.pStages=stages;
    info.pVertexInputState=&input;info.pInputAssemblyState=&assembly;info.pViewportState=&view;info.pRasterizationState=&raster;
    info.pMultisampleState=&ms;info.pDepthStencilState=&depth_state;info.pColorBlendState=&blend;info.layout=c.layout;info.renderPass=pass;
    VkPipeline result{};Check(vkCreateGraphicsPipelines(c.device,c.cache,1,&info,nullptr,&result),"create clip pipeline");pipelines[key]=result;return result;
  }
  std::array<uint8_t,kPixelBytes> Render(Buffer& vertices,unsigned vertex_count,bool geometry,bool cull,bool strip,unsigned index) {
    const auto pipeline=Pipeline(geometry,cull,strip);auto command=c.Begin();
    VkClearValue clear[2]{};clear[1].depthStencil={1,0};
    VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};begin.renderPass=pass;begin.framebuffer=framebuffer;
    begin.renderArea={{0,0},{kWidth,kHeight}};begin.clearValueCount=2;begin.pClearValues=clear;
    vkCmdBeginRenderPass(command,&begin,VK_SUBPASS_CONTENTS_INLINE);vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline);
    VkDeviceSize offset=0;vkCmdBindVertexBuffers(command,0,1,&vertices.handle,&offset);
    vkCmdPushConstants(command,c.layout,VK_SHADER_STAGE_FRAGMENT_BIT,0,4,&index);vkCmdDraw(command,vertex_count,1,0,0);vkCmdEndRenderPass(command);
    VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={kWidth,kHeight,1};
    vkCmdCopyImageToBuffer(command,samples==VK_SAMPLE_COUNT_1_BIT?color.handle:resolve.handle,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,readback.handle,1,&copy);
    VkBufferMemoryBarrier host{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};host.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;host.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
    host.srcQueueFamilyIndex=host.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;host.buffer=readback.handle;host.size=VK_WHOLE_SIZE;
    vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,nullptr,1,&host,0,nullptr);c.Submit(command);
    std::array<uint8_t,kPixelBytes> output;std::memcpy(output.data(),readback.mapped,output.size());return output;
  }
};

// CPU oracle uses doubles and emits an ordinary triangle fan, independently of
// the production shader's float implementation and alternating triangle strip.
using Wide=std::array<double,82>;
using Plane=std::array<double,4>;
std::vector<Wide> Clip(std::vector<Wide> polygon) {
  for(unsigned plane=0;plane<6 && !polygon.empty();++plane) {
    std::vector<Wide> next;auto previous=polygon.back();
    for(const auto& current:polygon) {
      const double a=previous[76+plane],b=current[76+plane];
      if((a>=0)!=(b>=0)){Wide edge;const double t=a/(a-b);for(unsigned i=0;i<82;++i)edge[i]=previous[i]+(current[i]-previous[i])*t;next.push_back(edge);}
      if(b>=0)next.push_back(current);previous=current;
    }
    polygon=std::move(next);
  }
  return polygon;
}
Wide Widen(const Vertex& v){Wide result;
  for(unsigned i=0;i<4;++i)result[i]=v.position[i];
  for(unsigned j=0;j<18;++j)for(unsigned i=0;i<4;++i)result[4+j*4+i]=v.values[j][i];
  for(unsigned i=0;i<6;++i)result[76+i]=v.distances[i];return result;}
Vertex Narrow(const Wide& v){Vertex result;
  for(unsigned i=0;i<4;++i)result.position[i]=float(v[i]);
  for(unsigned j=0;j<18;++j)for(unsigned i=0;i<4;++i)result.values[j][i]=float(v[4+j*4+i]);
  for(unsigned i=0;i<6;++i)result.distances[i]=float(v[76+i]);return result;}
std::vector<Vertex> Reference(const std::vector<Vertex>& input,bool strip,unsigned& max_polygon) {
  std::vector<Vertex> result;
  for(unsigned i=0;i+2<input.size();i+=strip?1:3) {
    unsigned a=i,b=i+1,d=i+2;if(strip && (i&1))std::swap(a,b);
    const auto polygon=Clip({Widen(input[a]),Widen(input[b]),Widen(input[d])});max_polygon=std::max(max_polygon,unsigned(polygon.size()));
    for(unsigned v=1;v+1<polygon.size();++v)for(auto index:{0u,v,v+1})result.push_back(Narrow(polygon[index]));
  }
  return result;
}
std::vector<Plane> NineVertexPlanes() {
  // Successively trim one corner of a convex polygon. Six trims must produce
  // nine vertices; this exercises the shader's advertised output bound.
  std::vector<Wide> polygon(3);const double xy[3][2]={{-.875,-.75},{.875,-.75},{0,.875}};
  for(unsigned v=0;v<3;++v){polygon[v][0]=xy[v][0];polygon[v][1]=xy[v][1];polygon[v][3]=1;for(unsigned p=0;p<6;++p)polygon[v][76+p]=1;}
  std::vector<Plane> planes;
  for(unsigned step=0;step<6;++step) {
    const unsigned n=unsigned(polygon.size()),i=step%n;
    auto before=polygon[(i+n-1)%n],point=polygon[i],after=polygon[(i+1)%n];
    double ax=point[0]+(before[0]-point[0])*.25,ay=point[1]+(before[1]-point[1])*.25;
    double bx=point[0]+(after[0]-point[0])*.25,by=point[1]+(after[1]-point[1])*.25;
    Plane plane={by-ay,ax-bx,0,0};plane[3]=-(plane[0]*ax+plane[1]*ay);
    double cx=0,cy=0;for(const auto& v:polygon){cx+=v[0]/n;cy+=v[1]/n;}
    if(plane[0]*cx+plane[1]*cy+plane[3]<0)for(auto& c:plane)c=-c;
    planes.push_back(plane);
    for(auto& v:polygon){for(unsigned p=0;p<6;++p)v[76+p]=1;v[76+step]=v[0]*plane[0]+v[1]*plane[1]+plane[3];}
    polygon=Clip(polygon);Require(polygon.size()==step+4,"CPU nine-vertex fixture");
  }
  return planes;
}
std::vector<Vertex> Input(unsigned test,bool flip) {
  const bool strip=test==13;const unsigned count=strip?4:3;
  const float triangle[3][2]={{-.875f,-.75f},{.875f,-.75f},{0,.875f}};
  const float quad[4][2]={{-.875f,-.75f},{.875f,-.75f},{-.875f,.875f},{.875f,.875f}};
  const Plane planes[6]={{{1,0,0,.125}},{{-1,0,0,.3}},{{0,1,0,.25}},{{0,-1,0,.2}},{{1,1,0,.25}},{{-2,1,0,.5}}};
  const auto nine=NineVertexPlanes();std::vector<Vertex> result(count);
  for(unsigned v=0;v<count;++v) {
    auto& out=result[v];const float w=test==14?(v==0?-.25f:v==1?1.f:2.f):test==11?(v==0?.5f:v==1?1.f:2.f):1.f;
    out.position[0]=(strip?quad[v][0]:triangle[v][0])*w*(flip?-1.f:1.f);
    out.position[1]=(strip?quad[v][1]:triangle[v][1])*w;out.position[2]=(.2f+.12f*v)*w;out.position[3]=w;
    if(test==15)out.position[2]=(v==0?-.25f:v==1?.5f:1.25f)*w;
    for(unsigned j=0;j<18;++j)for(unsigned c=0;c<4;++c)out.values[j][c]=.03f+.018f*j+.025f*c+.017f*v*(1+(j%4));
    for(unsigned p=0;p<6;++p) {
      out.distances[p]=1;
      if(test==1 && p==0)out.distances[p]=-1;
      bool active=(test>=2 && test<=7 && p==test-2)||test==8||test==9||test==11||test==12||test==13||test==14||(test==15 && p==2);
      if(active){const auto& plane=test==9?nine[p]:planes[p];double value=0;for(unsigned k=0;k<4;++k)value+=plane[k]*out.position[k];out.distances[p]=float(value);}
      if(test==10 && p==0)out.distances[p]=v==0?0.f:v==1?-1.f:1.f;
    }
    // Distances precede the adjustment, matching the original generated VS.
    if(test==12){out.position[0]+=.0625f*w;out.position[1]-=.0625f*w;}
  }
  return result;
}
}  // namespace

int main(int argc,char** argv) {
  try {
    Require(argc==2,"usage: android_clip_probe <diagnostic directory>");std::filesystem::path root(argv[1]);
    Context context;context.Init(root);unsigned comparisons=0,max_polygon=0;uint64_t channels=0,covered=0;unsigned max_error=0;
    for(auto samples:{VK_SAMPLE_COUNT_1_BIT,VK_SAMPLE_COUNT_4_BIT}) {
      Target target(context,samples);
      for(unsigned test=0;test<16;++test)for(bool flip:{false,true})for(bool cull:{false,true}) {
        const auto input=Input(test,flip);const bool strip=test==13;const auto reference=Reference(input,strip,max_polygon);
        Buffer original(context,input.size()*sizeof(Vertex),VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        Buffer clipped(context,std::max(reference.size(),size_t(1))*sizeof(Vertex),VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        std::memcpy(original.mapped,input.data(),input.size()*sizeof(Vertex));
        if(!reference.empty())std::memcpy(clipped.mapped,reference.data(),reference.size()*sizeof(Vertex));
        for(unsigned index=0;index<20;++index) {
          const auto actual=target.Render(original,unsigned(input.size()),true,cull,strip,index);
          const auto expected=target.Render(clipped,unsigned(reference.size()),false,cull,false,index);
          for(unsigned pixel=0;pixel<kWidth*kHeight;++pixel) {
            if((actual[pixel*4+3]!=0)!=(expected[pixel*4+3]!=0)) {
              std::cerr<<"Coverage mismatch: case="<<test<<" samples="<<samples<<" flip="<<flip<<" cull="<<cull
                <<" varying="<<index<<" pixel="<<pixel<<" actual_alpha="<<unsigned(actual[pixel*4+3])
                <<" expected_alpha="<<unsigned(expected[pixel*4+3])<<'\n';
              std::ofstream(root/"clip-actual.rgba",std::ios::binary).write(reinterpret_cast<const char*>(actual.data()),actual.size());
              std::ofstream(root/"clip-reference.rgba",std::ios::binary).write(reinterpret_cast<const char*>(expected.data()),expected.size());
              throw std::runtime_error("clip coverage mismatch");
            }
            covered+=actual[pixel*4+3]!=0;
            for(unsigned component=0;component<4;++component) {
              const unsigned offset=pixel*4+component;const unsigned error=unsigned(std::abs(int(actual[offset])-int(expected[offset])));
              max_error=std::max(max_error,error);
              if(error>1){std::cerr<<"Mismatch: case="<<test<<" samples="<<samples<<" flip="<<flip<<" cull="<<cull<<" varying="<<index<<" pixel="<<pixel<<" component="<<component<<" actual="<<unsigned(actual[offset])<<" expected="<<unsigned(expected[offset])<<'\n';throw std::runtime_error("clip interpolation/depth/facing mismatch");}
              ++channels;
            }
          }
          ++comparisons;
        }
      }
      std::cerr<<"Clipping comparisons passed at "<<samples<<" samples.\n";
    }
    Require(max_polygon==9 && covered!=0,"clip test fixture coverage/output bound");
    std::ofstream result(root/"clip-result.json");
    result<<"{\"gpu\":\""<<context.properties.deviceName<<"\",\"geometry_shader\":true,\"native_clip_distance\":false,"
      <<"\"base_cases\":16,\"sample_counts\":[1,4],\"render_comparisons\":"<<comparisons<<",\"channel_comparisons\":"<<channels
      <<",\"covered_pixel_comparisons\":"<<covered<<",\"max_channel_error\":"<<max_error<<",\"max_polygon_vertices\":"<<max_polygon
      <<",\"interpolators_checked\":18,\"reference\":\"double CPU clipping and triangle-fan rasterization\",\"game_renderer_started\":false,\"guest_entry_executed\":false}\n";
    result.close();Require(bool(result),"write clip result");std::cout<<"Production geometry clipping matches CPU reference.\n";return 0;
  }catch(const std::exception& error){std::cerr<<"clip probe: "<<error.what()<<'\n';return 1;}
}
