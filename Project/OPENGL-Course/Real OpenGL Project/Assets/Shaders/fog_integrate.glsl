#version 460

/**
 * @file fog_integrate.glsl
 * @brief Volumetric fog, pass 2: front-to-back integration of every froxel column.
 *
 * For each slice the in-scattered light is integrated analytically against the transmittance
 * across the slice (energy conserving: thick slices of dense fog do not over-brighten), then
 * the accumulated light and the remaining transmittance are stored. Texel z holds the result
 * at the far edge of slice z: the composite pass reads it at any view depth.
 */

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
layout(rgba16f, binding = 0) uniform readonly image3D inVolume;   // rgb = scattering x light, a = extinction
layout(rgba16f, binding = 1) uniform writeonly image3D outVolume; // rgb = accumulated light, a = transmittance

uniform ivec3 gridSize;
uniform float fogRange;
uniform mat4 invProjection;

void main()
{
    ivec2 xy = ivec2(gl_GlobalInvocationID.xy);
    if (any(greaterThanEqual(xy, gridSize.xy))) return;

    // Ray length per metre of view depth along this column (slices are view-depth slabs)
    vec2 uv = (vec2(xy) + 0.5) / vec2(gridSize.xy);
    vec4 farPoint = invProjection * vec4(uv * 2.0 - 1.0, 1.0, 1.0);
    vec3 rayView = normalize(farPoint.xyz / farPoint.w);
    float lengthPerDepth = 1.0 / max(-rayView.z, 1e-4);

    vec3 light = vec3(0.0);
    float transmittance = 1.0;
    float depthPrev = 0.0;
    for (int z = 0; z < gridSize.z; z++) {
        float w = float(z + 1) / float(gridSize.z);
        float depthNext = fogRange * w * w;
        float stepLength = (depthNext - depthPrev) * lengthPerDepth;
        depthPrev = depthNext;

        vec4 medium = imageLoad(inVolume, ivec3(xy, z));
        float extinction = max(medium.a, 1e-6);
        float stepTransmittance = exp(-extinction * stepLength);
        // Integral of scattering x transmittance over the slice
        light += transmittance * (medium.rgb - medium.rgb * stepTransmittance) / extinction;
        transmittance *= stepTransmittance;
        imageStore(outVolume, ivec3(xy, z), vec4(light, transmittance));
    }
}
