#version 460 core

layout (location = 0) in vec3 pos;
layout (location = 1) in vec2 tex;

struct PackedInstance {
    vec4 posAndScale;
    vec4 rotAndFlags;
};

layout(std430, binding = 1) readonly buffer VisibleInstances {
    PackedInstance instances[];
};

uniform mat4 directionalLightTransform;

out vec2 TexCoord;

mat3 eulerToMat3(vec3 euler) {
    float cx = cos(radians(euler.x));
    float sx = sin(radians(euler.x));
    float cy = cos(radians(euler.y));
    float sy = sin(radians(euler.y));
    float cz = cos(radians(euler.z));
    float sz = sin(radians(euler.z));
    mat3 rx = mat3(1.0, 0.0, 0.0, 0.0, cx, -sx, 0.0, sx, cx);
    mat3 ry = mat3(cy, 0.0, sy, 0.0, 1.0, 0.0, -sy, 0.0, cy);
    mat3 rz = mat3(cz, -sz, 0.0, sz, cz, 0.0, 0.0, 0.0, 1.0);
    return rz * ry * rx;
}

void main()
{
    PackedInstance inst = instances[gl_InstanceID];
    vec3 instancePos = inst.posAndScale.xyz;
    float instanceScale = inst.posAndScale.w;
    vec3 instanceRot = inst.rotAndFlags.xyz;
    
    mat3 rotMat = eulerToMat3(instanceRot);
    
    mat4 modelMatrix = mat4(
        vec4(rotMat[0] * instanceScale, 0.0),
        vec4(rotMat[1] * instanceScale, 0.0),
        vec4(rotMat[2] * instanceScale, 0.0),
        vec4(instancePos, 1.0)
    );

    TexCoord = tex;
    gl_Position = directionalLightTransform * modelMatrix * vec4(pos, 1.0);
}
