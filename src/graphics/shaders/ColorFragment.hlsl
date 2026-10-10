[[vk::binding(0, 0)]] cbuffer ColorBlock {
    float4 in_color;
    float2 corner_radius;
};

float4 main([[vk::location(0)]] float2 in_uv : TEXCOORD0) : SV_Target0 {
    float4 color = in_color;
    float2 uv_circle = ((1.0 - corner_radius) - (abs(in_uv - 0.5) * 2.0)) / corner_radius;
    float dist = length(uv_circle);
    color.a = lerp(color.a, 0.0, float(dist > 1.0) * float(uv_circle.x < 0.0) * float(uv_circle.y < 0.0));
    return color;
}
