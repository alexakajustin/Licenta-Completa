#version 330

layout (location = 0) in vec3 pos;
layout (location = 1) in vec2 tex;
layout (location = 2) in vec3 norm;
layout (location = 5) in mat4 instanceMatrix;

out vec2 TexCoord;
out vec3 Normal;
out vec3 FragPos;
out float vIsSelected;
out float vFadeFactor;

uniform mat4 model;
uniform mat4 projection;
uniform mat4 view;
uniform int useInstancing;

uniform float windSpeed = 1.0;
uniform float windStrength = 0.1;
uniform float time;
uniform vec4 clipPlane;

void main()
{
	mat4 modelMatrix = model;
	if (useInstancing == 1) {
		modelMatrix = instanceMatrix;
	}

	// Wind phase comes from the instance's world position: with only the local vertex
	// position every blade in the field swayed in perfect lockstep.
	vec2 phase = modelMatrix[3].xz * 0.35 + pos.xz;
	float bend = pos.y > 0.0 ? 1.0 : 0.0; // Roots stay planted
	vec3 displacedPos = pos;
	displacedPos.x += sin(time * windSpeed + phase.x) * windStrength * bend;
	displacedPos.z += cos(time * windSpeed * 0.83 + phase.y) * windStrength * bend;
	
	vIsSelected = 0.0;
	vFadeFactor = 0.0;
	vec4 worldPos = modelMatrix * vec4(displacedPos, 1.0);
	gl_ClipDistance[0] = dot(worldPos, clipPlane);
	gl_Position = projection * view * worldPos;
	TexCoord = tex;
	Normal = mat3(transpose(inverse(modelMatrix))) * norm;
	FragPos = worldPos.xyz;
}
