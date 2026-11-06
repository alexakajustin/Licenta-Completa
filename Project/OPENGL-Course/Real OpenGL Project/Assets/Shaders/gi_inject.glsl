#version 430 core

/**
 * @file gi_inject.glsl
 * @brief Lights the voxels of one level (Rendering/GlobalIllumination.h): the light each voxel's
 * surface sends back, which the probes' rays then pick up.
 *
 * Direct light: the sun (cascaded shadow map, cloud shadows) and the point / spot lights (their
 * cube shadow maps, within their reach, as the lit shaders), on the voxel's normal (thin, partly opaque voxels such as leaves are lit from both sides). Indirect:
 * the probes' ambient light at the voxel (last frame), so light keeps bouncing from frame to
 * frame. Output: rgb = outgoing light (albedo x incoming), a = opacity. Right after a level is
 * voxelized, the 4^3 blocks holding anything are marked too (the rays skip the empty ones).
 */

layout(local_size_x = 4, local_size_y = 4, local_size_z = 4) in;

layout(r32ui, binding = 0) readonly uniform uimage3D voxelData;
layout(rgba16f, binding = 1) writeonly uniform image3D voxelRadiance;
layout(r8ui, binding = 2) writeonly uniform uimage3D voxelBricks; ///< 1 per 4^3 block holding anything (empty space skipping)

#include "gi.glsl"
#include "cloud_shadow.glsl"

uniform int levelIndex;
uniform int resolution;
uniform vec3 levelMin;
uniform float voxelSize;
uniform int writeEmpty;       ///< 1: the level was just voxelized, empty voxels must be cleared too

uniform vec3 sunDir;          ///< Toward the sun
uniform vec3 sunRadiance;     ///< colour x diffuse intensity
uniform vec3 ambientColour;   ///< The flat ambient (GIAmbient's fallback while GI is off: unused here)
uniform float bounce;         ///< Strength of the indirect light that keeps bouncing

uniform sampler2DArray directionalShadowMap;
uniform mat4 cascadeMatrices[4];
uniform float cascadeSplits[4];
uniform float cascadeDepthRanges[4]; ///< Depth span of each cascade's projection (m): world bias to depth units
uniform int cascadeCount;
uniform vec3 cameraPos;
uniform vec3 cameraForward;

const int MAX_LIGHTS = 3;
uniform int pointCount;
uniform vec3 pointPos[MAX_LIGHTS];
uniform vec3 pointRadiance[MAX_LIGHTS];   ///< colour x diffuse intensity
uniform vec3 pointAtten[MAX_LIGHTS];      ///< constant, linear, exponent
uniform int spotCount;
uniform vec3 spotPos[MAX_LIGHTS];
uniform vec3 spotDir[MAX_LIGHTS];
uniform vec3 spotRadiance[MAX_LIGHTS];
uniform vec3 spotAtten[MAX_LIGHTS];
uniform float spotEdge[MAX_LIGHTS];       ///< cos of the cone's half angle
uniform samplerCube pointShadow[MAX_LIGHTS]; ///< The lights' cube shadow maps (distance / far plane)
uniform float pointFar[MAX_LIGHTS];          ///< Their far planes: the lights' reach (0: no shadow map)
uniform samplerCube spotShadow[MAX_LIGHTS];
uniform float spotFar[MAX_LIGHTS];

vec3 OctDecode(vec2 p)
{
    vec3 n = vec3(p, 1.0 - abs(p.x) - abs(p.y));
    if (n.z < 0.0) {
        vec2 s = vec2(n.x >= 0.0 ? 1.0 : -1.0, n.y >= 0.0 ? 1.0 : -1.0);
        n.xy = (1.0 - abs(n.yx)) * s;
    }
    return normalize(n);
}

// biasWS: how far (m) an occluder must be in front of the point along the light
float SunVisibility(vec3 worldPos, float biasWS)
{
    float viewDepth = dot(worldPos - cameraPos, cameraForward);
    int layer = -1;
    for (int i = 0; i < cascadeCount; i++) {
        if (viewDepth < cascadeSplits[i]) { layer = i; break; }
    }
    if (layer < 0) return 1.0; // Beyond the shadow distance: unshadowed
    vec4 ls = cascadeMatrices[layer] * vec4(worldPos, 1.0);
    vec3 p = ls.xyz / ls.w * 0.5 + 0.5;
    if (any(lessThan(p, vec3(0.0))) || any(greaterThan(p, vec3(1.0)))) return 1.0;
    float occluder = textureLod(directionalShadowMap, vec3(p.xy, float(layer)), 0.0).r;
    return (p.z - biasWS / cascadeDepthRanges[layer] > occluder) ? 0.0 : 1.0;
}

