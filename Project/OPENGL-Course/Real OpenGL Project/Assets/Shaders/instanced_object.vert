#version 430 core

// =====================================================================
// GPU-Driven Instanced Vertex Shader
//
// Reads per-instance data from an SSBO (PackedInstance: 32 bytes)
// and builds the model matrix on-GPU. Clean generic version.
// =====================================================================

layout (location = 0) in vec3 pos;
layout (location = 1) in vec2 tex;
layout (location = 2) in vec3 norm;
layout (location = 3) in vec3 tangent;
layout (location = 4) in vec3 bitangent;

struct PackedInstance {
    vec4 posAndScale;     // xyz = position, w = scale
    vec4 rotAndFlags;     // xyz = euler degrees, w = flags
};

layout(std430, binding = 1) readonly buffer VisibleInstances {
    PackedInstance instances[];
};

uniform mat4 projection;
uniform mat4 view;
uniform mat4 directionalLightTransform[4];
uniform vec4 clipPlane;

// Material struct (same as main shader for compatibility)
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

out vec4 vertex_color;
out vec2 TexCoord;
out vec3 Normal;
out vec3 FragPos;
// DirectionalLightSpacePos removed (calculated in frag shader for CSM)
out vec3 TangentWorld;
out vec3 BitangentWorld;
out vec3 NormalWorld;
out vec3 LocalPos;
out float vIsSelected;
out float vFadeFactor;

// Build rotation matrix from euler angles (degrees)
mat3 eulerToMat3(vec3 euler) {
    float cx = cos(radians(euler.x));
    float sx = sin(radians(euler.x));
    float cy = cos(radians(euler.y));
    float sy = sin(radians(euler.y));
    float cz = cos(radians(euler.z));
    float sz = sin(radians(euler.z));

    // Rotation order: X * Y * Z (matches GLM default)
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
    float rawW = inst.rotAndFlags.w;
    if (rawW > 5.0) {
        vIsSelected = 1.0;
        vFadeFactor = rawW - 10.0;
    } else {
        vIsSelected = 0.0;
        vFadeFactor = rawW;
    }
    
    // Build model matrix from packed data
    mat3 rotMat = eulerToMat3(instanceRot);
    
    // Scale + rotate vertex
    vec3 scaledPos = pos * instanceScale;
    vec3 rotatedPos = rotMat * scaledPos;
    
    // Generic placement
    vec3 worldPos = rotatedPos + instancePos;
    
    // Build full 4x4 model matrix for normal/light calculations
    mat4 modelMatrix = mat4(1.0);
    modelMatrix[0] = vec4(rotMat[0] * instanceScale, 0.0);
    modelMatrix[1] = vec4(rotMat[1] * instanceScale, 0.0);
    modelMatrix[2] = vec4(rotMat[2] * instanceScale, 0.0);
    modelMatrix[3] = vec4(instancePos, 1.0);
    
    gl_ClipDistance[0] = dot(vec4(worldPos, 1.0), clipPlane);
    gl_Position = projection * view * vec4(worldPos, 1.0);
    // DirectionalLightSpacePos calculation removed for CSM
    
    vertex_color = vec4(clamp(pos, 0.0, 1.0), 1.0);

	// Safe UVs
	vec2 tiling = (material.tiling.x == 0.0 && material.tiling.y == 0.0) ? vec2(1.0) : material.tiling;
    TexCoord = tex * tiling + material.offset;
    
    mat3 normalMatrix = rotMat;  // Correct for uniform scale
    Normal = normalMatrix * norm;
    FragPos = worldPos;
    LocalPos = pos;
    
    TangentWorld = normalize(normalMatrix * tangent);
    BitangentWorld = normalize(normalMatrix * bitangent);
    NormalWorld = normalize(normalMatrix * norm);
}
