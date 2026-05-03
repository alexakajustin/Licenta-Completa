#version 330

// CINEMATIC RIVER SHADER - "SEASCAPE" OVERHAUL
// Flow-aligned normal blending, depth-based shore softening, and scrolling caustics.

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
struct SpotLight { Light base; vec3 position; vec3 direction; float edge; float constant; float linear; float exponent; };

uniform int pointLightCount;
uniform int spotLightCount;
uniform DirectionalLight directionalLight;
uniform PointLight pointLights[16];
uniform SpotLight spotLights[16];

// Material Uniforms
uniform sampler2D material_waterNormalMap;
uniform sampler2D material_dudvMap;
uniform sampler2D material_causticsMap;

uniform vec4 material_waterColorDeep;
uniform vec4 material_waterColorShallow;
uniform float material_waterDepthScale;
uniform float material_waveSpeed;
uniform float material_waveStrength;
uniform float material_waveScale;
uniform vec4 material_foamColor;
uniform float material_foamDistance;
uniform float material_specularIntensityOverride;
uniform float material_shininessOverride;

// Environment Uniforms
uniform sampler2D refractionMap;
uniform sampler2D reflectionMap;
uniform sampler2D sceneDepthMap;
uniform float time;
uniform vec2 screenSize;
uniform vec3 eyePosition;
uniform float selectionTint;

float LinearizeDepth(float depth) {
    float near = 0.1; float far = 2000.0;
    float z = depth * 2.0 - 1.0;
    return (2.0 * near * far) / (far + near - z * (far - near));
}

// -----------------------------------------------------------
// NORMAL GENERATION (Scrolling along V-axis)
// -----------------------------------------------------------
vec3 GetFlowingNormal() {
    float speed = material_waveSpeed == 0.0 ? 0.15 : material_waveSpeed;
    float scale = material_waveScale == 0.0 ? 1.0 : material_waveScale;
    
    // U is across, V is flow direction
    vec2 tc = vec2(TexCoord.x * 2.0, TexCoord.y * 8.0) * scale;
    float t = time * speed;
    
    // Three layers of scrolling normals at different frequencies
    // Layer 1: Base flow
    vec3 n0 = texture(material_waterNormalMap, tc + vec2(0.0, -t * 0.5)).rgb * 2.0 - 1.0;
    // Layer 2: Fast micro-ripples (slightly distorted)
    vec3 n1 = texture(material_waterNormalMap, tc * 2.5 + vec2(sin(t * 0.2) * 0.1, -t * 1.2)).rgb * 2.0 - 1.0;
    // Layer 3: Secondary flow with phase shift
    vec3 n2 = texture(material_waterNormalMap, tc * 0.6 + vec2(0.1, -t * 0.3)).rgb * 2.0 - 1.0;
    
    vec3 normalLocal = normalize(n0 * 0.5 + n1 * 0.3 + n2 * 0.2);
    
    // TBN Transform
    mat3 TBN = mat3(normalize(TangentWorld), normalize(BitangentWorld), normalize(NormalWorld));
    return normalize(TBN * normalLocal);
}

vec3 CalcLight(Light light, vec3 direction, vec3 normal, vec3 viewDir) {
    float diff = max(dot(normal, -direction), 0.0);
    vec3 diffuse = light.colour * light.diffuseIntensity * diff;
    
    // Specular
    vec3 halfDir = normalize(-direction + viewDir);
    float specPower = material_shininessOverride == 0.0 ? 128.0 : material_shininessOverride;
    float specInt = material_specularIntensityOverride == 0.0 ? 4.0 : material_specularIntensityOverride;
    
    float spec = pow(max(dot(normal, halfDir), 0.0), specPower);
    vec3 specular = light.colour * specInt * spec * ((specPower + 8.0) / 32.0);
    
    return (light.ambientIntensity * light.colour) + diffuse + specular;
}

