#version 430 core
layout(local_size_x = 16, local_size_y = 16) in;

/**
 * @file impostor_coverage.glsl
 * @brief Coverage histogram of one mip level of an impostor atlas (Impostor.cpp).
 *
 * The cards are alpha-tested at 0.5. Averaged mips lose texels under that threshold and far crowns
 * would thin out; from these histograms the bake picks, per level, the alpha scale that lets as
 * many texels through the test as on level 0 (Castano, "Computing Alpha Mipmaps", 2010).
 * Workgroup histograms in shared memory first: most texels share a handful of bins.
 */

layout(binding = 0) uniform sampler2D premultAlbedo; ///< Alpha = average coverage
layout(std430, binding = 0) buffer Histogram { uint bins[]; }; ///< 256 bins per level

uniform int level;
uniform ivec2 levelSize;

shared uint localBins[256];

void main()
{
    uint tid = gl_LocalInvocationIndex;
    localBins[tid] = 0u;
    barrier();

    ivec2 p = ivec2(gl_GlobalInvocationID.xy);
    if (all(lessThan(p, levelSize))) {
        float coverage = clamp(texelFetch(premultAlbedo, p, level).a, 0.0, 1.0);
        uint bin = uint(coverage * 255.0 + 0.5);
        if (bin > 0u) atomicAdd(localBins[bin], 1u); // Empty texels: the rest of the level
    }
    barrier();

    if (localBins[tid] > 0u) atomicAdd(bins[level * 256 + int(tid)], localBins[tid]);
}
