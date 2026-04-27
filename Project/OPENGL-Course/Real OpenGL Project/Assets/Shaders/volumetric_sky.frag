#version 460

out vec4 FragColor;

in vec2 TexCoord;
in vec3 WorldDir;

uniform sampler2D depthMap;
uniform vec3 sunDir;
uniform vec3 sunColor;

void main()
{
    float depth = texture(depthMap, TexCoord).r;
    
    // Only render where there is no geometry (skybox area)
    // We use a very strict check to avoid 'eating' distant mountains
    if (depth < 1.0) {
        FragColor = vec4(0.0, 0.0, 0.0, 0.0);
        return;
    }

    vec3 normalizedWorldDir = normalize(WorldDir);
    float h = max(normalizedWorldDir.y, 0.0);
    
    // Base sky colors based on sun height
    float sunHeight = sunDir.y;
    
    // Day/Night transition colors - Darker and more natural
    vec3 zenithDay = vec3(0.05, 0.15, 0.5);
    vec3 horizonDay = vec3(0.3, 0.5, 0.8);
    
    vec3 zenithSunset = vec3(0.02, 0.02, 0.1);
    vec3 horizonSunset = vec3(0.6, 0.2, 0.05);
    
    vec3 zenithNight = vec3(0.005, 0.005, 0.01);
    vec3 horizonNight = vec3(0.01, 0.01, 0.02);

    float sunsetFactor = smoothstep(0.5, -0.1, sunHeight);
    float nightFactor = smoothstep(-0.1, -0.5, sunHeight);

    vec3 zenithColor = mix(zenithDay, zenithSunset, sunsetFactor);
    zenithColor = mix(zenithColor, zenithNight, nightFactor);
    
    vec3 horizonColor = mix(horizonDay, horizonSunset, sunsetFactor);
    horizonColor = mix(horizonColor, horizonNight, nightFactor);

    vec3 skyColor = mix(horizonColor, zenithColor, pow(h, 0.6));

    // Horizon haze/volume effect - Much tighter and dimmer
    float haze = pow(1.0 - h, 12.0) * 0.1;
    skyColor += horizonColor * haze;

    // Sun disk glow - Much smaller and less blinding
    float wdots = dot(normalizedWorldDir, sunDir);
    float mie = pow(max(0.0, wdots), 256.0) * 0.2;
    skyColor += sunColor * mie;

    // Output solid color to replace the skybox completely
    FragColor = vec4(skyColor, 1.0);
}
