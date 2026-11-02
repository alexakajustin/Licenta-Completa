#version 430 core
layout(local_size_x = 256) in;

/**
 * @file compute_cull.glsl
 * @brief GPU-driven culling and LOD selection for instanced groups (one thread per instance).
 *
 * 1. Visibility in this pass: the 6 frustum planes of the pass (camera, reflection or a shadow
 *    cascade — plane tests work for perspective and orthographic projections alike), or the
 *    distance to an omni light; Hi-Z occlusion in the camera pass.
 * 2. Level of detail by distance to the camera (LODPolicy.h): the stages of Graphics Settings
 *    (LOD0, LOD1, LOD2, impostor; switched-off ones are left out on the CPU), each ending at its
 *    own distance. Every pass measures it from the camera, so the shadow of a tree comes from the
 *    representation the camera sees (or a later stage in the far cascades).
 * 3. Dithered cross-fade: over the last 15% of a stage the instance is emitted to both stages
 *    with complementary 4x4 dither masks (the fragment shaders discard by vFadeFactor:
 *    > 0 fades out, < 0 fades in; foliage overlaps the two stages instead, see the end of
 *    main), and it fades out before the minimum screen size and the maximum draw distance — no popping.
 * 4. Detail foliage (grass: thousands of cheap instances without LOD meshes) thins out with
 *    distance instead: each instance keeps a stable random rank, blades about to drop out shrink
 *    away first and the survivors grow so the field stays covered.
 * 5. Impostors (Impostor.h): the last stage of trees is one camera-facing card baked from the
 *    full LOD0. A tree is several groups (bark, leaves) sharing the same instances, bounds and
 *    stages here, so they agree on every decision: one of them draws the impostor, the others
 *    draw nothing at that range.
 */

struct PackedInstance {
    vec4 posAndScale;     ///< xyz = position, w = uniform scale
    vec4 rotAndFlags;     ///< xyz = euler degrees, w = selection flag (input) / fade factor + 10 * selected (output)
};

layout(std430, binding = 0) readonly buffer AllInstances { PackedInstance allInstances[]; };

layout(std430, binding = 1) writeonly buffer VisibleInstancesLOD0 { PackedInstance visibleLOD0[]; };
layout(std430, binding = 3) writeonly buffer VisibleInstancesLOD1 { PackedInstance visibleLOD1[]; };
layout(std430, binding = 4) writeonly buffer VisibleInstancesLOD2 { PackedInstance visibleLOD2[]; };

layout(std430, binding = 2) buffer DrawIndirectLOD0 {
    uint indexCountLOD0; uint instanceCountLOD0; uint firstIndexLOD0; uint baseVertexLOD0; uint baseInstanceLOD0;
};
layout(std430, binding = 5) buffer DrawIndirectLOD1 {
    uint indexCountLOD1; uint instanceCountLOD1; uint firstIndexLOD1; uint baseVertexLOD1; uint baseInstanceLOD1;
};
layout(std430, binding = 6) buffer DrawIndirectLOD2 {
    uint indexCountLOD2; uint instanceCountLOD2; uint firstIndexLOD2; uint baseVertexLOD2; uint baseInstanceLOD2;
};

layout(std430, binding = 8) writeonly buffer VisibleInstancesImpostor { PackedInstance visibleImpostor[]; };
layout(std430, binding = 9) buffer DrawIndirectImpostor {
    uint indexCountImp; uint instanceCountImp; uint firstIndexImp; uint baseVertexImp; uint baseInstanceImp;
};

/// Camera pass, two-phase occlusion culling: 1 = the instance passed last frame's tests
layout(std430, binding = 7) buffer VisibilityBuffer { uint visibility[]; };

// --- Pass ---
uniform vec4  frustumPlanes[6];    ///< Normalised planes of the pass, inside where dot(n, p) + d >= 0
uniform int   useSphereCull;       ///< Omni shadows: distance to lightPos instead of the frustum
uniform vec3  lightPos;
uniform uint  totalInstances;      ///< Instances in this dispatch
uniform uint  instanceOffset;      ///< Chunked dispatches: global index of this chunk's first instance
uniform int   phase;               ///< 0: draw last frame's visible set, 1: test everything against Hi-Z, 2: single pass

// --- Bounds ---
uniform float instanceBoundRadius; ///< Bounding sphere radius of the LOD0 mesh (unscaled)
uniform vec3  meshBoundsCenter;    ///< Bounding sphere centre relative to the mesh origin

// --- Level of detail (always measured from the camera) ---
uniform vec3  cameraPos;
uniform int   stageCount;          ///< Stages in use, nearest first (1-4)
uniform int   stageRep[4];         ///< What each stage draws: mesh LOD 0-2 or IMPOSTOR
uniform float stageEnd[4];         ///< Camera distance where each stage ends (the last one: never)
uniform int   lodMin;              ///< Stages skipped (far shadow cascades)
uniform float crossfadeWidth;      ///< Relative width of the transition bands, 0 = hard switches
uniform float projScale;           ///< projection[1][1] of the camera (screen size of the bounds)
uniform float minScreenSize;       ///< Below: not drawn
uniform float maxDrawDistance;     ///< Hard limit, faded out over its last 15%
uniform float minCasterSize;       ///< Shadow cascades: casters thinner than a shadow texel are skipped
uniform int   impostorMode;        ///< 1: this group draws the tree's impostor, 2: a sibling group does
uniform int   overlapFade;         ///< 1: cross-fade by overlapping the two stages (foliage, trees)

