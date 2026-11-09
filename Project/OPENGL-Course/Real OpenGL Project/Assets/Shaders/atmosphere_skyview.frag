#version 460

/**
 * @file atmosphere_skyview.frag
 * @brief Atmosphere LUT 3: the sky's radiance in every direction from the camera (azimuth from
 * the sun x zenith angle, rows packed toward the horizon), ray-marched through the air to its top
 * or to the ground (which reflects the sun into it). Per unit sun illuminance; rebuilt each frame.
 */

in vec2 TexCoord;
out vec4 FragColor;

#include "atmosphere_common.glsl"

uniform vec3 cameraPos;
uniform vec3 toSunDir;

void main()
{
    vec2 uv = gl_FragCoord.xy / vec2(192.0, 108.0);
    vec3 cam = vec3(0.0, max(cameraPos.y, 1.0), 0.0);
    float r = AtmoRadius(cam);
    float horizon = acos(clamp(sqrt(max(r * r - ATMO_BOTTOM * ATMO_BOTTOM, 0.0)) / r, -1.0, 1.0));
    float horizonAngle = 0.5 * ATMO_PI + horizon;
    float zenithAngle;
    if (uv.y < 0.5) {
        float x = 1.0 - 2.0 * uv.y;
        zenithAngle = horizonAngle * (1.0 - x * x);
    } else {
        float x = 2.0 * uv.y - 1.0;
        zenithAngle = horizonAngle + x * x * (ATMO_PI - horizonAngle);
    }
    float az = uv.x * uv.x * ATMO_PI;
    vec3 sunH = normalize(vec3(toSunDir.x, 0.0, toSunDir.z) + vec3(1e-6, 0.0, 0.0));
    vec3 side = vec3(-sunH.z, 0.0, sunH.x);
    vec3 dir = vec3(0.0, cos(zenithAngle), 0.0) + sin(zenithAngle) * (cos(az) * sunH + sin(az) * side);

    float mu = dir.y;
    float tTop = AtmoRaySphere(r, mu, ATMO_TOP);
    float disc = r * r * (mu * mu - 1.0) + ATMO_BOTTOM * ATMO_BOTTOM;
    bool hitsGround = mu < 0.0 && disc >= 0.0;
    float len = hitsGround ? AtmoRaySphere(r, mu, ATMO_BOTTOM) : tTop;
    vec3 L, T;
    AtmoIntegrate(cam, dir, max(len, 0.0), toSunDir, 32, 0.3, L, T);
    if (hitsGround) {
        vec3 p = cam + dir * len;
        vec3 up = AtmoUp(p);
        float c = max(dot(up, toSunDir), 0.0);
        L += T * AtmoSunlight(AtmoRadius(p), dot(up, toSunDir)) * c * groundAlbedo / ATMO_PI;
    }
    FragColor = vec4(L, 1.0);
}
