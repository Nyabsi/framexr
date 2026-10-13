[[vk::binding(0, 0)]] Texture2D in_texture;
[[vk::binding(1, 0)]] SamplerState in_sampler;
[[vk::binding(0, 1)]] cbuffer ColorBlock {
    float4 in_color;
};

float4 main([[vk::location(0)]] float2 in_uv : TEXCOORD0) : SV_Target0 {
    float coverage = in_texture.Sample(in_sampler, in_uv).r;
    return float4(coverage, coverage, coverage, coverage) * in_color;
}
