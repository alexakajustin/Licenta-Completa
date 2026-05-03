#version 330

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

// TBN matrix vectors for normal mapping
out vec3 TangentWorld;
out vec3 BitangentWorld;
out vec3 NormalWorld;

out vec3 LocalPos;
out float vIsSelected;
out float vFadeFactor;
out vec4 clipSpaceCoords;
out float vObjectScale;

uniform mat4 model;
uniform mat4 projection;
uniform mat4 view;
uniform int useInstancing;
uniform float time;

struct Material {
	 float specularIntensity;
	 float shininess;
	 float sssScale;
	 float sssDistortion;
	 vec4 baseColor;
	 vec2 tiling;
	 vec2 offset;
};

uniform int textureLayerCount;
uniform Material material;

uniform vec4 clipPlane;

void main()
{
	mat4 modelMatrix = model;
	if (useInstancing == 1) {
	    modelMatrix = instanceMatrix;
	}

	vIsSelected = 0.0;
	vFadeFactor = 0.0; 

    float scaleX = length(modelMatrix[0].xyz);
    float scaleZ = length(modelMatrix[2].xyz);
    float objectScale = max(scaleX, scaleZ);
    
    vec4 worldPos = modelMatrix * vec4(pos, 1.0);

	
	// TBN in world space (Purely from mesh data, no waves!)
	vec3 tangentW = normalize((modelMatrix * vec4(tangent, 0.0)).xyz);
	vec3 bitangentW = normalize((modelMatrix * vec4(bitangent, 0.0)).xyz);
	vec3 normalW = normalize((modelMatrix * vec4(norm, 0.0)).xyz);

    clipSpaceCoords = projection * view * worldPos;

	gl_ClipDistance[0] = dot(worldPos, clipPlane);
	gl_Position = projection * view * worldPos;

	vertex_color = vec4(clamp(pos, 0.0f, 1.0f), 1.0f);
	
	if (textureLayerCount > 0) {
		TexCoord = tex;
	} else {
		TexCoord = tex * material.tiling + material.offset;
	}
	
    Normal = normalW;
	FragPos = worldPos.xyz; 
	LocalPos = pos; 
	vObjectScale = objectScale;

	TangentWorld = tangentW;
	BitangentWorld = bitangentW;
	NormalWorld = normalW;
}
