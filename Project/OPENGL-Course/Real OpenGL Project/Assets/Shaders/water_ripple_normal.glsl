#version 430 core

/**
 * @file water_ripple_normal.glsl
 * @brief Tileable ripple slopes for running water (Rendering/WaterFoamMap.h, generated once).
 *
 * A patch of wind- and current-driven ripples built like an ocean spectrum: a sum of sine waves
 * with random phases whose wave vectors are whole numbers of cycles across the tile (so it tiles
 * without seams), amplitude falling with the wavelength (a saturated, Phillips-like spectrum:
 * every wave about equally steep), a little stronger for waves running along x (their crests
 * lie along y, which river.frag aligns with the current: running water is streaked along its
 * flow) but spread wide (a narrow spread reads as stripes). Wavelengths
 * from a sixteenth to a quarter of the tile. Output: rg = slope (dh/dx, dh/dy), unit-free.
 */

layout(local_size_x = 8, local_size_y = 8) in;
layout(rg16f, binding = 0) writeonly uniform image2D outSlope;
uniform int size;

float Hash(vec2 p)
{
    p = fract(p * vec2(0.1031, 0.1030));
    p += dot(p, p.yx + 33.33);
    return fract((p.x + p.y) * p.x);
}

void main()
{
    ivec2 c = ivec2(gl_GlobalInvocationID.xy);
    if (any(greaterThanEqual(c, ivec2(size)))) return;
    vec2 x = (vec2(c) + 0.5) / float(size); // [0, 1) across the tile
    vec2 slope = vec2(0.0);
    const float TAU = 6.2831853;
    // Wave vectors (m, n) in cycles per tile, 4 to 16 cycles: one set per ring of wavelengths
    for (int m = -16; m <= 16; m++) {
        for (int n = 1; n <= 16; n++) {
            vec2 k = vec2(float(m), float(n));
            float len = length(k);
            if (len < 4.0 || len > 16.0) continue;
            // Crests mostly along the flow (+y), broad spread; about half the pairs (sparse)
            float h = Hash(k * 1.37 + 11.0);
            if (h > 0.55) continue;
            float align = abs(k.x) / len;
            float spread = mix(0.65, 1.0, align);
            float amplitude = spread / (len * len); // Equal steepness: slope ~ k * a ~ 1 / k
            float phase = Hash(k * 2.71 + 5.0) * TAU;
            float angle = TAU * dot(k, x) + phase;
            slope += amplitude * TAU * k * cos(angle);
        }
    }
    imageStore(outSlope, c, vec4(slope * 1.6, 0.0, 0.0));
}
