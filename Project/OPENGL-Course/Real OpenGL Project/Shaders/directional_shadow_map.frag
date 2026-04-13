#version 330

uniform float materialAlpha;

void main()
{
	// Transparent objects don't cast shadows
	if (materialAlpha < 0.5)
		discard;
}