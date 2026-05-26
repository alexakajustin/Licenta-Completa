#version 410 core

out vec4 FragColor;

in vec3 WorldPos;
in vec2 TexCoord;
in vec3 Normal;
in vec3 LocalPos;

uniform vec3 eyePosition;
uniform float time;

// Biome levels
uniform float seaLevel;
uniform float sandLevel;
uniform float grassLevel;
uniform float rockLevel;
uniform float snowLevel;

// Noise settings
uniform float noiseScale;
uniform int octaves;
uniform float persistence;
uniform float lacunarity;
uniform int seed;
uniform bool isSun;
uniform float temperature; // 0.0 = Cold (Blue), 1.0 = Hot (Red)

// --- SHADERTOY HELPERS (Morgan McGuire) ---
const float pi = 3.1415926535;
const vec3 atmosphereColor = vec3(0.3, 0.6, 1.0) * 1.2;

float hash(float p) { p = fract(p * 0.011); p *= p + 7.5; p *= p + p; return fract(p); }
float noise(vec3 x) { 
    const vec3 step = vec3(110, 241, 171); 
    vec3 i = floor(x); vec3 f = fract(x); 
    float n = dot(i, step); 
    vec3 u = f * f * (3.0 - 2.0 * f); 
    return mix(mix(mix( hash(n + dot(step, vec3(0, 0, 0))), hash(n + dot(step, vec3(1, 0, 0))), u.x), 
                   mix( hash(n + dot(step, vec3(0, 1, 0))), hash(n + dot(step, vec3(1, 1, 0))), u.x), u.y), 
               mix(mix( hash(n + dot(step, vec3(0, 0, 1))), hash(n + dot(step, vec3(1, 0, 1))), u.x), 
                   mix( hash(n + dot(step, vec3(0, 1, 1))), hash(n + dot(step, vec3(1, 1, 1))), u.x), u.y), u.z); 
}

float fbm3(vec3 x) {
    float v = 0.0; float a = 0.5; vec3 shift = vec3(100);
    for (int i = 0; i < 3; ++i) { v += a * noise(x); x = x * 2.0 + shift; a *= 0.5; }
    return v;
}

float fbm6(vec3 x) {
    float v = 0.0; float a = 0.5; vec3 shift = vec3(100);
    for (int i = 0; i < 6; ++i) { v += a * noise(x); x = x * 2.0 + shift; a *= 0.5; }
    return v;
}

vec3 rgb2hsv(vec3 c) {
    vec4 K = vec4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);
    vec4 p = mix(vec4(c.bg, K.wz), vec4(c.gb, K.xy), step(c.b, c.g));
    vec4 q = mix(vec4(p.xyw, c.r), vec4(c.r, p.yzx), step(p.x, c.r));
    float d = q.x - min(q.w, q.y);
    float e = 1.0e-10;
    return vec3(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);
}

vec3 hsv2rgb(vec3 c) {
    vec4 K = vec4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
    vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - vec3(K.www));
    return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}

float cloudDensity(vec3 p, float t, float temp) {
    float speedMult = 0.5 + temp * 2.0; // Hotter planets have faster, more chaotic winds
    vec3 p_move = p * vec3(1.5, 2.5, 2.0);
    float d = fbm6(p_move + 1.5 * fbm3(p_move - t * speedMult * 0.047) - vec3(t * speedMult * 0.03, t * speedMult * 0.01, t * speedMult * 0.01)) - 0.42;
    return clamp(d * 2.0, 0.0, 1.0);
}

