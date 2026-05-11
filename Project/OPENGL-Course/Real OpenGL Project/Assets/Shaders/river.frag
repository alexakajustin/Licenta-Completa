#version 330

// RIVER SHADER — Inspired by "Spout" (P_Malin) Shadertoy
// Self-contained procedural water with flow animation, Fresnel, Beer's extinction.
// Works beautifully with or without engine textures (reflectionMap, sceneDepthMap, etc.)

in vec4 vertex_color;
in vec2 TexCoord;
in vec3 Normal;
in vec3 FragPos;

in vec3 TangentWorld;
in vec3 BitangentWorld;
in vec3 NormalWorld;

in vec3 LocalPos;
in float vIsSelected;
in float vFadeFactor;
in vec4 clipSpaceCoords;
in float vObjectScale;

out vec4 colour;

// Light Uniforms
struct Light { vec3 colour; float ambientIntensity; float diffuseIntensity; };
struct DirectionalLight { Light base; vec3 direction; };
struct PointLight { Light base; vec3 position; float constant; float linear; float exponent; };

uniform int pointLightCount;
uniform DirectionalLight directionalLight;
uniform PointLight pointLights[16];

// Material Uniforms
uniform sampler2D material_waterNormalMap;
uniform sampler2D material_dudvMap;
uniform sampler2D material_causticsMap;

uniform vec4  material_waterColorDeep;
uniform vec4  material_waterColorShallow;
uniform float material_waterDepthScale;
uniform float material_waveSpeed;
uniform float material_waveStrength;
uniform float material_waveScale;
uniform vec4  material_foamColor;
uniform float material_foamDistance;
uniform float material_specularIntensityOverride;
uniform float material_shininessOverride;
uniform float material_flowDirection;  // 0.0 = along +Z, 1.0 = along +X

// Environment Uniforms
uniform sampler2D refractionMap;
uniform sampler2D reflectionMap;
uniform sampler2D sceneDepthMap;
uniform float time;
uniform vec2 screenSize;
uniform vec3 eyePosition;
uniform float selectionTint;

// ─────────────────────────────────────────────
// Procedural Noise (from Spout shader — texture-free)
// ─────────────────────────────────────────────
float Hash(vec2 p) {
    float h = dot(p, vec2(127.1, 311.7));
    return fract(sin(h) * 43758.5453);
}

float ValueNoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f); // smoothstep
    
    float a = Hash(i);
    float b = Hash(i + vec2(1.0, 0.0));
    float c = Hash(i + vec2(0.0, 1.0));
    float d = Hash(i + vec2(1.0, 1.0));
    
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

float FBM(vec2 p) {
    float f = 0.0;
    f += 0.5000 * ValueNoise(p); p *= 2.03;
    f += 0.2500 * ValueNoise(p); p *= 2.01;
    f += 0.1250 * ValueNoise(p); p *= 2.02;
    f += 0.0625 * ValueNoise(p);
    return f / 0.9375;
}

// Spout-style simple noise for surface ripples
float SpoutNoise(vec2 p) {
    vec2 s = sin(p * 0.6345) + sin(p * 1.62423);
    return dot(s, vec2(0.125)) + 0.5;
}

// ─────────────────────────────────────────────
// Procedural Normal from noise (no texture needed)
// ─────────────────────────────────────────────
vec3 GetProceduralNormal(vec2 worldXZ, float flowTime) {
    float eps = 0.05;
    float scale = 3.0;
    
    vec2 p = worldXZ * scale;
    float h  = FBM(p + vec2(0.0, -flowTime));
    float hx = FBM(p + vec2(eps, -flowTime));
    float hz = FBM(p + vec2(0.0, -flowTime) + vec2(0.0, eps));
    
    // Second octave at different speed and direction
    float h2  = FBM(p * 2.5 + vec2(flowTime * 0.3, -flowTime * 0.7));
    float h2x = FBM(p * 2.5 + vec2(flowTime * 0.3 + eps, -flowTime * 0.7));
    float h2z = FBM(p * 2.5 + vec2(flowTime * 0.3, -flowTime * 0.7 + eps));
    
    h  += h2 * 0.4;
    hx += h2x * 0.4;
    hz += h2z * 0.4;
    
    vec3 normal;
    normal.x = -(hx - h) / eps;
    normal.z = -(hz - h) / eps;
    normal.y = 1.0;
    
    return normalize(normal);
}

