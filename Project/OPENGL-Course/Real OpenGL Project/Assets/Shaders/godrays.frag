#version 460

out vec4 FragColor;

in vec2 TexCoord;
in vec3 WorldDir;

uniform sampler2D depthMap;
uniform sampler2D cloudMask;  // Accumulated sky, alpha = cloud transmittance
uniform int useCloudMask;     // 0 = no volumetric clouds (open sky everywhere)
uniform vec3 sunDir; // Direction TO the sun
uniform vec3 sunColor;
uniform mat4 projection;
uniform mat4 view;

uniform float exposure;
uniform float decay;
uniform float density;
uniform float weight;

const int NUM_SAMPLES = 80;

// How much sun light gets through the sky at uv: blocked by geometry, attenuated by clouds,
// so the radial blur turns the gaps between clouds into crepuscular rays
float SkyLight(vec2 uv)
{
    if (texture(depthMap, uv).r < 1.0) return 0.0;
    return useCloudMask == 1 ? texture(cloudMask, uv).a : 1.0;
}

void main()
{
    // 1. Calculate sun position in screen space
    // We only use the rotation part of the view matrix (mat3) so the sun stays at infinity
    vec3 sunViewDir = mat3(view) * sunDir;
    vec4 sunClipPos = projection * vec4(sunViewDir, 1.0);
    
    // Fade out as the sun approaches the camera plane instead of popping off at z = 0
    // (the projected position also explodes there, smearing rays across the screen)
    float facingFade = clamp(-sunViewDir.z * 4.0, 0.0, 1.0);
    if (facingFade <= 0.0) {
        FragColor = vec4(0.0);
        return;
    }

    vec2 sunScreenPos = (sunClipPos.xy / sunClipPos.w) * 0.5 + 0.5;

    // 2. Setup ray marching
    vec2 deltaTexCoord = (TexCoord - sunScreenPos);
    deltaTexCoord *= 1.0 / float(NUM_SAMPLES) * density;

    // Per-pixel jitter of the first step (interleaved gradient noise) turns the visible
    // sample banding of the 80-step march into fine noise
    float jitter = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
    vec2 texCoord = TexCoord + deltaTexCoord * jitter;
    float illuminationDecay = 1.0;
    float visibility = 0.0;

    // 3. Ray march from current pixel towards sun
    for (int i = 0; i < NUM_SAMPLES; i++)
    {
        texCoord -= deltaTexCoord;
        // Off-screen samples would read clamped edge texels and streak the border
        if (any(lessThan(texCoord, vec2(0.0))) || any(greaterThan(texCoord, vec2(1.0)))) break;
        visibility += SkyLight(texCoord) * illuminationDecay * weight;

        illuminationDecay *= decay;
    }

    // 4. Distance falloff from sun center
    float dist = length(TexCoord - sunScreenPos);
    float falloff = 1.0 - smoothstep(0.0, 1.2, dist); // smoothstep needs edge0 < edge1

    // 5. Sun Disk
    float wdots = dot(normalize(WorldDir), sunDir);
    float sunDisk = 0.0;
    float sunThreshold = 0.9995; 
    if (wdots > sunThreshold) {
        sunDisk = smoothstep(sunThreshold, 1.0, wdots) * 0.8 * SkyLight(TexCoord);
    }

    // 6. Final color blending
    vec3 godRayColor = sunColor * visibility * exposure * falloff * facingFade;
    vec3 sunDiskColor = sunColor * sunDisk;
    
    vec3 finalColor = godRayColor + sunDiskColor;
    finalColor = clamp(finalColor, 0.0, 1.2); 
    
    FragColor = vec4(finalColor, 1.0);
}
