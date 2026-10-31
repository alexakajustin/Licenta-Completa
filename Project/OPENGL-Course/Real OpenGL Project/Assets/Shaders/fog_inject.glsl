#version 460

/**
 * @file fog_inject.glsl
 * @brief Volumetric fog, pass 1: medium + lighting per froxel (Hillaire, "Physically Based and
 * Unified Volumetric Rendering in Frostbite", SIGGRAPH 2015).
 *
 * Froxels are camera-aligned cells: screen tiles x depth slices, the slices spread quadratically
 * up to fogRange (thin near the camera, where detail shows). Each froxel gets
 *  - extinction = height fog (full density below baseHeight, exponential falloff above) shaped
 *    by a wind-blown 3D noise,
 *  - in-scattered light = sun x cascaded-shadow visibility x Henyey-Greenstein phase + ambient,
 * at one point whose depth is jittered every frame (Halton, the same for every froxel), blended
 * with the reprojected previous result: the jitter averages out into a smooth integral.
 * Output: rgb = scattering coefficient x incoming light, a = extinction coefficient.
 */

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
layout(rgba16f, binding = 0) uniform writeonly image3D outVolume;

uniform sampler3D historyVolume;          // Previous frame's output (temporal reprojection)
uniform sampler2DArray directionalShadowMap;
uniform sampler3D noiseVolume;            // Tileable Perlin-Worley (cloud_noise.glsl)

uniform ivec3 gridSize;
uniform float fogRange;                   // View depth covered by the last slice
uniform float depthJitter;                // [0, 1): where in its slice this frame samples
uniform mat4 invProjection;
uniform mat4 invView;
uniform mat4 prevViewProj;
uniform mat4 prevView;
uniform int historyValid;
uniform float temporalBlend;              // Weight of the new frame

uniform vec3 cameraPos;
uniform float density;                    // Extinction per metre at/below baseHeight
uniform float baseHeight;
uniform float heightFalloff;              // Per metre above baseHeight
uniform float noiseStrength;
uniform float noiseScale;                 // Noise repeats per metre
uniform vec3 windOffset;                  // Metres the noise has drifted
uniform vec3 albedo;
uniform vec3 sunDir;                      // Towards the sun
uniform vec3 sunRadiance;
uniform vec3 ambientRadiance;
uniform float anisotropy;                 // Henyey-Greenstein g

uniform mat4 cascadeMatrices[4];
uniform float cascadeSplits[4];
uniform int cascadeCount;

const float PI = 3.14159265;

float SliceToDepth(float slice) { float w = slice / float(gridSize.z); return fogRange * w * w; }
float DepthToSlice01(float depth) { return sqrt(clamp(depth / fogRange, 0.0, 1.0)); }

// Normalised so isotropic scattering = 1
float Phase(float cosTheta, float g)
{
    float g2 = g * g;
    return (1.0 - g2) / pow(1.0 + g2 - 2.0 * g * cosTheta, 1.5);
}

float SunVisibility(vec3 worldPos, float viewDepth)
{
    int layer = -1;
    for (int i = 0; i < cascadeCount; i++) {
        if (viewDepth < cascadeSplits[i]) { layer = i; break; }
    }
    if (layer < 0) return 1.0; // Beyond the shadow distance: unshadowed
    vec4 ls = cascadeMatrices[layer] * vec4(worldPos, 1.0);
    vec3 p = ls.xyz / ls.w * 0.5 + 0.5;
    if (any(lessThan(p, vec3(0.0))) || any(greaterThan(p, vec3(1.0)))) return 1.0;
    float occluder = texture(directionalShadowMap, vec3(p.xy, float(layer))).r;
    return (p.z - 0.0005 > occluder) ? 0.0 : 1.0;
}

void main()
{
    ivec3 cell = ivec3(gl_GlobalInvocationID);
    if (any(greaterThanEqual(cell, gridSize))) return;

    // View ray through the froxel centre, point at the jittered depth inside the slice
    vec2 uv = (vec2(cell.xy) + 0.5) / vec2(gridSize.xy);
    vec4 farPoint = invProjection * vec4(uv * 2.0 - 1.0, 1.0, 1.0);
    vec3 rayView = normalize(farPoint.xyz / farPoint.w);
    float viewDepth = SliceToDepth(float(cell.z) + depthJitter);
    vec3 viewPos = rayView * (viewDepth / max(-rayView.z, 1e-4));
    vec3 worldPos = (invView * vec4(viewPos, 1.0)).xyz;

    // Medium: height fog shaped by drifting noise
    float heightFactor = exp(-max(worldPos.y - baseHeight, 0.0) * heightFalloff);
    vec3 q = (worldPos + windOffset) * noiseScale;
    vec4 n = texture(noiseVolume, q);
    float shape = n.r * 0.65 + texture(noiseVolume, q * 2.7 + 0.31).g * 0.35;
    float noiseFactor = mix(1.0, smoothstep(0.2, 0.85, shape) * 2.0, noiseStrength);
    float extinction = density * heightFactor * noiseFactor;

    // Light arriving at the point and scattered toward the camera
    vec3 toPoint = normalize(worldPos - cameraPos);
    vec3 sun = sunRadiance * SunVisibility(worldPos, viewDepth) * Phase(dot(toPoint, sunDir), anisotropy);
    vec3 scattering = extinction * albedo * (sun + ambientRadiance);
    vec4 current = vec4(scattering, extinction);

    // Temporal reprojection: where was this point in last frame's volume?
    if (historyValid == 1) {
        vec4 prevClip = prevViewProj * vec4(worldPos, 1.0);
        if (prevClip.w > 0.0) {
            vec2 prevUV = prevClip.xy / prevClip.w * 0.5 + 0.5;
            float prevDepth = -(prevView * vec4(worldPos, 1.0)).z;
            vec3 prevCoord = vec3(prevUV, DepthToSlice01(prevDepth));
            if (all(greaterThanEqual(prevCoord, vec3(0.0))) && all(lessThanEqual(prevCoord, vec3(1.0))) && prevDepth < fogRange) {
                current = mix(texture(historyVolume, prevCoord), current, temporalBlend);
            }
        }
    }
    imageStore(outVolume, cell, current);
}