// Does the light reach the point at fromLight (from the light) past its cube shadow map? One tap
float OmniVisibility(samplerCube map, float farPlane, vec3 fromLight, float bias)
{
    float closest = textureLod(map, fromLight, 0.0).r * farPlane;
    return length(fromLight) - bias > closest ? 0.0 : 1.0;
}

void main()
{
    ivec3 c = ivec3(gl_GlobalInvocationID);
    if (any(greaterThanEqual(c, ivec3(resolution)))) return;
    ivec3 texel = c + ivec3(0, 0, levelIndex * resolution);
    uint v = imageLoad(voxelData, texel).r;
    float opacity = float(v >> 24) / 255.0;
    if (opacity <= 0.0) {
        if (writeEmpty == 1) imageStore(voxelRadiance, texel, vec4(0.0));
        return;
    }
    if (writeEmpty == 1) imageStore(voxelBricks, (c >> 2) + ivec3(0, 0, levelIndex * (resolution >> 2)), uvec4(1u));
    vec3 albedo = vec3((v >> 19) & 31u, (v >> 13) & 63u, (v >> 8) & 31u) / vec3(31.0, 63.0, 31.0);
    vec3 N = OctDecode(vec2((v >> 4) & 15u, v & 15u) / 15.0 * 2.0 - 1.0);
    vec3 p = levelMin + (vec3(c) + 0.5) * voxelSize;
    bool thin = opacity < 0.9; // Leaves: lit from both sides

    // Sun (the shadow lookup from just off the surface, a voxel toward the sun: the voxel centre
    // can be inside it; occluders from half a voxel further on count)
    float nl = dot(N, sunDir);
    float sunCos = thin ? abs(nl) : max(nl, 0.0);
    vec3 direct = vec3(0.0);
    if (sunCos > 0.0) {
        vec3 lookup = p + N * (nl >= 0.0 ? 0.5 : -0.5) * voxelSize + sunDir * voxelSize;
        direct += sunRadiance * sunCos * SunVisibility(lookup, 0.5 * voxelSize) * CloudShadow(p);
    }
    // Point and spot lights, as far as they reach (their shadow map's far plane, attenuation under
    // 1/250: as the lit shaders), shadowed from a voxel toward the light
    for (int i = 0; i < pointCount; i++) {
        vec3 toLight = pointPos[i] - p;
        float d = length(toLight);
        float att = pointAtten[i].x + pointAtten[i].y * d + pointAtten[i].z * d * d;
        if ((pointFar[i] > 0.0 && d >= pointFar[i]) || att > 250.0) continue;
        vec3 L = toLight / max(d, 1e-4);
        float cosine = thin ? abs(dot(N, L)) : max(dot(N, L), 0.0);
        if (cosine <= 0.0) continue;
        float visible = pointFar[i] > 0.0 ? OmniVisibility(pointShadow[i], pointFar[i], -L * max(d - voxelSize, 0.0), 0.5 * voxelSize) : 1.0;
        direct += pointRadiance[i] * cosine * visible / max(att, 1e-3);
    }
    for (int i = 0; i < spotCount; i++) {
        vec3 toLight = spotPos[i] - p;
        float d = length(toLight);
        float att = spotAtten[i].x + spotAtten[i].y * d + spotAtten[i].z * d * d;
        if ((spotFar[i] > 0.0 && d >= spotFar[i]) || att > 250.0) continue;
        vec3 L = toLight / max(d, 1e-4);
        float spot = dot(-L, normalize(spotDir[i]));
        if (spot <= spotEdge[i]) continue;
        float cone = 1.0 - (1.0 - spot) / max(1.0 - spotEdge[i], 1e-4);
        float cosine = thin ? abs(dot(N, L)) : max(dot(N, L), 0.0);
        if (cosine <= 0.0) continue;
        float visible = spotFar[i] > 0.0 ? OmniVisibility(spotShadow[i], spotFar[i], -L * max(d - voxelSize, 0.0), 0.5 * voxelSize) : 1.0;
        direct += spotRadiance[i] * cosine * cone * visible / max(att, 1e-3);
    }

    // Bounces: the light the probes saw around here last frame
    vec3 indirect = GIAmbient(p, N, N, ambientColour) * bounce;
    imageStore(voxelRadiance, texel, vec4(albedo * (direct + indirect), opacity));
}
