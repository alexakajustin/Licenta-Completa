#version 460

/**
 * @file cloud_shadow_map.frag
 * @brief Cloud shadow map (Rendering/CloudShadows.h): for each texel, a point on the plane
 * y = 0 in a square around the camera, how much sun gets through the clouds.
 *
 * Each point takes the opacity of the cloud column where its sun ray crosses the middle of the
 * layer, with the drawn clouds' density (cloud_common.glsl, erosion detail at its average): the
 * shadow is the cloud's footprint, moved along the sun, and drifts with it. (Marching the slanted
 * sun ray itself summed every cloud along kilometres of path at a low sun: one grey sheet.) The
 * opacity then goes through a soft threshold, so a shadow has the cloud's outline (dark core,
 * short penumbra) instead of a blurry fade.
 *
 * Two texels per fragment, 4 bits each, packed into one byte: the map lives in a uniform block
 * (cloud_shadow.glsl), and that is what fits a 180 x 180 map into its guaranteed 16 KB.
 */

out vec4 FragColor;
in vec2 TexCoord;

#include "cloud_common.glsl"

uniform vec3 sunDir;      ///< Towards the sun
uniform vec2 areaOrigin;  ///< World xz of the map's corner
uniform float areaSize;   ///< World size of the map (m)
uniform float mapSize;    ///< Texels along a side (the target is mapSize / 2 x mapSize bytes)

const int STEPS = 24;

// Sun transmittance at the ground point of map texel `texel`: the opacity of the cloud column
// where its sun ray crosses the middle of the layer (the cloud's footprint, moved along the sun)
float Transmittance(vec2 texel, vec3 toSun)
{
    vec2 xz = areaOrigin + (texel + 0.5) / mapSize * areaSize;
    const float MID = (CLOUD_BOTTOM + CLOUD_TOP) * 0.5;
    vec2 column = xz + toSun.xz / toSun.y * MID;
    float lod = max(log2(areaSize / mapSize / SHAPE_TEXEL), 0.0);

    float stepLen = (CLOUD_TOP - CLOUD_BOTTOM) / float(STEPS);
    float depth = 0.0;
    for (int i = 0; i < STEPS; i++) {
        vec3 p = vec3(column.x, CLOUD_BOTTOM + (float(i) + 0.5) * stepLen, column.y);
        float h = HeightFraction(p);
        if (h < 0.0 || h > 1.0) continue;
        depth += CloudErodedAverage(CloudShape(p, h, lod)) * stepLen;
    }
    float opacity = 1.0 - exp(-depth * SIGMA);
    // The cloud's outline: thin wisps cast little, the body a full shadow, a short soft edge between
    return 1.0 - smoothstep(0.12, 0.55, opacity);
}

void main()
{
    CloudSetup();
    vec3 toSun = normalize(sunDir);
    if (toSun.y < 0.01) { FragColor = vec4(1.0); return; }

    ivec2 byteCoord = ivec2(gl_FragCoord.xy);
    float a = Transmittance(vec2(byteCoord.x * 2, byteCoord.y), toSun);
    float b = Transmittance(vec2(byteCoord.x * 2 + 1, byteCoord.y), toSun);
    float packed = round(clamp(a, 0.0, 1.0) * 15.0) + 16.0 * round(clamp(b, 0.0, 1.0) * 15.0);
    FragColor = vec4(packed / 255.0, 0.0, 0.0, 1.0);
}
