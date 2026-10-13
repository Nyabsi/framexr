[[vk::binding(0, 0)]] Texture2D in_texture;
[[vk::binding(1, 0)]] SamplerState in_sampler;

float4 main([[vk::location(0)]] float2 in_uv : TEXCOORD0) : SV_Target0 {
    return in_texture.Sample(in_sampler, in_uv);
}
