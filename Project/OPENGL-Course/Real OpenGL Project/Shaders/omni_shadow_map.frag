#version 330

in vec4 FragPos;

uniform vec3 lightPos;
uniform float farPlane;

void main()
{
	// distance between fragment and light
	float distance = length(FragPos.xyz - lightPos);
	distance = distance / farPlane; // 0 - 1
	gl_FragDepth = distance; // the depth attachment
}