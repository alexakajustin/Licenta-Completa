#version 430 core
layout(local_size_x = 8, local_size_y = 8) in;

/**
 * @file impostor_resolve.glsl
 * @brief One mip level of an impostor atlas from its premultiplied pyramid (Impostor.cpp).
 *
 * The baked views are premultiplied by coverage (empty texels are 0), so glGenerateMipmap gives
 * alpha-weighted averages of colour, normal and depth at every level; this divides the coverage
 * out. Texels with no coverage take the value of the coarser level, resolved just before
 * (push-pull), so bilinear filtering never pulls black into the edges of the leaves. Alpha stays
 * the plain average coverage: the cards dither against it.
 */

layout(binding = 0) uniform sampler2D premultAlbedo; ///< Premultiplied pyramid (RGBA16F)
layout(binding = 1) uniform sampler2D premultData;
layout(binding = 2) uniform sampler2D coarserAlbedo; ///< Final atlas, level + 1 already resolved
layout(binding = 3) uniform sampler2D coarserData;
layout(binding = 4) uniform sampler2D premultMaterial; ///< Leaves vs bark (R16F)
layout(binding = 5) uniform sampler2D coarserMaterial;
layout(rgba8, binding = 0) uniform writeonly image2D outAlbedo; ///< Final atlas, this level
layout(rgba8, binding = 1) uniform writeonly image2D outData;
layout(r8, binding = 2) uniform writeonly image2D outMaterial;

uniform int level;
uniform int hasCoarser;
uniform ivec2 levelSize;

void main()
{
    ivec2 p = ivec2(gl_GlobalInvocationID.xy);
    if (any(greaterThanEqual(p, levelSize))) return;

    vec4 albedo = texelFetch(premultAlbedo, p, level);
    vec4 data = texelFetch(premultData, p, level);
    float foliage = texelFetch(premultMaterial, p, level).r;
    float coverage = albedo.a;

    vec4 outA, outD;
    float outM;
    if (coverage > 1.0 / 1024.0) {
        vec3 n = data.rgb / coverage * 2.0 - 1.0;
        n = dot(n, n) > 1e-6 ? normalize(n) : vec3(0.0, 1.0, 0.0);
        outA = vec4(albedo.rgb / coverage, coverage);
        outD = vec4(n * 0.5 + 0.5, data.a / coverage);
        outM = foliage / coverage;
    } else if (hasCoarser == 1) {
        outA = vec4(texelFetch(coarserAlbedo, p / 2, level + 1).rgb, coverage);
        outD = texelFetch(coarserData, p / 2, level + 1);
        outM = texelFetch(coarserMaterial, p / 2, level + 1).r;
    } else {
        outA = vec4(0.0, 0.0, 0.0, coverage);
        outD = vec4(0.5, 1.0, 0.5, 0.5);
        outM = 1.0;
    }
    imageStore(outAlbedo, p, outA);
    imageStore(outData, p, outD);
    imageStore(outMaterial, p, vec4(clamp(outM, 0.0, 1.0)));
}
