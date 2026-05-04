#version 430 core

layout(local_size_x = 8, local_size_y = 8) in;

layout(binding = 0) uniform sampler2D inTexture;
layout(binding = 0, r32f) restrict writeonly uniform image2D outImage;

uniform vec2 inputSize;
uniform int lod;

void main()
{
    ivec2 outPos = ivec2(gl_GlobalInvocationID.xy);
    ivec2 outSize = imageSize(outImage);
    
    if (outPos.x >= outSize.x || outPos.y >= outSize.y) return;

    // Calculate base pixel coordinates in the previous mip level
    ivec2 inPos = outPos * 2;
    
    // Clamp to ensure we don't read out of bounds on odd-sized textures
    ivec2 inSize = ivec2(inputSize);
    ivec2 pos00 = min(inPos + ivec2(0, 0), inSize - 1);
    ivec2 pos10 = min(inPos + ivec2(1, 0), inSize - 1);
    ivec2 pos01 = min(inPos + ivec2(0, 1), inSize - 1);
    ivec2 pos11 = min(inPos + ivec2(1, 1), inSize - 1);

    float d0 = texelFetch(inTexture, pos00, lod).r;
    float d1 = texelFetch(inTexture, pos10, lod).r;
    float d2 = texelFetch(inTexture, pos01, lod).r;
    float d3 = texelFetch(inTexture, pos11, lod).r;

    float maxDepth = max(max(d0, d1), max(d2, d3));

    imageStore(outImage, outPos, vec4(maxDepth, 0.0, 0.0, 0.0));
}