// --- Detail foliage ---
uniform int   detailFoliage;
uniform vec2  detailRange;         ///< Full density up to x, a quarter from y on
uniform float detailDensity;       ///< Global density multiplier (Graphics Settings)

// --- Hi-Z occlusion (camera pass) ---
uniform int   useHiZ;
uniform vec2  screenSize;
uniform float nearPlane;
uniform float farPlane;
uniform mat4  hizViewProj;
layout(binding = 15) uniform sampler2D hizMap;

/// Same rotation as the instanced vertex shaders (instanced_header.glsl, instanced_shadow.vert)
mat3 eulerToMat3(vec3 euler) {
    float cx = cos(radians(euler.x)), sx = sin(radians(euler.x));
    float cy = cos(radians(euler.y)), sy = sin(radians(euler.y));
    float cz = cos(radians(euler.z)), sz = sin(radians(euler.z));
    mat3 rx = mat3(1.0, 0.0, 0.0, 0.0, cx, -sx, 0.0, sx, cx);
    mat3 ry = mat3(cy, 0.0, sy, 0.0, 1.0, 0.0, -sy, 0.0, cy);
    mat3 rz = mat3(cz, -sz, 0.0, sz, cz, 0.0, 0.0, 0.0, 1.0);
    return rz * ry * rx;
}

