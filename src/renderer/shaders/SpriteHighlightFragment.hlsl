[[vk::binding(0, 0)]] Texture2D in_texture;
[[vk::binding(1, 0)]] SamplerState in_sampler;
[[vk::binding(0, 1)]] cbuffer SpriteBlock {
    float4 st;
    float4 tint;
};

float4 main([[vk::location(0)]] float2 in_uv : TEXCOORD0) : SV_Target0 {
    return in_texture.Sample(in_sampler, in_uv * st.xy + st.zw).a * tint;
}
