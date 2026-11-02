#version 460

/**
 * @file ssr.frag
 * @brief Stochastic screen-space reflections (Rendering/Reflections.h), added into the viewport.
 *
 * For every pixel of a reflective surface (attachments 2-4 written by shader.frag: normal,
 * reflection weight and roughness, the probe's reflection there) one ray is traced through the
 * depth buffer. Its direction is drawn from the GGX lobe of the surface's roughness with noise
 * that moves every frame (Stachowiak, "Stochastic Screen-Space Reflections", Frostbite 2015):
 * TAA averages the rays into a glossy reflection. The ray marches in screen space with a
 * perspective-correct depth, steps over a few pixels at a time and refines a hit by bisection.
 * The colour at a hit comes from the previous frame (reprojected), so reflections show the lit,
 * fogged scene. Where the ray leaves the screen, goes toward the camera or finds nothing, the
 * probe (parallax-corrected cubemap) fills in, blended smoothly.
 */

out vec4 FragColor;
in vec2 TexCoord;

#include "frame.glsl"

uniform sampler2D depthTex;    ///< Scene depth
uniform sampler2D normalTex;   ///< xyz world normal, w view depth (< 0: not reflective)
uniform sampler2D weightTex;   ///< rgb reflection weight, a roughness
uniform sampler2D probeTex;    ///< rgb the probe's reflection
uniform sampler2D prevColor;   ///< Last frame's HDR scene
uniform int prevValid;
uniform mat4 view;
uniform mat4 projection;       ///< Without the TAA jitter
uniform mat4 invViewProj;
uniform mat4 prevViewProj;
uniform vec3 cameraPos;
uniform float nearPlane;
uniform float maxDistance;     ///< Longest reflected ray (m)

const int MAX_STEPS = 48;
const float PI = 3.14159265;

vec2 gSize;

float ViewDepth(float d)
{
    // Perspective depth -> distance along the view axis
    return projection[3][2] / ((d * 2.0 - 1.0) + projection[2][2]);
}

float IGN(vec2 p) { return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715)))); }

