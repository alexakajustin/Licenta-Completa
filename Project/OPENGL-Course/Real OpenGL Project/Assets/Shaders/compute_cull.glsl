#version 430 core
layout(local_size_x = 256) in;

// =====================================================================
// GPU Frustum + Distance Culling + LOD Classification Compute Shader
//
// For each instance, tests:
//   1. Distance culling (skip instances beyond maxDrawDistance)
//   2. Frustum culling (clip-space sphere test — 3 axis pairs)
//   3. LOD classification (distance-based bucket assignment)
//
// Visible instances are written to LOD-specific output SSBOs.
// Each LOD's indirect draw command instanceCount is atomically incremented.
// =====================================================================

struct PackedInstance {
    vec4 posAndScale;     // xyz = position, w = scale
    vec4 rotAndFlags;     // xyz = euler degrees, w = flags
};

// Binding 0: All instances (input — read only)
layout(std430, binding = 0) readonly buffer AllInstances {
    PackedInstance allInstances[];
};

// Binding 1: LOD 0 Visible instances (output)
layout(std430, binding = 1) writeonly buffer VisibleInstancesLOD0 {
    PackedInstance visibleLOD0[];
};

// Binding 2: LOD 0 Indirect draw command
layout(std430, binding = 2) buffer DrawIndirectLOD0 {
    uint indexCountLOD0;
    uint instanceCountLOD0;
    uint firstIndexLOD0;
    uint baseVertexLOD0;
    uint baseInstanceLOD0;
};

// Binding 3: LOD 1 Visible instances (optional)
layout(std430, binding = 3) writeonly buffer VisibleInstancesLOD1 {
    PackedInstance visibleLOD1[];
};

// Binding 4: LOD 2 Visible instances (optional)
layout(std430, binding = 4) writeonly buffer VisibleInstancesLOD2 {
    PackedInstance visibleLOD2[];
};

// Binding 5: LOD 1 Indirect draw command (optional)
layout(std430, binding = 5) buffer DrawIndirectLOD1 {
    uint indexCountLOD1;
    uint instanceCountLOD1;
    uint firstIndexLOD1;
    uint baseVertexLOD1;
    uint baseInstanceLOD1;
};

// Binding 6: LOD 2 Indirect draw command (optional)
layout(std430, binding = 6) buffer DrawIndirectLOD2 {
    uint indexCountLOD2;
    uint instanceCountLOD2;
    uint firstIndexLOD2;
    uint baseVertexLOD2;
    uint baseInstanceLOD2;
};

uniform mat4  viewProj;
uniform vec3  cameraPos;
uniform float maxDrawDistance;
uniform float instanceBoundRadius;  // Bounding sphere radius of the shared mesh
uniform uint  totalInstances;
uniform vec3  meshBoundsCenter;     // Center of mesh AABB relative to origin (e.g. (0, 0.5, 0) for grass)

// LOD configuration
uniform int   lodCount;             // 1, 2, or 3
uniform float lodDistances[3];      // Max distance for each LOD level

void main()
{
    uint id = gl_GlobalInvocationID.x;
    if (id >= totalInstances) return;

    PackedInstance inst = allInstances[id];
    vec3 pos = inst.posAndScale.xyz;
    float scale = inst.posAndScale.w;
    float radius = instanceBoundRadius * scale;

    // Offset test position to the mesh's bounding sphere center.
    // Instance position is at mesh origin (e.g. grass base at y=0),
    // but the visual center is at meshBoundsCenter (e.g. y=0.5).
    // Without this offset, tall objects get culled when their base
    // goes off-screen even though the top is still visible.
    vec3 testPos = pos + meshBoundsCenter * scale;

    // ------- Distance Culling -------
    float dist = distance(pos, cameraPos);
    if (dist > maxDrawDistance) return;

    // ------- Distance Fade (dithered discard in fragment shader) -------
    // Fade zone: 85% to 100% of maxDrawDistance
    float fadeStart = maxDrawDistance * 0.85;
    float fadeFactor = 0.0;
    if (dist > fadeStart) {
        fadeFactor = clamp((dist - fadeStart) / (maxDrawDistance - fadeStart), 0.0, 1.0);
    }
    // Store fade factor in rotAndFlags.w for the fragment shader
    inst.rotAndFlags.w = fadeFactor;

    // ------- Frustum Culling (clip-space sphere test) -------
    vec4 clipPos = viewProj * vec4(testPos, 1.0);

    float w = clipPos.w;
    
    // Behind camera check
    if (w < -radius) return;

    float absW = abs(w) + radius;

    // Left/Right planes
    if (clipPos.x < -absW || clipPos.x > absW) return;
    // Bottom/Top planes
    if (clipPos.y < -absW || clipPos.y > absW) return;
    // Near/Far planes
    if (clipPos.z < -radius || clipPos.z > absW) return;

    // ------- LOD Classification + Density-Based Culling -------
    // For simple meshes (grass quads), LOD meshes are identical to LOD0.
    // Density culling skips instances at far distances to reduce triangle count:
    //   LOD 1: keep every 2nd instance (50% density)
    //   LOD 2: keep every 4th instance (25% density)
    if (lodCount >= 3 && dist > lodDistances[1]) {
        // LOD 2: Farthest — 25% density
        if (id % 4u != 0u) return;
        uint idx = atomicAdd(instanceCountLOD2, 1);
        visibleLOD2[idx] = inst;
    }
    else if (lodCount >= 2 && dist > lodDistances[0]) {
        // LOD 1: Medium distance — 50% density
        if (id % 2u != 0u) return;
        uint idx = atomicAdd(instanceCountLOD1, 1);
        visibleLOD1[idx] = inst;
    }
    else {
        // LOD 0: Closest — full density, full detail
        uint idx = atomicAdd(instanceCountLOD0, 1);
        visibleLOD0[idx] = inst;
    }
}