void main()
{
    vec3 n = normalize(Normal);
    vec3 v = normalize(eyePosition - WorldPos);
    vec3 lPos = normalize(LocalPos);
    
    // Generate height from noise (fbm6 for spice)
    vec3 offset = vec3(float(seed) * 0.123, float(seed) * 0.456, float(seed) * 0.789);
    float h = fbm6(lPos * noiseScale + offset) * 0.8 + 0.1;

    // Biome colors interpolated by temperature
    vec3 c_deep = mix(vec3(0.0, 0.05, 0.2), vec3(0.2, 0.0, 0.0), temperature);      
    vec3 c_shallow = mix(vec3(0.0, 0.3, 0.5), vec3(0.8, 0.2, 0.0), temperature);   
    vec3 c_sand = mix(vec3(0.8, 0.7, 0.5), vec3(0.4, 0.2, 0.1), temperature);      
    vec3 c_grass = mix(vec3(0.2, 0.4, 0.1), vec3(0.7, 0.4, 0.1), temperature);     
    vec3 c_rock = mix(vec3(0.4, 0.4, 0.4), vec3(0.1, 0.1, 0.1), temperature);      
    vec3 c_snow = mix(vec3(0.95, 0.95, 1.0), vec3(0.8, 0.6, 0.3), temperature);    

    vec3 finalColor;
    float smoothness = 0.0;
    float metallic = 0.0;

    // Biome coloring logic
    if (h < seaLevel) {
        float relDepth = clamp((seaLevel - h) * 15.0, 0.0, 1.0);
        float wave = fbm3(lPos * 25.0 + vec3(time * 0.5)) * 0.02;
        n = normalize(n + vec3(wave)); 
        finalColor = mix(c_shallow, c_deep, relDepth);
        smoothness = 0.8; metallic = 0.2;
    } else if (h < sandLevel) {
        finalColor = c_sand;
    } else if (h < grassLevel) {
        finalColor = mix(c_sand, c_grass, (h - sandLevel) / (grassLevel - sandLevel));
    } else if (h < rockLevel) {
        finalColor = mix(c_grass, c_rock, (h - grassLevel) / (rockLevel - grassLevel));
    } else {
        finalColor = mix(c_rock, c_snow, (h - rockLevel) / (1.0 - rockLevel));
    }

    // Latitude based snow caps
    float lat = abs(lPos.y);
    float noiseLat = fbm3(lPos * 10.0 + offset * 0.5) * 0.1;
    if (lat + noiseLat > 0.85) {
        float snowFactor = smoothstep(0.85, 0.95, lat + noiseLat);
        finalColor = mix(finalColor, c_snow, snowFactor);
        smoothness = mix(smoothness, 0.4, snowFactor);
    }

    // --- CLOUDS ---
    float density = cloudDensity(lPos, time, temperature);
    // Cloud shadows on ground
    float cloudShadow = clamp(1.0 - density * 0.5, 0.5, 1.0);

    // Apply seed-based variation
    float hueShift = (fract(sin(float(seed) * 12.9898) * 43758.5453) - 0.5) * 0.05;
    vec3 hsv = rgb2hsv(finalColor);
    hsv.x = fract(hsv.x + hueShift);
    finalColor = hsv2rgb(hsv);

    // --- LIGHTING ---
    vec3 lDir = normalize(vec3(1.0, 1.0, 1.0));
    vec3 hDir = normalize(lDir + v);
    float diff = max(dot(n, lDir), 0.0);
    float spec = pow(max(dot(n, hDir), 0.0), mix(10.0, 100.0, smoothness)) * smoothness;
    
    vec3 ambient = finalColor * 0.15;
    vec3 diffuse = finalColor * diff * cloudShadow;
    vec3 specular = vec3(1.0) * spec * mix(0.04, 0.5, metallic) * cloudShadow;
    
    vec3 litColor = ambient + diffuse + specular;

    // --- ATMOSPHERIC SCATTERING ---
    float fresnel = pow(1.0 - max(dot(n, v), 0.0), 3.0);
    litColor = mix(litColor, atmosphereColor * max(diff, 0.2), fresnel * 0.6);

    // --- CLOUD COMPOSITION ---
    if (density > 0.0) {
        vec3 cloudHot = vec3(0.8, 0.6, 0.3); // Sulfurous/Ash
        vec3 cloudCold = vec3(0.95, 0.95, 1.0); // Icy/White
        vec3 cloudColor = mix(cloudCold, cloudHot, temperature);

        // Simple wrap shading for cloud "fluffiness"
        float cloudDiff = max(dot(n, lDir), 0.0) * 1.2;
        vec3 cloudLit = cloudColor * (cloudDiff + 0.3); // Ambient boost for clouds
        // Tint clouds with atmosphere at edges
        cloudLit = mix(cloudLit, atmosphereColor, fresnel * 0.4);
        litColor = mix(litColor, cloudLit, density);
    }

    FragColor = vec4(litColor, 1.0);
}
