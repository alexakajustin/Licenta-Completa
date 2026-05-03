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

// ============================================================
// DuDv-based normal perturbation (from reference water repo)
// This gives micro-detail ripples entirely in fragment shader
// ============================================================
vec3 GetWaterNormal(vec2 uv) {
    float scaleFactor = max(vObjectScale / 100.0, 0.01);
    // Increase default tiling massively so ripples are crisp and small instead of huge and blurry
    float dudvTiling = material_dudvTiling == 0.0 ? 60.0 * scaleFactor : material_dudvTiling;
    float moveSpeed = material_waveSpeed == 0.0 ? 0.75 : material_waveSpeed;
    
    // Layer 1: Fast, small ripples
    vec2 uv1 = uv * dudvTiling;
    vec2 move1 = vec2(time * moveSpeed * 0.03, time * moveSpeed * 0.015);
    vec2 dist1 = texture(material_dudvMap, uv1 + move1).rg * 0.1;
    vec2 finalUV1 = uv1 + dist1 + vec2(dist1.x, dist1.y + move1.y);
    vec4 n1Col = texture(material_waterNormalMap, finalUV1);
    vec3 normal1 = vec3(n1Col.r * 2.0 - 1.0, n1Col.b * 3.0, n1Col.g * 2.0 - 1.0);
    
    // Layer 2: Slow, large waves panning opposite direction
    vec2 uv2 = uv * dudvTiling * 0.5;
    vec2 move2 = vec2(-time * moveSpeed * 0.015, -time * moveSpeed * 0.02);
    vec2 dist2 = texture(material_dudvMap, uv2 + move2).rg * 0.1;
    vec2 finalUV2 = uv2 + dist2 + vec2(dist2.x, dist2.y + move2.y);
    vec4 n2Col = texture(material_waterNormalMap, finalUV2);
    vec3 normal2 = vec3(n2Col.r * 2.0 - 1.0, n2Col.b * 3.0, n2Col.g * 2.0 - 1.0);
    
    // Blend normals for dual-layer butter smooth look
    vec3 rippleNormal = normalize(normal1 + normal2);
    return rippleNormal;
}

vec2 GetDuDvDistortion(vec2 uv) {
    float scaleFactor = max(vObjectScale / 100.0, 0.01);
    float dudvTiling = material_dudvTiling == 0.0 ? 60.0 * scaleFactor : material_dudvTiling;
    float dudvStrength = material_dudvStrength == 0.0 ? 0.02 : material_dudvStrength;
    float moveSpeed = material_waveSpeed == 0.0 ? 0.75 : material_waveSpeed;
    float moveFactor = time * moveSpeed * 0.03;
    
    // Dual layer distortion
    vec2 uv1 = uv * dudvTiling;
    vec2 dist1 = texture(material_dudvMap, vec2(uv1.x + moveFactor, uv1.y)).rg * 0.1;
    vec2 finalUV1 = uv1 + vec2(dist1.x, dist1.y + moveFactor);
    vec2 totalDistortion = (texture(material_dudvMap, finalUV1).rg * 2.0 - 1.0) * dudvStrength;
    
    vec2 uv2 = uv * dudvTiling * 0.5;
    vec2 finalUV2 = uv2 + vec2(-moveFactor * 0.5, -moveFactor * 0.6);
    totalDistortion += (texture(material_dudvMap, finalUV2).rg * 2.0 - 1.0) * dudvStrength * 0.5;
    
    return totalDistortion;
}

