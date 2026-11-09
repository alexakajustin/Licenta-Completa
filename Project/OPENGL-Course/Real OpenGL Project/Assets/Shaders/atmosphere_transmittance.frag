#version 460

/**
 * @file atmosphere_transmittance.frag
 * @brief Atmosphere LUT 1: the fraction of light that crosses the air from a radius r, toward a
 * direction at mu from the zenith, to the top of the atmosphere (atmosphere_common.glsl).
 */

in vec2 TexCoord;
out vec4 FragColor;

#include "atmosphere_common.glsl"

void main()
{
    float r, mu;
    AtmoTransmittanceRMu(gl_FragCoord.xy / vec2(256.0, 64.0), r, mu);
    float len = AtmoRaySphere(r, mu, ATMO_TOP);
    const int STEPS = 40;
    vec3 depth = vec3(0.0);
    float dt = max(len, 0.0) / float(STEPS);
    for (int i = 0; i < STEPS; i++) {
        float t = (float(i) + 0.5) * dt;
        float ri = sqrt(r * r + t * t + 2.0 * r * mu * t);
        depth += AtmoMedium(ri - ATMO_BOTTOM).extinction * dt;
    }
    FragColor = vec4(exp(-depth), 1.0);
}
