#version 330

// PREMIUM RIVER SHADER - "SEASCAPE" ADAPTATION
// Discarded old logic. Implementing high-fidelity dual-layer phase synchronization.

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

// Standard Light Uniforms
struct Light { vec3 colour; float ambientIntensity; float diffuseIntensity; };
struct DirectionalLight { Light base; vec3 direction; };
struct PointLight { Light base; vec3 position; float constant; float linear; float exponent; };
struct SpotLight { Light base; vec3 position; vec3 direction; float edge; float constant; float linear; float exponent; };

uniform int pointLightCount;
uniform int spotLightCount;
uniform DirectionalLight directionalLight;
uniform PointLight pointLights[16];
uniform SpotLight spotLights[16];

// Material & Textures
struct Material { 
    float specularIntensity; 
    float shininess; 
    float sssScale; 
    float sssDistortion; 
    vec4 baseColor; 
    vec2 tiling; 
    vec2 offset; 
};
uniform Material material;
uniform sampler2D material_dudvMap;
uniform sampler2D material_waterNormalMap;
uniform sampler2D material_causticsMap;

// Custom Uniforms
uniform sampler2D refractionMap;
uniform sampler2D reflectionMap;
uniform sampler2D sceneDepthMap;
uniform float time;
uniform vec2 screenSize;
uniform vec3 eyePosition;
uniform float selectionTint;

uniform vec4 material_waterColorDeep;
uniform vec4 material_waterColorShallow;
uniform float material_waterDepthScale;
uniform float material_fresnelPower;
uniform float material_waveSpeed;
uniform float material_waveStrength;
uniform float material_waveScale;
uniform vec4 material_foamColor;
uniform float material_foamDistance;
uniform float material_dudvTiling;
uniform float material_dudvStrength;
uniform float material_specularIntensityOverride;
uniform float material_shininessOverride;

float LinearizeDepth(float depth) {
    float near = 0.1; float far = 2000.0;
    float z = depth * 2.0 - 1.0;
    return (2.0 * near * far) / (far + near - z * (far - near));
}

// -----------------------------------------------------------
// THE "SEASCAPE" CORE MATH
// -----------------------------------------------------------

vec3 GetPremiumNormal() {
    float moveSpeed = material_waveSpeed;
    float tiling = material_waveScale == 0.0 ? 1.0 : material_waveScale;
    float scaleFactor = max(vObjectScale / 50.0, 0.01);
    vec2 tc = TexCoord * 8.0 * scaleFactor * tiling;
    
    // Dual-Layer Phase Sync
    float t = time * moveSpeed * 0.15;
    float phase0 = fract(t);
    float phase1 = fract(t + 0.5);
    float lerpFactor = 2.0 * abs(phase0 - 0.5); // Ping-pong blend
    
    // Layer 0
    vec2 uv0 = tc + vec2(0.0, -phase0);
    vec3 n0 = texture(material_waterNormalMap, uv0).rgb * 2.0 - 1.0;
    
    // Layer 1
    vec2 uv1 = tc + vec2(0.0, -phase1);
    vec3 n1 = texture(material_waterNormalMap, uv1).rgb * 2.0 - 1.0;
    
    // Combine with magnitude correction to prevent flattening
    float mag = sqrt(2.0 - 2.0 * abs(phase0 - 0.5));
    vec3 normalLocal = mix(n0, n1, lerpFactor) * mag;
    
    // Micro-shimmer (High frequency)
    vec2 microUV = tc * 4.5 + vec2(0.0, -t * 8.0);
    vec3 nMicro = texture(material_waterNormalMap, microUV).rgb * 2.0 - 1.0;
    normalLocal += nMicro * 0.3;

    // TBN Transform
    mat3 TBN = mat3(normalize(TangentWorld), normalize(BitangentWorld), normalize(NormalWorld));
    return normalize(TBN * normalLocal);
}

