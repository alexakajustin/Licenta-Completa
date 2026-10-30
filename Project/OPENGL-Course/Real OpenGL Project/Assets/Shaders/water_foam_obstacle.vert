#version 460

/**
 * @file water_foam_obstacle.vert
 * @brief Top-down pass of the water foam map for regular (non-instanced) objects.
 * The fragment shader keeps only the parts that cross the water surface.
 */

layout (location = 0) in vec3 pos;
layout (location = 1) in vec2 tex;

uniform mat4 model;
uniform mat4 directionalLightTransform; // Top-down foam window (same name as the instanced variant)

out vec3 WorldPos;
out vec2 TexCoord;

void main()
{
    vec4 world = model * vec4(pos, 1.0);
    WorldPos = world.xyz;
    TexCoord = tex;
    gl_Position = directionalLightTransform * world;
}
