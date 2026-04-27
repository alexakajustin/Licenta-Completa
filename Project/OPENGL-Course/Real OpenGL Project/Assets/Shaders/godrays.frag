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

const int NUM_SAMPLES = 100;

void main()
{
    // 1. Calculate sun position in screen space
    vec4 sunViewPos = view * vec4(sunDir * 1000.0, 1.0); // Far away sun
    vec4 sunClipPos = projection * sunViewPos;
    
    // Check if sun is behind the camera
    if (sunViewPos.z > 0.0) {
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
        
        // Sample depth at current point
        float depth = texture(depthMap, texCoord).r;
        
        // Only contribute if it's the skybox (depth is ~1.0)
        // If depth < 1.0, it's occluded by something
        if (depth >= 0.9999) 
        {
            visibility += illuminationDecay * weight;
        }
        
        illuminationDecay *= decay;
    }

    // 4. Distance falloff from sun center
    float dist = length(TexCoord - sunScreenPos);
    // Use smoothstep for a softer transition
    float falloff = smoothstep(1.2, 0.0, dist);

    // 5. Final color blending
    // Add a clamp to prevent extreme brightness
    vec3 finalColor = sunColor * visibility * exposure * falloff;
    finalColor = clamp(finalColor, 0.0, 1.0);
    
    FragColor = vec4(finalColor, 1.0);
}
