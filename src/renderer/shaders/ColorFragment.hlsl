[[vk::binding(0, 0)]] cbuffer ColorBlock {
    float4 in_color;
    float4 outline_color;
    float2 rect_size;
    float corner_radius;
    float outline_width;
};

float RoundedRectDistance(float2 position, float2 half_size, float radius) {
    float2 q = abs(position) - half_size + radius;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - radius;
}

float4 main([[vk::location(0)]] float2 in_uv : TEXCOORD0) : SV_Target0 {
    float2 position = (in_uv - 0.5) * rect_size;
    float distance = RoundedRectDistance(position, rect_size * 0.5, corner_radius);
    float coverage = saturate(0.5 - distance);

    float4 color = in_color;
    if (outline_width > 0.0) {
        float edge = saturate(distance + outline_width + 0.5);
        float glow = exp(-max(-distance - outline_width, 0.0) / (outline_width * 2.0)) * 0.5;
        color = lerp(in_color, outline_color, max(edge, glow));
    }
    color.a *= coverage;
    return color;
}
