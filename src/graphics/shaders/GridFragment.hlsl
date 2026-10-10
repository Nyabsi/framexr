float4 main([[vk::location(0)]] float2 in_uv : TEXCOORD0) : SV_Target0 {
    float fade = max(1.0 - 2.0 * length(in_uv - 0.5), 0.0);
    float grid = (frac(in_uv.x / 0.0005) < 0.01 || frac(in_uv.y / 0.0005) < 0.01) ? 1.0 : 0.0;
    return float4(1.0, 1.0, 1.0, grid * fade);
}
