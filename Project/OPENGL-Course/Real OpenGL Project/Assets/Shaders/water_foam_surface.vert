#version 460

/**
 * @file water_foam_surface.vert
 * @brief Top-down pass of the water foam map: water surfaces (lakes, rivers) seen from above.
 * Writes the surface height and the river flow direction for every texel of the foam window.
 */

layout (location = 0) in vec3 pos;
layout (location = 4) in vec3 bitangent; // RiverNode stores the flow direction here

uniform mat4 model;
uniform mat4 foamViewProj; // Top-down orthographic window around the camera
uniform int isRiver;       // Lakes have no current

out vec3 WorldPos;
out vec2 Flow;

void main()
{
    vec4 world = model * vec4(pos, 1.0);
    WorldPos = world.xyz;
    vec2 b = (mat3(model) * bitangent).xz;
    Flow = (isRiver == 1 && dot(b, b) > 1e-8) ? normalize(b) : vec2(0.0);
    gl_Position = foamViewProj * world;
}
