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

// Sphere culling (for omni light shadow pass)
uniform vec3  lightPos;
uniform int   useSphereCull;        // When 1, cull by distance from lightPos instead of frustum

// LOD configuration
uniform int   lodCount;             // 1, 2, or 3
uniform float lodDistances[3];      // Max distance for each LOD level

// Hi-Z Occlusion Culling
uniform int useHiZ;
uniform vec2 screenSize;
uniform float nearPlane;
uniform float farPlane;
layout(binding = 15) uniform sampler2D hizMap;

// =====================================================================
// Utility: Apply Euler Rotation (Z -> X -> Y order, matching C++ Transform)
// =====================================================================
vec3 rotateEulerZYX(vec3 p, vec3 eulerDeg) {
    vec3 rad = radians(eulerDeg);
    vec3 c = cos(rad);
    vec3 s = sin(rad);

    // 1. Rotate Z
    vec3 p1 = vec3(
        p.x * c.z - p.y * s.z,
        p.x * s.z + p.y * c.z,
        p.z
    );

    // 2. Rotate X
    vec3 p2 = vec3(
        p1.x,
        p1.y * c.x - p1.z * s.x,
        p1.y * s.x + p1.z * c.x
    );

    // 3. Rotate Y
    vec3 p3 = vec3(
        p2.x * c.y + p2.z * s.y,
        p2.y,
       -p2.x * s.y + p2.z * c.y
    );

    return p3;
}


