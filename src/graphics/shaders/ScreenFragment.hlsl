[[vk::binding(0, 0)]] Texture2D in_texture;
[[vk::binding(1, 0)]] SamplerState in_sampler;
[[vk::binding(0, 1)]] cbuffer AlphaBlock {
    float alpha;
};

float4 main([[vk::location(0)]] float2 in_uv : TEXCOORD0) : SV_Target0 {
    float4 color = in_texture.Sample(in_sampler, in_uv);
    color.a = alpha;
    return color;
}
