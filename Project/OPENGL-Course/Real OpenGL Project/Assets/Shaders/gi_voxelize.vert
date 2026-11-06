#version 430 core

/**
 * @file gi_voxelize.vert
 * @brief Voxelization of the scene for the global illumination (Rendering/GlobalIllumination.h):
 * world-space vertices of a plain object (model matrix) or of a GPU instance (the visible list the
 * culler wrote for the voxel level's box, as the shadow passes read it).
 */

layout (location = 0) in vec3 pos;
layout (location = 1) in vec2 tex;
layout (location = 2) in vec3 norm;

struct PackedInstance {
    vec4 posAndScale;     // xyz = position, w = scale
    vec4 rotAndFlags;     // xyz = euler degrees, w = flags
};
layout(std430, binding = 1) readonly buffer VisibleInstances {
    PackedInstance instances[];
};

uniform int instanced;  ///< 1: an instanced group's LOD lists
uniform mat4 model;
struct Material {
    vec2 tiling;
    vec2 offset;
};
uniform Material material;

out VS_OUT {
    vec3 worldPos;
    vec3 normal;
    vec2 uv;
} vsOut;

mat3 EulerToMat3(vec3 euler)
{
    float cx = cos(radians(euler.x)), sx = sin(radians(euler.x));
    float cy = cos(radians(euler.y)), sy = sin(radians(euler.y));
    float cz = cos(radians(euler.z)), sz = sin(radians(euler.z));
    mat3 rx = mat3(1, 0, 0,   0, cx, -sx,   0, sx, cx);
    mat3 ry = mat3(cy, 0, sy,  0, 1, 0,     -sy, 0, cy);
    mat3 rz = mat3(cz, -sz, 0,  sz, cz, 0,   0, 0, 1);
    return rz * ry * rx;
}

void main()
{
    vec2 tiling = (material.tiling.x == 0.0 && material.tiling.y == 0.0) ? vec2(1.0) : material.tiling;
    vsOut.uv = tex * tiling + material.offset;
    if (instanced == 1) {
        PackedInstance inst = instances[gl_InstanceID];
        mat3 rot = EulerToMat3(inst.rotAndFlags.xyz);
        vsOut.worldPos = rot * (pos * inst.posAndScale.w) + inst.posAndScale.xyz;
        vsOut.normal = rot * norm;
    } else {
        vsOut.worldPos = (model * vec4(pos, 1.0)).xyz;
        vsOut.normal = mat3(transpose(inverse(model))) * norm;
    }
    gl_Position = vec4(vsOut.worldPos, 1.0);
}
