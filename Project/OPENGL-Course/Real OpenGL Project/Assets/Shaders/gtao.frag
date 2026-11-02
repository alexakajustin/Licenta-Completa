#version 460

/**
 * @file gtao.frag
 * @brief Ground-truth ambient occlusion at half resolution (Rendering/AmbientOcclusion.h).
 *
 * Jimenez et al., "Practical Real-Time Strategies for Accurate Indirect Occlusion" (2016), in
 * the form of Intel's XeGTAO: for a few directions around the pixel (slices), walk the depth
 * buffer both ways to find the highest horizon within a world radius, then integrate the
 * visible arc of the hemisphere around the normal (cosine-weighted) analytically. The normal is
 * rebuilt from the depth (the neighbour on the same surface on each axis). Directions and step
 * offsets are rotated per pixel with interleaved gradient noise that also moves every frame
 * while TAA is on (frame.glsl), so two slices of six steps converge like many more.
 *
 * Output: r = visibility (1 = open), g = view depth (the upsample keeps edges sharp with it).
 */

out vec2 FragColor;
in vec2 TexCoord;

#include "frame.glsl"

uniform sampler2D depthTex;     ///< Full-resolution scene depth
uniform mat4 invProjection;
uniform float projScale;        ///< Pixels covered by 1 m at 1 m from the camera
uniform float radius;           ///< World radius of the occlusion (m)
uniform float falloff;          ///< Fraction of the radius over which occluders fade out

const float PI = 3.14159265;
const float HALF_PI = 1.57079633;
const int SLICES = 2;
const int STEPS = 6;

ivec2 gMaxP;

vec3 ViewPos(ivec2 p)
{
    p = clamp(p, ivec2(0), gMaxP);
    float d = texelFetch(depthTex, p, 0).r;
    vec2 uv = (vec2(p) + 0.5) / vec2(gMaxP + 1);
    vec4 v = invProjection * vec4(uv * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);
    return v.xyz / v.w;
}

float IGN(vec2 p) { return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715)))); }

void main()
{
    gMaxP = textureSize(depthTex, 0) - 1;
    ivec2 p = ivec2(gl_FragCoord.xy) * 2;
    float depth = texelFetch(depthTex, clamp(p, ivec2(0), gMaxP), 0).r;
    if (depth >= 1.0) { FragColor = vec2(1.0, 1.0e6); return; } // Sky

    vec3 P = ViewPos(p);
    float viewDepth = -P.z;

    // Normal from the depth: on each axis the neighbour closest in depth (same surface)
    vec3 pr = ViewPos(p + ivec2(1, 0)), pl = ViewPos(p - ivec2(1, 0));
    vec3 pu = ViewPos(p + ivec2(0, 1)), pd = ViewPos(p - ivec2(0, 1));
    vec3 dx = abs(pr.z - P.z) < abs(P.z - pl.z) ? pr - P : P - pl;
    vec3 dy = abs(pu.z - P.z) < abs(P.z - pd.z) ? pu - P : P - pd;
    vec3 N = normalize(cross(dx, dy));
    vec3 V = normalize(-P);
    if (dot(N, V) < 0.0) N = -N;

    float screenRadius = radius * projScale / max(viewDepth, 1e-3); // Pixels
    if (screenRadius < 1.5) { FragColor = vec2(1.0, viewDepth); return; }
    screenRadius = min(screenRadius, 192.0);

    float falloffRange = falloff * radius;
    float falloffMul = -1.0 / falloffRange;
    float falloffAdd = (radius - falloffRange) / falloffRange + 1.0;

    float noise = IGN(gl_FragCoord.xy);
    float rotation = fract(noise + frameJitter.z);
    float stepNoise = fract(noise * 1.6180339 + frameJitter.z * 0.7548777);

    float visibility = 0.0;
    for (int slice = 0; slice < SLICES; slice++) {
        float phi = (float(slice) + rotation) * PI / float(SLICES);
        vec2 omega = vec2(cos(phi), sin(phi));
        vec3 dirVec = vec3(omega, 0.0);
        vec3 orthoDir = dirVec - dot(dirVec, V) * V;
        vec3 axis = normalize(cross(orthoDir, V));
        vec3 projN = N - axis * dot(N, axis);
        float projLen = length(projN);
        if (projLen < 1e-4) continue;
        float cosN = clamp(dot(projN, V) / projLen, 0.0, 1.0);
        float n = sign(dot(orthoDir, projN)) * acos(cosN);

        float low0 = cos(n + HALF_PI), low1 = cos(n - HALF_PI);
        float hc0 = low0, hc1 = low1;
        for (int i = 0; i < STEPS; i++) {
            float s = (float(i) + stepNoise) / float(STEPS);
            s = s * s;                                   // Denser near the pixel
            vec2 offset = omega * (s * screenRadius + 1.0);
            ivec2 o = ivec2(round(offset));

            vec3 d0 = ViewPos(p + o) - P;
            float l0 = length(d0);
            float c0 = dot(d0, V) / max(l0, 1e-4);
            c0 = mix(low0, c0, clamp(l0 * falloffMul + falloffAdd, 0.0, 1.0));
            hc0 = max(hc0, c0);

            vec3 d1 = ViewPos(p - o) - P;
            float l1 = length(d1);
            float c1 = dot(d1, V) / max(l1, 1e-4);
            c1 = mix(low1, c1, clamp(l1 * falloffMul + falloffAdd, 0.0, 1.0));
            hc1 = max(hc1, c1);
        }

        float h0 = -acos(clamp(hc1, -1.0, 1.0));
        float h1 = acos(clamp(hc0, -1.0, 1.0));
        h0 = n + clamp(h0 - n, -HALF_PI, HALF_PI);
        h1 = n + clamp(h1 - n, -HALF_PI, HALF_PI);
        float sinN = sin(n);
        float arc0 = (cosN + 2.0 * h0 * sinN - cos(2.0 * h0 - n)) * 0.25;
        float arc1 = (cosN + 2.0 * h1 * sinN - cos(2.0 * h1 - n)) * 0.25;
        visibility += projLen * (arc0 + arc1);
    }
    visibility /= float(SLICES);
    FragColor = vec2(clamp(visibility, 0.0, 1.0), viewDepth);
}
