#version 400 core				

/**
 * @file shader.frag
 * @brief Primary fragment shader for drawing meshes with support for cascaded shadow mapping, PBR/Phong lighting, normal mapping, and multi-texture height/slope blending.
 */

in vec4 vertex_color;	
in vec2 TexCoord;
in vec3 Normal;
in vec3 FragPos;

// TBN vectors from vertex shader
in vec3 TangentWorld;
in vec3 BitangentWorld;
in vec3 NormalWorld;

// Object-space position for height-based layer blending
in vec3 LocalPos;

out vec4 colour;	
in float vIsSelected;
in float vFadeFactor;

const int MAX_POINT_LIGHTS = 3;
const int MAX_SPOT_LIGHTS = 3;

/**
 * @struct Light
 * @brief Base lighting parameters.
 */
struct Light {
	vec3 colour; ///< RGB light color.
	float ambientIntensity; ///< Ambient lighting intensity.
	float diffuseIntensity; ///< Diffuse lighting intensity.
};


/**
 * @struct DirectionalLight
 * @brief Directional light source configuration.
 */
struct DirectionalLight {
	Light base; ///< Base light parameters.
	vec3 direction; ///< World-space direction pointing towards the light source.
};

/**
 * @struct PointLight
 * @brief Point light source configuration.
 */
struct PointLight
{
	Light base; ///< Base light parameters.
	vec3 position; ///< World-space position coordinate.
	float constant; ///< Constant attenuation factor.
	float linear; ///< Linear attenuation factor.
	float exponent; ///< Quadratic attenuation factor.
};

/**
 * @struct SpotLight
 * @brief Spot light source configuration.
 */
struct SpotLight
{
	PointLight base; ///< Base point light parameters.
	vec3 direction; ///< Light cone direction.
	float edge; ///< Cutoff angle in radians.
};

/**
 * @struct Material
 * @brief Custom material surface parameters.
 */
struct Material {
	 float specularIntensity; ///< Specular highlight reflection scale.
	 float shininess; ///< Specular exponent shininess.
	 vec4 baseColor; ///< Base tint color.
	 vec2 tiling; ///< UV tiling multiplier.
	 vec2 offset; ///< UV offset coordinate translation.
};

/**
 * @struct OmniShadowMap
 * @brief Cube maps tracking point and spot light shadow maps.
 */
struct OmniShadowMap
{
	samplerCube shadowMap; ///< Depth cubemap sampler.
	samplerCube shadowColorMap; ///< Color cubemap sampler for transparent shadows.
	float farPlane; ///< Far clipping plane distance.
};

uniform int pointLightCount; ///< Count of active point lights.
uniform int spotLightCount; ///< Count of active spot lights.

uniform DirectionalLight directionalLight; ///< Active directional light.
uniform PointLight pointLights[MAX_POINT_LIGHTS]; ///< Array of point lights.
uniform SpotLight spotLights[MAX_SPOT_LIGHTS]; ///< Array of spot lights.


uniform sampler2D theTexture; ///< Base diffuse texture sampler (LOD 0 / primary).
uniform int useDiffuseTexture; ///< Set to 1 if base texture should be sampled.
uniform sampler2D normalMap; ///< Normal map texture sampler.
uniform int useNormalMap; ///< Set to 1 if normal mapping is active.
uniform sampler2DArray directionalShadowMap; ///< Array of cascaded directional shadow maps.
uniform sampler2DArray directionalShadowColorMap; ///< Array of cascaded directional shadow color maps.
uniform OmniShadowMap omniShadowMaps[MAX_POINT_LIGHTS + MAX_SPOT_LIGHTS]; ///< Array of omni shadow maps.

uniform mat4 directionalLightTransform[4]; ///< Transformation matrices mapping cascaded shadow map layers.
uniform float cascadeSplits[4]; ///< Split distance values for cascaded shadow layers.
uniform float lodDistances[3];
uniform mat4 viewMatrix; ///< View matrix to determine view-space camera depth.

