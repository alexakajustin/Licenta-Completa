#version 460

/**
 * @file fog_apply.frag
 * @brief Volumetric fog, pass 3: composite over the rendered scene.
 *
 * Reads the integrated froxel volume at the pixel's own depth (trilinear, so the low-resolution
 * grid never shows as blocks) and outputs (light, transmittance); the blend state
 * (ONE, SRC_ALPHA) turns that into scene * transmittance + light. Sky pixels take the whole
 * fog range.
 */

in vec2 TexCoord;
out vec4 FragColor;

uniform sampler2D sceneDepth;
uniform sampler3D integratedVolume;
uniform mat4 projection;
uniform float fogRange;
uniform int gridDepth;

float LinearDepth(float depth)
{
    float ndcZ = depth * 2.0 - 1.0;
    return projection[3][2] / (ndcZ + projection[2][2]);
}

void main()
{
    float depth = texture(sceneDepth, TexCoord).r;
    float viewDepth = depth >= 1.0 ? fogRange : min(LinearDepth(depth), fogRange);

    // Slices crossed so far (quadratic distribution); texel z = result after z + 1 slices
    float slices = sqrt(viewDepth / fogRange) * float(gridDepth);
    float coord = (max(slices, 1.0) - 0.5) / float(gridDepth);
    vec4 fog = texture(integratedVolume, vec3(TexCoord, coord));
    // Inside the first slice: only part of it lies in front of the surface
    if (slices < 1.0) {
        fog.rgb *= slices;
        fog.a = mix(1.0, fog.a, slices);
    }
    // The scene is lit straight into LDR (no tonemapper): roll the in-scattered light off
    // toward 1 instead of clipping, keeping its hue, so the glow toward the sun stays a soft
    // orange halo rather than a flat white disc.
    float peak = max(max(fog.r, fog.g), fog.b);
    if (peak > 0.7) fog.rgb *= (0.7 + 0.3 * (1.0 - exp(-(peak - 0.7) / 0.3))) / peak;
    FragColor = vec4(fog.rgb, fog.a);
}