void main()
{
    uint id = gl_GlobalInvocationID.x;
    if (id >= totalInstances) return;

    PackedInstance inst = allInstances[id];
    vec3 pos = inst.posAndScale.xyz;
    float scale = inst.posAndScale.w;
    float radius = instanceBoundRadius * scale;

    // Offset test position to the mesh's bounding sphere center.
    // We must rotate the meshBoundsCenter by the instance's rotation before scaling and adding to pos.
    vec3 rotatedCenter = rotateEulerZYX(meshBoundsCenter, inst.rotAndFlags.xyz);
    vec3 testPos = pos + rotatedCenter * scale;

    // ------- Distance Culling -------
    float dist = distance(testPos, cameraPos);
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
    // Skip frustum/Hi-Z when using sphere culling (omni light shadows)
    if (useSphereCull == 1) {
        // For omni shadows, just use distance from light and skip all view-dependent tests
        float distToLight = distance(testPos, lightPos);
        if (distToLight > maxDrawDistance) return;
    } else {
        vec4 clipPos = viewProj * vec4(testPos, 1.0);

        float w = clipPos.w;
        
        // Behind camera check
        if (w < -radius) return;

        // Add a safety margin (20% of W + radius expansion) to prevent precision popping
        float margin = radius * 2.0 + abs(w) * 0.2;
        float absW = abs(w) + margin;

        // Left/Right planes
        if (clipPos.x < -absW || clipPos.x > absW) return;
        // Bottom/Top planes
        if (clipPos.y < -absW || clipPos.y > absW) return;
        // Near/Far planes (standard OpenGL depth range -w to w)
        if (clipPos.z < -absW || clipPos.z > absW) return;

        // ------- Hi-Z Occlusion Culling -------
        if (useHiZ == 1) {
            // If the camera is inside the bounding sphere, it cannot be occluded!
            if (distance(cameraPos, testPos) <= radius) {
                // Skip Hi-Z
            } else {
                // Compute NDC coordinates of the bounding sphere's screen projection
                // We use a simplified conservative screen-space bounding box for the sphere
                vec3 ndcPos = clipPos.xyz / clipPos.w;
                
                // Project the 8 corners of the world-space AABB to find the screen-space bounding box
                vec3 minPos = testPos - vec3(radius);
                vec3 maxPos = testPos + vec3(radius);
                
                vec4 corners[8];
                corners[0] = viewProj * vec4(minPos.x, minPos.y, minPos.z, 1.0);
                corners[1] = viewProj * vec4(maxPos.x, minPos.y, minPos.z, 1.0);
                corners[2] = viewProj * vec4(minPos.x, maxPos.y, minPos.z, 1.0);
                corners[3] = viewProj * vec4(maxPos.x, maxPos.y, minPos.z, 1.0);
                corners[4] = viewProj * vec4(minPos.x, minPos.y, maxPos.z, 1.0);
                corners[5] = viewProj * vec4(maxPos.x, minPos.y, maxPos.z, 1.0);
                corners[6] = viewProj * vec4(minPos.x, maxPos.y, maxPos.z, 1.0);
                corners[7] = viewProj * vec4(maxPos.x, maxPos.y, maxPos.z, 1.0);
                
                vec2 minNDC = vec2(1.0);
                vec2 maxNDC = vec2(-1.0);
                bool crossesNearPlane = false;
                
                for(int i = 0; i < 8; ++i) {
                    if (corners[i].w < 0.01) {
                        crossesNearPlane = true;
                        break;
                    }
                    vec2 ndc = corners[i].xy / corners[i].w;
                    minNDC = min(minNDC, ndc);
                    maxNDC = max(maxNDC, ndc);
                }
                
                // If the object crosses the camera near plane, it's highly visible and mathematically tricky to bound in NDC
                if (crossesNearPlane) {
                    // skip Hi-Z
                } else {
                    minNDC = clamp(minNDC, -1.0, 1.0);
                    maxNDC = clamp(maxNDC, -1.0, 1.0);
                    
                    vec2 minUV = minNDC * 0.5 + 0.5;
                    vec2 maxUV = maxNDC * 0.5 + 0.5;
                    
                    // Convert to screen pixels to determine mip level
                    vec2 sizePixels = (maxUV - minUV) * screenSize;
                    float maxDim = max(sizePixels.x, sizePixels.y);
                    
                    // Target mip level where the bounding box spans ~2x2 texels
                    float mip = ceil(log2(max(maxDim, 1.0)));
                    
                    // Compute conservative depth of the instance nearest point
                    // Calculate the point on the bounding sphere closest to the camera
                    vec3 toCamera = normalize(cameraPos - testPos);
                    vec3 nearestPos = testPos + toCamera * radius;
                    
                    // Project the nearest point to get its depth
                    vec4 nearestClip = viewProj * vec4(nearestPos, 1.0);
                    float nearestZ = nearestClip.z / nearestClip.w;
                    float nearestDepth = nearestZ * 0.5 + 0.5; // NDC to 0..1 range
                
                    // Sample the 4 texels from the Hi-Z map
                    float d0 = textureLod(hizMap, vec2(minUV.x, minUV.y), mip).r;
                    float d1 = textureLod(hizMap, vec2(maxUV.x, minUV.y), mip).r;
                    float d2 = textureLod(hizMap, vec2(minUV.x, maxUV.y), mip).r;
                    float d3 = textureLod(hizMap, vec2(maxUV.x, maxUV.y), mip).r;
                    
                    // The max value from the map is the deepest point of the occluders in that region.
                    // If the nearest part of our instance is deeper than the deepest occluder, it's hidden!
                    float maxOccluderDepth = max(max(d0, d1), max(d2, d3));
                    
                    // Linearize both depths for comparison.
                    // Standard OpenGL depth is nonlinearly compressed: objects at 200m and 300m
                    // differ by only ~0.0002 in raw depth. Any fixed bias would either
                    // fail to cull or over-cull. Linearizing makes the comparison work correctly.
                    float linNearest  = (2.0 * nearPlane * farPlane) / (farPlane + nearPlane - (nearestDepth * 2.0 - 1.0) * (farPlane - nearPlane));
                    float linOccluder = (2.0 * nearPlane * farPlane) / (farPlane + nearPlane - (maxOccluderDepth * 2.0 - 1.0) * (farPlane - nearPlane));
                    
                    // In linear space, 5.0m bias matches the CPU Hi-Z path
                    if (linNearest > linOccluder + 5.0) {
                        return; // Occluded!
                    }
                }
                
            }
        }

        // ------- Contribution Culling (skip sub-pixel instances) -------
        // Project the bounding sphere to screen pixels; skip if < 2px
        if (screenSize.x > 0.0 && w > 0.01) {
            float projSize = (radius * screenSize.y) / w;
            if (projSize < 2.0) return;
        }
    }

    // ------- LOD Classification + Density-Based Culling -------
    // Density culling skips instances at far distances to reduce triangle count:
    //   LOD 1: keep every 2nd instance (50% density)
    //   LOD 2: keep every 4th instance (25% density)
    if (lodCount >= 3 && dist > lodDistances[1]) {
        // LOD 2: Farthest — 25% density (keep every 4th instance)
        if (id % 4u != 0u) return;
        uint idx = atomicAdd(instanceCountLOD2, 1);
        visibleLOD2[idx] = inst;
    }
    else if (lodCount >= 2 && dist > lodDistances[0]) {
        // LOD 1: Medium distance — 50% density (keep every 2nd instance)
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
