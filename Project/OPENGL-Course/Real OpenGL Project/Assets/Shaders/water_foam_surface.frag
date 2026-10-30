#version 460

/**
 * @file water_foam_surface.frag
 * @brief Water surface texel of the foam map: r = surface height, gb = flow direction (xz),
 * a = 1 (water present). The depth test keeps the highest surface where two overlap.
 */

in vec3 WorldPos;
in vec2 Flow;

out vec4 FragColor;

void main()
{
    FragColor = vec4(WorldPos.y, Flow, 1.0);
}
