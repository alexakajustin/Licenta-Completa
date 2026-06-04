#version 400 core

layout (location = 0) in vec3 pos;
layout (location = 1) in vec2 tex;
layout (location = 2) in vec3 norm;
layout (location = 3) in vec3 tangent;
layout (location = 4) in vec3 bitangent;
layout (location = 5) in mat4 instanceMatrix;

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
out float vFadeFactor;

uniform mat4 model;
uniform mat4 projection;
uniform mat4 view;
uniform mat4 directionalLightTransform[4];
uniform int useInstancing;
struct Material {
	 float specularIntensity;
	 float shininess;
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
	mat4 modelMatrix = model;
	if (useInstancing == 1) {
	modelMatrix = instanceMatrix;
	}

	vIsSelected = 0.0;
	vFadeFactor = 0.0; // Non-instanced objects are always fully visible

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