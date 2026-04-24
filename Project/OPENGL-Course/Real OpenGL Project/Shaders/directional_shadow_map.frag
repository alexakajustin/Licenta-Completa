#version 460 core

uniform float materialAlpha;
uniform sampler2D theTexture;
uniform int useDiffuseTexture;
in vec2 TexCoord;

layout(location = 0) out float outAlpha;

void main() { 
    if (useDiffuseTexture == 1) {
        if (texture(theTexture, TexCoord).a < 0.5) discard;
    }
	// Output alpha to the shadow color map
	outAlpha = materialAlpha;
}