vec3 ImportanceSampleGGX(vec2 xi, vec3 N, float roughness)
{
    float a = roughness * roughness;
    float phi = 2.0 * PI * xi.x;
    float cosTheta = sqrt((1.0 - xi.y) / (1.0 + (a * a - 1.0) * xi.y));
    float sinTheta = sqrt(max(1.0 - cosTheta * cosTheta, 0.0));
    vec3 h = vec3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
    vec3 up = abs(N.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 T = normalize(cross(up, N));
    vec3 B = cross(N, T);
    return normalize(T * h.x + B * h.y + N * h.z);
}

void main()
{
    ivec2 p = ivec2(gl_FragCoord.xy);
    vec4 nt = texelFetch(normalTex, p, 0);
    if (nt.w <= 0.0) { FragColor = vec4(0.0); return; }
    gSize = vec2(textureSize(depthTex, 0));

    // Still the visible surface? (something drawn later may cover the reflective object)
    float d = texelFetch(depthTex, p, 0).r;
    float viewDepth = ViewDepth(d);
    if (abs(nt.w - viewDepth) > 0.01 * viewDepth + 0.05) { FragColor = vec4(0.0); return; }

    vec4 wr = texelFetch(weightTex, p, 0);
    vec3 weight = wr.rgb;
    float roughness = wr.a;
    vec3 probe = texelFetch(probeTex, p, 0).rgb;
    vec2 uv = (vec2(p) + 0.5) / gSize;
    vec4 world = invViewProj * vec4(uv * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);
    world /= world.w;
    vec3 N = normalize(nt.xyz);
    vec3 V = normalize(world.xyz - cameraPos);

    float confidence = 0.0;
    vec3 hitColor = vec3(0.0);
    if (prevValid == 1 && roughness < 0.75) {
        // A ray from the GGX lobe (biased toward its centre: less noise, Frostbite's 0.7)
        float noise = IGN(gl_FragCoord.xy);
        vec2 xi = vec2(fract(noise + frameJitter.z), fract(noise * 1.6180339 + frameJitter.z * 0.7548777));
        xi.y *= 0.7;
        vec3 H = ImportanceSampleGGX(xi, N, roughness);
        vec3 R = reflect(V, H);
        if (dot(R, N) <= 0.0) R = reflect(V, N);

        // The ray in view space, cut at the near plane
        vec3 Pv = (view * vec4(world.xyz, 1.0)).xyz;
        vec3 Rv = normalize(mat3(view) * R);
        float len = maxDistance;
        if (Rv.z > 0.0) len = min(len, (-nearPlane * 1.5 - Pv.z) / Rv.z);
        if (len > 0.05) {
            vec4 h0 = projection * vec4(Pv, 1.0);
            vec4 h1 = projection * vec4(Pv + Rv * len, 1.0);
            float k0 = 1.0 / h0.w, k1 = 1.0 / h1.w;
            vec2 s0 = (h0.xy * k0 * 0.5 + 0.5) * gSize;
            vec2 s1 = (h1.xy * k1 * 0.5 + 0.5) * gSize;
            // Steps of ~4 px along the projected ray
            int steps = clamp(int(length(s1 - s0) / 4.0), 8, MAX_STEPS);
            float jitter = fract(noise * 2.6180339 + frameJitter.z * 0.5698403);
            float tPrev = 0.0;
            float rayPrev = viewDepth;
            for (int i = 0; i < MAX_STEPS; i++) {
                if (i >= steps) break;
                float t = (float(i) + jitter) / float(steps);
                vec2 s = mix(s0, s1, t);
                if (any(lessThan(s, vec2(0.0))) || any(greaterThanEqual(s, gSize))) break;
                float rayDepth = 1.0 / mix(k0, k1, t);
                float sceneDepth = ViewDepth(texelFetch(depthTex, ivec2(s), 0).r);
                float behind = rayDepth - sceneDepth;
                float thickness = max(0.25, rayDepth * 0.02) + abs(rayDepth - rayPrev);
                if (behind > 0.0 && behind < thickness) {
                    // Bisection between the last step in front and this one
                    float a = tPrev, b = t;
                    for (int j = 0; j < 5; j++) {
                        float m = (a + b) * 0.5;
                        vec2 sm = mix(s0, s1, m);
                        float rm = 1.0 / mix(k0, k1, m);
                        if (rm - ViewDepth(texelFetch(depthTex, ivec2(sm), 0).r) > 0.0) b = m; else a = m;
                    }
                    vec2 hit = mix(s0, s1, b);
                    ivec2 hp = ivec2(hit);
                    float hd = texelFetch(depthTex, hp, 0).r;
                    if (hd >= 1.0) break; // The sky: the probe has it
                    vec2 huv = (vec2(hp) + 0.5) / gSize;
                    vec4 hw = invViewProj * vec4(huv * 2.0 - 1.0, hd * 2.0 - 1.0, 1.0);
                    hw /= hw.w;
                    vec4 pc = prevViewProj * hw;
                    vec2 puv = pc.xy / pc.w * 0.5 + 0.5;
                    if (pc.w <= 0.0 || any(lessThan(puv, vec2(0.0))) || any(greaterThan(puv, vec2(1.0)))) break;
                    hitColor = min(textureLod(prevColor, puv, 0.0).rgb, vec3(32.0));
                    // Trust: away from the screen edges, before the ray's end, not toward the camera
                    vec2 edge = min(huv, 1.0 - huv);
                    confidence = smoothstep(0.0, 0.08, min(edge.x, edge.y))
                               * (1.0 - smoothstep(0.75, 1.0, b))
                               * (1.0 - smoothstep(0.4, 0.9, Rv.z))
                               * (1.0 - smoothstep(0.5, 0.75, roughness));
                    break;
                }
                tPrev = t;
                rayPrev = rayDepth;
            }
        }
    }

    vec3 reflection = mix(probe, hitColor, confidence);
    FragColor = vec4(reflection * weight, 1.0);
}
