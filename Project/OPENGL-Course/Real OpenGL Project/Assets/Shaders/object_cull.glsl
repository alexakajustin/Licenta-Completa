#version 460 core

// GPU-Driven Object Occlusion Culling against the Hi-Z (max-depth) pyramid.
//
// Conservative screen-rect test: the AABB's projected rectangle is covered
// completely by reading every Hi-Z texel it touches, at a mip where that is at
// most 5x5 texels. The object is occluded only if its NEAREST point is behind
// the FARTHEST occluder depth anywhere in that rectangle.
//
// (The previous per-corner test sampled only 8 corners + the center, so an
// object visible through a gap between those sample points - a window, a gap
// between two walls - was wrongly culled and popped in/out.)

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
    // Step 2: Conservative Hi-Z occlusion test over the whole screen rect
    // ---------------------------------------------------------------

    // Box crosses the camera plane: its screen rect is unbounded -> visible
    if (anyBehindCamera) {
        visibility[id] = 1u;
        return;
    }

    // Screen rect in mip-0 pixels, clipped to the viewport (off-screen parts can't be seen)
    vec2 uvMin = clamp(clipmin.xy * 0.5 + 0.5, 0.0, 1.0);
    vec2 uvMax = clamp(clipmax.xy * 0.5 + 0.5, 0.0, 1.0);
    vec2 pixMin = uvMin * viewSize;
    vec2 pixMax = uvMax * viewSize;

    // Nearest point of the box: depth is monotonic in view z, so it is the min over the corners
    float nearestDepth01 = clamp(clipmin.z * 0.5 + 0.5, 0.0, 1.0);

    // Mip where the rect spans <= 4 texels per axis (<= 5 after alignment)
    int levels = textureQueryLevels(depthTex);
    float extent = max(pixMax.x - pixMin.x, pixMax.y - pixMin.y);
    int mip = clamp(int(ceil(log2(max(extent / 4.0, 1.0)))), 0, levels - 1);

    // Mip-m texel t covers mip-0 pixels [t*2^m, (t+1)*2^m) — the NPOT downsample
    // folds odd edges into the last texel, so clamping to the last texel stays conservative.
    ivec2 mipSize = textureSize(depthTex, mip);
    ivec2 t0 = min(ivec2(floor(pixMin)) >> mip, mipSize - 1);
    ivec2 t1 = min(ivec2(floor(pixMax)) >> mip, mipSize - 1);

    float maxOccluderDepth = 0.0;
    for (int y = t0.y; y <= t1.y && y <= t0.y + 5; y++) {
        for (int x = t0.x; x <= t1.x && x <= t0.x + 5; x++) {
            maxOccluderDepth = max(maxOccluderDepth, texelFetch(depthTex, ivec2(x, y), mip).r);
        }
    }

    // Linearized comparison with a small bias against z-fighting / dynamic surfaces
    bool occluded = linearizeDepth(nearestDepth01) > linearizeDepth(maxOccluderDepth) + 0.5;
    visibility[id] = occluded ? 0u : 1u;
}