uniform Material material;

// camera position
uniform vec3 eyePosition; ///< Camera position coordinate.

// Selection highlight (0.0 = not selected, > 0 = selected)
uniform float selectionTint; ///< Selection outline tint multiplier.

// Debug Visualizers
uniform bool debugLODColoring;
uniform vec3 lodDebugColor;

// ========== Texture Layers ==========
const int MAX_TEXTURE_LAYERS = 5;

/**
 * @struct TextureLayerData
 * @brief Defines blending metrics, opacity, and limits for a single terrain blend layer.
 */
struct TextureLayerData {
	int blendMode;       ///< Blending modes: 0=Normal, 1=Height, 2=Slope, 3=HeightSlope.
	float opacity; ///< Blend opacity.
	float tiling; ///< UV tiling factor.
	float heightMin; ///< Lower height boundary.
	float heightMax; ///< Upper height boundary.
	float slopeMin; ///< Lower slope angle boundary.
	float slopeMax; ///< Upper slope angle boundary.
	int invert; ///< True to invert height calculations.
	int hasNormalMap;       ///< True if this layer has a normal map.
	int hasDisplacementMap; ///< True if this layer has a displacement map.
	float displacementScale; ///< Displacement scaling factor.
};

uniform int textureLayerCount; ///< Count of active texture blending layers.
uniform sampler2D textureLayers[MAX_TEXTURE_LAYERS];         ///< Diffuse samplers.
uniform sampler2D layerNormalMaps[MAX_TEXTURE_LAYERS];       ///< Normal map samplers.
uniform sampler2D layerDisplacementMaps[MAX_TEXTURE_LAYERS]; ///< Displacement samplers.
uniform TextureLayerData layerData[MAX_TEXTURE_LAYERS]; ///< Parameter configurations for each layer.


// Global override normal: set by layer blending, used by lighting functions
vec3 gBlendedNormal = vec3(0.0, 1.0, 0.0);
bool gUseBlendedNormal = false;

// Build the TBN matrix (reused by multiple functions)
mat3 GetTBN()
{
	vec3 N = normalize(NormalWorld);
	vec3 T = normalize(TangentWorld);
	vec3 B = normalize(BitangentWorld);
	T = normalize(T - dot(T, N) * N);
	B = normalize(B - dot(B, N) * N - dot(B, T) * T);
	return mat3(T, B, N);
}

// Transform a tangent-space normal map sample to world space
vec3 TangentToWorld(vec3 tangentNormal, mat3 TBN)
{
	return normalize(TBN * tangentNormal);
}

// Compute the effective normal: from layer blending, single normal map, or vertex normal
vec3 GetEffectiveNormal()
{
	// If layers already computed a blended normal, use it
	if(gUseBlendedNormal) return gBlendedNormal;

	if(useNormalMap == 1)
	{
		vec3 sampledNormal = texture(normalMap, TexCoord).rgb;
		sampledNormal = normalize(sampledNormal * 2.0 - 1.0);
		return TangentToWorld(sampledNormal, GetTBN());
	}
	else
	{
		return normalize(Normal);
	}
}

// ========== Triplanar Mapping ==========
// Returns blending weights based on the normal vector
vec3 GetTriplanarWeights(vec3 normal)
{
    vec3 weights = abs(normal);
    // Sharpen the blend transitions
    weights = pow(weights, vec3(4.0)); 
    weights /= (weights.x + weights.y + weights.z);
    return weights;
}

// Samples a diffuse texture using triplanar projection
vec3 SampleTriplanarDiffuse(sampler2D tex, vec3 pos, vec3 weights, float tiling)
{
    vec3 xDiff = texture(tex, pos.zy * tiling).rgb;
    vec3 yDiff = texture(tex, pos.xz * tiling).rgb;
    vec3 zDiff = texture(tex, pos.xy * tiling).rgb;
    return xDiff * weights.x + yDiff * weights.y + zDiff * weights.z;
}

