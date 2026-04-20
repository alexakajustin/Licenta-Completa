#version 330
out float FragColor;
in vec2 TexCoords;

uniform sampler2D depthMap;
uniform sampler2D texNoise;

uniform vec3 samples[64];
uniform mat4 projection;
uniform mat4 invProjection;

uniform vec2 noiseScale;

uniform int kernelSize = 64;
uniform float radius = 0.5;
uniform float bias = 0.025;
uniform float intensity = 1.5;

vec3 getFragPos(vec2 uv) {
    float depth = texture(depthMap, uv).r;
    // Depth buffer stores [0,1]. Convert to NDC [-1,1] range for clip space.
    vec4 clipSpace = vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 viewSpace = invProjection * clipSpace;
    return viewSpace.xyz / viewSpace.w;
}

void main() {
    // Skip skybox pixels (depth ~1.0)
    float rawDepth = texture(depthMap, TexCoords).r;
    if (rawDepth >= 0.9999) {
        FragColor = 1.0;
        return;
    }

    vec3 fragPos = getFragPos(TexCoords);

    // Reconstruct normal from depth derivatives (screen-space)
    vec3 fragPosDx = dFdx(fragPos);
    vec3 fragPosDy = dFdy(fragPos);
    vec3 normal = normalize(cross(fragPosDy, fragPosDx));

    // Ensure normal faces the camera (view-space: camera at origin, looking -Z)
    // Normal should point towards the camera (positive Z component)
    if (normal.z < 0.0) normal = -normal;

    vec3 randomVec = normalize(texture(texNoise, TexCoords * noiseScale).xyz);

    // Build TBN (tangent-bitangent-normal) matrix to orient kernel
    vec3 tangent = normalize(randomVec - normal * dot(randomVec, normal));
    vec3 bitangent = cross(normal, tangent);
    mat3 TBN = mat3(tangent, bitangent, normal);

    float occlusion = 0.0;
    int actualSamples = clamp(kernelSize, 1, 64);
    
    for(int i = 0; i < actualSamples; ++i) {
        // Orient sample to hemisphere around the fragment normal
        vec3 samplePos = TBN * samples[i]; 
        samplePos = fragPos + samplePos * radius; 
        
        // Project sample to screen space to look up what's at that position
        vec4 offset = projection * vec4(samplePos, 1.0);
        offset.xyz /= offset.w; 
        offset.xyz = offset.xyz * 0.5 + 0.5; // NDC to [0,1]
        
        // Clamp to valid texture range
        if (offset.x < 0.0 || offset.x > 1.0 || offset.y < 0.0 || offset.y > 1.0) continue;

        // Get the actual geometry depth at the sample's screen position
        float sampleDepth = getFragPos(offset.xy).z;
        
        // Range check: prevent contribution from samples too far away
        float rangeCheck = smoothstep(0.0, 1.0, radius / abs(fragPos.z - sampleDepth));
        
        // Occlusion test: is the geometry closer to camera than our sample?
        // In view space, Z is negative: closer = less negative = larger value
        occlusion += (sampleDepth >= samplePos.z + bias ? 1.0 : 0.0) * rangeCheck;           
    }
    
    occlusion = 1.0 - (occlusion / float(actualSamples)) * intensity;
    occlusion = clamp(occlusion, 0.0, 1.0);
    FragColor = occlusion;
}
