#ifndef NATIVE_GEOMETRY_CLIP_INTERFACE_INCLUDED
#define NATIVE_GEOMETRY_CLIP_INTERFACE_INCLUDED

#define CLIP_INTERPOLATORS(M) \
    M(0) M(1) M(2) M(3) M(4) M(5) M(6) M(7) M(8) \
    M(9) M(10) M(11) M(12) M(13) M(14) M(15) M(16) M(17)
#define DECLARE_INTERPOLATOR(n) [[vk::location(n)]] float4 value##n : TEXCOORD##n;
struct ClipVertex {
    float4 position : SV_Position;
    CLIP_INTERPOLATORS(DECLARE_INTERPOLATOR)
    [[vk::location(18)]] float4 clip0 : TEXCOORD18;
    [[vk::location(19)]] float2 clip1 : TEXCOORD19;
};
struct RasterVertex {
    float4 position : SV_Position;
    CLIP_INTERPOLATORS(DECLARE_INTERPOLATOR)
};
#undef DECLARE_INTERPOLATOR

RasterVertex Raster(ClipVertex vertex) {
    RasterVertex output;
    output.position = vertex.position;
#define COPY_INTERPOLATOR(n) output.value##n = vertex.value##n;
    CLIP_INTERPOLATORS(COPY_INTERPOLATOR)
#undef COPY_INTERPOLATOR
    return output;
}
ClipVertex Intersect(ClipVertex first, ClipVertex second, float amount) {
    ClipVertex output;
    output.position = lerp(first.position, second.position, amount);
#define LERP_INTERPOLATOR(n) output.value##n = lerp(first.value##n, second.value##n, amount);
    CLIP_INTERPOLATORS(LERP_INTERPOLATOR)
#undef LERP_INTERPOLATOR
    output.clip0 = lerp(first.clip0, second.clip0, amount);
    output.clip1 = lerp(first.clip1, second.clip1, amount);
    return output;
}
float Distance(ClipVertex vertex, uint plane) {
    return plane < 4 ? vertex.clip0[plane] : vertex.clip1[plane - 4];
}
#endif
