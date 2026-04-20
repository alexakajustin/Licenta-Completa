#version 330
out float FragColor;
in vec2 TexCoords;

uniform sampler2D depthMap;
uniform sampler2D texNoise;

uniform vec3 samples[64];
uniform mat4 projection;
uniform mat4 invProjection;

uniform vec2 noiseScale;

uniform float radius = 0.5;
uniform float bias = 0.025;
uniform float intensity = 1.0;

vec3 getFragPos(vec2 uv) {
    float depth = texture(depthMap, uv).r;
    // Vulkan/D3D depth is 0 to 1, but typical OpenGL sets depth from -1 to 1 in clip space.
    // However, the depth buffer texture read returns 0.0 to 1.0. 
    // Clip space Z is -1.0 to 1.0.
    vec4 clipSpace = vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 viewSpace = invProjection * clipSpace;
    return viewSpace.xyz / viewSpace.w;
}

void main() {
    vec3 fragPos = getFragPos(TexCoords);
    
    // Ignore skybox
    float rawDepth = texture(depthMap, TexCoords).r;
    if (rawDepth >= 0.9999) {
        FragColor = 1.0;
        return;
    }
    
    vec3 fragPosDx = dFdx(fragPos);
    vec3 fragPosDy = dFdy(fragPos);
    // Determine normal from depth derivatives
    vec3 normal = normalize(cross(fragPosDx, fragPosDy));

    vec3 randomVec = texture(texNoise, TexCoords * noiseScale).xyz;
    
    // TBN
    vec3 tangent = normalize(randomVec - normal * dot(randomVec, normal));
    vec3 bitangent = cross(normal, tangent);
    mat3 TBN = mat3(tangent, bitangent, normal);
    
    float occlusion = 0.0;
    for(int i = 0; i < 64; ++i) {
        vec3 aSample = TBN * samples[i]; 
        aSample = fragPos + aSample * radius; 
        
        vec4 offset = vec4(aSample, 1.0);
        offset = projection * offset; 
        offset.xyz /= offset.w; 
        offset.xyz = offset.xyz * 0.5 + 0.5; 
        
        float sampleDepth = getFragPos(offset.xy).z;
        
        float rangeCheck = smoothstep(0.0, 1.0, radius / abs(fragPos.z - sampleDepth));
        occlusion += (sampleDepth >= aSample.z + bias ? 1.0 : 0.0) * rangeCheck;           
    }
    
    occlusion = 1.0 - (occlusion / 64.0) * intensity;
    FragColor = occlusion;
}
