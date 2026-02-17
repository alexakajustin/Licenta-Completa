#version 330

out vec4 colour;

uniform vec3 gizmoColor;

void main()
{
    colour = vec4(gizmoColor, 1.0f);
}