vec3 CalcLight(Light light, vec3 direction, vec3 normal, vec3 viewDir) {
    // Diffuse
    float diff = max(dot(normal, -direction), 0.0);
    vec3 diffuse = light.colour * light.diffuseIntensity * diff;
    
    // Specular (Energy Conserving Blinn-Phong)
    vec3 halfDir = normalize(-direction + viewDir);
    float specPower = material_shininessOverride == 0.0 ? 256.0 : material_shininessOverride;
    float specIntensity = material_specularIntensityOverride == 0.0 ? 5.0 : material_specularIntensityOverride;
    
    float spec = pow(max(dot(normal, halfDir), 0.0), specPower);
    float normFactor = (specPower + 8.0) / 32.0; // Toned down normalization
    vec3 specular = light.colour * specIntensity * spec * normFactor;
    
    return (light.ambientIntensity * light.colour) + diffuse + specular;
}

void main() {
    // 0. Bayer Dither Discard (Fade)
    if (vFadeFactor > 0.001) {
        float bayer[16] = float[16](0.0, 0.5, 0.125, 0.625, 0.75, 0.25, 0.875, 0.375, 0.1875, 0.6875, 0.0625, 0.5625, 0.9375, 0.4375, 0.8125, 0.3125);
        if (vFadeFactor > bayer[int(mod(gl_FragCoord.y, 4.0)) * 4 + int(mod(gl_FragCoord.x, 4.0))]) discard;
    }

    vec3 viewDir = normalize(eyePosition - FragPos);
    vec3 worldNormal = GetPremiumNormal();
    
    // 1. Depth & Volumetric Absorption (Beer's Law)
    vec2 screenUV = gl_FragCoord.xy / screenSize;
    float backgroundDepth = texture(sceneDepthMap, screenUV).r;
    float linearDepth = LinearizeDepth(backgroundDepth);
    float depthDiff = max(linearDepth - LinearizeDepth(gl_FragCoord.z), 0.0);
    
    float depthScale = material_waterDepthScale == 0.0 ? 0.25 : material_waterDepthScale;
    float depthFactor = exp(-depthDiff * depthScale);
    
    vec3 deepCol = material_waterColorDeep == vec4(0.0) ? vec3(0.02, 0.08, 0.15) : material_waterColorDeep.rgb;
    vec3 shallowCol = material_waterColorShallow == vec4(0.0) ? vec3(0.3, 0.7, 0.65) : material_waterColorShallow.rgb;
    vec3 waterTint = mix(deepCol, shallowCol, depthFactor);

    // 2. Refraction
    vec2 refractUV = screenUV + worldNormal.xz * 0.015;
    if (LinearizeDepth(texture(sceneDepthMap, refractUV).r) < LinearizeDepth(gl_FragCoord.z)) refractUV = screenUV;
    vec3 refractedColor = texture(refractionMap, refractUV).rgb;
    
    // Fresnel (Schlick's approximation with boosted base for visibility)
    float fresnel = 0.15 + 0.85 * pow(1.0 - max(dot(worldNormal, viewDir), 0.0), 3.0);
    vec2 reflectUV = screenUV + worldNormal.xz * 0.02;
    vec3 reflectedColor = texture(reflectionMap, reflectUV).rgb;
    
    // 4. Combine Base (Toned down multipliers)
    vec3 baseWater = mix(refractedColor * waterTint, reflectedColor, fresnel * 0.8);
    
    // 5. Foam (Depth-based)
    float foamDist = material_foamDistance == 0.0 ? 0.4 : material_foamDistance;
    float foamMask = texture(material_dudvMap, TexCoord * 5.0 + vec2(0.0, -time * material_waveSpeed * 0.2)).r;
    float foamAlpha = smoothstep(foamDist, 0.0, depthDiff) * smoothstep(0.3, 0.6, foamMask);
    vec3 foamCol = material_foamColor == vec4(0.0) ? vec3(0.9, 0.95, 1.0) : material_foamColor.rgb;
    baseWater = mix(baseWater, foamCol, foamAlpha * 0.7);

    // 6. Final Lighting
    vec3 lighting = CalcLight(directionalLight.base, directionalLight.direction, worldNormal, viewDir);
    for(int i=0; i<pointLightCount; i++) lighting += CalcLight(pointLights[i].base, normalize(pointLights[i].position - FragPos), worldNormal, viewDir);
    
    vec3 finalColor = baseWater + lighting * 0.1; // Reduced lighting influence
    
    // Selection
    float selected = max(selectionTint, vIsSelected > 0.5 ? 1.0 : 0.0);
    finalColor += vec3(0.4, 0.3, 0.0) * selected;

    colour = vec4(finalColor, mix(1.0, shallowCol.g, depthFactor * 0.5));
}
