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

uniform mat4 directionalLightTransform[4];
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
uniform sampler2D material_causticsMap;
uniform float material_dudvTiling;
uniform float material_dudvStrength;
uniform float material_waveSpeed;

// Foam & Refraction
uniform sampler2D sceneDepthMap;
uniform sampler2D reflectionMap;
uniform sampler2D refractionMap;
uniform vec2 screenSize;
uniform vec4 material_foamColor;
uniform float material_foamDistance;
uniform float material_foamOpacity;
uniform float material_waterDepthScale;

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
    float dudvTiling = material_dudvTiling == 0.0 ? 25.0 * scaleFactor : material_dudvTiling;
    float moveSpeed = material_waveSpeed == 0.0 ? 0.08 : material_waveSpeed;
    
    // 1. Strict Flow Vector (UV.y is forward along the river ribbon)
    vec2 flowVector = vec2(0.0, -1.0); 
    
    // 2. Dual-Layer Ping-Ponging (Valve style flow)
    float cycleTime = 1.0; 
    float t = time * moveSpeed;
    
    float phase0 = fract(t / cycleTime);
    float phase1 = fract((t / cycleTime) + 0.5);
    
    // Blend factor: 0 at start/end of phase, 1 in middle
    float lerpFactor = abs((phase0 - 0.5) * 2.0); 
    
    // Layer 0
    vec2 uv0 = uv * dudvTiling + (flowVector * phase0);
    vec4 n0 = texture(material_waterNormalMap, uv0);
    vec3 normal0 = vec3(n0.r * 2.0 - 1.0, n0.b * 3.0, n0.g * 2.0 - 1.0);
    
    // Layer 1 (Offset phase)
    vec2 uv1 = uv * dudvTiling + (flowVector * phase1);
    vec4 n1 = texture(material_waterNormalMap, uv1);
    vec3 normal1 = vec3(n1.r * 2.0 - 1.0, n1.b * 3.0, n1.g * 2.0 - 1.0);
    
    // Blend the two layers using the oscillating factor
    vec3 rippleNormal = normalize(mix(normal0, normal1, lerpFactor));
    
    // Add micro-ripples that strictly pan ALONG the flow (no X drift)
    vec2 microUV = uv * dudvTiling * 2.5 + vec2(0.0, -time * 0.15);
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
	
	vec4 fragPosLightSpace = directionalLightTransform[layer] * vec4(worldPosWithOffset, 1.0);
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
    // Refraction & Volumetric Depth (Beer's Law)
    // ============================================================
    vec4 deepColor = material_waterColorDeep == vec4(0.0) ? vec4(0.01, 0.1, 0.25, 0.98) : material_waterColorDeep;
    vec4 shallowColor = material_waterColorShallow == vec4(0.0) ? vec4(0.1, 0.5, 0.6, 0.6) : material_waterColorShallow;
    
    vec2 screenUV = gl_FragCoord.xy / screenSize;
    float backgroundDepth = texture(sceneDepthMap, screenUV).r;
    float linearBackgroundDepth = LinearizeDepth(backgroundDepth);
    float linearFragmentDepth = LinearizeDepth(gl_FragCoord.z);
    float depthDiff = max(linearBackgroundDepth - linearFragmentDepth, 0.0);

    vec3 viewDir = normalize(eyePosition - FragPos);
    vec3 effectiveNormal = GetEffectiveNormal();
    vec3 waterNormal = GetWaterNormal(TexCoord); // Detailed ripples

    // 1. Refraction with distortion
    vec2 refractionDistort = waterNormal.xz * 0.015;
    vec2 refractionUV = screenUV + refractionDistort;
    // Check if the distorted sample is actually BEHIND the water surface
    float refractedDepth = texture(sceneDepthMap, refractionUV).r;
    if (LinearizeDepth(refractedDepth) < linearFragmentDepth) {
        refractionUV = screenUV; // Fallback to avoid sampling foreground objects
    }
    vec3 refractedColor = texture(refractionMap, refractionUV).rgb;

    // 2. Beer's Law for volumetric absorption
    float depthColorScale = material_waterDepthScale == 0.0 ? 0.35 : material_waterDepthScale;
    float depthFactor = exp(-max(depthDiff, 0.0) * depthColorScale);
    
    // Mix refracted scene with water tint
    vec3 refractionFinal = mix(deepColor.rgb, refractedColor * shallowColor.rgb * 1.5, depthFactor);
    float waterAlpha = mix(deepColor.a, 0.4, depthFactor);

    // ============================================================
    // Soft Shoreline & Flow-based Foam
    // ============================================================
    float moveSpeed = material_waveSpeed == 0.0 ? 0.12 : material_waveSpeed;
    float flowTime = time * moveSpeed;
    float tiling = material_dudvTiling == 0.0 ? 6.0 : material_dudvTiling;

    // Local scrolling foam (Bitangent/V-axis)
    vec2 foamUV = TexCoord * tiling + vec2(0.0, -flowTime * 1.5);
    float foamNoise = texture(material_dudvMap, foamUV).r;
    
    // Shoreline "Lapping" Foam
    float shorelineFactor = 1.0 - smoothstep(0.0, 0.4, depthDiff);
    float foamShore = smoothstep(0.4, 0.7, foamNoise * shorelineFactor);
    
    // High-velocity/Slope Churn (using bitangent Y component for "white water" on rapids)
    float rapidFactor = clamp(abs(BitangentWorld.y) * 2.0, 0.0, 1.0);
    float foamRapids = smoothstep(0.3, 0.6, foamNoise * rapidFactor);
    
    float totalFoam = clamp(foamShore + foamRapids, 0.0, 1.0);
    vec3 foamCol = material_foamColor == vec4(0.0) ? vec3(0.95, 0.98, 1.0) : material_foamColor.rgb;
    
    vec3 waterBaseColor = mix(refractionFinal, foamCol, totalFoam * 0.8);

    // 3. Flow-aligned Caustics (Following the river path)
    // We use TexCoord because it follows the river ribbon (V is along the flow)
    vec2 causticUV = vec2(TexCoord.x * 2.0, TexCoord.y * 12.0) + vec2(0.0, -flowTime * 3.0);
    vec3 causticCol = texture(material_causticsMap, causticUV).rgb;
    causticCol += texture(material_causticsMap, causticUV * 0.7 + vec2(0.15, 0.15)).rgb;
    waterBaseColor += causticCol * 0.25 * (1.0 - totalFoam); // Fades under foam

    float finalAlpha = max(waterAlpha, totalFoam * 0.9) * smoothstep(0.0, 0.05, depthDiff);

    // ============================================================
    // Soft Planar Reflections (Fresnel)
    // ============================================================
    float fresnelFactor = max(dot(viewDir, effectiveNormal), 0.0);
    fresnelFactor = pow(1.0 - fresnelFactor, 4.0);
    fresnelFactor = clamp(fresnelFactor, 0.0, 0.6);
    
    vec2 reflectUV = clamp(screenUV + waterNormal.xz * 0.01, 0.001, 0.999);
    vec3 reflectionColor = texture(reflectionMap, reflectUV).rgb;
    vec3 finalWaterColor = mix(waterBaseColor, reflectionColor, fresnelFactor);

    // ============================================================
    // Lighting & Cinematic Specular
    // ============================================================
	vec3 finalLight = CalcDirectionalLight(finalWaterColor);
	finalLight += CalcPointLights(finalWaterColor);
	finalLight += CalcSpotLights(finalWaterColor);

    // Sun Highlight
    vec3 lightDir = normalize(directionalLight.direction);
    vec3 halfVector = normalize(viewDir - lightDir);
    float NdotH = max(dot(effectiveNormal, halfVector), 0.0);
    finalLight += directionalLight.base.colour * pow(NdotH, 128.0) * 0.5;

	float selectedVal = max(selectionTint, vIsSelected > 0.5 ? 1.0 : 0.0);
	if (selectedVal > 0.0) finalLight += vec3(0.35, 0.25, 0.0) * selectedVal;

	colour = vec4(finalLight, finalAlpha);
}
