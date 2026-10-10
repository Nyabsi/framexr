struct VertexOutput {
    float4 position : SV_Position;
    [[vk::location(0)]] float2 uv : TEXCOORD0;
};

VertexOutput main([[vk::location(0)]] float2 in_position : POSITION0, [[vk::location(1)]] float2 in_uv : TEXCOORD0) {
    VertexOutput output;
    output.uv = in_uv;
    output.position = float4(in_position * 2.0 - 1.0, 0.0, 1.0);
    return output;
}
