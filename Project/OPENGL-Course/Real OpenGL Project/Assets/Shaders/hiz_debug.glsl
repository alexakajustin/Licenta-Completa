#version 430 core

layout(local_size_x = 8, local_size_y = 8) in;

layout(binding = 0) uniform sampler2D hizTexture;
layout(binding = 0, rgba8) restrict writeonly uniform image2D outImage;

uniform float nearPlane;
uniform float farPlane;

void main()
{
    ivec2 pos = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size = imageSize(outImage);
    
    if (pos.x >= size.x || pos.y >= size.y) return;

    // Sample the Hi-Z map at mip 0 (full res)
    vec2 uv = (vec2(pos) + 0.5) / vec2(size);
    float depth = texture(hizTexture, uv).r;
    
    // Linearize: convert from [0,1] nonlinear depth to [near, far] linear distance
    float linearDepth = (2.0 * nearPlane * farPlane) / (farPlane + nearPlane - (depth * 2.0 - 1.0) * (farPlane - nearPlane));
    
    // Normalize to [0, 1] for visualization (0 = near, 1 = far)
    float visualDepth = clamp(linearDepth / farPlane, 0.0, 1.0);
    
    // Invert so near = white, far = black (more intuitive)
    visualDepth = 1.0 - visualDepth;
    
    imageStore(outImage, pos, vec4(visualDepth, visualDepth, visualDepth, 1.0));
}
