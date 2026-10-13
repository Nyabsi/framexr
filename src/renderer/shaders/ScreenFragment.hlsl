[[vk::binding(0, 0)]] Texture2D in_texture;
[[vk::binding(1, 0)]] SamplerState in_sampler;
[[vk::binding(0, 1)]] cbuffer ScreenBlock {
    float2 overlay_size;
    float2 pixels_per_radian;
    float3 eye;
    float cutoff;
    float kaiser_alpha;
    float alpha;
    float filtered;
};

static const float kMaxRadius = 3.0;
static const float kMinVisible = 0.2;

float2 Footprint(float2 uv, float2 texture_size) {
    if (pixels_per_radian.x <= 0.0 || pixels_per_radian.y <= 0.0) {
        return float2(1.0, 1.0);
    }
    float3 view = float3((uv.x - 0.5) * overlay_size.x, (0.5 - uv.y) * overlay_size.y, 0.0) - eye;
    float distance = max(length(view), 1e-4);
    float3 direction = view / distance;
    float2 visible = sqrt(max(1.0 - direction.xy * direction.xy, kMinVisible * kMinVisible));
    float2 texel_size = overlay_size / texture_size;
    return distance / (texel_size * visible) / pixels_per_radian;
}

float BesselI0(float x) {
    float quarter_square = 0.25 * x * x;
    float term = 1.0;
    float sum = 1.0;
    [unroll] for (int k = 1; k < 12; ++k) {
        term *= quarter_square / float(k * k);
        sum += term;
    }
    return sum;
}

float Kaiser(float distance, float shape) {
    return distance >= 1.0 ? 0.0 : BesselI0(shape * sqrt(1.0 - distance * distance));
}

float4 main([[vk::location(0)]] float2 in_uv : TEXCOORD0) : SV_Target0 {
    float4 color = in_texture.Sample(in_sampler, in_uv);
    if (filtered > 0.5) {
        uint width;
        uint height;
        in_texture.GetDimensions(width, height);
        int2 last_texel = int2(width, height) - 1;
        float2 radius = clamp(Footprint(in_uv, float2(width, height)), 1.0, kMaxRadius) / cutoff;
        float2 center = in_uv * float2(width, height) - 0.5;
        int2 low = int2(ceil(center - radius));
        int2 high = int2(floor(center + radius));
        float4 sum = 0.0;
        float weight_sum = 0.0;
        [loop] for (int v = low.y; v <= high.y; ++v) {
            [loop] for (int u = low.x; u <= high.x; ++u) {
                float weight = Kaiser(length((center - float2(u, v)) / radius), kaiser_alpha);
                sum += weight * in_texture.Load(int3(clamp(int2(u, v), int2(0, 0), last_texel), 0));
                weight_sum += weight;
            }
        }
        if (weight_sum > 0.0) {
            color = sum / weight_sum;
        }
    }
    color.a = alpha;
    return color;
}
