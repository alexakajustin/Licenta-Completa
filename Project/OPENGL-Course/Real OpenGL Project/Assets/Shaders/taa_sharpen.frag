#version 460

/**
 * @file taa_sharpen.frag
 * @brief Contrast-adaptive sharpening (AMD FidelityFX CAS, simplified 5-tap form) of the TAA
 * result, written back into the viewport (Rendering/TemporalAA.h).
 *
 * Accumulating jittered frames softens the image a little; CAS restores the detail. It sharpens
 * less where the neighbourhood already has strong contrast, so edges do not ring and noise is
 * not amplified.
 */

out vec4 FragColor;
in vec2 TexCoord;

uniform sampler2D resolved;
uniform float sharpness; ///< 0 = none, 1 = strongest

void main()
{
    ivec2 p = ivec2(gl_FragCoord.xy);
    ivec2 maxP = textureSize(resolved, 0) - 1;
    vec3 c = texelFetch(resolved, p, 0).rgb;
    vec3 n = texelFetch(resolved, clamp(p + ivec2(0, 1), ivec2(0), maxP), 0).rgb;
    vec3 s = texelFetch(resolved, clamp(p - ivec2(0, 1), ivec2(0), maxP), 0).rgb;
    vec3 e = texelFetch(resolved, clamp(p + ivec2(1, 0), ivec2(0), maxP), 0).rgb;
    vec3 w = texelFetch(resolved, clamp(p - ivec2(1, 0), ivec2(0), maxP), 0).rgb;

    if (sharpness <= 0.001) {
        FragColor = vec4(c, 1.0);
        return;
    }

    vec3 mn = min(c, min(min(n, s), min(e, w)));
    vec3 mx = max(c, max(max(n, s), max(e, w)));
    // Room to sharpen before clipping, relative to the local maximum
    vec3 amp = sqrt(clamp(min(mn, 1.0 - mx) / max(mx, vec3(1e-4)), 0.0, 1.0));
    float peak = -1.0 / mix(8.0, 5.0, sharpness);
    vec3 wgt = amp * peak;
    vec3 result = (c + (n + s + e + w) * wgt) / (1.0 + 4.0 * wgt);
    FragColor = vec4(clamp(result, 0.0, 1.0), 1.0);
}
