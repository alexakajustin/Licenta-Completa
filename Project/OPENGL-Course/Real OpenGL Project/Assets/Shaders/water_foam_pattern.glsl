#version 460

/**
 * @file water_foam_pattern.glsl
 * @brief Generates the tileable foam pattern sampled by water.frag / river.frag (run once).
 *
 * Each channel is a "foam height" in [0, 1]: closeness to the walls between bubbles (Worley
 * F2 - F1 of a domain-warped point set) at two scales, shifted by a low-frequency noise so
 * froth gathers in patches. Thresholding it gives real-looking foam: a low threshold leaves
 * white froth pierced by round bubble holes, a higher one only the lacy network of bubble
 * walls, then isolated junctions. The water shaders raise the threshold as the foam mask fades
 * (Waterways-style erosion), so foam thins out naturally away from its source.
 * R and G use different seeds (two layers whose repeats never line up).
 */

layout(local_size_x = 8, local_size_y = 8) in;
layout(rgba8, binding = 0) uniform writeonly image2D outPattern;

uniform int size;

uvec2 Pcg2d(uvec2 v)
{
    v = v * 1664525u + 1013904223u;
    v.x += v.y * 1664525u; v.y += v.x * 1664525u;
    v ^= v >> 16u;
    v.x += v.y * 1664525u; v.y += v.x * 1664525u;
    v ^= v >> 16u;
    return v;
}

vec2 Hash22(ivec2 c, int seed)
{
    return vec2(Pcg2d(uvec2(c) + uvec2(seed * 7919, seed * 104729))) / 4294967295.0;
}

// Tileable value noise (period in cells)
float ValueNoise(vec2 p, int period, int seed)
{
    vec2 x = p * float(period);
    ivec2 i = ivec2(floor(x));
    vec2 f = fract(x);
    f = f * f * (3.0 - 2.0 * f);
    float a = Hash22((i + ivec2(0, 0) + period) % period, seed).x;
    float b = Hash22((i + ivec2(1, 0) + period) % period, seed).x;
    float c = Hash22((i + ivec2(0, 1) + period) % period, seed).x;
    float d = Hash22((i + ivec2(1, 1) + period) % period, seed).x;
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

// Closeness to the wall between neighbouring bubbles: 1 on the cell borders of a Worley
// point set (F2 - F1 = 0), falling to 0 towards the bubble centres. The cell grid wraps every
// 'period' cells.
float BubbleWalls(vec2 p, int period, int seed)
{
    vec2 x = p * float(period);
    ivec2 id = ivec2(floor(x));
    vec2 f = fract(x);
    float d1 = 9.0, d2 = 9.0;
    for (int j = -2; j <= 2; j++)
    for (int i = -2; i <= 2; i++) {
        ivec2 o = ivec2(i, j);
        vec2 fp = vec2(o) + Hash22((id + o + period) % period, seed) - f;
        float d = dot(fp, fp);
        if (d < d1) { d2 = d1; d1 = d; }
        else if (d < d2) { d2 = d; }
    }
    return clamp(1.0 - (sqrt(d2) - sqrt(d1)) * 1.6, 0.0, 1.0);
}

float FoamHeight(vec2 p, int seed)
{
    // Domain warp: squashed, irregular bubbles instead of a regular cell grid
    vec2 warp = vec2(ValueNoise(p, 6, seed + 1), ValueNoise(p, 6, seed + 2)) - 0.5;
    vec2 q = p + warp * 0.06;

    float large = BubbleWalls(q, 18, seed + 3);   // ~22 cm bubbles on a 4 m tile
    float small = BubbleWalls(q, 47, seed + 4);   // ~8 cm bubbles
    float h = large * 0.6 + small * 0.4;

    // Froth gathers in patches: shift the whole pattern up/down with a low-frequency noise
    float clump = ValueNoise(p, 4, seed + 5) * 0.65 + ValueNoise(p, 9, seed + 6) * 0.35;
    return clamp(h + (clump - 0.5) * 0.4, 0.0, 1.0);
}

void main()
{
    ivec2 texel = ivec2(gl_GlobalInvocationID.xy);
    if (any(greaterThanEqual(texel, ivec2(size)))) return;
    vec2 p = (vec2(texel) + 0.5) / float(size);
    imageStore(outPattern, texel, vec4(FoamHeight(p, 11), FoamHeight(p, 37), 0.0, 1.0));
}
