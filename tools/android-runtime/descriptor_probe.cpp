#define BROKEN_BOND_TEXTURE_SUPPORT_ONLY
#include "texture_probe.cpp"
#include "gpu/backend/vulkan/bounded_descriptors.h"

namespace {
struct DimensionImage {
  Context& c; unsigned dimension; VkImage image{}; VkDeviceMemory memory{}; VkImageView view{};
  DimensionImage(Context& context, unsigned dim) : c(context), dimension(dim) {
    VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    info.imageType=dim==1?VK_IMAGE_TYPE_3D:VK_IMAGE_TYPE_2D;info.extent={2,2,dim==1?2u:1u};
    info.format=VK_FORMAT_R8G8B8A8_UNORM;info.mipLevels=1;info.arrayLayers=dim==2?6:1;
    info.samples=VK_SAMPLE_COUNT_1_BIT;info.tiling=VK_IMAGE_TILING_OPTIMAL;
    info.flags=dim==2?VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT:0;
    info.usage=VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    Check(vkCreateImage(c.device,&info,nullptr,&image),"create dimension image");
    VkMemoryRequirements req{};vkGetImageMemoryRequirements(c.device,image,&req);
    VkMemoryAllocateInfo allocate{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};allocate.allocationSize=req.size;
    allocate.memoryTypeIndex=c.MemoryType(req.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    Check(vkAllocateMemory(c.device,&allocate,nullptr,&memory),"allocate dimension image");
    Check(vkBindImageMemory(c.device,image,memory,0),"bind dimension image");
    VkImageViewCreateInfo v{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};v.image=image;v.format=info.format;
    v.viewType=dim==0?VK_IMAGE_VIEW_TYPE_2D:dim==1?VK_IMAGE_VIEW_TYPE_3D:VK_IMAGE_VIEW_TYPE_CUBE;
    v.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,info.arrayLayers};
    Check(vkCreateImageView(c.device,&v,nullptr,&view),"create dimension view");
  }
  ~DimensionImage(){if(view)vkDestroyImageView(c.device,view,nullptr);if(image)vkDestroyImage(c.device,image,nullptr);if(memory)vkFreeMemory(c.device,memory,nullptr);}
  void Clear(VkCommandBuffer command,const std::array<float,4>& color) {
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED;barrier.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
    barrier.image=image;barrier.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,dimension==2?6u:1u};barrier.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&barrier);
    VkClearColorValue value{};std::copy(color.begin(),color.end(),value.float32);
    vkCmdClearColorImage(command,image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,&value,1,&barrier.subresourceRange);
    barrier.oldLayout=barrier.newLayout;barrier.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,0,nullptr,0,nullptr,1,&barrier);
  }
};
struct ProbePipeline {
  Context& c;std::array<VkDescriptorSetLayout,4> layouts{};
  VkDescriptorSetLayout output_layout{};VkPipelineLayout layout{};VkPipeline pipeline{};
  VkDescriptorPool output_pool{};VkDescriptorSet output_set{};std::array<VkSampler,2> samplers{};
  ProbePipeline(Context& context,const std::filesystem::path& root,Buffer& output):c(context) {
    Check(gpu::vulkan::CreateBoundedLayouts(c.device,layouts),"production bounded layouts");
    VkDescriptorSetLayoutBinding binding{0,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr};
    VkDescriptorSetLayoutCreateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};info.bindingCount=1;info.pBindings=&binding;
    Check(vkCreateDescriptorSetLayout(c.device,&info,nullptr,&output_layout),"output layout");
    const VkDescriptorSetLayout all[]={layouts[0],layouts[1],layouts[2],layouts[3],output_layout};
    VkPushConstantRange push{VK_SHADER_STAGE_COMPUTE_BIT,0,16};
    VkPipelineLayoutCreateInfo pipeline_info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};pipeline_info.setLayoutCount=5;pipeline_info.pSetLayouts=all;
    pipeline_info.pushConstantRangeCount=1;pipeline_info.pPushConstantRanges=&push;
    Check(vkCreatePipelineLayout(c.device,&pipeline_info,nullptr,&layout),"bounded compute layout");
    std::ifstream file(root/"descriptor_probe.spv",std::ios::binary|std::ios::ate);Require(bool(file),"open descriptor shader");
    auto size=file.tellg();Require(size>0 && size%4==0,"descriptor shader size");std::vector<uint32_t> code(size_t(size)/4);
    file.seekg(0);file.read(reinterpret_cast<char*>(code.data()),size);Require(bool(file),"read descriptor shader");
    VkShaderModuleCreateInfo module_info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};module_info.codeSize=size_t(size);module_info.pCode=code.data();VkShaderModule module{};
    Check(vkCreateShaderModule(c.device,&module_info,nullptr,&module),"descriptor shader module");
    VkComputePipelineCreateInfo compute{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};compute.layout=layout;
    compute.stage={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};compute.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;compute.stage.module=module;compute.stage.pName="main";
    auto result=vkCreateComputePipelines(c.device,VK_NULL_HANDLE,1,&compute,nullptr,&pipeline);vkDestroyShaderModule(c.device,module,nullptr);Check(result,"bounded compute pipeline");
    VkDescriptorPoolSize pool_size{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1};VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool.maxSets=1;pool.poolSizeCount=1;pool.pPoolSizes=&pool_size;Check(vkCreateDescriptorPool(c.device,&pool,nullptr,&output_pool),"output pool");
    VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};allocate.descriptorPool=output_pool;allocate.descriptorSetCount=1;allocate.pSetLayouts=&output_layout;
    Check(vkAllocateDescriptorSets(c.device,&allocate,&output_set),"output descriptor");
    VkDescriptorBufferInfo buffer{output.handle,0,VK_WHOLE_SIZE};VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet=output_set;write.descriptorCount=1;write.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;write.pBufferInfo=&buffer;vkUpdateDescriptorSets(c.device,1,&write,0,nullptr);
    for(unsigned i=0;i<2;++i){VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
      sampler.addressModeU=sampler.addressModeV=sampler.addressModeW=i?VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER:VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
      sampler.borderColor=VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;sampler.maxLod=0;
      Check(vkCreateSampler(c.device,&sampler,nullptr,&samplers[i]),"probe sampler");}
  }
  ~ProbePipeline(){for(auto s:samplers)if(s)vkDestroySampler(c.device,s,nullptr);if(output_pool)vkDestroyDescriptorPool(c.device,output_pool,nullptr);
    if(pipeline)vkDestroyPipeline(c.device,pipeline,nullptr);if(layout)vkDestroyPipelineLayout(c.device,layout,nullptr);
    if(output_layout)vkDestroyDescriptorSetLayout(c.device,output_layout,nullptr);for(auto l:layouts)if(l)vkDestroyDescriptorSetLayout(c.device,l,nullptr);}
};
unsigned CheckRemap() {
  for(unsigned reg=0;reg<20;++reg)for(bool cube:{false,true}) {
    std::array<uint8_t,512> shared;shared.fill(0xA5);auto expected=shared;
    const uint32_t slot=reg+1;const unsigned image=reg<16?(cube?128:0)+reg*4:288+(reg-16)*4;
    const unsigned sampler=reg<16?192+reg*4:304+(reg-16)*4;
    std::memcpy(expected.data()+image,&slot,4);std::memcpy(expected.data()+sampler,&slot,4);
    Require(gpu::vulkan::RemapBoundedRegister(shared,reg,cube) && shared==expected,"register remap damaged constants or used wrong index");
  }
  std::array<uint8_t,512> shared{};auto original=shared;
  Require(!gpu::vulkan::RemapBoundedRegister(shared,20,false) && shared==original,"invalid register accepted");return 41;
}
} // namespace

