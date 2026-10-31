#version 430 core

/**
 * @file impostor_shadow.frag
 * @brief Impostor card in a directional shadow cascade: the tree's silhouette seen from the light.
 *
 * The card faces the light (impostor.vert, orthographic) and its depth is moved back onto the
 * tree's surface with the baked depth, so a crown shadows its own far side and the ground under
 * it, like the meshes it replaces. impostor_common.glsl is pasted after the #version line.
 */

uniform mat4 impostorViewProj;
layout(location = 0) out float outAlpha; ///< Shadow colour map: opaque caster

void main()
{
    if (vFadeFactor > 0.001 && DitherDiscard(vFadeFactor)) discard;

    ImpostorAlbedo albedo = SampleImpostorAlbedo();
    if (CoverageDiscard(albedo.coverage)) discard;
    ImpostorTexel t = SampleImpostorSurface(albedo, false);

    vec4 clip = impostorViewProj * vec4(t.worldPos, 1.0);
    gl_FragDepth = clamp(clip.z / clip.w * 0.5 + 0.5, 0.0, 1.0);
    outAlpha = 1.0;
}