// Samples a normal map using triplanar projection and transforms to world space
vec3 SampleTriplanarNormal(sampler2D tex, vec3 pos, vec3 weights, float tiling, vec3 surfaceNorm)
{
    // Sample normal maps for each plane
    vec3 xNorm = texture(tex, pos.zy * tiling).rgb * 2.0 - 1.0;
    vec3 yNorm = texture(tex, pos.xz * tiling).rgb * 2.0 - 1.0;
    vec3 zNorm = texture(tex, pos.xy * tiling).rgb * 2.0 - 1.0;

    // Swizzle tangent-space normals to world-space based on the axis they represent
    // X-plane: Normal points in X, so tangent is Z, bitangent is Y
    vec3 xWorld = vec3(xNorm.z * sign(surfaceNorm.x), xNorm.y, xNorm.x);
    // Y-plane: Normal points in Y (standard X-Z mapping), so tangent is X, bitangent is Z
    vec3 yWorld = vec3(yNorm.x, yNorm.z * sign(surfaceNorm.y), yNorm.y);
    // Z-plane: Normal points in Z, so tangent is X, bitangent is Y
    vec3 zWorld = vec3(zNorm.x, zNorm.y, zNorm.z * sign(surfaceNorm.z));

    return normalize(xWorld * weights.x + yWorld * weights.y + zWorld * weights.z);
}

// ========== Parallax Occlusion Mapping ==========
// Returns shifted UVs based on view direction and height map
// OPTIMIZED: Reduced sample count + distance LOD + early exit for steep angles
vec2 CalcParallaxUVs(vec2 texCoords, vec3 viewDirTangent, sampler2D heightMap, float heightScale)
{
    // Early exit for near-perpendicular views (POM invisible at grazing angles)
    float dotNV = abs(dot(vec3(0.0, 0.0, 1.0), viewDirTangent));
    if (dotNV < 0.05) return texCoords;

    // Distance-based LOD: fade out POM beyond a threshold to prevent GPU overload
    float fragDist = length(eyePosition - FragPos);
    float pomFade = 1.0 - smoothstep(50.0, 150.0, fragDist);
    if (pomFade < 0.01) return texCoords;

    // Reduced layer counts: 8-32 instead of 32-128 (4x faster, still looks great)
    const float minLayers = 8.0;
    const float maxLayers = 32.0;
    float numLayers = mix(maxLayers, minLayers, dotNV);
    // Further reduce layers with distance
    numLayers = max(numLayers * pomFade, 4.0);
    
    float layerDepth = 1.0 / numLayers;
    float currentLayerDepth = 0.0;
    
    // Scale displacement with distance fade
    float effectiveScale = heightScale * pomFade;
    
    // Shift magnitude along the view vector
    // Fix: Clamp Z to prevent division by zero and use abs() for triplanar slopes
    float viewZ = max(abs(viewDirTangent.z), 0.05);
    vec2 p = viewDirTangent.xy / viewZ * effectiveScale;
    
    // Cap the maximum shift to prevent "melting" / extreme stretching at grazing angles
    float maxShift = effectiveScale * 1.5; 
    if (length(p) > maxShift) {
        p = normalize(p) * maxShift;
    }
    
    vec2 deltaTexCoords = p / numLayers;
    
    vec2 currentTexCoords = texCoords;
    float currentDepthMapValue = textureLod(heightMap, currentTexCoords, 0.0).r;
    
    // Hard cap on iterations to prevent GPU hang
    int maxSteps = int(numLayers) + 1;
    int step = 0;
    while(currentLayerDepth < currentDepthMapValue && step < maxSteps)
    {
        currentTexCoords -= deltaTexCoords;
        currentDepthMapValue = textureLod(heightMap, currentTexCoords, 0.0).r;
        currentLayerDepth += layerDepth;
        step++;
    }
    
    // Parallax Occlusion Mapping (Interpolation)
    vec2 prevTexCoords = currentTexCoords + deltaTexCoords;
    float afterDepth  = currentDepthMapValue - currentLayerDepth;
    float beforeDepth = textureLod(heightMap, prevTexCoords, 0.0).r - currentLayerDepth + layerDepth;
    float weight = afterDepth / (afterDepth - beforeDepth);
    
    return mix(currentTexCoords, prevTexCoords, weight);
}

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

