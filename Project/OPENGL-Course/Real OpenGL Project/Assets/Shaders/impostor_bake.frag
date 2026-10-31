#version 430 core

/**
 * @file impostor_bake.frag
 * @brief Writes one texel of an impostor view: albedo + coverage, and the mesh-space normal + depth.
 *
 * Covered texels write alpha 1 to both targets and empty ones stay 0, so the supersampled views
 * filtered down are premultiplied by coverage (Impostor.cpp divides it out).
 */

in vec2 TexCoord;
in vec3 MeshNormal;
in vec3 MeshPos;

uniform sampler2D theTexture;
uniform int useTexture;
uniform vec3 baseColor;
uniform vec3 frameDir;
uniform vec3 frameCenter;
uniform float frameRadius;
uniform float foliage;      ///< 1 for leaves (cutout cards), 0 for bark

layout (location = 0) out vec4 outAlbedo; ///< rgb albedo, a coverage
layout (location = 1) out vec4 outData;   ///< rgb normal * 0.5 + 0.5, a depth toward the viewer (0.5 = centre)
layout (location = 2) out float outFoliage; ///< Leaves vs bark

void main()
{
    vec4 texColor = useTexture == 1 ? texture(theTexture, TexCoord) : vec4(1.0);
    if (useTexture == 1 && texColor.a < 0.5) discard; // Same cutout as shader.frag

    // Double-sided cards are lit with the normal facing the viewer (shader.frag does the same)
    vec3 n = normalize(MeshNormal);
    if (!gl_FrontFacing) n = -n;

    float depth = clamp(dot(MeshPos - frameCenter, frameDir) / (2.0 * frameRadius) + 0.5, 0.0, 1.0);
    outAlbedo = vec4(baseColor * texColor.rgb, 1.0);
    outData = vec4(n * 0.5 + 0.5, depth);
    outFoliage = foliage;
}
