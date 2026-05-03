#version 330				

in vec4 vertex_color;	
in vec2 TexCoord;
in vec3 Normal;
in vec3 FragPos;

in vec3 TangentWorld;
in vec3 BitangentWorld;
in vec3 NormalWorld;

in vec3 LocalPos;
in vec4 clipSpaceCoords;

out vec4 colour;	
in float vIsSelected;
in float vFadeFactor;
in float vObjectScale;

const int MAX_POINT_LIGHTS = 3;
const int MAX_SPOT_LIGHTS = 3;

struct Light {
	vec3 colour;
	float ambientIntensity;
	float diffuseIntensity;
};

struct DirectionalLight {
	Light base;
	vec3 direction;
};

struct PointLight
{
	Light base;
	vec3 position;
	float constant;
	float linear;
	float exponent;
};

struct SpotLight
{
	PointLight base;
	vec3 direction;
	float edge;
};

struct Material {
	 float specularIntensity;
	 float shininess;
	 float sssScale;
	 float sssDistortion;
	 vec4 baseColor;
	 vec2 tiling;
	 vec2 offset;
};

struct OmniShadowMap
{
	samplerCube shadowMap;
	samplerCube shadowColorMap;
	float farPlane;
};

uniform int pointLightCount;
uniform int spotLightCount;

uniform DirectionalLight directionalLight;
uniform PointLight pointLights[MAX_POINT_LIGHTS];
uniform SpotLight spotLights[MAX_SPOT_LIGHTS];

uniform sampler2D normalMap;
uniform int useNormalMap;
uniform sampler2DArray directionalShadowMap;
uniform sampler2DArray directionalShadowColorMap;
uniform OmniShadowMap omniShadowMaps[MAX_POINT_LIGHTS + MAX_SPOT_LIGHTS];

uniform mat4 dirLightMatrices[3];
uniform float cascadeSplits[3];
uniform mat4 viewMatrix;

uniform Material material;

uniform vec3 eyePosition;
uniform float selectionTint;
uniform float time;

// Water-specific uniforms
uniform vec4 material_waterColorDeep;
uniform vec4 material_waterColorShallow;
uniform float material_fresnelPower;
uniform float material_specularIntensityOverride;
uniform float material_shininessOverride;

// DuDv distortion (from reference repo)
uniform sampler2D material_dudvMap;
uniform sampler2D material_waterNormalMap;
uniform float material_dudvTiling;
uniform float material_dudvStrength;
uniform float material_waveSpeed;

// Foam
uniform sampler2D sceneDepthMap;
uniform sampler2D reflectionMap;
uniform vec2 screenSize;
uniform vec4 material_foamColor;
uniform float material_foamDistance;

vec3 sampleOffsetDirections[20] = vec3[]
(
   vec3( 1,  1,  1), vec3( 1, -1,  1), vec3(-1, -1,  1), vec3(-1,  1,  1), 
   vec3( 1,  1, -1), vec3( 1, -1, -1), vec3(-1, -1, -1), vec3(-1,  1, -1),
   vec3( 1,  1,  0), vec3( 1, -1,  0), vec3(-1, -1,  0), vec3(-1,  1,  0),
   vec3( 1,  0,  1), vec3(-1,  0,  1), vec3( 1,  0, -1), vec3(-1,  0, -1),
   vec3( 0,  1,  1), vec3( 0, -1,  1), vec3( 0, -1, -1), vec3( 0,  1, -1)
);

vec2 poissonDisk[16] = vec2[](
   vec2( -0.94201624, -0.39906216 ), vec2( 0.94558609, -0.76890725 ),
   vec2( -0.094184101, -0.92938870 ), vec2( 0.34495938, 0.29387760 ),
   vec2( -0.91588581, 0.45771432 ), vec2( -0.81544232, -0.87912464 ),
   vec2( -0.38277543, 0.27676845 ), vec2( 0.97484398, 0.75648379 ),
   vec2( 0.44323325, -0.97511554 ), vec2( 0.53742981, -0.47373420 ),
   vec2( -0.65476012, -0.051443273 ), vec2( -0.43701662, -0.81599235 ),
   vec2( 0.60288572, 0.44872051 ), vec2( -0.21151840, -0.36960612 ),
   vec2( -0.54245971, 0.48821213 ), vec2( -0.34613928, -0.64444749 )
);

float random(vec3 seed, int i){
	vec4 seed4 = vec4(seed,i);
	float dot_product = dot(seed4, vec4(12.9898,78.233,45.164,94.673));
	return fract(sin(dot_product) * 43758.5453);
}

