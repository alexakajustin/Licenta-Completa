#version 460 core

// =====================================================================
// Instanced Omni Shadow Vertex Shader
//
// Reads per-instance packed transforms from an SSBO and renders
// into the omni (cube map) shadow map, moving with the scene's wind
// (wind.glsl; the plant's wind mode, base and height are set per group).
// =====================================================================

layout (location = 0) in vec3 pos;
layout (location = 1) in vec2 tex;

struct PackedInstance {
    vec4 posAndScale;     // xyz = position, w = scale
    vec4 rotAndFlags;     // xyz = euler degrees, w = flags
};

layout(std430, binding = 1) readonly buffer VisibleInstances {
    PackedInstance instances[];
};

uniform mat4 lightMatrix;
uniform float farPlane;

struct Material {
    float specularIntensity;
    float shininess;
    vec4 baseColor;
    vec2 tiling;
    vec2 offset;
};
uniform Material material;

uniform float time;
// The scene's wind (wind.glsl). Plants (set per instanced group): 0 still, 1 grass, 2 plant
// (trunk + branches: bark), 3 plant leaves (+ flutter). Base and height are the whole plant's
// (bark and leaves together, mesh space), so every part of it moves as one.
#include "wind.glsl"
uniform int windMode;
uniform float windPlantBase;
uniform float windPlantHeight;

layout(location = 0) out vec4 FragPosGS;
layout(location = 1) out vec2 TexCoordGS;
layout(location = 2) out float vFadeFactorGS;

// Build rotation matrix from euler angles (degrees)
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
    
    vec3 instancePos = inst.posAndScale.xyz;
    float instanceScale = inst.posAndScale.w;
    vec3 instanceRot = inst.rotAndFlags.xyz;
    
    mat3 rotMat = eulerToMat3(instanceRot);
    // The scene's wind, exactly as the camera sees the instance (grass.vert, shader.vert)
    vec3 worldPos = rotMat * (pos * instanceScale) + instancePos;
    if (windMode == 1) {
        vec3 up = vec3(0.0, 1.0, 0.0);
        worldPos = WindGrass(worldPos, instancePos, (windPlantHeight > 0.0 ? windPlantHeight : 1.0) * instanceScale, up);
    } else if (windMode >= 2) {
        vec3 root = instancePos + rotMat * vec3(0.0, windPlantBase, 0.0) * instanceScale;
        worldPos = WindPlant(worldPos, root, windPlantHeight * instanceScale, length(pos.xz) * instanceScale,
            pos, vec3(0.0, 1.0, 0.0), windMode == 3);
    }
    
    // Output projected position for this face
    FragPosGS = vec4(worldPos, 1.0);
    gl_Position = lightMatrix * vec4(worldPos, 1.0);
    
    TexCoordGS = tex * material.tiling + material.offset;
    float rawW = inst.rotAndFlags.w;
    vFadeFactorGS = (rawW > 5.0) ? (rawW - 10.0) : rawW;
}
