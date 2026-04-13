#version 330

uniform float materialAlpha;
layout(location = 0) out float outAlpha;

void main() { 
	// Output alpha to the shadow color map
	outAlpha = materialAlpha;
}