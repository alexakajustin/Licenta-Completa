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
    ivec2 inSize = ivec2(inputSize);

    // NVIDIA-style conservative NPOT handling:
    // When the input has odd dimensions, a simple 2x2 downsample misses the last
    // row/column. We expand to a 3x3 kernel for odd axes to be fully conservative.
    bool oddX = (inSize.x & 1) != 0;
    bool oddY = (inSize.y & 1) != 0;

    // Always sample the base 2x2 quad
    ivec2 pos00 = min(inPos + ivec2(0, 0), inSize - 1);
    ivec2 pos10 = min(inPos + ivec2(1, 0), inSize - 1);
    ivec2 pos01 = min(inPos + ivec2(0, 1), inSize - 1);
    ivec2 pos11 = min(inPos + ivec2(1, 1), inSize - 1);

    float d0 = texelFetch(inTexture, pos00, lod).r;
    float d1 = texelFetch(inTexture, pos10, lod).r;
    float d2 = texelFetch(inTexture, pos01, lod).r;
    float d3 = texelFetch(inTexture, pos11, lod).r;

    float maxDepth = max(max(d0, d1), max(d2, d3));

    // For odd width: sample an extra column (x+2)
    if (oddX) {
        ivec2 pos20 = min(inPos + ivec2(2, 0), inSize - 1);
        ivec2 pos21 = min(inPos + ivec2(2, 1), inSize - 1);
        maxDepth = max(maxDepth, max(
            texelFetch(inTexture, pos20, lod).r,
            texelFetch(inTexture, pos21, lod).r));
    }

    // For odd height: sample an extra row (y+2)
    if (oddY) {
        ivec2 pos02 = min(inPos + ivec2(0, 2), inSize - 1);
        ivec2 pos12 = min(inPos + ivec2(1, 2), inSize - 1);
        maxDepth = max(maxDepth, max(
            texelFetch(inTexture, pos02, lod).r,
            texelFetch(inTexture, pos12, lod).r));
    }

    // For both odd: sample the corner (x+2, y+2)
    if (oddX && oddY) {
        ivec2 pos22 = min(inPos + ivec2(2, 2), inSize - 1);
        maxDepth = max(maxDepth, texelFetch(inTexture, pos22, lod).r);
    }

    imageStore(outImage, outPos, vec4(maxDepth, 0.0, 0.0, 0.0));
}
