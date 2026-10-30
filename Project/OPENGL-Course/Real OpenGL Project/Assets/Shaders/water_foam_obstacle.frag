#version 460

/**
 * @file water_foam_obstacle.frag
 * @brief Marks the foam-map texels where geometry crosses the water surface.
 *
 * Seen from above with no depth test, every fragment of an object is rasterised; only those
 * within a band from just below to a little above the local water surface count, so rocks,
 * logs and reeds standing in the water mark their waterline footprint, while tree crowns
 * hanging over a river or anything on dry land (no water texel) do not.
 */

in vec3 WorldPos;
in vec2 TexCoord;

uniform sampler2D waterSurface; // r = surface height, gb = flow, a = 1 where there is water
uniform vec2 foamOrigin;        // World xz of the window's corner
uniform float foamSize;         // World size of the window
uniform sampler2D theTexture;
uniform int useDiffuseTexture;

out vec4 FragColor;

void main()
{
    vec4 water = texture(waterSurface, (WorldPos.xz - foamOrigin) / foamSize);
    if (water.a < 0.5) discard;
    if (WorldPos.y < water.r - 0.3 || WorldPos.y > water.r + 0.6) discard;
    // Alpha-tested cards (grass, leaves): only the opaque parts are obstacles
    if (useDiffuseTexture == 1 && texture(theTexture, TexCoord).a < 0.5) discard;
    FragColor = vec4(1.0);
}
