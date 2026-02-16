#version 330

layout (location = 0) in vec3 pos;
layout (location = 1) in vec2 tex;
layout (location = 2) in vec3 norm;
layout (location = 3) in vec3 tangent;
layout (location = 4) in vec3 bitangent;

out vec4 vertex_color;
out vec2 TexCoord;
out vec3 Normal;
out vec3 FragPos;

// fragment relative to light
out vec4 DirectionalLightSpacePos;

// TBN matrix vectors for normal mapping
out vec3 TangentWorld;
out vec3 BitangentWorld;
out vec3 NormalWorld;

uniform mat4 model;
uniform mat4 projection;
uniform mat4 view;
uniform mat4 directionalLightTransform;


void main()
{
	gl_Position = projection * view * model * vec4(pos, 1.0);
	DirectionalLightSpacePos = directionalLightTransform * model * vec4(pos, 1.0f);

	vertex_color = vec4(clamp(pos, 0.0f, 1.0f), 1.0f);
	
	TexCoord = tex;
	
	Normal = mat3(transpose(inverse(model))) * norm;
	
	FragPos = (model * vec4(pos, 1.0)).xyz; 

	// Transform TBN vectors to world space for normal mapping
	mat3 normalMatrix = mat3(transpose(inverse(model)));
	TangentWorld = normalize(normalMatrix * tangent);
	BitangentWorld = normalize(normalMatrix * bitangent);
	NormalWorld = normalize(normalMatrix * norm);
}