// High-fidelity Flow Distortion (Valve/Portal 2 Style)
// This technique uses two layers of UVs that pan and reset periodically.
// By blending between them, we avoid 'texture stretching' and 'straight lines'.
vec3 GetWaterNormal(vec2 uv) {
    float scaleFactor = max(vObjectScale / 100.0, 0.01);
    float dudvTiling = material_dudvTiling == 0.0 ? 40.0 * scaleFactor : material_dudvTiling;
    float moveSpeed = material_waveSpeed == 0.0 ? 0.08 : material_waveSpeed;
    
    // 1. Procedural Flow Vector (UV.y is forward)
    // We add a bit of 'wobble' to the flow direction using a noise texture
    vec2 noiseUV = uv * (dudvTiling * 0.1) + vec2(time * 0.02, time * 0.01);
    vec2 flowDrift = (texture(material_dudvMap, noiseUV).rg * 2.0 - 1.0) * 0.15;
    vec2 flowVector = vec2(flowDrift.x, 1.0 + flowDrift.y); // Mostly forward (+V)
    
    // 2. Dual-Layer Ping-Ponging
    float cycleTime = 2.0; // Time for one full reset
    float t = time * moveSpeed;
    
    float phase0 = fract(t / cycleTime);
    float phase1 = fract((t / cycleTime) + 0.5);
    
    // Blend factor: 0 at start/end of phase, 1 in middle
    float lerpFactor = abs((phase0 - 0.5) * 2.0); 
    
    // Layer 0
    vec2 uv0 = uv * dudvTiling + (flowVector * phase0 * 0.5);
    vec4 n0 = texture(material_waterNormalMap, uv0);
    vec3 normal0 = vec3(n0.r * 2.0 - 1.0, n0.b * 3.0, n0.g * 2.0 - 1.0);
    
    // Layer 1 (Offset phase)
    vec2 uv1 = uv * dudvTiling + (flowVector * phase1 * 0.5);
    vec4 n1 = texture(material_waterNormalMap, uv1);
    vec3 normal1 = vec3(n1.r * 2.0 - 1.0, n1.b * 3.0, n1.g * 2.0 - 1.0);
    
    // Blend the two layers using the oscillating factor
    vec3 rippleNormal = normalize(mix(normal0, normal1, lerpFactor));
    
    // Add a third layer of micro-ripples that pans constantly to break up patterns
    vec2 microUV = uv * dudvTiling * 2.5 + vec2(time * 0.05, -time * 0.2);
    vec4 nMicro = texture(material_waterNormalMap, microUV);
    vec3 normalMicro = vec3(nMicro.r * 2.0 - 1.0, nMicro.b * 2.0, nMicro.g * 2.0 - 1.0) * 0.4;
    
    return normalize(rippleNormal + normalMicro);
}

vec2 GetDuDvDistortion(vec2 uv) {
    float moveSpeed = material_waveSpeed == 0.0 ? 0.08 : material_waveSpeed;
    float dudvStrength = material_dudvStrength == 0.0 ? 0.02 : material_dudvStrength;
    
    // Use the same dual-layer logic for distortion to match normal mapping
    float t = time * moveSpeed;
    float phase0 = fract(t / 2.0);
    vec2 uv0 = uv * 30.0 + vec2(0.0, phase0 * 0.5);
    vec2 dist = (texture(material_dudvMap, uv0).rg * 2.0 - 1.0) * dudvStrength;
    
    return dist;
}

// ============================================================
// Effective normal: World-space TBN mapping for flow ripples
// ============================================================
vec3 GetEffectiveNormal()
{
    // World-space TBN from the mesh normals (Clean & static)
    vec3 N = normalize(NormalWorld);
    vec3 T = normalize(TangentWorld);
    vec3 B = normalize(BitangentWorld);
    mat3 TBN = mat3(T, B, N);
    
    // Get the high-fidelity flow normal (all the movement is here now!)
    vec3 rippleNormal = GetWaterNormal(TexCoord);
    
    // Transform ripple into world space
    return normalize(TBN * rippleNormal);
}