/// Integer hash (lowbias32): a stable random rank per instance
uint Hash(uint x) {
    x ^= x >> 16; x *= 0x7feb352du;
    x ^= x >> 15; x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

float LinearDepth(float depth) {
    return (2.0 * nearPlane * farPlane) / (farPlane + nearPlane - (depth * 2.0 - 1.0) * (farPlane - nearPlane));
}

/// Conservative test of the bounding sphere against the Hi-Z pyramid (max depth per texel)
bool OccludedByHiZ(vec3 center, float radius) {
    if (distance(cameraPos, center) <= radius) return false; // Camera inside the sphere

    vec3 minPos = center - vec3(radius), maxPos = center + vec3(radius);
    vec2 minNDC = vec2(1.0), maxNDC = vec2(-1.0);
    for (int i = 0; i < 8; i++) {
        vec3 corner = vec3((i & 1) != 0 ? maxPos.x : minPos.x, (i & 2) != 0 ? maxPos.y : minPos.y, (i & 4) != 0 ? maxPos.z : minPos.z);
        vec4 clip = hizViewProj * vec4(corner, 1.0);
        if (clip.w < 0.01) return false; // Crosses the near plane: treat as visible
        vec2 ndc = clip.xy / clip.w;
        minNDC = min(minNDC, ndc);
        maxNDC = max(maxNDC, ndc);
    }
    vec2 minUV = clamp(minNDC, -1.0, 1.0) * 0.5 + 0.5;
    vec2 maxUV = clamp(maxNDC, -1.0, 1.0) * 0.5 + 0.5;

    // Mip where the box spans at most one texel, so the 4 corner samples cover it
    vec2 sizePixels = (maxUV - minUV) * screenSize;
    float mip = ceil(log2(max(max(sizePixels.x, sizePixels.y), 1.0)));

    vec3 nearest = center + normalize(cameraPos - center) * radius;
    vec4 nearestClip = hizViewProj * vec4(nearest, 1.0);
    float nearestDepth = (nearestClip.z / nearestClip.w) * 0.5 + 0.5;

    float d0 = textureLod(hizMap, vec2(minUV.x, minUV.y), mip).r;
    float d1 = textureLod(hizMap, vec2(maxUV.x, minUV.y), mip).r;
    float d2 = textureLod(hizMap, vec2(minUV.x, maxUV.y), mip).r;
    float d3 = textureLod(hizMap, vec2(maxUV.x, maxUV.y), mip).r;
    float occluderDepth = max(max(d0, d1), max(d2, d3));

    // Compared linearly: raw depths 200 m and 300 m apart differ by ~0.0002
    return LinearDepth(nearestDepth) > LinearDepth(occluderDepth) + 0.5;
}

const int IMPOSTOR = 3; ///< Representation slot of the impostor card

void Emit(int lod, PackedInstance inst, float fade, float selectedFlag) {
    inst.rotAndFlags.w = fade + selectedFlag;
    if (lod == IMPOSTOR) {
        if (impostorMode != 1) return; // Drawn by a sibling group of the same tree
        uint i = atomicAdd(instanceCountImp, 1u);
        visibleImpostor[i] = inst;
    } else if (lod == 0) {
        uint i = atomicAdd(instanceCountLOD0, 1u);
        visibleLOD0[i] = inst;
    } else if (lod == 1) {
        uint i = atomicAdd(instanceCountLOD1, 1u);
        visibleLOD1[i] = inst;
    } else {
        uint i = atomicAdd(instanceCountLOD2, 1u);
        visibleLOD2[i] = inst;
    }
}

void main()
{
    uint id = gl_GlobalInvocationID.x;
    if (id >= totalInstances) return;
    uint globalId = id + instanceOffset;

    // Phase 0 draws only what was visible last frame (it builds the depth for the Hi-Z)
    bool wasVisible = (phase != 2) && visibility[globalId] == 1u;
    if (phase == 0 && !wasVisible) return;

    PackedInstance inst = allInstances[id];
    float scale = inst.posAndScale.w;
    float radius = instanceBoundRadius * scale;
    vec3 center = inst.posAndScale.xyz + eulerToMat3(inst.rotAndFlags.xyz) * meshBoundsCenter * scale;
    float selectedFlag = inst.rotAndFlags.w > 0.5 ? 10.0 : 0.0;

    float dist = distance(center, cameraPos);
    bool visible = dist <= maxDrawDistance && radius * 2.0 >= minCasterSize;

    if (visible) {
        if (useSphereCull == 1) {
            visible = distance(center, lightPos) - radius <= maxDrawDistance;
        } else {
            for (int p = 0; p < 6; p++) {
                if (dot(frustumPlanes[p].xyz, center) + frustumPlanes[p].w < -radius) { visible = false; break; }
            }
        }
    }
    if (visible && useHiZ == 1 && OccludedByHiZ(center, radius)) visible = false;

    // Phase 1 refreshes the visible set for the next frame and draws only the newcomers
    if (phase == 1) {
        visibility[globalId] = visible ? 1u : 0u;
        if (wasVisible) return; // Already drawn in phase 0
    }
    if (!visible) return;

    // Fade out over the last 15% of the draw distance, and just above the minimum screen size
    float fadeOut = clamp((dist - maxDrawDistance * 0.85) / (maxDrawDistance * 0.15), 0.0, 1.0);
    float size = radius * projScale / max(dist, 1e-4);
    if (size < minScreenSize) return;
    if (crossfadeWidth > 0.0)
        fadeOut = max(fadeOut, clamp((minScreenSize * (1.0 + crossfadeWidth) - size) / (minScreenSize * crossfadeWidth), 0.0, 1.0));

    if (detailFoliage == 1) {
        // Down to a quarter of the instances across detailRange; the survivors grow by
        // 1/sqrt(density) so the ground stays just as covered (only the distance thinning is
        // compensated, not the global density setting)
        float distanceDensity = mix(1.0, 0.25, smoothstep(detailRange.x, detailRange.y, dist));
        float density = distanceDensity * detailDensity;
        float rank = float(Hash(globalId) & 0xFFFFu) / 65536.0;
        if (rank >= density) return;
        float shrink = clamp((density - rank) / 0.08, 0.0, 1.0);
        inst.posAndScale.w *= shrink * inversesqrt(distanceDensity);
        Emit(0, inst, fadeOut, selectedFlag);
        return;
    }

    // Stage at this distance (LODPolicy::Select)
    int stage = 0;
    while (stage < stageCount - 1 && dist >= stageEnd[stage]) stage++;
    stage = max(stage, min(lodMin, stageCount - 1));
    int rep = stageRep[stage];
    int nextRep = stage < stageCount - 1 ? stageRep[stage + 1] : rep;

    // Progress of the transition to the next stage over the last part of this one, quantised to
    // the 16 dither levels so the two halves cover every pixel exactly once
    float t = 0.0;
    if (crossfadeWidth > 0.0 && nextRep != rep) {
        float end = stageEnd[stage];
        t = clamp((dist - end * (1.0 - crossfadeWidth)) / (end * crossfadeWidth), 0.0, 1.0);
    }
    float q = floor(t * 16.0 + 0.5) / 16.0;

    if (fadeOut > 0.0 || q <= 0.0) {
        Emit(rep, inst, fadeOut, selectedFlag);
    } else if (q >= 1.0) {
        Emit(nextRep, inst, 0.0, selectedFlag);
    } else if (overlapFade == 1) {
        // Foliage: each half of a complementary dither is half of a sparse crown (with holes in
        // different places), so a tree in the band showed the sky through both. Instead the
        // incoming stage fades in over the whole outgoing one, then the outgoing one fades out
        // under the whole incoming one: there is always at least one complete crown.
        float inShare = min(q * 2.0, 1.0);         // Share of the incoming stage drawn
        float outRemoved = max(q * 2.0 - 1.0, 0.0); // Share of the outgoing stage removed
        Emit(rep, inst, outRemoved, selectedFlag);
        Emit(nextRep, inst, -inShare, selectedFlag);
    } else {
        Emit(rep, inst, q, selectedFlag);
        Emit(nextRep, inst, -q, selectedFlag);
    }
}
