#version 430 core

#define IS_VERTEX_SHADER
// =====================================================================
// GPU-Driven Instanced Header
//
// Shared logic for "Instancifying" any vertex shader.
// Provides the SSBO structure and a helper to build the model matrix.
// =====================================================================

struct PackedInstance {
    vec4 posAndScale;     // xyz = position, w = scale
    vec4 rotAndFlags;     // xyz = euler degrees, w = fade factor
};

layout(std430, binding = 1) readonly buffer VisibleInstances {
    PackedInstance instances[];
};

// Stage-specific Instance data block
struct InstanceData {
    int iInstanceID;
    float vFadeFactor;
};

float _instanceFadeFactor = 0.0;

#if defined(IS_VERTEX_SHADER)
    layout(location = 10) flat out InstanceData vData;
    #define INST_ID gl_InstanceID
    #ifdef HAS_TESSELLATION
        float vFadeFactor; // Local shadow in VS if tessellation handles the 'out'
    #endif
#elif defined(IS_TCS)
    layout(location = 10) flat in InstanceData vDataIn[];
    layout(location = 10) flat out InstanceData vDataOut[];
    #define INST_ID vDataIn[gl_InvocationID].iInstanceID
#elif defined(IS_TES)
    layout(location = 10) flat in InstanceData vDataIn[];
    layout(location = 10) flat out InstanceData vData; // To Frag
    #define INST_ID vDataIn[0].iInstanceID
#elif defined(IS_FRAG)
    layout(location = 10) flat in InstanceData vData;
    #define INST_ID vData.iInstanceID
    #define vFadeFactor vData.vFadeFactor
#else
    // Fallback for single-stage shaders
    #define INST_ID gl_InstanceID
    out float vFadeFactor;
#endif

// Build rotation matrix from euler angles (degrees)
mat3 eulerToMat3(vec3 euler) {
    float cx = cos(radians(euler.x));
    float sx = sin(radians(euler.x));
    float cy = cos(radians(euler.y));
    float sy = sin(radians(euler.y));
    float cz = cos(radians(euler.z));
    float sz = sin(radians(euler.z));

    // Rotation order: X * Y * Z (matches GLM default)
    mat3 rx = mat3(1.0, 0.0, 0.0, 0.0, cx, -sx, 0.0, sx, cx);
    mat3 ry = mat3(cy, 0.0, sy, 0.0, 1.0, 0.0, -sy, 0.0, cy);
    mat3 rz = mat3(cz, -sz, 0.0, sz, cz, 0.0, 0.0, 0.0, 1.0);

    return rz * ry * rx;
}

// Build the model matrix for the current instance (gl_InstanceID)
// Stores fade factor in _instanceFadeFactor (copied to vFadeFactor in main())
mat4 ResolveInstancedModelMatrix() {
    PackedInstance inst = instances[INST_ID];
    
    vec3 instancePos = inst.posAndScale.xyz;
    float instanceScale = inst.posAndScale.w;
    vec3 instanceRot = inst.rotAndFlags.xyz;
    
    // Store fade factor for later copy to vFadeFactor
    _instanceFadeFactor = inst.rotAndFlags.w;
    
    mat3 rotMat = eulerToMat3(instanceRot);
    
    // Construct the matrix column by column (standard constructor)
    return mat4(
        vec4(rotMat[0] * instanceScale, 0.0),
        vec4(rotMat[1] * instanceScale, 0.0),
        vec4(rotMat[2] * instanceScale, 0.0),
        vec4(instancePos, 1.0)
    );
}


layout (location = 0) in vec3 pos;
layout (location = 1) in vec2 tex;
layout (location = 2) in vec3 norm;
layout (location = 3) in vec3 tangent;
layout (location = 4) in vec3 bitangent;
layout (location = 5) in mat4 _unused_inst;

out vec4 vertex_color;
out vec2 TexCoord;
out vec3 Normal;
out vec3 FragPos;

// fragment relative to light
// DirectionalLightSpacePos removed (calculated in frag shader for CSM)

// TBN matrix vectors for normal mapping
out vec3 TangentWorld;
out vec3 BitangentWorld;
out vec3 NormalWorld;

// Object-space position for height-based layer blending
out vec3 LocalPos;

out vec3 WorldXBasis;
out vec3 WorldZBasis;
out float vIsSelected;
// Redefined by header

uniform mat4 _unused_model;
uniform mat4 projection;
uniform mat4 view;
uniform mat4 directionalLightTransform[4];
uniform int useInstancing;
struct Material {
	 float specularIntensity;
	 float shininess;
	 float sssScale;
	 float sssDistortion;
	 vec4 baseColor;
	 vec2 tiling;
	 vec2 offset;
};
uniform int useDiffuseTexture;
uniform int useNormalMap;
uniform Material material;
uniform int textureLayerCount;  // Passed in so vertex shader can skip material tiling when layers are active
uniform vec4 clipPlane;


void main()
{
    mat4 model; model = ResolveInstancedModelMatrix();
    vData.vFadeFactor = _instanceFadeFactor;
    vData.iInstanceID = gl_InstanceID;

	mat4 modelMatrix = model;
	if (useInstancing == 1) {
	modelMatrix = instanceMatrix;
	}

	vIsSelected = 0.0;
	// vFadeFactor set by ResolveInstancedModelMatrix() // Non-instanced objects are always fully visible

	vec4 worldPosition = modelMatrix * vec4(pos, 1.0);
	gl_ClipDistance[0] = dot(worldPosition, clipPlane);
	gl_Position = projection * view * worldPosition;
	// DirectionalLightSpacePos calculation removed for CSM

	vertex_color = vec4(clamp(pos, 0.0f, 1.0f), 1.0f);
	
	// When texture layers are active, each layer applies its own tiling in the fragment shader.
	// Applying material.tiling here too would double-tile. Skip it when layers are in use.
	if (textureLayerCount > 0) {
		TexCoord = tex; // raw UVs — layers compute 'tex * layerData[i].tiling' themselves
	} else {
		TexCoord = tex * material.tiling + material.offset; // legacy single-texture path
	}
	
	Normal = mat3(transpose(inverse(modelMatrix))) * norm;
	
	FragPos = worldPosition.xyz; 
	LocalPos = pos; // Object-space position (before model transform)

	// Transform TBN vectors to world space for normal mapping
	mat3 normalMatrix = mat3(transpose(inverse(modelMatrix)));
	TangentWorld = normalize(normalMatrix * tangent);
	BitangentWorld = normalize(normalMatrix * bitangent);
	NormalWorld = normalize(normalMatrix * norm);
}