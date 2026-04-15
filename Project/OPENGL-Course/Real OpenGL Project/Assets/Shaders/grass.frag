#version 330

in vec2 TexCoord;
in vec3 Normal;
in vec3 FragPos;

out vec4 colour;

struct Material {
    float specularIntensity;
    float shininess;
    float sssScale;
    float sssDistortion;
    vec4 baseColor;
    vec2 tiling;
    vec2 offset;
};
uniform Material material;
uniform sampler2D theTexture;

void main()
{
	colour = texture(theTexture, TexCoord) * material.baseColor;
}