void main() {
    // 0. Fade
    if (vFadeFactor > 0.001) {
        float bayer[16] = float[16](0.0, 0.5, 0.125, 0.625, 0.75, 0.25, 0.875, 0.375, 0.1875, 0.6875, 0.0625, 0.5625, 0.9375, 0.4375, 0.8125, 0.3125);
        if (vFadeFactor > bayer[int(mod(gl_FragCoord.y, 4.0)) * 4 + int(mod(gl_FragCoord.x, 4.0))]) discard;
    }

    vec3 viewDir = normalize(eyePosition - FragPos);
    vec3 worldNormal = GetFlowingNormal();
    
    // 1. Depth & Absorption
    vec2 screenUV = gl_FragCoord.xy / screenSize;
    float backgroundDepth = texture(sceneDepthMap, screenUV).r;
    float linearDepth = LinearizeDepth(backgroundDepth);
    float currentDepth = LinearizeDepth(gl_FragCoord.z);
    float depthDiff = max(linearDepth - currentDepth, 0.0);
    
    // Beer's Law for color absorption
    float depthScale = material_waterDepthScale == 0.0 ? 0.3 : material_waterDepthScale;
    float absorption = exp(-depthDiff * depthScale);
    
    vec3 deepCol = material_waterColorDeep == vec4(0.0) ? vec3(0.02, 0.1, 0.2) : material_waterColorDeep.rgb;
    vec3 shallowCol = material_waterColorShallow == vec4(0.0) ? vec3(0.4, 0.8, 0.7) : material_waterColorShallow.rgb;
    vec3 waterTint = mix(deepCol, shallowCol, absorption);

    // 2. Refraction & Distortion
    float distortionStr = material_waveStrength == 0.0 ? 0.02 : material_waveStrength * 0.01;
    vec2 refractUV = screenUV + worldNormal.xz * distortionStr;
    // Safety check for depth to prevent "bleeding" over foreground objects
    if (LinearizeDepth(texture(sceneDepthMap, refractUV).r) < currentDepth) refractUV = screenUV;
    vec3 refractedColor = texture(refractionMap, refractUV).rgb;
    
    // 3. Fresnel & Reflection
    float fresnel = 0.15 + 0.85 * pow(1.0 - max(dot(worldNormal, viewDir), 0.0), 3.0);
    vec2 reflectUV = screenUV + worldNormal.xz * 0.03;
    vec3 reflectedColor = texture(reflectionMap, reflectUV).rgb;
    
    vec3 baseWater = mix(refractedColor * waterTint, reflectedColor, fresnel * 0.85);

    // 4. Scrolling Caustics (Projected in World Space)
    vec2 causticUV = FragPos.xz * 0.1 + vec2(0.0, -time * 0.1);
    vec3 caustics = texture(material_causticsMap, causticUV).rgb;
    caustics *= absorption; // Fade with depth
    baseWater += caustics * 0.15 * max(0.0, directionalLight.direction.y);

    // 5. Shoreline Foam & Softening
    float foamDist = material_foamDistance == 0.0 ? 0.5 : material_foamDistance;
    float foamNoise = texture(material_dudvMap, TexCoord * 4.0 + vec2(0.0, -time * 0.2)).r;
    float foamAlpha = smoothstep(foamDist, 0.0, depthDiff) * smoothstep(0.4, 0.6, foamNoise);
    vec3 foamCol = material_foamColor == vec4(0.0) ? vec3(1.0) : material_foamColor.rgb;
    baseWater = mix(baseWater, foamCol, foamAlpha * 0.6);

    // 6. Final Lighting
    vec3 lighting = CalcLight(directionalLight.base, directionalLight.direction, worldNormal, viewDir);
    
    // Subsurface Scattering (Light bleed at shallow angles toward sun)
    float sss = pow(max(0.0, dot(viewDir, -directionalLight.direction)), 12.0) * absorption;
    lighting += material_waterColorShallow.rgb * sss * 0.5;

    for(int i=0; i<pointLightCount; i++) lighting += CalcLight(pointLights[i].base, normalize(pointLights[i].position - FragPos), worldNormal, viewDir);
    
    // 7. Transparency (Shoreline Softening)
    float edgeSoftening = smoothstep(0.0, 0.1, depthDiff); // Fade to 0 alpha at very shallow depths
    
    vec3 finalColor = baseWater + lighting * 0.15;
    
    // Selection highlight
    float selected = max(selectionTint, vIsSelected > 0.5 ? 1.0 : 0.0);
    finalColor += vec3(0.5, 0.4, 0.0) * selected;

    colour = vec4(finalColor, edgeSoftening);
}
