#version 330

// PREMIUM LAKE SHADER - "SEASCAPE" ADAPTATION 
// Discarded old logic. Implementing iterative high-fidelity normal blending and Beer's Law.

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

vec3 GetPremiumNormal() {
    float moveSpeed = material_waveSpeed == 0.0 ? 0.05 : material_waveSpeed;
    float tiling = material_waveScale == 0.0 ? 1.0 : material_waveScale;
    float scaleFactor = max(vObjectScale / 100.0, 0.01);
    
    vec2 tc = FragPos.xz * 0.1 * tiling; // Use world space for lakes to prevent stretching
    float t = time * moveSpeed;
    
    // Iterative blending of 3 normal layers at different scales
    vec3 n0 = texture(material_waterNormalMap, tc + vec2(t * 0.02, t * 0.01)).rgb * 2.0 - 1.0;
    vec3 n1 = texture(material_waterNormalMap, tc * 2.5 + vec2(-t * 0.03, t * 0.02)).rgb * 2.0 - 1.0;
    vec3 n2 = texture(material_waterNormalMap, tc * 6.0 + vec2(t * 0.05, -t * 0.04)).rgb * 2.0 - 1.0;
    
    vec3 normalLocal = normalize(n0 * 0.5 + n1 * 0.3 + n2 * 0.2);
    
    // TBN Transform
    mat3 TBN = mat3(normalize(TangentWorld), normalize(BitangentWorld), normalize(NormalWorld));
    return normalize(TBN * normalLocal);
}

vec3 CalcLight(Light light, vec3 direction, vec3 normal, vec3 viewDir) {
    float diff = max(dot(normal, -direction), 0.0);
    vec3 diffuse = light.colour * light.diffuseIntensity * diff;
    
    vec3 halfDir = normalize(-direction + viewDir);
    float specPower = material_shininessOverride == 0.0 ? 512.0 : material_shininessOverride;
    float specIntensity = material_specularIntensityOverride == 0.0 ? 3.0 : material_specularIntensityOverride;
    
    float spec = pow(max(dot(normal, halfDir), 0.0), specPower);
    float normFactor = (specPower + 8.0) / 8.0; 
    vec3 specular = light.colour * specIntensity * spec * normFactor;
    
    return (light.ambientIntensity * light.colour) + diffuse + specular;
}

void main() {
    if (vFadeFactor > 0.001) {
        float bayer[16] = float[16](0.0, 0.5, 0.125, 0.625, 0.75, 0.25, 0.875, 0.375, 0.1875, 0.6875, 0.0625, 0.5625, 0.9375, 0.4375, 0.8125, 0.3125);
        if (vFadeFactor > bayer[int(mod(gl_FragCoord.y, 4.0)) * 4 + int(mod(gl_FragCoord.x, 4.0))]) discard;
    }

    vec3 viewDir = normalize(eyePosition - FragPos);
    vec3 worldNormal = GetPremiumNormal();
    
    vec2 screenUV = gl_FragCoord.xy / screenSize;
    float backgroundDepth = texture(sceneDepthMap, screenUV).r;
    float linearDepth = LinearizeDepth(backgroundDepth);
    float depthDiff = max(linearDepth - LinearizeDepth(gl_FragCoord.z), 0.0);
    
    float depthScale = material_waterDepthScale == 0.0 ? 0.15 : material_waterDepthScale;
    float depthFactor = exp(-depthDiff * depthScale);
    
    vec3 deepCol = material_waterColorDeep == vec4(0.0) ? vec3(0.01, 0.1, 0.2) : material_waterColorDeep.rgb;
    vec3 shallowCol = material_waterColorShallow == vec4(0.0) ? vec3(0.2, 0.6, 0.7) : material_waterColorShallow.rgb;
    vec3 waterTint = mix(deepCol, shallowCol, depthFactor);

    // Refraction & Reflection
    vec2 refractUV = screenUV + worldNormal.xz * 0.02;
    if (LinearizeDepth(texture(sceneDepthMap, refractUV).r) < LinearizeDepth(gl_FragCoord.z)) refractUV = screenUV;
    vec3 refractedColor = texture(refractionMap, refractUV).rgb;
    
    // Fresnel (Schlick's approximation with boosted base for visibility)
    float fresnel = 0.15 + 0.85 * pow(1.0 - max(dot(worldNormal, viewDir), 0.0), 3.0);
    vec2 reflectUV = screenUV + worldNormal.xz * 0.03;
    vec3 reflectedColor = texture(reflectionMap, reflectUV).rgb;
    
    vec3 baseWater = mix(refractedColor * waterTint * 1.5, reflectedColor * 1.1, fresnel);
    
    // Caustics (World Space)
    vec2 causticUV = FragPos.xz * 0.05 + vec2(time * 0.02, time * 0.01);
    vec3 caustics = texture(material_causticsMap, causticUV).rgb;
    caustics += texture(material_causticsMap, causticUV * 1.5 - vec2(time * 0.01, 0.0)).rgb;
    baseWater += caustics * 0.15 * depthFactor;

    // Foam
    float foamDist = material_foamDistance == 0.0 ? 0.8 : material_foamDistance;
    float foamAlpha = smoothstep(foamDist, 0.0, depthDiff);
    vec3 foamCol = material_foamColor == vec4(0.0) ? vec3(1.0) : material_foamColor.rgb;
    baseWater = mix(baseWater, foamCol, foamAlpha * 0.7);

    // Final Lighting
    vec3 lighting = CalcLight(directionalLight.base, directionalLight.direction, worldNormal, viewDir);
    for(int i=0; i<pointLightCount; i++) lighting += CalcLight(pointLights[i].base, normalize(pointLights[i].position - FragPos), worldNormal, viewDir);
    
    vec3 finalColor = baseWater + lighting * 0.2;
    
    float selected = max(selectionTint, vIsSelected > 0.5 ? 1.0 : 0.0);
    finalColor += vec3(0.4, 0.3, 0.0) * selected;

    colour = vec4(finalColor, 1.0);
}
