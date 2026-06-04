#version 460 core

layout (location = 0) in vec3 pos;
layout (location = 5) in mat4 instanceMatrix;

uniform mat4 model;
uniform mat4 lightMatrix;
uniform int useInstancing;

out vec4 FragPosOut;
out vec2 TexCoordOut;
out float vFadeFactorOut;

void main()
{
	mat4 modelMatrix = model;
	if (useInstancing == 1) {
		modelMatrix = instanceMatrix;
	}
	// Project using the face's lightMatrix
	vec4 worldPos = modelMatrix * vec4(pos, 1.0);
	FragPosOut = worldPos;
	gl_Position = lightMatrix * worldPos;
	
	// Default values for non-instanced omni shadows (instanced version overrides these)
	TexCoordOut = vec2(0.0);
	vFadeFactorOut = 0.0;
}
