#version 460 core

uniform vec3 pickingColor;
uniform bool isBillboard;

out vec4 colour;

void main()
{
    colour = vec4(pickingColor, 1.0);
}
