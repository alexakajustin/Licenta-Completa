#version 460

/**
 * @file atmosphere_apply.frag
 * @brief Aerial perspective: the air between the camera and each scene pixel (atmosphere_common.glsl),
 * marched along the pixel's own ray (its altitude falling and rising with it), the air's density
 * times distanceScale. Dual-source blending (ONE, SRC1_COLOR): scene * transmittance (per colour:
 * the far hills turn blue as well as pale) + in-scattered light. Sky pixels are the sky already.
 */

in vec2 TexCoord;
layout(location = 0, index = 0) out vec4 FragColor;
layout(location = 0, index = 1) out vec4 Transmittance;

#include "atmosphere_common.glsl"

uniform sampler2D sceneDepth;
uniform mat4 invProjection;
uniform mat4 invView;
uniform vec3 cameraPos;
uniform vec3 toSunDir;
uniform float distanceScale;
uniform int frame;

void main()
{
    float depth = texture(sceneDepth, TexCoord).r;
    if (depth >= 1.0) { FragColor = vec4(0.0); Transmittance = vec4(1.0); return; }
    vec4 v = invProjection * vec4(TexCoord * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec3 viewPos = v.xyz / v.w;
    vec3 worldOffset = mat3(invView) * viewPos;
    float len = length(worldOffset);
    vec3 dir = worldOffset / max(len, 1e-4);

    // Interleaved gradient noise (no pattern the TAA cannot average)
    vec2 jp = gl_FragCoord.xy + vec2(5.588238, 3.7342) * float(frame % 64);
    float jitter = fract(52.9829189 * fract(dot(jp, vec2(0.06711056, 0.00583715))));

    gAtmoDensityScale = distanceScale;
    vec3 L, T;
    AtmoIntegrate(cameraPos, dir, len, toSunDir, 12, jitter, L, T);
    // In the scene's encoding: scene' = scene * T^(1/2.2) + L^(1/2.2) is exact over black and
    // reaches the sky's own colour where the air hides everything
    vec3 Te = AtmoEncode(T);
    vec3 Le = AtmoEncode(L * sunIlluminance);
    vec3 farSky = AtmoEncode(L * sunIlluminance / max(vec3(1.0) - T, vec3(1e-4)));
    FragColor = vec4(min(Le, farSky * (vec3(1.0) - Te)), 0.0);
    Transmittance = vec4(Te, 1.0);
}
