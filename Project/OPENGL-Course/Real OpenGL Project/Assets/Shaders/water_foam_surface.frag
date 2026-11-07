#version 460

/**
 * @file water_foam_surface.frag
 * @brief Water surface texel of the foam map: r = surface height, gb = flow direction (xz),
 * a = 1 + current speed (m/s; water present: a >= 1). The depth test keeps the highest surface
 * where two overlap. (Also draws the ground's height for the water simulation: r only.)
 */

in vec3 WorldPos;
in vec2 Flow;
in float Speed;
in float Across;

out vec4 FragColor;

void main()
{
    float profile = 1.0 - pow(min(abs(Across * 2.0 - 1.0), 1.0), 3.0); // Fastest mid-stream (river.frag); past 0..1 under a bank
    FragColor = vec4(WorldPos.y, Flow, 1.0 + Speed * mix(0.2, 1.0, profile));
}
