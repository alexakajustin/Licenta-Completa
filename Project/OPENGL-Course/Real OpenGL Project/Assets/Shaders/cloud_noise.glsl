#version 460

/**
 * @file cloud_noise.glsl
 * @brief Generates the tileable 3D noise volumes used by the volumetric clouds (run once at startup).
 *
 * mode 0 (shape, 128^3): R = Perlin-Worley (billowy base), G/B/A = inverted Worley fBm at
 *                        increasing frequencies (used to erode the base into cloud lumps).
 * mode 1 (detail, 32^3): R/G/B = inverted Worley fBm at increasing frequencies (edge erosion).
 * Every noise wraps with the texture, so the volumes tile seamlessly with GL_REPEAT.
 * Reference: Schneider, "The Real-time Volumetric Cloudscapes of Horizon Zero Dawn" (2015).
 */

layout(local_size_x = 4, local_size_y = 4, local_size_z = 4) in;
layout(rgba8, binding = 0) uniform writeonly image3D outNoise;

uniform int mode;
uniform int size;

uvec3 Pcg3d(uvec3 v)
{
    v = v * 1664525u + 1013904223u;
    v.x += v.y * v.z; v.y += v.z * v.x; v.z += v.x * v.y;
    v ^= v >> 16u;
    v.x += v.y * v.z; v.y += v.z * v.x; v.z += v.x * v.y;
    return v;
}

vec3 Hash33(ivec3 c, int seed)
{
    return vec3(Pcg3d(uvec3(c) + uvec3(seed * 1013, seed * 7919, seed * 2741))) / 4294967295.0;
}

// Distance to the nearest feature point (cell units), feature grid wraps every `period` cells
float Worley(vec3 p, int period, int seed)
{
    vec3 x = p * float(period);
    ivec3 id = ivec3(floor(x));
    vec3 f = fract(x);
    float d = 1e9;
    for (int k = -1; k <= 1; k++)
    for (int j = -1; j <= 1; j++)
    for (int i = -1; i <= 1; i++) {
        ivec3 o = ivec3(i, j, k);
        ivec3 c = (id + o + period) % period;
        vec3 fp = vec3(o) + Hash33(c, seed) - f;
        d = min(d, dot(fp, fp));
    }
    return clamp(sqrt(d), 0.0, 1.0);
}

float WorleyFbm(vec3 p, int period, int seed)
{
    return (1.0 - Worley(p, period, seed)) * 0.625
         + (1.0 - Worley(p, period * 2, seed + 1)) * 0.25
         + (1.0 - Worley(p, period * 4, seed + 2)) * 0.125;
}

float Gradient(ivec3 c, int period, vec3 f, int seed)
{
    c = (c % period + period) % period;
    vec3 g = normalize(Hash33(c, seed) * 2.0 - 1.0 + 1e-5);
    return dot(g, f);
}

// Tileable gradient (Perlin) noise, roughly in [-1, 1]
float Perlin(vec3 p, int period, int seed)
{
    vec3 x = p * float(period);
    ivec3 i = ivec3(floor(x));
    vec3 f = fract(x);
    vec3 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    float n000 = Gradient(i + ivec3(0, 0, 0), period, f - vec3(0, 0, 0), seed);
    float n100 = Gradient(i + ivec3(1, 0, 0), period, f - vec3(1, 0, 0), seed);
    float n010 = Gradient(i + ivec3(0, 1, 0), period, f - vec3(0, 1, 0), seed);
    float n110 = Gradient(i + ivec3(1, 1, 0), period, f - vec3(1, 1, 0), seed);
    float n001 = Gradient(i + ivec3(0, 0, 1), period, f - vec3(0, 0, 1), seed);
    float n101 = Gradient(i + ivec3(1, 0, 1), period, f - vec3(1, 0, 1), seed);
    float n011 = Gradient(i + ivec3(0, 1, 1), period, f - vec3(0, 1, 1), seed);
    float n111 = Gradient(i + ivec3(1, 1, 1), period, f - vec3(1, 1, 1), seed);
    return mix(mix(mix(n000, n100, u.x), mix(n010, n110, u.x), u.y),
               mix(mix(n001, n101, u.x), mix(n011, n111, u.x), u.y), u.z);
}

float Remap(float v, float lo, float hi, float newLo, float newHi)
{
    return newLo + (v - lo) / max(hi - lo, 1e-5) * (newHi - newLo);
}

void main()
{
    ivec3 texel = ivec3(gl_GlobalInvocationID);
    if (any(greaterThanEqual(texel, ivec3(size)))) return;
    vec3 p = (vec3(texel) + 0.5) / float(size);

    vec4 result;
    if (mode == 0)
    {
        float perlin = 0.0, amp = 1.0, norm = 0.0;
        int period = 4;
        for (int o = 0; o < 5; o++) {
            perlin += Perlin(p, period, 11 + o) * amp;
            norm += amp;
            amp *= 0.5;
            period *= 2;
        }
        perlin = clamp(perlin / norm * 0.85 + 0.5, 0.0, 1.0);
        float worley = WorleyFbm(p, 4, 3);
        float perlinWorley = clamp(Remap(perlin, 0.0, 1.0, worley, 1.0), 0.0, 1.0);
        result = vec4(perlinWorley, WorleyFbm(p, 4, 21), WorleyFbm(p, 8, 31), WorleyFbm(p, 16, 41));
    }
    else
    {
        result = vec4(WorleyFbm(p, 2, 51), WorleyFbm(p, 4, 61), WorleyFbm(p, 8, 71), 1.0);
    }
    imageStore(outNoise, texel, result);
}
