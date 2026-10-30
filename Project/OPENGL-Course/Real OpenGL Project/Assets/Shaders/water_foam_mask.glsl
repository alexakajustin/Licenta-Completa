#version 460

/**
 * @file water_foam_mask.glsl
 * @brief Builds the foam mask of the water foam map from the obstacle map (two dispatches).
 *
 * mode 0 - proximity: distance-based falloff around every texel where geometry crosses the
 *          water (a soft ring of foam around rocks, logs and reeds).
 * mode 1 - wakes: the proximity is smeared downstream along the river flow, widening and
 *          fading with distance, so foam trails behind obstacles (Waterways' "foam offset").
 * Texel (x, y) covers world (origin.x + x * texel, origin.z + y * texel); flow is world xz,
 * so it maps directly onto texture uv.
 */

layout(local_size_x = 8, local_size_y = 8) in;

uniform int mode;
uniform int size;           // Texels per side
uniform float texelWorld;   // Metres per texel

layout(binding = 0) uniform sampler2D obstacleMap;   // 1 where geometry crosses the water surface
layout(binding = 1) uniform sampler2D waterSurface;  // r = height, gb = flow, a = water present
layout(binding = 2) uniform sampler2D proximityMap;  // Output of mode 0 (bilinear)
layout(r16f, binding = 0) uniform writeonly image2D outMap;

const float RING = 0.9;     // Metres of foam around an obstacle
const float WAKE = 7.0;     // Metres a wake trails downstream
const int WAKE_STEPS = 20;

void main()
{
    ivec2 t = ivec2(gl_GlobalInvocationID.xy);
    if (any(greaterThanEqual(t, ivec2(size)))) return;
    vec4 water = texelFetch(waterSurface, t, 0);
    if (water.a < 0.5) { imageStore(outMap, t, vec4(0.0)); return; }

    if (mode == 0)
    {
        int r = int(ceil(RING / texelWorld));
        float prox = 0.0;
        for (int y = -r; y <= r; y++)
        for (int x = -r; x <= r; x++) {
            float d = length(vec2(x, y)) * texelWorld / RING;
            if (d > 1.0) continue;
            ivec2 s = clamp(t + ivec2(x, y), ivec2(0), ivec2(size - 1));
            float falloff = 1.0 - d;
            prox = max(prox, texelFetch(obstacleMap, s, 0).r * falloff * falloff);
        }
        imageStore(outMap, t, vec4(prox));
        return;
    }

    float foam = texelFetch(proximityMap, t, 0).r;
    vec2 flow = water.gb;
    if (dot(flow, flow) > 0.25)
    {
        vec2 side = vec2(-flow.y, flow.x);
        vec2 uv = (vec2(t) + 0.5) / float(size);
        float toUV = 1.0 / (texelWorld * float(size));
        for (int k = 1; k <= WAKE_STEPS; k++) {
            float back = WAKE * float(k) / float(WAKE_STEPS);
            float fade = 1.0 - float(k) / float(WAKE_STEPS + 1);
            // The wake widens downstream: sample the source slightly to both sides as well
            for (int s = -1; s <= 1; s++) {
                vec2 offset = -flow * back + side * (back * 0.12 * float(s));
                float w = (s == 0) ? 1.0 : 0.75;
                foam = max(foam, texture(proximityMap, uv + offset * toUV).r * fade * w * 0.85);
            }
        }
    }
    imageStore(outMap, t, vec4(foam));
}
