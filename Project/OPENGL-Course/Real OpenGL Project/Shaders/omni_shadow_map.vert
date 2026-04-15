#version 460 core

layout (location = 0) in vec3 pos;
layout (location = 5) in mat4 instanceMatrix;

uniform mat4 model;
uniform int useInstancing;

void main()
{
	mat4 modelMatrix = model;
	if (useInstancing == 1) {
		modelMatrix = instanceMatrix;
	}
	// just set the position in the world so that the geometry shader can pick it up
	gl_Position = modelMatrix * vec4(pos, 1.0);
}
