#version 330

layout (location = 0) in vec3 pos;

uniform mat4 model;

void main()
{
	// just set the position in the world so that the geometry shader can pick it up
	gl_Position = model * vec4(pos, 1.0);
}