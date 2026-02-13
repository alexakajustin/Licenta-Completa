#version 330

in vec2 TexCoord;

out vec4 colour;

uniform sampler2D theTexture;
uniform vec3 iconColor;

void main()
{
    vec4 texColor = texture(theTexture, TexCoord);
    if(texColor.a < 0.1)
        discard;
    colour = vec4(iconColor, texColor.a);
}
