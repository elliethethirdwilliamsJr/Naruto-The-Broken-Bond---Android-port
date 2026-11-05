// Use the production HLSL stage interface throughout. The initial diagnostic
// mixed GLSL array varyings and individual HLSL varyings and failed comparison.
#include "../../src/gpu/backend/clip_interface.hlsli"
struct InputVertex {
    [[vk::location(0)]] float4 position : POSITION0;
#define INPUT_INTERPOLATOR(n) [[vk::location(n + 1)]] float4 value##n : TEXCOORD##n;
    CLIP_INTERPOLATORS(INPUT_INTERPOLATOR)
#undef INPUT_INTERPOLATOR
    [[vk::location(19)]] float4 clip0 : TEXCOORD18;
    [[vk::location(20)]] float2 clip1 : TEXCOORD19;
};
ClipVertex vs_main(InputVertex input) {
    ClipVertex output;
    output.position = input.position;
#define COPY_INPUT(n) output.value##n = input.value##n;
    CLIP_INTERPOLATORS(COPY_INPUT)
#undef COPY_INPUT
    output.clip0 = input.clip0;
    output.clip1 = input.clip1;
    return output;
}
struct Settings { uint index; };
[[vk::push_constant]] ConstantBuffer<Settings> settings;
float4 ps_main(RasterVertex input, bool facing : SV_IsFrontFace) : SV_Target0 {
    switch (settings.index) {
#define SELECT_INTERPOLATOR(n) case n: return input.value##n;
        CLIP_INTERPOLATORS(SELECT_INTERPOLATOR)
#undef SELECT_INTERPOLATOR
        case 18: return facing ? float4(1,0,0,1) : float4(0,1,0,1);
        default: return float4(input.position.z, input.position.w * 0.25, 0.5, 1);
    }
}
