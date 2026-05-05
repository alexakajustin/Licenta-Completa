#version 430 core

layout(local_size_x = 8, local_size_y = 8) in;

layout(binding = 0) uniform sampler2D hizTexture;
layout(binding = 0, rgba8) restrict writeonly uniform image2D outImage;

uniform float nearPlane;
uniform float farPlane;

// Helper to linearize depth
float linearize(float d) {
    return (2.0 * nearPlane * farPlane) / (farPlane + nearPlane - (d * 2.0 - 1.0) * (farPlane - nearPlane));
}

void main()
{
    ivec2 pos = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size = imageSize(outImage);

    if (pos.x >= size.x || pos.y >= size.y) return;

    vec2 texelSize = 1.0 / vec2(size);
    vec2 uv = (vec2(pos) + 0.5) * texelSize;


    float d0 = texture(hizTexture, uv).r;


    // Sky background
    if (d0 >= 0.99999) {
        imageStore(outImage, pos, vec4(0.05, 0.05, 0.1, 1.0));
        return;
    }

    // Sample neighbors to compute screen-space derivatives
    float dx = texture(hizTexture, uv + vec2(texelSize.x, 0.0)).r;
    float dy = texture(hizTexture, uv + vec2(0.0, texelSize.y)).r;

    float lin0 = linearize(d0);
    float linX = linearize(dx);
    float linY = linearize(dy);

    float ddx = linX - lin0;
    float ddy = linY - lin0;

    // Silhouette Edge Detection
    // If the depth difference between adjacent pixels is large, it's an edge.
    float edge = (abs(ddx) > 1.5 || abs(ddy) > 1.5) ? 0.0 : 1.0;

    // Fake normal from depth gradient for surface shading
    // The Z component (0.2) controls how steep the gradients appear
    vec3 normal = normalize(vec3(-ddx, -ddy, 0.2));
    
    // Light coming from top-left, slightly forward
    vec3 lightDir = normalize(vec3(-0.5, -0.5, 1.0));
    float ndotl = max(dot(normal, lightDir), 0.0);

    // Add depth banding (topographic contour lines every 10 meters)
    float contour = fract(lin0 / 10.0);
    // Smooth the contour lines slightly
    float contourAlpha = smoothstep(0.0, 0.1, contour) * smoothstep(1.0, 0.9, contour);

    // Base color fades with distance
    float distanceFade = 1.0 - clamp(lin0 / 500.0, 0.0, 0.7);
    vec3 baseColor = vec3(0.5) * distanceFade;

    // Mix shading, base color, and contour lines
    vec3 finalColor = baseColor * (ndotl * 0.8 + 0.2);
    finalColor = mix(finalColor * 0.5, finalColor, contourAlpha);
    
    // Apply black silhouette edges
    finalColor *= edge;

    imageStore(outImage, pos, vec4(finalColor, 1.0));
}