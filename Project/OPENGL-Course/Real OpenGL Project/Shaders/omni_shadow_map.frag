#version 460 core

in vec4 FragPosGS;
in vec2 TexCoordGS;
in float vFadeFactorGS;

uniform vec3 lightPos;
uniform float farPlane;
uniform float materialAlpha;
uniform sampler2D theTexture;
uniform int useDiffuseTexture;

layout(location = 0) out float outAlpha;

void main()
{
	// Alpha Testing (for foliage/leaves)
	if (useDiffuseTexture == 1) {
		float alpha = texture(theTexture, TexCoordGS).a;
		if (alpha < 0.5) discard;
	}

	// Distance dithered fade (for instanced grass)
	if (vFadeFactorGS > 0.001) {
		int x = int(mod(gl_FragCoord.x, 4.0));
		int y = int(mod(gl_FragCoord.y, 4.0));
		float bayerMatrix[16] = float[16](
			 0.0/16.0,  8.0/16.0,  2.0/16.0, 10.0/16.0,
			12.0/16.0,  4.0/16.0, 14.0/16.0,  6.0/16.0,
			 3.0/16.0, 11.0/16.0,  1.0/16.0,  9.0/16.0,
			15.0/16.0,  7.0/16.0, 13.0/16.0,  5.0/16.0
		);
		float threshold = bayerMatrix[y * 4 + x];
		if (vFadeFactorGS > threshold) discard;
	}

	// distance between fragment and light
	float distance = length(FragPosGS.xyz - lightPos);
	distance = distance / farPlane; // 0 - 1
	gl_FragDepth = distance; // the depth attachment
	
	// Output alpha to the shadow color map
	outAlpha = materialAlpha;
}