float GetShadowFactorAtLayer(int layer, vec3 normal, vec3 lightDir)
{
	float offsetScale = 0.2 * (layer + 1); 
	vec3 worldPosWithOffset = FragPos + normal * (offsetScale * (1.0 - dot(normal, -lightDir)));
	
	vec4 fragPosLightSpace = dirLightMatrices[layer] * vec4(worldPosWithOffset, 1.0);
	vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
	projCoords = (projCoords * 0.5) + 0.5;
	
	if(projCoords.z > 1.0) return 0.0;
	
	float current = projCoords.z;
	float bias = max(0.0005 * (1.0 - dot(normal, -lightDir)), 0.0001);
	
	float shadow = 0.0;
	vec2 texSize = vec2(textureSize(directionalShadowMap, 0).xy);
	vec2 texelSize = 1.0 / texSize;
	
	float angle = random(FragPos, 0) * 6.283185;
	float s = sin(angle);
	float c = cos(angle);
	mat2 rot = mat2(c, s, -s, c);

	for(int i = 0; i < 16; i++)
	{
		vec2 offset = rot * poissonDisk[i] * texelSize * 0.8;
		vec3 samplePos = vec3(projCoords.xy + offset, layer);
		float pcfDepth = texture(directionalShadowMap, samplePos).r;
		float occluderAlpha = texture(directionalShadowColorMap, samplePos).r;
		
		if (current - bias > pcfDepth) {
			shadow += 1.0 * occluderAlpha;
		}
	}
	shadow /= 16.0;

	if (layer == 2) {
		float fadeMargin = 0.1;
		float edgeFade = 1.0;
		edgeFade = min(edgeFade, smoothstep(0.0, fadeMargin, projCoords.x));
		edgeFade = min(edgeFade, smoothstep(0.0, fadeMargin, 1.0 - projCoords.x));
		edgeFade = min(edgeFade, smoothstep(0.0, fadeMargin, projCoords.y));
		edgeFade = min(edgeFade, smoothstep(0.0, fadeMargin, 1.0 - projCoords.y));
		shadow *= edgeFade;
	}

	return shadow;
}

float CalcDirectionalShadowFactor(DirectionalLight light)
{
	vec4 fragPosViewSpace = viewMatrix * vec4(FragPos, 1.0);
	float depth = abs(fragPosViewSpace.z);
	vec3 normal = GetEffectiveNormal();
	vec3 lightDir = normalize(directionalLight.direction);

	int layer = -1;
	for (int i = 0; i < 3; i++) {
		if (depth < cascadeSplits[i]) {
			layer = i;
			break;
		}
	}
	if (layer == -1) layer = 2;

	float shadow = GetShadowFactorAtLayer(layer, normal, lightDir);

	float blendThreshold = 5.0;
	if (layer < 2) {
		float splitDist = cascadeSplits[layer];
		if (depth > splitDist - blendThreshold) {
			float blendFactor = (depth - (splitDist - blendThreshold)) / blendThreshold;
			float nextShadow = GetShadowFactorAtLayer(layer + 1, normal, lightDir);
			shadow = mix(shadow, nextShadow, blendFactor);
		}
	}
	return shadow;
}

float CalcOmniShadowFactor(PointLight light, int shadowIndex)
{
	vec3 fragToLight = FragPos - light.position;
	float currentDepth = length(fragToLight);
	float shadow = 0.0;
	float bias = 0.05;
	int samples = 20;
	
	float viewDistance = length(eyePosition - FragPos);
	float diskRadius = (1.0 + (viewDistance / omniShadowMaps[shadowIndex].farPlane)) / 75.0; 

	for(int i = 0; i < samples; i++)
	{
		vec3 samplePos = fragToLight + sampleOffsetDirections[i] * diskRadius;
		float closestDepth = texture(omniShadowMaps[shadowIndex].shadowMap, samplePos).r;
		closestDepth *= omniShadowMaps[shadowIndex].farPlane;

		if(currentDepth - bias > closestDepth) 
		{
			float occluderAlpha = texture(omniShadowMaps[shadowIndex].shadowColorMap, samplePos).r;
			shadow += 1.0 * occluderAlpha;
		}
	}
	shadow /= float(samples);
	return shadow;
}

