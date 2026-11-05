Texture2D<float4> g_Texture2DDescriptorHeap[64] : register(t0, space0);
Texture3D<float4> g_Texture3DDescriptorHeap[64] : register(t0, space1);
TextureCube<float4> g_TextureCubeDescriptorHeap[64] : register(t0, space2);
SamplerState g_SamplerDescriptorHeap[64] : register(s0, space3);
RWStructuredBuffer<float4> results : register(u0, space4);
struct Settings { uint image; uint sampler; uint dimension; uint output_index; };
[[vk::push_constant]] ConstantBuffer<Settings> settings;
[numthreads(1,1,1)]
void main() {
    float4 value;
    if (settings.dimension == 0)
        value = g_Texture2DDescriptorHeap[settings.image].SampleLevel(g_SamplerDescriptorHeap[settings.sampler], float2(-1,-1), 0);
    else if (settings.dimension == 1)
        value = g_Texture3DDescriptorHeap[settings.image].SampleLevel(g_SamplerDescriptorHeap[settings.sampler], float3(-1,-1,-1), 0);
    else
        value = g_TextureCubeDescriptorHeap[settings.image].SampleLevel(g_SamplerDescriptorHeap[settings.sampler], float3(1,0,0), 0);
    results[settings.output_index] = value;
}
