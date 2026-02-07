#version 330

layout (location = 0) in vec3 pos;

//world space in orthogonal light
uniform mat4 model;
uniform mat4 directionalLightTransform;

void main()
{
	gl_Position = directionalLightTransform * model * vec4(pos, 1.0);
}
