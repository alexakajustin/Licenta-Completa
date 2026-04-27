#version 460

out vec4 FragColor;

in vec2 TexCoord;
in vec3 WorldDir;

uniform sampler2D depthMap;
uniform vec3 sunDir; // Direction TO the sun
uniform vec3 sunColor;
uniform mat4 projection;
uniform mat4 view;

uniform float exposure;
uniform float decay;
uniform float density;
uniform float weight;

const int NUM_SAMPLES = 80;

void main()
{
    // 1. Calculate sun position in screen space
    // We only use the rotation part of the view matrix (mat3) so the sun stays at infinity
    vec3 sunViewDir = mat3(view) * sunDir;
    vec4 sunClipPos = projection * vec4(sunViewDir, 1.0);
    
    // Check if sun is behind the camera
    if (sunViewDir.z > 0.0) {
        FragColor = vec4(0.0);
        return;
    }

    vec2 sunScreenPos = (sunClipPos.xy / sunClipPos.w) * 0.5 + 0.5;

    // 2. Setup ray marching
    vec2 deltaTexCoord = (TexCoord - sunScreenPos);
    deltaTexCoord *= 1.0 / float(NUM_SAMPLES) * density;
    
    vec2 texCoord = TexCoord;
    float illuminationDecay = 1.0;
    float visibility = 0.0;

    // 3. Ray march from current pixel towards sun
    for (int i = 0; i < NUM_SAMPLES; i++)
    {
        texCoord -= deltaTexCoord;
        float depth = texture(depthMap, texCoord).r;
        
        // Only contribute if it's the skybox area
        if (depth >= 1.0) 
        {
            visibility += illuminationDecay * weight;
        }
        
        illuminationDecay *= decay;
    }

    // 4. Distance falloff from sun center
    float dist = length(TexCoord - sunScreenPos);
    float falloff = smoothstep(1.2, 0.0, dist);

    // 5. Sun Disk
    float wdots = dot(normalize(WorldDir), sunDir);
    float sunDisk = 0.0;
    float sunThreshold = 0.9995; 
    if (wdots > sunThreshold) {
        float depth = texture(depthMap, TexCoord).r;
        if (depth >= 1.0) {
             sunDisk = smoothstep(sunThreshold, 1.0, wdots) * 0.8;
        }
    }

    // 6. Final color blending
    vec3 godRayColor = sunColor * visibility * exposure * falloff;
    vec3 sunDiskColor = sunColor * sunDisk;
    
    vec3 finalColor = godRayColor + sunDiskColor;
    finalColor = clamp(finalColor, 0.0, 1.2); 
    
    FragColor = vec4(finalColor, 1.0);
}
