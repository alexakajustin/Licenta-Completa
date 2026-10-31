// =====================================================================
// impostor_common.glsl — reading an octahedral impostor atlas (Impostor.h)
//
// Included (pasted by Impostor.cpp) into impostor.frag and impostor_shadow.frag after #version.
// The atlas is impostorFrames x impostorFrames baked views; impostor.vert picked the three views
// around the view direction, their barycentric weights and where the fragment lands in each.
// =====================================================================

in vec4 FrameUV01;
in vec2 FrameUV2;
in vec3 CardPos;
flat in vec4 FrameCell01;
flat in vec2 FrameCell2;
flat in vec3 FrameWeights;
flat in mat3 InstanceRotation;
flat in vec3 InstancePos;
flat in float InstanceScale;
flat in vec3 ToViewer;
flat in float vFadeFactor;
flat in float vIsSelected;

uniform sampler2D impostorAlbedo; ///< rgb albedo, a coverage (mips: average coverage)
uniform sampler2D impostorData;   ///< rgb mesh-space normal * 0.5 + 0.5, a depth toward the viewer
uniform sampler2D impostorMaterial; ///< r: 1 leaves, 0 bark
uniform int impostorFrames;
uniform float impostorRadius;
uniform vec3 impostorCenter;

struct ImpostorTexel {
    float coverage;
    vec3 albedo;
    vec3 normal;   ///< World space
    float foliage; ///< 1 leaves, 0 bark
    vec3 worldPos; ///< On the tree's surface (the card moved by the baked depth)
};

/// Dithered fades on the 4x4 Bayer pattern of the mesh shaders (compute_cull.glsl):
/// vFadeFactor > 0 fades out, < 0 fades in with the complementary mask
bool DitherDiscard(float fade) {
    if (abs(fade) <= 0.001) return false;
    int x = int(mod(gl_FragCoord.x, 4.0));
    int y = int(mod(gl_FragCoord.y, 4.0));
    float bayerMatrix[16] = float[16](
         0.0/16.0,  8.0/16.0,  2.0/16.0, 10.0/16.0,
        12.0/16.0,  4.0/16.0, 14.0/16.0,  6.0/16.0,
         3.0/16.0, 11.0/16.0,  1.0/16.0,  9.0/16.0,
        15.0/16.0,  7.0/16.0, 13.0/16.0,  5.0/16.0
    );
    float threshold = bayerMatrix[y * 4 + x];
    return fade > 0.0 ? fade > threshold : threshold >= -fade;
}

/// Coverage is dithered, not alpha-tested: a texel 60% covered draws 60% of its pixels. A far crown
/// keeps its gaps (an alpha test would close the averaged mips into a solid blob) and the three
/// views blend smoothly. Interleaved gradient noise (Jimenez 2014), uncorrelated with the Bayer
/// pattern of the LOD cross-fades.
bool CoverageDiscard(float coverage) {
    float noise = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
    return coverage <= noise;
}

/// Atlas positions of the three views (a texel inside each view: filtering never reaches into the
/// neighbouring one) and their albedo + coverage. Views with no weight are not read: FrameWeights is
/// the same over the whole card, so the branches are uniform and the derivatives stay valid.
struct ImpostorAlbedo {
    vec2 uv0, uv1, uv2;
    vec4 a0, a1, a2;
    float coverage;
};

ImpostorAlbedo SampleImpostorAlbedo()
{
    vec2 cellSize = vec2(1.0 / float(impostorFrames));
    ImpostorAlbedo s;
    s.uv0 = (FrameCell01.xy + clamp(FrameUV01.xy, 0.0, 1.0)) * cellSize;
    s.uv1 = (FrameCell01.zw + clamp(FrameUV01.zw, 0.0, 1.0)) * cellSize;
    s.uv2 = (FrameCell2 + clamp(FrameUV2, 0.0, 1.0)) * cellSize;
    s.a0 = FrameWeights.x > 0.0 ? texture(impostorAlbedo, s.uv0) : vec4(0.0);
    s.a1 = FrameWeights.y > 0.0 ? texture(impostorAlbedo, s.uv1) : vec4(0.0);
    s.a2 = FrameWeights.z > 0.0 ? texture(impostorAlbedo, s.uv2) : vec4(0.0);
    s.coverage = dot(FrameWeights, vec3(s.a0.a, s.a1.a, s.a2.a));
    return s;
}

/// Colour, normal and surface position of a covered fragment, weighted by coverage too: a view that
/// misses this texel has nothing to say about it. withMaterial = false skips leaf/bark (shadows)
ImpostorTexel SampleImpostorSurface(ImpostorAlbedo s, bool withMaterial)
{
    vec3 w = FrameWeights * vec3(s.a0.a, s.a1.a, s.a2.a);
    w /= max(w.x + w.y + w.z, 1e-4);
    vec4 data = vec4(0.0);
    if (w.x > 0.0) data += texture(impostorData, s.uv0) * w.x;
    if (w.y > 0.0) data += texture(impostorData, s.uv1) * w.y;
    if (w.z > 0.0) data += texture(impostorData, s.uv2) * w.z;
    float foliage = 0.0;
    if (withMaterial) {
        if (w.x > 0.0) foliage += texture(impostorMaterial, s.uv0).r * w.x;
        if (w.y > 0.0) foliage += texture(impostorMaterial, s.uv1).r * w.y;
        if (w.z > 0.0) foliage += texture(impostorMaterial, s.uv2).r * w.z;
    }

    ImpostorTexel t;
    t.coverage = s.coverage;
    t.albedo = s.a0.rgb * w.x + s.a1.rgb * w.y + s.a2.rgb * w.z;
    t.foliage = foliage;
    vec3 n = data.xyz * 2.0 - 1.0;
    t.normal = InstanceRotation * (dot(n, n) > 1e-6 ? normalize(n) : vec3(0.0, 1.0, 0.0));
    t.worldPos = CardPos + ToViewer * ((data.w - 0.5) * 2.0 * impostorRadius * InstanceScale);
    return t;
}
