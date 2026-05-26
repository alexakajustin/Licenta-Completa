#version 460

/**
 * @file volumetric_sky.frag
 * @brief Volumetric sky shader simulating daylight/night gradients, sunset transitions, horizon haze, Mie scattering, and procedural fractal noise clouds.
 */

out vec4 FragColor;

in vec2 TexCoord;
in vec3 WorldDir; ///< Ray direction mapping into 3D space.

uniform float time; ///< Current animation timer.
uniform int cloudsEnabled; ///< Set to 1 if procedural cloud generation is active.
uniform float cloudsDensity; ///< Relative cloud coverage density value.
uniform float cloudsSpeed; ///< Cloud wind speed offset multiplier.
uniform float cloudsSharpness; ///< Cloud edge transition sharpness threshold.

uniform vec3 sunDir; ///< Direction vector pointing towards the sun.
uniform vec3 sunColor; ///< RGB intensity color of the sun light source.


// Simple 2D Noise for clouds
float hash(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    float a = hash(i);
    float b = hash(i + vec2(1.0, 0.0));
    float c = hash(i + vec2(0.0, 1.0));
    float d = hash(i + vec2(1.0, 1.0));
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(a, b, u.x) + (c - a) * u.y * (1.0 - u.x) + (d - b) * u.x * u.y;
}

float fbm(vec2 p) {
    float v = 0.0;
    float a = 0.5;
    for (int i = 0; i < 5; i++) {
        v += a * noise(p);
        p *= 2.0;
        a *= 0.5;
    }
    return v;
}

void main()
{
    vec3 normalizedWorldDir = normalize(WorldDir);
    float h = max(normalizedWorldDir.y, 0.0);
    
    // Base sky colors based on sun height
    float sunHeight = sunDir.y;
    
    // Day/Night transition colors
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

    // Procedural Clouds
    if (cloudsEnabled == 1 && normalizedWorldDir.y > 0.0) {
        vec2 cloudUV = normalizedWorldDir.xz / (normalizedWorldDir.y + 0.01);
        cloudUV += time * cloudsSpeed * 0.1;
        
        float cloudNoise = fbm(cloudUV * 0.5);
        float cloudMask = smoothstep(1.0 - cloudsDensity, 1.0 - cloudsDensity + cloudsSharpness, cloudNoise);
        
        // Cloud coloring: brighter near sun, darker at night
        vec3 cloudBaseColor = mix(vec3(1.0), vec3(0.1, 0.1, 0.15), nightFactor);
        cloudBaseColor = mix(cloudBaseColor, vec3(1.0, 0.8, 0.6), sunsetFactor * cloudMask);
        
        // Add some sun highlights to clouds
        float sunCloudGlow = pow(max(0.0, dot(normalizedWorldDir, sunDir)), 8.0) * 0.5;
        cloudBaseColor += sunColor * sunCloudGlow * sunsetFactor;

        skyColor = mix(skyColor, cloudBaseColor, cloudMask * h);
    }

    // Horizon haze/volume effect
    float haze = pow(1.0 - h, 12.0) * 0.1;
    skyColor += horizonColor * haze;

    // Sun disk glow
    float wdots = dot(normalizedWorldDir, sunDir);
    float mie = pow(max(0.0, wdots), 256.0) * 0.2;
    skyColor += sunColor * mie;

    FragColor = vec4(skyColor, 1.0);
}
