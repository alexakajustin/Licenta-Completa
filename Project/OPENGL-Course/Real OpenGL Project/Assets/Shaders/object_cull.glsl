#version 460 core

// GPU-Driven Object Occlusion Culling
// Uses per-corner depth testing against the Hi-Z pyramid.
//
// Instead of the classic screen-space AABB + high-mip approach (which fails
// when the AABB spans regions with mixed depths like wall + sky), we test
// each AABB corner independently at its own screen position at a low mip.
// An object is occluded only if ALL of its corners are behind the depth buffer.
//
// This avoids the mip-level catastrophe where a tall/wide AABB picks up
// sky depth (1.0) at high mip levels, making occlusion impossible.

layout(local_size_x = 64) in;

struct ObjectBounds {
    vec4 bboxMin;   // xyz = world min, w = unused
    vec4 bboxMax;   // xyz = world max, w = unused
};

layout(std430, binding = 0) readonly buffer BoundsBuffer {
    ObjectBounds bounds[];
};

layout(std430, binding = 1) writeonly buffer VisibilityBuffer {
    uint visibility[];
};

uniform uint  objectCount;
uniform mat4  viewProjTM;
uniform vec2  viewSize;
uniform float viewCullThreshold;
uniform float nearPlane;
uniform float farPlane;

layout(binding = 0) uniform sampler2D depthTex; // Hi-Z pyramid

// =====================================================================
// Helpers
// =====================================================================
vec4 getBoxCorner(vec4 bboxMin, vec4 bboxMax, int n) {
    bvec3 useMax = bvec3((n & 1) != 0, (n & 2) != 0, (n & 4) != 0);
    return vec4(mix(bboxMin.xyz, bboxMax.xyz, useMax), 1.0);
}

uint getCullBits(vec4 hPos) {
    uint bits = 0u;
    bits |= hPos.x < -hPos.w ?  1u : 0u;
    bits |= hPos.x >  hPos.w ?  2u : 0u;
    bits |= hPos.y < -hPos.w ?  4u : 0u;
    bits |= hPos.y >  hPos.w ?  8u : 0u;
    bits |= hPos.z < -hPos.w ? 16u : 0u;
    bits |= hPos.z >  hPos.w ? 32u : 0u;
    bits |= hPos.w <= 0.0     ? 64u : 0u;
    return bits;
}

float linearizeDepth(float d) {
    return (2.0 * nearPlane * farPlane) / (farPlane + nearPlane - (d * 2.0 - 1.0) * (farPlane - nearPlane));
}

// =====================================================================
// Main
// =====================================================================
void main() {
    uint id = gl_GlobalInvocationID.x;
    if (id >= objectCount) return;

    vec4 bboxMin = bounds[id].bboxMin;
    vec4 bboxMax = bounds[id].bboxMax;

    // ---------------------------------------------------------------
    // Step 1: Project all 8 corners for frustum + sub-pixel cull
    // ---------------------------------------------------------------
    vec4 hPositions[8];
    vec3 clipmin = vec3(1e10);
    vec3 clipmax = vec3(-1e10);
    uint clipbits = 0xFFFFFFFFu; // Start with all bits set
    bool anyBehindCamera = false;

    for (int n = 0; n < 8; n++) {
        hPositions[n] = viewProjTM * getBoxCorner(bboxMin, bboxMax, n);
        
        // Update frustum cull bits (Intersection: all corners must be outside SAME plane)
        clipbits &= getCullBits(hPositions[n]);

        if (hPositions[n].w > 0.0) {
            vec3 ndc = hPositions[n].xyz / hPositions[n].w;
            clipmin = min(clipmin, ndc);
            clipmax = max(clipmax, ndc);
        } else {
            anyBehindCamera = true;
        }
    }

    // Frustum cull: if all corners are on the wrong side of any plane -> culled
    if (clipbits != 0u) {
        visibility[id] = 0u;
        return;
    }

    // Sub-pixel cull: only if all corners are in front of camera
    if (!anyBehindCamera) {
        vec2 dim = (clipmax.xy - clipmin.xy) * 0.5 * viewSize;
        if (max(dim.x, dim.y) < viewCullThreshold) {
            visibility[id] = 0u;
            return;
        }
    }

    // ---------------------------------------------------------------
    // Step 2: Per-corner Hi-Z occlusion test
    //
    // For each corner, project to screen UV and sample the Hi-Z at
    // a LOW mip level at that specific position. Compare the corner's
    // own depth against the Hi-Z depth. If ALL corners (+ center) are
    // behind the depth buffer, the object is fully occluded.
    //
    // Using mip 3 (~8x8 pixel footprint per sample) provides enough
    // spatial margin to avoid sub-pixel flickering while remaining
    // precise enough to not pick up sky/background depth from far away.
    // ---------------------------------------------------------------
    const float sampleMip = 3.0;
    bool allOccluded = true;

    // Test 8 corners
    for (int n = 0; n < 8; n++) {
        // Corner behind camera → can't be occluded
        if (hPositions[n].w <= 0.01) {
            allOccluded = false;
            break;
        }

        vec3 ndc = hPositions[n].xyz / hPositions[n].w;
        vec2 uv = ndc.xy * 0.5 + 0.5;
        float cornerDepth01 = ndc.z * 0.5 + 0.5;

        // Corner off-screen → conservatively visible
        if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
            allOccluded = false;
            break;
        }

        // Sample Hi-Z at this corner's screen position
        float hiZDepth = textureLod(depthTex, uv, sampleMip).r;

        // Linearized comparison: 1.0m bias prevents z-fighting and accounts for dynamic surfaces
        float linCorner = linearizeDepth(cornerDepth01);
        float linHiZ    = linearizeDepth(hiZDepth);

        // If this corner is in front of (or near) the occluder -> visible
        if (linCorner <= linHiZ + 1.0) {
            allOccluded = false;
            break;
        }
    }

    // Also test the AABB center to catch objects whose corners are all
    // behind a wall but whose center is visible through a gap
    if (allOccluded) {
        vec4 center = vec4((bboxMin.xyz + bboxMax.xyz) * 0.5, 1.0);
        vec4 hCenter = viewProjTM * center;
        if (hCenter.w > 0.01) {
            vec3 ndcC = hCenter.xyz / hCenter.w;
            vec2 uvC = ndcC.xy * 0.5 + 0.5;
            float depthC = ndcC.z * 0.5 + 0.5;
            if (uvC.x >= 0.0 && uvC.x <= 1.0 && uvC.y >= 0.0 && uvC.y <= 1.0) {
                float hiZC = textureLod(depthTex, uvC, sampleMip).r;
                float linC = linearizeDepth(depthC);
                float linH = linearizeDepth(hiZC);
                if (linC <= linH + 1.0) {
                    allOccluded = false;
                }
            }
        }
    }

    visibility[id] = allOccluded ? 0u : 1u;
}
