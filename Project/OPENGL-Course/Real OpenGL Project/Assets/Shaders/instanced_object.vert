#version 430 core

// =====================================================================
// GPU-Driven Instanced Vertex Shader
//
// Reads per-instance data from an SSBO (PackedInstance: 32 bytes)
// and builds the model matrix on-GPU. Includes procedural wind animation.
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
uniform mat4 directionalLightTransform;
uniform float time;
uniform int windEnabled;

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
out vec4 DirectionalLightSpacePos;
out vec3 TangentWorld;
out vec3 BitangentWorld;
out vec3 NormalWorld;
out vec3 LocalPos;

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

// Procedural wind displacement
vec3 calcWind(vec3 worldPos, float vertexHeight, float t) {
    if (windEnabled == 0) return vec3(0.0);
    
    // Only displace the tips (top of grass) — vertexHeight is normalized 0..1
    float tipFactor = vertexHeight * vertexHeight; // Quadratic falloff
    
    // Multi-frequency wind for natural motion
    float wind1 = sin(worldPos.x * 0.5 + t * 1.2) * 0.3;
    float wind2 = sin(worldPos.z * 0.7 + t * 0.8) * 0.2;
    float wind3 = sin((worldPos.x + worldPos.z) * 0.3 + t * 2.5) * 0.1; // Gust
    
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
    
    // Build model matrix from packed data
    mat3 rotMat = eulerToMat3(instanceRot);
    
    // Scale + rotate vertex
    vec3 scaledPos = pos * instanceScale;
    vec3 rotatedPos = rotMat * scaledPos;
    
    // Wind animation (before final translation)
    // Use pos.y as vertex height (assumes grass blade Y is up, 0=base, max=tip)
    // Normalize by getting approximate mesh height
    float vertexHeight = clamp(pos.y, 0.0, 1.0);
    vec3 wind = calcWind(instancePos, vertexHeight, time);
    
    vec3 worldPos = rotatedPos + instancePos + wind;
    
    // Build full 4x4 model matrix for normal/light calculations
    mat4 modelMatrix = mat4(1.0);
    modelMatrix[0] = vec4(rotMat[0] * instanceScale, 0.0);
    modelMatrix[1] = vec4(rotMat[1] * instanceScale, 0.0);
    modelMatrix[2] = vec4(rotMat[2] * instanceScale, 0.0);
    modelMatrix[3] = vec4(instancePos, 1.0);
    
    gl_Position = projection * view * vec4(worldPos, 1.0);
    DirectionalLightSpacePos = directionalLightTransform * vec4(worldPos, 1.0);
    
    vertex_color = vec4(clamp(pos, 0.0, 1.0), 1.0);
    TexCoord = tex * material.tiling + material.offset;
    
    // OPTIMIZATION: For uniform scale (w component), the normal matrix
    // is just the rotation matrix. Avoids expensive per-vertex inverse().
    // This saves ~30% vertex shader cost at 10M+ instances.
    mat3 normalMatrix = rotMat;  // Correct for uniform scale
    Normal = normalMatrix * norm;
    FragPos = worldPos;
    LocalPos = pos;
    
    TangentWorld = normalize(normalMatrix * tangent);
    BitangentWorld = normalize(normalMatrix * bitangent);
    NormalWorld = normalize(normalMatrix * norm);
}
