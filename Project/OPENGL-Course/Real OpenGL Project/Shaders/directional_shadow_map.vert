#version 460 core

layout (location = 0) in vec3 pos;
layout (location = 5) in mat4 instanceMatrix;

//world space in orthogonal light
uniform mat4 model;
uniform mat4 directionalLightTransform;
uniform int useInstancing;

void main()
{
	mat4 modelMatrix = model;
	if (useInstancing == 1) {
		modelMatrix = instanceMatrix;
	}
	gl_Position = directionalLightTransform * modelMatrix * vec4(pos, 1.0);
}
