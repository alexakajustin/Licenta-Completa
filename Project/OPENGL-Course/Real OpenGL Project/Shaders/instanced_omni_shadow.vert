#version 460 core

// =====================================================================
// Instanced Omni Shadow Vertex Shader
//
// Reads per-instance packed transforms from an SSBO and renders
// into the omni (cube map) shadow map. Outputs world-space positions
// for the geometry shader to project onto all 6 cube faces.
// Includes wind animation so shadows match the animated grass blades.
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

uniform vec3 omniLightPos;
uniform float farPlane;
uniform float time;
uniform int windEnabled;

struct Material {
    float specularIntensity;
    float shininess;
    float sssScale;
    float sssDistortion;
    vec4 baseColor;
    vec2 tiling;
    vec2 offset;
};
uniform Material material;

out vec4 FragPos;
out vec2 TexCoord;
out float vFadeFactor;

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

// Procedural wind displacement (must match instanced_shadow.vert exactly!)
vec3 calcWind(vec3 worldPos, float vertexHeight, float t) {
    if (windEnabled == 0) return vec3(0.0);
    
    float tipFactor = vertexHeight * vertexHeight;
    
    float wind1 = sin(worldPos.x * 0.5 + t * 1.2) * 0.3;
    float wind2 = sin(worldPos.z * 0.7 + t * 0.8) * 0.2;
    float wind3 = sin((worldPos.x + worldPos.z) * 0.3 + t * 2.5) * 0.1;
    
    float windX = (wind1 + wind3) * tipFactor;
    float windZ = (wind2 + wind3 * 0.5) * tipFactor;
    
    return vec3(windX, 0.0, windZ);
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
    
    // Wind animation (synchronized with camera pass)
    float vertexHeight = clamp(pos.y, 0.0, 1.0);
    vec3 wind = calcWind(instancePos, vertexHeight, time);
    
    vec3 worldPos = rotatedPos + instancePos + wind;
    
    // Output world position for geometry shader (matches omni_shadow_map.vert convention)
    FragPos = vec4(worldPos, 1.0);
    gl_Position = vec4(worldPos, 1.0);
    
    TexCoord = tex * material.tiling + material.offset;
    vFadeFactor = inst.rotAndFlags.w;
}