vec3 CalcLightByDirection(Light light, vec3 direction, float shadowFactor, vec3 baseColor) 
{
	vec3 effectiveNormal = GetEffectiveNormal();

	vec3 ambientColour = light.colour * light.ambientIntensity;
	float diffuseFactor = max(dot(effectiveNormal, normalize(-direction)), 0.0f);
	vec3 diffuseColor = light.colour * light.diffuseIntensity * diffuseFactor;
	vec3 specularColour = vec3(0, 0, 0);

	if(diffuseFactor > 0.0f)
	{
		vec3 fragToEye = normalize(eyePosition - FragPos);
		vec3 halfwayDir = normalize(fragToEye + normalize(-direction));
		float specularFactor = max(dot(effectiveNormal, halfwayDir), 0.0);

		if(specularFactor > 0.0f) 
		{
            float specPower = material_shininessOverride == 0.0 ? 256.0 : material_shininessOverride;
            float specIntens = material_specularIntensityOverride == 0.0 ? 3.0 : material_specularIntensityOverride;
			specularFactor = pow(specularFactor, specPower);
			specularColour = light.colour * specIntens * specularFactor * light.diffuseIntensity;
		}
	}

	vec3 diffuseAmbient = baseColor * (ambientColour + (1.0 - shadowFactor) * diffuseColor);
	vec3 finalSpecular = (1.0 - shadowFactor) * specularColour;

	return diffuseAmbient + finalSpecular;
}

vec3 CalcDirectionalLight(vec3 baseColor) 
{
	float shadowFactor = CalcDirectionalShadowFactor(directionalLight);
	return CalcLightByDirection(directionalLight.base, directionalLight.direction, shadowFactor, baseColor);
}

vec3 CalcPointLight(PointLight pLight, int shadowIndex, vec3 baseColor) 
{
	vec3 direction = FragPos - pLight.position;
	float distance = length(direction);
	direction = normalize(direction);

	float shadowFactor = CalcOmniShadowFactor(pLight, shadowIndex);
	vec3 lightFinal = CalcLightByDirection(pLight.base, direction, shadowFactor, baseColor);
	float attenuation = pLight.exponent * distance * distance + pLight.linear * distance + pLight.constant;

	return (lightFinal / attenuation);
}

vec3 CalcPointLights(vec3 baseColor) 
{
	vec3 totalColour = vec3(0, 0, 0);
	for(int i = 0; i < pointLightCount; i++) 
	{
		totalColour += CalcPointLight(pointLights[i], i, baseColor);
	}
	return totalColour;
}

vec3 CalcSpotLight(SpotLight sLight, int shadowIndex, vec3 baseColor) 
{
	vec3 rayDirection = normalize(FragPos - sLight.base.position);
	float slFactor = dot(rayDirection, sLight.direction);

	if(slFactor > sLight.edge) 
	{
		vec3 lightFinal = CalcPointLight(sLight.base, shadowIndex, baseColor);
		return lightFinal * (1.0f - (1.0f - slFactor) * (1.0f / (1.0f - sLight.edge)));
	} 
	else 
	{	
		return vec3(0, 0, 0);
	}
}

vec3 CalcSpotLights(vec3 baseColor) 
{
	vec3 totalColour = vec3(0, 0, 0);
	for(int i = 0; i < spotLightCount; i++) 
	{
		totalColour += CalcSpotLight(spotLights[i], i + pointLightCount, baseColor);
	}
	return totalColour;
}

float LinearizeDepth(float depth) 
{
    float near = 0.1; 
    float far  = 2000.0; 
    float z = depth * 2.0 - 1.0; 
    return (2.0 * near * far) / (far + near - z * (far - near));	
}

