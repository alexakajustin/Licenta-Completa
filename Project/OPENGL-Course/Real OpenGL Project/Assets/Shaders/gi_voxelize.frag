#version 430 core

/**
 * @file gi_voxelize.frag
 * @brief Writes a surface into its voxel: opacity, albedo and normal packed into one 32-bit value
 * (8 bits opacity, RGB 5:6:5, normal as 4:4 octahedral). Several triangles can land in the same
 * voxel: an atomic max keeps the most opaque one, whatever the drawing order.
 *
 * Leaf and grass cards (alpha-tested textures) are only partly opaque: their alpha at a coarse
 * mip is the coverage of the area around the fragment, so a crown lets some light through.
 */

in GS_OUT {
    vec3 worldPos;
    vec3 normal;
    vec2 uv;
} fsIn;

layout(r32ui, binding = 0) uniform coherent uimage3D voxels;

uniform vec3 levelMin;
uniform float voxelSize;
uniform int resolution;
uniform int levelIndex;
uniform vec3 albedoColor;        ///< Material colour (x the average of a layered surface's textures)
uniform sampler2D theTexture;    ///< Diffuse texture of the draw (models, instanced cutouts)
uniform int useDiffuseTexture;
uniform sampler2D voxelTexture;  ///< Diffuse texture bound for this draw only (instanced groups)
uniform int useVoxelTexture;
uniform float albedoLod;         ///< Coarse mip: the local average colour and coverage

vec2 OctEncode(vec3 n)
{
    n /= (abs(n.x) + abs(n.y) + abs(n.z));
    vec2 s = vec2(n.x >= 0.0 ? 1.0 : -1.0, n.y >= 0.0 ? 1.0 : -1.0);
    return n.z >= 0.0 ? n.xy : (1.0 - abs(n.yx)) * s;
}

void main()
{
    ivec3 c = ivec3(floor((fsIn.worldPos - levelMin) / voxelSize));
    if (any(lessThan(c, ivec3(0))) || any(greaterThanEqual(c, ivec3(resolution)))) discard;

    vec4 texel = vec4(1.0);
    float opacity = 1.0;
    if (useDiffuseTexture == 1) {
        texel = textureLod(theTexture, fsIn.uv, albedoLod);
        // A cutout card (leaves): its coverage, and a crown never blocks all the light
        if (texel.a < 0.98) opacity = clamp(texel.a, 0.0, 1.0) * 0.7;
    } else if (useVoxelTexture == 1) {
        texel = textureLod(voxelTexture, fsIn.uv, albedoLod);
    }
    if (opacity < 0.04) discard;

    vec3 albedo = clamp(albedoColor * texel.rgb, 0.0, 1.0);
    vec3 n = normalize(fsIn.normal);
    if (any(isnan(n))) n = vec3(0.0, 1.0, 0.0);
    uvec2 q = uvec2(clamp(OctEncode(n) * 0.5 + 0.5, 0.0, 1.0) * 15.0 + 0.5);
    uvec3 rgb = uvec3(albedo * vec3(31.0, 63.0, 31.0) + 0.5);
    uint packed = (uint(opacity * 255.0 + 0.5) << 24) | (rgb.r << 19) | (rgb.g << 13) | (rgb.b << 8) | (q.x << 4) | q.y;
    imageAtomicMax(voxels, c + ivec3(0, 0, levelIndex * resolution), packed);
}
