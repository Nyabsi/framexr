[[vk::binding(0, 0)]] cbuffer ColorBlock {
    float4 in_color;
    float2 unused;
};

float4 main([[vk::location(0)]] float2 in_uv : TEXCOORD0) : SV_Target0 {
    return in_color;
}