int main(int argc,char** argv) {
  try {
    Require(argc==2,"usage: android_descriptor_probe <directory>");const std::filesystem::path root(argv[1]);
    const unsigned remaps=CheckRemap();Context c;c.Init(root/"texture_probe.spv",true);
    Require(c.properties.limits.maxPerStageDescriptorSampledImages>=192 && c.properties.limits.maxPerStageDescriptorSamplers>=64 &&
      c.properties.limits.maxPerStageResources>=257 && c.properties.limits.maxBoundDescriptorSets>=5,"bounded probe limits");
    constexpr unsigned snapshots=35,lookups=6;
    Buffer output(c,snapshots*lookups*4*sizeof(float),VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    ProbePipeline pipeline(c,root,output);
    std::vector<std::unique_ptr<DimensionImage>> images;
    for(unsigned dim=0;dim<3;++dim)for(unsigned variant=0;variant<3;++variant)images.push_back(std::make_unique<DimensionImage>(c,dim));
    const std::array<float,4> colors[]={{0,0,0,0},{1,0,0,1},{0,1,0,1}};
    auto command=c.Begin();for(unsigned dim=0;dim<3;++dim)for(unsigned variant=0;variant<3;++variant)images[dim*3+variant]->Clear(command,colors[variant]);c.Submit(command);
    gpu::vulkan::BoundedDescriptorPool descriptors(c.device);unsigned values=0,negative=0;
    for(unsigned round=0;round<3;++round) {
      std::memset(output.mapped,0xCD,snapshots*lookups*4*sizeof(float));
      std::vector<std::array<float,4>> expected;
      command=c.Begin();vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline.pipeline);
      vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline.layout,4,1,&pipeline.output_set,0,nullptr);
      for(unsigned draw=0;draw<snapshots;++draw) {
        gpu::vulkan::BoundedImages views{};gpu::vulkan::BoundedSamplers samplers{};
        for(unsigned dim=0;dim<3;++dim)views[dim].fill(images[dim*3]->view);
        samplers.fill(pipeline.samplers[0]);const unsigned reg=draw%20,slot=reg+1,variant=1+(draw+round)%2,sampler=(draw+round)%2;
        for(unsigned dim=0;dim<3;++dim)views[dim][slot]=images[dim*3+variant]->view;
        samplers[slot]=pipeline.samplers[sampler];
        std::array<VkDescriptorSet,4> sets{};
        if(draw==0){auto invalid=views;invalid[1][63]=VK_NULL_HANDLE;
          Require(descriptors.Allocate(pipeline.layouts,invalid,samplers,sets)==VK_ERROR_INITIALIZATION_FAILED,"unpopulated descriptor accepted");++negative;}
        Check(descriptors.Allocate(pipeline.layouts,views,samplers,sets),"production immutable snapshot allocation");
        vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline.layout,0,4,sets.data(),0,nullptr);
        std::array<uint8_t,512> shared{};Require(gpu::vulkan::RemapBoundedRegister(shared,reg,false),"shared remap");
        uint32_t image_index=0,sampler_index=0;
        std::memcpy(&image_index,shared.data()+(reg<16?reg*4:288+(reg-16)*4),4);
        std::memcpy(&sampler_index,shared.data()+(reg<16?192+reg*4:304+(reg-16)*4),4);
        for(unsigned query=0;query<lookups;++query) {
          const unsigned dim=query%3;const bool fallback=query>=3;
          const uint32_t push[]={fallback?63u:image_index,fallback?63u:sampler_index,dim,draw*lookups+query};
          vkCmdPushConstants(command,pipeline.layout,VK_SHADER_STAGE_COMPUTE_BIT,0,16,push);vkCmdDispatch(command,1,1,1);
          expected.push_back(fallback?colors[0]:sampler && dim!=2?std::array<float,4>{1,1,1,1}:colors[variant]);
        }
      }
      VkBufferMemoryBarrier barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};barrier.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
      barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;barrier.buffer=output.handle;barrier.size=VK_WHOLE_SIZE;
      vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,nullptr,1,&barrier,0,nullptr);c.Submit(command);
      const float* data=static_cast<float*>(output.mapped);
      for(unsigned pixel=0;pixel<expected.size();++pixel)for(unsigned component=0;component<4;++component){
        const auto got=data[pixel*4+component],want=expected[pixel][component];
        if(!std::isfinite(got)||std::abs(got-want)>.0001f){std::cerr<<"round="<<round<<" lookup="<<pixel<<" component="<<component<<" got="<<got<<" expected="<<want<<'\n';throw std::runtime_error("descriptor snapshot sampling mismatch");}++values;}
      descriptors.Recycle();std::cerr<<"Bounded descriptors round "<<round+1<<": 35 immutable snapshots and pool rollover passed.\n";
    }
    std::ofstream result(root/"descriptor-result.json");result<<"{\"gpu\":\""<<c.properties.deviceName<<"\",\"slots_per_array\":64,\"texture_dimensions\":3,\"rounds\":3,\"immutable_snapshots\":105,\"sampled_values_checked\":"<<values
      <<",\"register_remap_checks\":"<<remaps<<",\"unpopulated_rejections\":"<<negative<<",\"descriptor_indexing_enabled\":false,\"game_renderer_started\":false,\"guest_entry_executed\":false}\n";
    result.close();Require(bool(result),"write descriptor result");std::cout<<"Production bounded descriptor pool, remap, recycling and GPU sampling passed.\n";return 0;
  } catch(const std::exception& error){std::cerr<<"descriptor probe: "<<error.what()<<'\n';return 1;}
}