// ============================================================
// Effective normal: blend Gerstner geometric normal + DuDv ripple normal
// ============================================================
vec3 GetEffectiveNormal()
{
    vec3 gerstnerNormal = normalize(Normal);
    
    // World-space TBN from the Gerstner wave normals
    vec3 N = normalize(NormalWorld);
    vec3 T = normalize(TangentWorld);
    vec3 B = normalize(BitangentWorld);
    T = normalize(T - dot(T, N) * N);
    B = normalize(B - dot(B, N) * N - dot(B, T) * T);
    mat3 TBN = mat3(T, B, N);
    
    // Get the ripple normal from DuDv + normal map
    vec3 rippleNormal = GetWaterNormal(TexCoord);
    
    // Transform ripple into world space and blend with Gerstner normal
    vec3 worldRipple = normalize(TBN * rippleNormal);
    
    return worldRipple;
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
    // Water color via Fresnel
    // ============================================================
    // ============================================================
    // Water color via Depth & Fresnel
    // ============================================================
    vec4 deepColor = material_waterColorDeep == vec4(0.0) ? vec4(0.01, 0.1, 0.25, 0.98) : material_waterColorDeep;
    vec4 shallowColor = material_waterColorShallow == vec4(0.0) ? vec4(0.1, 0.5, 0.6, 0.6) : material_waterColorShallow;
    float fresnelPower = material_fresnelPower == 0.0 ? 5.0 : material_fresnelPower;

    vec2 screenUV = gl_FragCoord.xy / screenSize;
    float backgroundDepth = texture(sceneDepthMap, screenUV).r;
    float linearBackgroundDepth = LinearizeDepth(backgroundDepth);
    float linearFragmentDepth = LinearizeDepth(gl_FragCoord.z);
    float depthDiff = max(linearBackgroundDepth - linearFragmentDepth, 0.0);

    vec3 viewDir = normalize(eyePosition - FragPos);
    vec3 effectiveNormal = GetEffectiveNormal();

    // Fresnel: glancing angles are more reflective
    float fresnelFactor = max(dot(viewDir, effectiveNormal), 0.0);
    fresnelFactor = pow(1.0 - fresnelFactor, fresnelPower);
    fresnelFactor = clamp(fresnelFactor, 0.0, 1.0);
    
    // Beer's Law for natural exponential depth absorption
    float depthColorScale = 0.15; // Extinction coefficient
    float depthFactor = exp(-max(depthDiff, 0.0) * depthColorScale);
    // depthFactor is 1.0 at surface (shallow), approaching 0.0 at depth
    vec3 waterBaseColor = mix(deepColor.rgb, shallowColor.rgb, depthFactor);
    float waterAlpha = mix(deepColor.a, shallowColor.a, depthFactor);

    // ============================================================
    // Planar Reflections & Refraction Distortion
    // ============================================================
    vec2 ndc = (clipSpaceCoords.xy / clipSpaceCoords.w) / 2.0 + 0.5;
    vec2 distortion = GetDuDvDistortion(TexCoord);
    
    vec2 reflectTexCoords = ndc + distortion;
    reflectTexCoords = clamp(reflectTexCoords, 0.001, 0.999);
    vec3 reflectionColor = texture(reflectionMap, reflectTexCoords).rgb;
    
    // Blend base color with reflection
    vec3 waterColor = mix(waterBaseColor, reflectionColor, fresnelFactor);
    // Add a bit of the shallow/deep tint to the reflection for character
    waterColor = mix(waterColor, mix(shallowColor.rgb, deepColor.rgb, 0.5), fresnelFactor * 0.2);

    vec4 finalBaseColor = vec4(waterColor, waterAlpha);

    // ============================================================
    // Foam Intersection (Animated & Layered)
    // ============================================================
    vec4 foamColor = material_foamColor == vec4(0.0) ? vec4(0.9, 0.95, 1.0, 0.9) : material_foamColor;
    float autoFoamScaleFactor = max(vObjectScale / 100.0, 0.01);
    float foamDist = material_foamDistance == 0.0 ? 1.5 * autoFoamScaleFactor : material_foamDistance;
    
    if (depthDiff < foamDist) {
        float foamFade = smoothstep(0.0, foamDist, depthDiff);
        float foamFactor = 1.0 - foamFade;
        
        if (foamFactor > 0.0) {
            // Dynamic foam noise
            vec2 foamUV1 = TexCoord * 3.0 + vec2(time * 0.03, time * 0.015);
            vec2 foamUV2 = TexCoord * 2.0 + vec2(-time * 0.02, time * 0.01);
            float noise1 = texture(material_dudvMap, foamUV1).r;
            float noise2 = texture(material_dudvMap, foamUV2).g;
            float combinedNoise = (noise1 + noise2) * 0.5;
            
            // Organic cutoff for foam instead of linear fade
            float foamMask = smoothstep(0.3, 0.7, foamFactor * combinedNoise);
            
            finalBaseColor.rgb = mix(finalBaseColor.rgb, foamColor.rgb, foamMask * 0.85);
            finalBaseColor.a = max(finalBaseColor.a, foamMask * 0.95);
        }
    }

    // ============================================================
    // Lighting & Specular Shimmer
    // ============================================================
	vec3 finalLight = CalcDirectionalLight(finalBaseColor.rgb);
	finalLight += CalcPointLights(finalBaseColor.rgb);
	finalLight += CalcSpotLights(finalBaseColor.rgb);

    // GGX-style multi-lobe specular shimmer
    vec3 lightDir = normalize(directionalLight.direction);
    vec3 halfVector = normalize(viewDir - lightDir);
    float NdotH = max(dot(effectiveNormal, halfVector), 0.0);
    
    // Core highlight
    float specCore = pow(NdotH, 1024.0);
    // Wide dispersion
    float specWide = pow(NdotH, 128.0) * 0.25;
    
    float shimmerFactor = specCore + specWide;
    
    // Dynamic sub-pixel sparkle
    float sparkleNoise = random(FragPos * 10.0 + vec3(time * 0.1), 0);
    float sparkle = smoothstep(0.8, 1.0, sparkleNoise) * shimmerFactor * 2.0;
    
    finalLight += directionalLight.base.colour * (shimmerFactor + sparkle);

	vec3 finalColor = finalLight;

    // Edge alpha fade - Very tight fade to fix river merging
    // River surfaces are very close to lake surfaces, so we only fade the extreme edge (0.15 units)
    float edgeAlpha = smoothstep(0.0, 0.15, depthDiff);
    float finalAlpha = finalBaseColor.a * edgeAlpha;

	float selectedVal = max(selectionTint, vIsSelected > 0.5 ? 1.0 : 0.0);
	if (selectedVal > 0.0) {
		finalColor += vec3(0.35, 0.25, 0.0) * selectedVal;
	}

	colour = vec4(finalColor, finalAlpha);
}
