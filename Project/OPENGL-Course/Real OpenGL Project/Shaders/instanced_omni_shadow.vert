#version 460 core

// =====================================================================
// Instanced Omni Shadow Vertex Shader
//
// Reads per-instance packed transforms from an SSBO and renders
// into the omni (cube map) shadow map.
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

out vec4 FragPosOut;
out vec2 TexCoordOut;
out float vFadeFactorOut;

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
    
    // Build model transform
    mat3 rotMat = eulerToMat3(instanceRot);
    vec3 scaledPos = pos * instanceScale;
    vec3 rotatedPos = rotMat * scaledPos;
    
    vec3 worldPos = rotatedPos + instancePos;
    
    // Output projected position for this face
    FragPosOut = vec4(worldPos, 1.0);
    gl_Position = lightMatrix * vec4(worldPos, 1.0);
    
    TexCoordOut = tex * material.tiling + material.offset;
    float rawW = inst.rotAndFlags.w;
    vFadeFactorOut = (rawW > 5.0) ? (rawW - 10.0) : rawW;
}
