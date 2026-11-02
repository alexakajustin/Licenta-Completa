#version 460

/**
 * @file post_adapt.frag
 * @brief Auto exposure, step 2 (Rendering/PostProcess.h): the eye adapts. The adapted log
 * luminance (1x1) moves toward this frame's weighted log average, faster toward brighter
 * scenes than toward darker ones, like an eye.
 */

out float FragColor;

uniform sampler2D luminance;  ///< post_luminance.frag, with mips
uniform sampler2D previous;   ///< Last frame's adapted log luminance (1x1)
uniform float lastLevel;      ///< The 1x1 mip of `luminance`
uniform float deltaTime;
uniform float speed;          ///< 1 / seconds
uniform int reset;            ///< 1: jump straight to this frame's value

void main()
{
    vec2 m = textureLod(luminance, vec2(0.5), lastLevel).rg;
    float target = m.x / max(m.y, 1e-5);
    float prev = texelFetch(previous, ivec2(0), 0).r;
    if (reset == 1 || isnan(prev) || isinf(prev)) { FragColor = target; return; }
    float rate = target > prev ? speed * 2.0 : speed; // Into the light quickly, into the dark slowly
    FragColor = prev + (target - prev) * (1.0 - exp(-deltaTime * rate));
}