float GetShadowFactorAtLayer(int layer, vec3 normal, vec3 lightDir)
{
	// Normal Offset Bias: Offset world position along normal to prevent acne
	// Normal Offset Bias: Offset world position along normal to prevent acne.
	// We scale the offset by the layer because distant cascades have larger texels.
	float offsetScale = 0.05;
	if (layer == 1) offsetScale = 0.1;
	if (layer == 2) offsetScale = 0.3;
	if (layer == 3) offsetScale = 0.6;

	vec3 worldPosWithOffset = FragPos + normal * (offsetScale * (1.0 - dot(normal, -lightDir)));
	
	vec4 fragPosLightSpace = directionalLightTransform[layer] * vec4(worldPosWithOffset, 1.0);
	vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
	projCoords = (projCoords * 0.5) + 0.5;
	
	if(projCoords.z > 1.0) return 0.0;
	
	float current = projCoords.z;
	
	// Depth bias scaled by cascade layer.
	float bias = max(0.0002 * (1.0 - dot(normal, -lightDir)), 0.00002);
	if (layer == 1) bias *= 1.5;
	if (layer == 2) bias *= 3.0;
	if (layer == 3) bias *= 6.0;
	
	float shadow = 0.0;
	vec2 texSize = vec2(textureSize(directionalShadowMap, 0).xy);
	vec2 texelSize = 1.0 / texSize;
	
	float angle = random(FragPos, 0) * 6.283185;
	float s = sin(angle);
	float c = cos(angle);
	mat2 rot = mat2(c, s, -s, c);

	for(int i = 0; i < 16; i++)
	{
		// Reduced kernel size (0.8) for sharper details while maintaining soft edges
		vec2 offset = rot * poissonDisk[i] * texelSize * 0.8;
		vec3 samplePos = vec3(projCoords.xy + offset, layer);
		float pcfDepth = texture(directionalShadowMap, samplePos).r;
		float occluderAlpha = texture(directionalShadowColorMap, samplePos).r;
		
		if (current - bias > pcfDepth) {
			shadow += 1.0 * occluderAlpha;
		}
	}

	shadow /= 16.0;

	// Edge fade for the last cascade only
	if (layer == 3) {
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
	for (int i = 0; i < 4; i++) {
		if (depth < cascadeSplits[i]) {
			layer = i;
			break;
		}
	}
	if (layer == -1) layer = 3;

	float shadow = GetShadowFactorAtLayer(layer, normal, lightDir);

	// Cascade Blending: Smoothly blend between cascades at the split points
	float blendThreshold = 5.0; // 5 meters before split
	if (layer < 3) {
		float splitDist = cascadeSplits[layer];
		if (depth > splitDist - blendThreshold) {
			float blendFactor = (depth - (splitDist - blendThreshold)) / blendThreshold;
			float nextShadow = GetShadowFactorAtLayer(layer + 1, normal, lightDir);
			shadow = mix(shadow, nextShadow, blendFactor);
		}
	}

	return shadow;
}

float CalcOmniShadowFactor(PointLight light, int shadowIndex, float currentDepth)
{
	// get fragment to light ( depth ) POSITION
	vec3 fragToLight = FragPos - light.position;

	float shadow = 0.0;
	float bias = 0.05;
	
	float viewDistance = length(eyePosition - FragPos);
	float diskRadius = (1.0 + (viewDistance / omniShadowMaps[shadowIndex].farPlane)) / 75.0; 

	int samples = 20;
	if (viewDistance > 60.0) {
		samples = 8;
	} else if (viewDistance > 30.0) {
		samples = 12;
	}

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

    // We assume 'direction' is light -> fragment.
    // For diffuse, we need fragment -> light, so we use -direction.
	float diffuseFactor = max(dot(effectiveNormal, normalize(-direction)), 0.0f);
	vec3 diffuseColor = light.colour * light.diffuseIntensity * diffuseFactor;

	vec3 specularColour = vec3(0, 0, 0);

	if(diffuseFactor > 0.0f)
	{
		vec3 fragToEye = normalize(eyePosition - FragPos);
        // reflect expects vector FROM light source TO surface, which 'direction' is.
		vec3 reflectedVertex = normalize(reflect(normalize(direction), effectiveNormal));

		float specularFactor = dot(fragToEye, reflectedVertex);

		if(specularFactor > 0.0f) 
		{
            // Energy conservation normalization (inspired by 3DWorld's ads_lighting)
            float normalization = (material.shininess + 8.0) / 8.0;
			specularFactor = pow(specularFactor, material.shininess) * normalization;
			specularColour = light.colour * material.specularIntensity * specularFactor * light.diffuseIntensity;
		}
	}

	// Specular light is purely ADDED on top (dielectric), diffuse/ambient is MULTIPLIED by baseColor
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
	vec3 direction = FragPos - pLight.position; // get the vector from point light to fragment = direction
	float distance = length(direction);

	// Early out if fragment is beyond the light's shadow far plane
	float farPlane = omniShadowMaps[shadowIndex].farPlane;
	if (distance >= farPlane) {
		return vec3(0.0);
	}

	direction = normalize(direction);

	// Also check if attenuation makes it negligible
	float attenuation = pLight.exponent * distance * distance + pLight.linear * distance + pLight.constant;
	if (attenuation > 250.0) {
		return vec3(0.0);
	}

	float shadowFactor = CalcOmniShadowFactor(pLight, shadowIndex, distance);

	vec3 lightFinal = CalcLightByDirection(pLight.base, direction, shadowFactor, baseColor);

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



// ========== Texture Layer Blending ==========
// Computes blended diffuse color AND blended normal from all active layers.
// Sets gBlendedNormal/gUseBlendedNormal before lighting runs.
void CalcLayeredSurface(out vec3 outColor)
{
	// Start with vertex normal (geometry normal for slope computation)
	vec3 geometryNormal = normalize(Normal);
	float slope = 1.0 - max(dot(geometryNormal, vec3(0.0, 1.0, 0.0)), 0.0);

	mat3 TBN = GetTBN();
	vec3 baseColor = vec3(1.0); // Start neutral, material color applied as tint at the end
	vec3 blendedNorm = geometryNormal;
    vec3 triWeights = GetTriplanarWeights(geometryNormal);
	float totalWeight = 0.0;

	for (int i = 0; i < textureLayerCount && i < MAX_TEXTURE_LAYERS; i++)
	{
		vec4 layerSample;
        vec3 layerWorldNorm = geometryNormal;
        float tiling = layerData[i].tiling;

		// Terrain/Structured modes: Triplanar Mapping with per-axis POM
		// Each axis gets independent POM-displaced UVs to prevent seams
		// at the boundary where the dominant projection axis switches.

		// Per-axis base UVs from the original object-space position
		vec2 uvX = LocalPos.zy * tiling;
		vec2 uvY = LocalPos.xz * tiling;
		vec2 uvZ = LocalPos.xy * tiling;

		// Freeze derivatives from the ORIGINAL (un-displaced) UVs.
		// POM shifts UVs non-linearly, which corrupts hardware dFdx/dFdy
		// and causes mipmap banding at axis transitions.
		vec2 duvX_dx = dFdx(uvX); vec2 duvX_dy = dFdy(uvX);
		vec2 duvY_dx = dFdx(uvY); vec2 duvY_dy = dFdy(uvY);
		vec2 duvZ_dx = dFdx(uvZ); vec2 duvZ_dy = dFdy(uvZ);

		// Apply POM independently per projection axis
		if (layerData[i].hasDisplacementMap == 1) {
			vec3 viewDirWorld = normalize(eyePosition - FragPos);

			// X-plane (cliffs facing X): UV = pos.zy, depth = viewDir.x
			if (triWeights.x > 0.01) {
				vec3 vdt = vec3(viewDirWorld.z, viewDirWorld.y, abs(viewDirWorld.x));
				uvX = CalcParallaxUVs(uvX, vdt, layerDisplacementMaps[i], layerData[i].displacementScale);
			}
			// Y-plane (flat ground): UV = pos.xz, depth = viewDir.y
			if (triWeights.y > 0.01) {
				vec3 vdt = vec3(viewDirWorld.x, viewDirWorld.z, abs(viewDirWorld.y));
				uvY = CalcParallaxUVs(uvY, vdt, layerDisplacementMaps[i], layerData[i].displacementScale);
			}
			// Z-plane (cliffs facing Z): UV = pos.xy, depth = viewDir.z
			if (triWeights.z > 0.01) {
				vec3 vdt = vec3(viewDirWorld.x, viewDirWorld.y, abs(viewDirWorld.z));
				uvZ = CalcParallaxUVs(uvZ, vdt, layerDisplacementMaps[i], layerData[i].displacementScale);
			}
		}

		// Sample diffuse per-axis using textureGrad with frozen derivatives
		vec3 xDiff = textureGrad(textureLayers[i], uvX, duvX_dx, duvX_dy).rgb;
		vec3 yDiff = textureGrad(textureLayers[i], uvY, duvY_dx, duvY_dy).rgb;
		vec3 zDiff = textureGrad(textureLayers[i], uvZ, duvZ_dx, duvZ_dy).rgb;
		layerSample.rgb = xDiff * triWeights.x + yDiff * triWeights.y + zDiff * triWeights.z;
		layerSample.a = 1.0;

		// Sample normal maps per-axis with frozen derivatives
		if (layerData[i].hasNormalMap == 1) {
			vec3 xNorm = textureGrad(layerNormalMaps[i], uvX, duvX_dx, duvX_dy).rgb * 2.0 - 1.0;
			vec3 yNorm = textureGrad(layerNormalMaps[i], uvY, duvY_dx, duvY_dy).rgb * 2.0 - 1.0;
			vec3 zNorm = textureGrad(layerNormalMaps[i], uvZ, duvZ_dx, duvZ_dy).rgb * 2.0 - 1.0;

			vec3 xWorld = vec3(xNorm.z * sign(geometryNormal.x), xNorm.y, xNorm.x);
			vec3 yWorld = vec3(yNorm.x, yNorm.z * sign(geometryNormal.y), yNorm.y);
			vec3 zWorld = vec3(zNorm.x, zNorm.y, zNorm.z * sign(geometryNormal.z));

			layerWorldNorm = normalize(xWorld * triWeights.x + yWorld * triWeights.y + zWorld * triWeights.z);
		}

		// Compute blend weight
		float weight = 1.0;

		if (layerData[i].blendMode == 1) {
			// Height: hard cutoff at heightMin. heightMax adds optional transition width.
			float range = max(layerData[i].heightMax - layerData[i].heightMin, 0.01);
			weight = clamp((LocalPos.y - layerData[i].heightMin) / range, 0.0, 1.0);
		}
		else if (layerData[i].blendMode == 2) {
			// Slope: hard cutoff at slopeMin with optional transition
			float range = max(layerData[i].slopeMax - layerData[i].slopeMin, 0.001);
			weight = clamp((slope - layerData[i].slopeMin) / range, 0.0, 1.0);
		}
		else if (layerData[i].blendMode == 3) {
			// Height + Slope combined: both hard cutoffs
			float hRange = max(layerData[i].heightMax - layerData[i].heightMin, 0.01);
			float hWeight = clamp((LocalPos.y - layerData[i].heightMin) / hRange, 0.0, 1.0);
			float sRange = max(layerData[i].slopeMax - layerData[i].slopeMin, 0.001);
			float sWeight = clamp((slope - layerData[i].slopeMin) / sRange, 0.0, 1.0);
			weight = hWeight * sWeight;
		}

		if (layerData[i].invert == 1) {
			weight = 1.0 - weight;
		}

		weight *= layerData[i].opacity; // Removed layerSample.a dependency for triplanar simplicity
		weight = clamp(weight, 0.0, 1.0);

		// Blend diffuse color
		baseColor = mix(baseColor, layerSample.rgb, weight);

		// Blend normal map
		blendedNorm = mix(blendedNorm, layerWorldNorm, weight);
	}

	outColor = baseColor * material.baseColor.rgb; // Unity-style multiplicative tint
	gBlendedNormal = normalize(blendedNorm);
	gUseBlendedNormal = true;
}

void main()								         
{
	// Distance dithered fade: discard pixels based on a 4x4 Bayer pattern
	if (vFadeFactor > 0.001) {
		int x = int(mod(gl_FragCoord.x, 4.0));
		int y = int(mod(gl_FragCoord.y, 4.0));
		// 4x4 Bayer dither matrix (values 0.0 to ~0.9375)
		float bayerMatrix[16] = float[16](
			 0.0/16.0,  8.0/16.0,  2.0/16.0, 10.0/16.0,
			12.0/16.0,  4.0/16.0, 14.0/16.0,  6.0/16.0,
			 3.0/16.0, 11.0/16.0,  1.0/16.0,  9.0/16.0,
			15.0/16.0,  7.0/16.0, 13.0/16.0,  5.0/16.0
		);
		float threshold = bayerMatrix[y * 4 + x];
		if (vFadeFactor > threshold) discard;
	}

	// 1. Compute surface color (and blended normal if layers active)
	vec3 baseColor;

	if (textureLayerCount > 0) {
		// Layer system: computes color + blended normal BEFORE lighting
		CalcLayeredSurface(baseColor);
	} else {
		// Legacy single-texture path (for models, old objects)
		// Multiply blend: baseColor tints the texture (Unity-style)
		// White (1,1,1) = no tint, Red (1,0,0) = red tint, etc.
		vec4 texColor = (useDiffuseTexture == 1) ? texture(theTexture, TexCoord) : vec4(1.0);
		
		// Alpha discard for cutout transparency (e.g. foliage)
		// Threshold increased to 0.5 to prevent white outlines/halo (mip bleed)
		if (useDiffuseTexture == 1 && texColor.a < 0.5) {
			discard; 
		}
		
		baseColor = material.baseColor.rgb * texColor.rgb;
	}

	// 2. Compute lighting (uses gBlendedNormal if layers set it)
	vec3 finalLight = CalcDirectionalLight(baseColor);
	finalLight += CalcPointLights(baseColor);
	finalLight += CalcSpotLights(baseColor);

	// 3. Final output
	vec3 finalColor = finalLight;

	// Selection highlight: additive yellow tint
	float selectedVal = max(selectionTint, vIsSelected > 0.5 ? 1.0 : 0.0);
	if (selectedVal > 0.0) {
		finalColor += vec3(0.35, 0.25, 0.0) * selectedVal;
	}

	// LOD debug coloring (tint 30% of original color)
	if (debugLODColoring) {
		float distToCam = distance(FragPos, eyePosition);
		vec3 distColor = vec3(1.0, 0.2, 0.2); // Red (LOD 0)
		if (distToCam > lodDistances[1]) distColor = vec3(0.2, 0.2, 1.0); // Blue (LOD 2)
		else if (distToCam > lodDistances[0]) distColor = vec3(0.2, 1.0, 0.2); // Green (LOD 1)
		
		finalColor = mix(finalColor, distColor, 0.4);
	}

	colour = vec4(finalColor, material.baseColor.a);
}