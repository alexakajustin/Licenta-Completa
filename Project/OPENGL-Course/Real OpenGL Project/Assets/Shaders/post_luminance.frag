#version 460

/**
 * @file post_luminance.frag
 * @brief Auto exposure, step 1 (Rendering/PostProcess.h): the HDR image's log luminance on a
 * small grid, weighted toward the centre of the screen (what one looks at). r = weight x log2
 * luminance, g = weight: the mip chain averages both, their ratio is the weighted log average.
 */

out vec2 FragColor;
in vec2 TexCoord;

uniform sampler2D source;

void main()
{
    // Four taps per cell: a 256-wide grid over a 2560-wide image still sees every bright patch
    const vec2 cell = vec2(1.0 / 256.0); // One texel of the grid (PostProcess::LUM_SIZE)
    float logSum = 0.0;
    for (int i = 0; i < 4; i++) {
        vec2 o = (vec2(i & 1, i >> 1) - 0.5) * cell * 0.5;
        vec3 c = textureLod(source, TexCoord + o, 0.0).rgb;
        float lum = dot(max(c, vec3(0.0)), vec3(0.2126, 0.7152, 0.0722));
        logSum += log2(max(lum, 1e-3));
    }
    float r = length((TexCoord - 0.5) * vec2(1.0, 0.75));
    float w = mix(1.0, 0.25, smoothstep(0.15, 0.6, r));
    FragColor = vec2(w * logSum * 0.25, w);
}
