#version 460

/**
 * @file atmosphere_multiscatter.frag
 * @brief Atmosphere LUT 2: light scattered more than once (Hillaire 2020, section 5.5). For a
 * point at radius r with the sun at muSun from its zenith: the light reaching it after a second
 * scattering (L2), from every direction, and the fraction f of light that scatters again;
 * every further order is f times the one before, so all of them sum to L2 / (1 - f). Isotropic.
 */

in vec2 TexCoord;
out vec4 FragColor;

#include "atmosphere_common.glsl"

void main()
{
    vec2 uv = gl_FragCoord.xy / 32.0;
    float muSun = uv.x * 2.0 - 1.0;
    float r = ATMO_BOTTOM + clamp(uv.y, 0.0, 1.0) * (ATMO_TOP - ATMO_BOTTOM - 1.0) + 0.5;
    vec3 toSun = vec3(sqrt(max(1.0 - muSun * muSun, 0.0)), muSun, 0.0);
    vec3 origin = vec3(0.0, r - ATMO_BOTTOM, 0.0);

    const int DIRS = 64, STEPS = 20;
    vec3 L2 = vec3(0.0), fms = vec3(0.0);
    const float uniformPhase = 1.0 / (4.0 * ATMO_PI);
    for (int d = 0; d < DIRS; d++) {
        // Fibonacci sphere
        float z = 1.0 - (float(d) + 0.5) / float(DIRS) * 2.0;
        float ph = float(d) * 2.39996323;
        float s = sqrt(1.0 - z * z);
        vec3 dir = vec3(cos(ph) * s, z, sin(ph) * s);
        float mu = dir.y;
        float tGround = AtmoRaySphere(r, mu, ATMO_BOTTOM);
        bool hitsGround = tGround > 0.0 && r * r * (mu * mu - 1.0) + ATMO_BOTTOM * ATMO_BOTTOM >= 0.0 && mu < 0.0;
        float len = hitsGround ? tGround : AtmoRaySphere(r, mu, ATMO_TOP);
        float dt = max(len, 0.0) / float(STEPS);
        vec3 T = vec3(1.0), l = vec3(0.0), f = vec3(0.0);
        for (int i = 0; i < STEPS; i++) {
            vec3 p = origin + dir * ((float(i) + 0.5) * dt);
            float ri = AtmoRadius(p);
            AtmoSample m = AtmoMedium(ri - ATMO_BOTTOM);
            vec3 stepT = exp(-m.extinction * dt);
            vec3 sun = AtmoSunlight(ri, dot(AtmoUp(p), toSun));
            vec3 Sl = m.scattering * uniformPhase * sun;
            vec3 Sf = m.scattering;
            vec3 k = (vec3(1.0) - stepT) / max(m.extinction, vec3(1e-12));
            l += T * Sl * k;
            f += T * Sf * k;
            T *= stepT;
        }
        if (hitsGround) {
            vec3 p = origin + dir * len;
            vec3 up = AtmoUp(p);
            l += T * AtmoSunlight(AtmoRadius(p), dot(up, toSun)) * max(dot(up, toSun), 0.0) * groundAlbedo / ATMO_PI;
        }
        L2 += l / float(DIRS);
        fms += f / float(DIRS);
    }
    // Each direction above was weighted 1/DIRS of the sphere; the isotropic phase of the next
    // bounce turns the sums into the radiance per unit illuminance
    vec3 psi = L2 / max(vec3(1.0) - fms * uniformPhase * 4.0 * ATMO_PI, vec3(1e-3));
    FragColor = vec4(psi, 1.0);
}