// ─────────────────────────────────────────────
// Schlick Fresnel (from Spout)
// ─────────────────────────────────────────────
float Schlick(float cosTheta, float fR0) {
    float x = clamp(1.0 - cosTheta, 0.0, 1.0);
    return fR0 + (1.0 - fR0) * x * x * x * x * x;
}

// ─────────────────────────────────────────────
// Depth utility
// ─────────────────────────────────────────────
float LinearizeDepth(float depth) {
    float near = 0.1; float far = 2000.0;
    float z = depth * 2.0 - 1.0;
    return (2.0 * near * far) / (far + near - z * (far - near));
}

// ─────────────────────────────────────────────
// Main
// ─────────────────────────────────────────────
void main() {
    // Dither-fade for LOD transitions
    if (vFadeFactor > 0.001) {
        float bayer[16] = float[16](0.0,0.5,0.125,0.625,0.75,0.25,0.875,0.375,0.1875,0.6875,0.0625,0.5625,0.9375,0.4375,0.8125,0.3125);
        if (vFadeFactor > bayer[int(mod(gl_FragCoord.y,4.0))*4 + int(mod(gl_FragCoord.x,4.0))]) discard;
    }

    vec3 viewDir = normalize(eyePosition - FragPos);
    vec3 sunDir = -directionalLight.direction;
    
    // ─── Flow Parameters ───
    float speed   = material_waveSpeed == 0.0 ? 0.8 : material_waveSpeed;
    float strength = material_waveStrength == 0.0 ? 1.0 : material_waveStrength;
    float scale   = material_waveScale == 0.0 ? 1.0 : material_waveScale;
    float flowTime = time * speed;
    
    // ─── 1. Surface Normal ───
    // Try texture-based normal first, fall back to procedural
    vec3 worldNormal;
    vec3 rawSample = texture(material_waterNormalMap, TexCoord).rgb;
    bool hasNormalMap = dot(rawSample, rawSample) > 0.001;
    
    if (hasNormalMap) {
        // Texture-based: 3-layer scrolling normals along flow direction
        vec2 tc = vec2(TexCoord.x * 2.0, TexCoord.y * 8.0) * scale;
        float t = flowTime;
        
        vec3 n0 = texture(material_waterNormalMap, tc + vec2(0.0, -t * 0.5)).rgb * 2.0 - 1.0;
        vec3 n1 = texture(material_waterNormalMap, tc * 2.5 + vec2(sin(t*0.2)*0.1, -t * 1.2)).rgb * 2.0 - 1.0;
        vec3 n2 = texture(material_waterNormalMap, tc * 0.6 + vec2(0.1, -t * 0.3)).rgb * 2.0 - 1.0;
        
        vec3 localN = normalize(n0 * 0.5 + n1 * 0.3 + n2 * 0.2);
        localN.xy *= strength * 0.5;
        localN = normalize(localN);
        
        mat3 TBN = mat3(normalize(TangentWorld), normalize(BitangentWorld), normalize(NormalWorld));
        worldNormal = normalize(TBN * localN);
    } else {
        // Procedural: Spout-inspired FBM noise normals
        // Use UV space so flow follows the mesh shape, not world axes
        vec2 flowUV = vec2(TexCoord.x * 2.0, TexCoord.y * 6.0) * scale;
        vec3 procN = GetProceduralNormal(flowUV, flowTime * 0.5);
        
        // Add Spout-style high-frequency ripples along UV flow
        vec2 rippleDomain = flowUV * 15.0;
        rippleDomain.y -= time * speed * 8.0;
        float ripple = SpoutNoise(rippleDomain) * 0.025 * strength;
        procN.x += ripple;
        procN.z += ripple * 0.7;
        procN = normalize(procN);
        
        // Map procedural normal into mesh's tangent space
        mat3 TBN = mat3(normalize(TangentWorld), normalize(BitangentWorld), normalize(NormalWorld));
        worldNormal = normalize(TBN * procN);
    }
    
    // ─── 2. Depth & Beer's Law Extinction ───
    vec2 screenUV = gl_FragCoord.xy / max(screenSize, vec2(1.0));
    float depthDiff = 1.0; // Default: assume deep water
    float currentDepth = LinearizeDepth(gl_FragCoord.z);
    
    float bgRaw = texture(sceneDepthMap, screenUV).r;
    // Only use depth if we got a valid sample (not a clear buffer / unbound texture)
    if (bgRaw > 0.0001 && bgRaw < 0.9999) {
        float linearBg = LinearizeDepth(bgRaw);
        depthDiff = max(linearBg - currentDepth, 0.0);
    }
    
    // Spout's RGB extinction: 1/(1 + extinction * distance)
    // Extinction vec3(0.3, 0.7, 0.9) = red absorbed fast, blue survives
    vec3 extinction = vec3(0.3, 0.7, 0.9);
    float depthScale = material_waterDepthScale == 0.0 ? 0.3 : material_waterDepthScale;
    vec3 cExtinction = 1.0 / (1.0 + extinction * depthDiff * depthScale);
    float absorption = exp(-depthDiff * depthScale * 0.3);
    
    // Water color gradient
    vec3 deepCol    = material_waterColorDeep == vec4(0.0)    ? vec3(0.012, 0.149, 0.349) : material_waterColorDeep.rgb;
    vec3 shallowCol = material_waterColorShallow == vec4(0.0) ? vec3(0.051, 0.6, 0.749)   : material_waterColorShallow.rgb;
    vec3 waterTint = mix(deepCol, shallowCol, absorption);
    
    // ─── 3. Refraction ───
    float distortStr = material_waveStrength == 0.0 ? 0.015 : material_waveStrength * 0.003;
    vec2 refractUV = screenUV + worldNormal.xz * distortStr;
    
    vec3 refractedColor = texture(refractionMap, refractUV).rgb;
    // If refraction map is empty/unbound, use water tint directly
    if (dot(refractedColor, refractedColor) < 0.0001) {
        refractedColor = waterTint;
    } else {
        // Apply Beer's extinction to refracted scene color
        refractedColor *= cExtinction;
        refractedColor = mix(waterTint, refractedColor, absorption);
    }
    
    // ─── 4. Fresnel & Reflection ───
    float NdotV = max(dot(worldNormal, viewDir), 0.0);
    float fresnel = Schlick(NdotV, 0.02); // Water IOR ~1.33 -> R0 ≈ 0.02
    
    // Reflections removed per user request. 
    // We blend slightly with the shallow tint at glancing angles to keep the water from looking flat.
    vec3 baseWater = mix(refractedColor, shallowCol, fresnel * 0.5);
    
    // ─── 5. Caustics ───
    vec3 rawCaustic = texture(material_causticsMap, TexCoord * 2.0).rgb;
    if (dot(rawCaustic, rawCaustic) > 0.001) {
        vec2 causticUV = TexCoord * vec2(3.0, 6.0) + vec2(0.0, -time * 0.08);
        vec3 caustics = texture(material_causticsMap, causticUV).rgb;
        // Second layer for shimmer
        vec2 causticUV2 = TexCoord * vec2(4.0, 8.0) + vec2(time * 0.03, -time * 0.05);
        caustics = max(caustics, texture(material_causticsMap, causticUV2).rgb * 0.6);
        baseWater += caustics * absorption * 0.12;
    }
    
    // ─── 6. Foam ───
    // Only apply foam where there's actual terrain beneath (real shoreline),
    // NOT where two river meshes overlap (depthDiff ≈ 0 from z-fighting).
    // A minimum depth of 0.05 filters out water-on-water overlap artifacts.
    float foamDist = material_foamDistance == 0.0 ? 0.5 : material_foamDistance;
    vec3 foamCol = material_foamColor == vec4(0.0) ? vec3(0.9, 0.95, 1.0) : material_foamColor.rgb;
    
    float foamMask = 0.0;
    if (depthDiff > 0.05 && depthDiff < foamDist) {
        // Use dudv texture if available, otherwise procedural (UV-based flow)
        float foamNoise;
        vec3 dudvSample = texture(material_dudvMap, TexCoord * 4.0).rgb;
        if (dot(dudvSample, dudvSample) > 0.001) {
            foamNoise = texture(material_dudvMap, TexCoord * vec2(4.0, 8.0) + vec2(0.0, -time * 0.2)).r;
        } else {
            foamNoise = FBM(TexCoord * vec2(5.0, 20.0) + vec2(0.0, -time * 2.0));
        }
        
        foamMask = smoothstep(foamDist, 0.05, depthDiff) * smoothstep(0.35, 0.65, foamNoise);
    }
    baseWater = mix(baseWater, foamCol, foamMask * 0.4);
    
    // ─── 7. Lighting ───
    // Diffuse
    float diff = max(dot(worldNormal, sunDir), 0.0);
    vec3 diffuseLight = directionalLight.base.colour * directionalLight.base.diffuseIntensity * diff;
    vec3 ambientLight = directionalLight.base.colour * directionalLight.base.ambientIntensity;
    
    // Blinn-Phong specular (energy-conserving from Spout)
    vec3 halfVec = normalize(sunDir + viewDir);
    float NdotH = max(dot(worldNormal, halfVec), 0.0);
    float specPower = material_shininessOverride == 0.0 ? 256.0 : material_shininessOverride;
    float specInt   = material_specularIntensityOverride == 0.0 ? 3.0 : material_specularIntensityOverride;
    float spec = pow(NdotH, specPower) * (specPower + 2.0) * 0.125; // energy conservation
    vec3 specularLight = directionalLight.base.colour * spec * specInt;
    
    // Subsurface scattering
    float sss = pow(max(0.0, dot(viewDir, directionalLight.direction)), 10.0) * absorption;
    vec3 sssColor = shallowCol * sss * 0.4;
    
    // Point lights
    vec3 pointContrib = vec3(0.0);
    for(int i = 0; i < pointLightCount; i++) {
        vec3 toLight = pointLights[i].position - FragPos;
        float dist = length(toLight);
        vec3 lDir = toLight / dist;
        float atten = 1.0 / (pointLights[i].constant + pointLights[i].linear * dist + pointLights[i].exponent * dist * dist);
        float pDiff = max(dot(worldNormal, lDir), 0.0);
        pointContrib += pointLights[i].base.colour * pDiff * atten * pointLights[i].base.diffuseIntensity;
    }
    
    // Combine
    vec3 totalLight = ambientLight + diffuseLight + pointContrib;
    vec3 finalColor = baseWater * totalLight + specularLight + sssColor;
    
    // Sun flare on water surface when looking toward light (Spout-style)
    float sunFlare = pow(clamp(dot(directionalLight.direction, -viewDir), 0.0, 1.0), 10.0);
    finalColor += directionalLight.base.colour * sunFlare * 0.03;
    
    // ─── 8. Edge Softening ───
    float alpha = smoothstep(0.0, 0.3, depthDiff);
    
    // Selection tint
    float selected = max(selectionTint, vIsSelected > 0.5 ? 1.0 : 0.0);
    finalColor += vec3(0.5, 0.4, 0.0) * selected;

    // Clamp the maximum opacity to the material's alpha (e.g. 0.85)
    // This allows overlapping river meshes to blend into each other instead of 
    // rendering as hard, opaque overlapping planes (which look 'floating').
    colour = vec4(finalColor, clamp(alpha, 0.0, 0.85));
}
