#version 330

in vec2 TexCoord;
in vec3 Normal;
in vec3 FragPos;

out vec4 colour;

struct Material {
	vec3 baseColor;
};
uniform Material material;
uniform sampler2D theTexture;

void main()
{
	colour = texture(theTexture, TexCoord) * vec4(material.baseColor, 1.0);
}
