#version 460 core

out vec4 colour;

uniform vec3 gizmoColor;

void main()
{
    colour = vec4(gizmoColor, 1.0f);
}