void main()								         
{
	if (vFadeFactor > 0.001) {
		int x = int(mod(gl_FragCoord.x, 4.0));
		int y = int(mod(gl_FragCoord.y, 4.0));
		float bayerMatrix[16] = float[16](
			 0.0/16.0,  8.0/16.0,  2.0/16.0, 10.0/16.0,
			12.0/16.0,  4.0/16.0, 14.0/16.0,  6.0/16.0,
			 3.0/16.0, 11.0/16.0,  1.0/16.0,  9.0/16.0,
			15.0/16.0,  7.0/16.0, 13.0/16.0,  5.0/16.0
		);
		float threshold = bayerMatrix[y * 4 + x];
		if (vFadeFactor > threshold) discard;
	}

    // ============================================================
    // Water color via Depth & Fresnel
    // ============================================================
    vec4 deepColor = material_waterColorDeep == vec4(0.0) ? vec4(0.01, 0.1, 0.25, 0.98) : material_waterColorDeep;
    vec4 shallowColor = material_waterColorShallow == vec4(0.0) ? vec4(0.1, 0.5, 0.6, 0.6) : material_waterColorShallow;
    float fresnelPower = material_fresnelPower == 0.0 ? 4.0 : material_fresnelPower;

    vec2 screenUV = gl_FragCoord.xy / screenSize;
    float backgroundDepth = texture(sceneDepthMap, screenUV).r;
    float linearBackgroundDepth = LinearizeDepth(backgroundDepth);
    float linearFragmentDepth = LinearizeDepth(gl_FragCoord.z);
    float depthDiff = max(linearBackgroundDepth - linearFragmentDepth, 0.0);

    vec3 viewDir = normalize(eyePosition - FragPos);
    vec3 effectiveNormal = GetEffectiveNormal();

    // Fresnel: glancing angles are more reflective, but we keep it subtle for cinematic rivers
    float fresnelFactor = max(dot(viewDir, effectiveNormal), 0.0);
    fresnelFactor = pow(1.0 - fresnelFactor, 4.0); // Softer fresnel
    fresnelFactor = clamp(fresnelFactor, 0.0, 0.7); // Cap it to avoid over-exposure
    
    // Beer's Law for natural volumetric depth absorption
    float depthColorScale = 0.35; // Higher extinction for "heavier" water
    float depthFactor = exp(-max(depthDiff, 0.0) * depthColorScale);
    vec3 waterBaseColor = mix(deepColor.rgb, shallowColor.rgb, depthFactor);
    float waterAlpha = mix(deepColor.a, 0.4, depthFactor); // More transparent in shallow areas

    // ============================================================
    // Cinematic Smooth Flow & "Milky" Aeration
    // ============================================================
    // Slow, organic movement
    float flowTime = time * 0.12;
    
    // 1. Local ripples (Panned on X axis to fix "sideways" scrolling)
    vec2 uv1 = TexCoord * 6.0 + vec2(-flowTime, 0.0);
    vec2 uv2 = TexCoord * 4.5 + vec2(-flowTime * 0.8, 0.1);
    float r1 = texture(material_dudvMap, uv1).r;
    float r2 = texture(material_dudvMap, uv2).g;
    float rippleChurn = smoothstep(0.5, 0.85, (r1 + r2) * 0.5);

    // 2. Large-scale world-space aeration (Panned on World-X to match river orientation)
    vec2 worldUV = FragPos.xz * 0.04 + vec2(-flowTime * 0.5, 0.0);
    float w1 = texture(material_dudvMap, worldUV).r;
    float w2 = texture(material_dudvMap, worldUV * 0.5 + vec2(0.2)).g;
    float worldChurn = smoothstep(0.4, 0.7, (w1 + w2) * 0.5);
    
    // Combine detail and large-scale milkiness
    float churn = max(rippleChurn * 0.6, worldChurn);
    churn *= smoothstep(0.0, 0.8, depthDiff); // Soft fade at shorelines
    
    vec3 foamColor = vec3(0.92, 0.96, 1.0);
    vec4 finalBaseColor = vec4(mix(waterBaseColor, foamColor, churn * 0.5), max(waterAlpha, churn * 0.6));

    // ============================================================
    // Soft Planar Reflections
    // ============================================================
    vec3 waterNormal = GetWaterNormal(TexCoord);
    vec2 reflectionDistortion = waterNormal.xz * 0.015; // Subtle distortion
    
    vec2 reflectTexCoords = screenUV + reflectionDistortion;
    reflectTexCoords = clamp(reflectTexCoords, 0.001, 0.999);
    vec3 reflectionColor = texture(reflectionMap, reflectTexCoords).rgb;
    
    // Mix reflection with base color using soft fresnel
    float reflectionStrength = fresnelFactor * 0.5; // Cap reflection impact
    vec3 waterColor = mix(finalBaseColor.rgb, reflectionColor, reflectionStrength);

    // ============================================================
    // Smooth Lighting & Subdued Specular
    // ============================================================
	vec3 finalLight = CalcDirectionalLight(waterColor);
	finalLight += CalcPointLights(waterColor);
	finalLight += CalcSpotLights(waterColor);

    // Soft specular glint (Cinematic sheen instead of sharp glints)
    vec3 lightDir = normalize(directionalLight.direction);
    vec3 halfVector = normalize(viewDir - lightDir);
    float NdotH = max(dot(effectiveNormal, halfVector), 0.0);
    
    float specularSheen = pow(NdotH, 64.0) * 0.5; // Soft lobe
    finalLight += directionalLight.base.colour * specularSheen;

	vec3 finalColor = finalLight;

    // Edge alpha fade for seamless shoreline integration
    float edgeAlpha = smoothstep(0.0, 0.05, depthDiff);
    float finalAlpha = finalBaseColor.a * edgeAlpha;

	float selectedVal = max(selectionTint, vIsSelected > 0.5 ? 1.0 : 0.0);
	if (selectedVal > 0.0) finalColor += vec3(0.35, 0.25, 0.0) * selectedVal;

	colour = vec4(finalColor, finalAlpha);
}
