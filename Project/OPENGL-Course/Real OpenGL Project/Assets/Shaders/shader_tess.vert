#version 400 core

// Tessellation pass-through vertex shader
// Transforms to world space and passes all attributes to the TCS
// Output names are suffixed with _VS so the TCS can receive them as arrays

layout (location = 0) in vec3 pos;
layout (location = 1) in vec2 tex;
layout (location = 2) in vec3 norm;
layout (location = 3) in vec3 tangent;
layout (location = 4) in vec3 bitangent;

// Outputs to TCS (must match TCS inputs)
out vec2 TexCoord_VS;
out vec3 Normal_VS;
out vec3 FragPos_VS;
out vec3 TangentWorld_VS;
out vec3 BitangentWorld_VS;
out vec3 NormalWorld_VS;
out vec3 LocalPos_VS;

uniform mat4 model;
uniform vec4 clipPlane;

struct Material {
	float specularIntensity;
	float shininess;
	vec4 baseColor;
	vec2 tiling;
	vec2 offset;
	float reflectivity;
};
uniform Material material;
uniform int textureLayerCount;

void main()
{
	vec4 worldPosition = model * vec4(pos, 1.0);

	// Clip plane for reflection pass
	gl_ClipDistance[0] = dot(worldPosition, clipPlane);

	// World-space fragment position
	FragPos_VS = worldPosition.xyz;
	
	// Object-space position for height-based layer blending
	LocalPos_VS = pos;

	// Texture coordinates (skip material tiling when layers are active — layers do their own)
	if (textureLayerCount > 0) {
		TexCoord_VS = tex;
	} else {
		TexCoord_VS = tex * material.tiling + material.offset;
	}

	// Normal in world space
	mat3 normalMatrix = mat3(transpose(inverse(model)));
	Normal_VS = normalMatrix * norm;

	// TBN vectors in world space for normal mapping
	TangentWorld_VS = normalize(normalMatrix * tangent);
	BitangentWorld_VS = normalize(normalMatrix * bitangent);
	NormalWorld_VS = normalize(normalMatrix * norm);

	// NOTE: gl_Position is NOT set here — the TES will set it after displacement
}
