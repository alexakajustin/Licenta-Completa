#version 460 core

layout (location = 0) in vec3 pos;
layout (location = 5) in mat4 instanceMatrix;

uniform mat4 model;
uniform int useInstancing;

out vec4 FragPos;
out vec2 TexCoord;
out float vFadeFactor;

void main()
{
	mat4 modelMatrix = model;
	if (useInstancing == 1) {
		modelMatrix = instanceMatrix;
	}
	// just set the position in the world so that the geometry shader can pick it up
	vec4 worldPos = modelMatrix * vec4(pos, 1.0);
	FragPos = worldPos;
	gl_Position = worldPos;
	
	// Default values for non-instanced omni shadows (instanced version overrides these)
	TexCoord = vec2(0.0);
	vFadeFactor = 0.0;
}
