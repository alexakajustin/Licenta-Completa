#version 460 core

in vec4 FragPos;

uniform vec3 lightPos;
uniform float farPlane;
uniform float materialAlpha;

layout(location = 0) out float outAlpha;

void main()
{
	// distance between fragment and light
	float distance = length(FragPos.xyz - lightPos);
	distance = distance / farPlane; // 0 - 1
	gl_FragDepth = distance; // the depth attachment
	
	// Output alpha to the shadow color map
	outAlpha = materialAlpha;
}
