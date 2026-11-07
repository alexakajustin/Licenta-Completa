#version 460

/**
 * @file water_foam_surface.vert
 * @brief Top-down pass of the water foam map: water surfaces (lakes, rivers) seen from above.
 * Writes the surface height, the river flow direction and its speed for every texel of the foam
 * window (the water simulation, water_sim.glsl, is driven by that current).
 */

layout (location = 0) in vec3 pos;
layout (location = 1) in vec2 tex;       // RiverNode: x = across the channel (0..1), y = -1 - opacity at a lake mouth
layout (location = 2) in vec3 norm;      // Real surface normal (the downstream slope)
layout (location = 4) in vec3 bitangent; // RiverNode stores the flow direction here

uniform mat4 model;
uniform mat4 foamViewProj; // Top-down orthographic window around the camera
uniform int isRiver;       // Lakes have no current
uniform float speedScale;  // The river material's flow speed (material_waveSpeed)

out vec3 WorldPos;
out vec2 Flow;
out float Speed;           // m/s mid-stream (as river.frag's current: faster down steep stretches)
out float Across;          // 0..1 across the channel (beyond: under the banks): the frag shapes the speed across

void main()
{
    vec4 world = model * vec4(pos, 1.0);
    WorldPos = world.xyz;
    vec2 b = (mat3(model) * bitangent).xz;
    Flow = (isRiver == 1 && dot(b, b) > 1e-8) ? normalize(b) : vec2(0.0);
    Across = tex.x;
    float steepness = clamp(1.0 - normalize(mat3(model) * norm).y, 0.0, 1.0);
    float mouth = tex.y <= -0.5 ? clamp(-tex.y - 1.0, 0.0, 1.0) : 1.0;
    Speed = isRiver == 1 ? 1.4 * speedScale * (1.0 + steepness * 8.0) * mouth : 0.0;
    gl_Position = foamViewProj * world;
}
