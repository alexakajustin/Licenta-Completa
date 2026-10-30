#version 460 core

/**
 * @file water_foam_obstacle_instanced.vert
 * @brief Top-down pass of the water foam map for GPU-instanced groups (rocks, logs, plants).
 * Drawn through InstancedGroup::CullAndDrawShadow with the foam window as the "light" matrix,
 * so it reads the culled instances exactly like Shaders/instanced_shadow.vert (same wind).
 */

layout (location = 0) in vec3 pos;
layout (location = 1) in vec2 tex;

struct PackedInstance {
    vec4 posAndScale;     // xyz = position, w = scale
    vec4 rotAndFlags;     // xyz = euler degrees, w = flags
};

layout(std430, binding = 1) readonly buffer VisibleInstances {
    PackedInstance instances[];
};

uniform mat4 directionalLightTransform; // Top-down foam window

struct Material {
    float specularIntensity;
    float shininess;
    vec4 baseColor;
    vec2 tiling;
    vec2 offset;
};
uniform Material material;

uniform float time;
uniform float windSpeed;
uniform float windStrength;

out vec3 WorldPos;
out vec2 TexCoord;

// Same rotation as the instanced render/shadow/cull shaders
mat3 eulerToMat3(vec3 euler) {
    float cx = cos(radians(euler.x));
    float sx = sin(radians(euler.x));
    float cy = cos(radians(euler.y));
    float sy = sin(radians(euler.y));
    float cz = cos(radians(euler.z));
    float sz = sin(radians(euler.z));

    mat3 rx = mat3(1, 0, 0,   0, cx, -sx,   0, sx, cx);
    mat3 ry = mat3(cy, 0, sy,  0, 1, 0,     -sy, 0, cy);
    mat3 rz = mat3(cz, -sz, 0,  sz, cz, 0,   0, 0, 1);

    return rz * ry * rx;
}

void main()
{
    PackedInstance inst = instances[gl_InstanceID];

    // Wind sway, identical to grass.vert / instanced_shadow.vert
    vec2 phase = inst.posAndScale.xz * 0.35 + pos.xz;
    float bend = pos.y > 0.0 ? 1.0 : 0.0;
    vec3 localPos = pos;
    localPos.x += sin(time * windSpeed + phase.x) * windStrength * bend;
    localPos.z += cos(time * windSpeed * 0.83 + phase.y) * windStrength * bend;

    vec3 world = eulerToMat3(inst.rotAndFlags.xyz) * (localPos * inst.posAndScale.w) + inst.posAndScale.xyz;
    WorldPos = world;
    TexCoord = tex * material.tiling + material.offset;
    gl_Position = directionalLightTransform * vec4(world, 1.0);
}
