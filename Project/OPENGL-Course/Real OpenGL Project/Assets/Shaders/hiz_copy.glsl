#version 430 core

layout(local_size_x = 8, local_size_y = 8) in;

layout(binding = 0) uniform sampler2D depthTex;
layout(binding = 0, r32f) restrict writeonly uniform image2D outImage;

void main()
{
    ivec2 pos = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size = imageSize(outImage);
    
    if (pos.x >= size.x || pos.y >= size.y) return;

    // Fetch the depth value exactly at this pixel
    float d = texelFetch(depthTex, pos, 0).r;
    
    // Store it in the R32F texture
    imageStore(outImage, pos, vec4(d, 0.0, 0.0, 0.0));
}
