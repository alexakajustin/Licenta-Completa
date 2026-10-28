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
// Linear part of the model matrix (rotation * scale), for object-space triplanar mapping
in mat3 ObjectBasis;

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
	 float reflectivity;
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
uniform samplerCube skybox; ///< Reflection cubemap. Bind the skybox OR a scene cubemap per-object from C++.
// camera position
uniform vec3 eyePosition; ///< Camera position coordinate.
uniform int isFoliage; ///< 1 while drawing alpha-tested foliage cards (leaves, ferns): wrapped + translucent lighting.
uniform float foliageRadius; ///< Bounding radius of the foliage mesh (instanced draws), 0 = unknown: no crown occlusion.

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


// How strongly displacement maps steer the transition between layers
// (0 = plain crossfade, ~1 = rocks/stones clearly poke through the layer above).
const float HEIGHT_BLEND_STRENGTH = 0.8;

// Max parallax relief depth as a fraction of the camera distance (see CalcParallaxUVs)
const float POM_MAX_DEPTH_RATIO = 0.1;

// Global override normal: set by layer blending, used by lighting functions
vec3 gBlendedNormal = vec3(0.0, 1.0, 0.0);
bool gUseBlendedNormal = false;

// Build the TBN matrix (reused by multiple functions)
mat3 GetTBN()
{
	vec3 N = normalize(NormalWorld);
	vec3 T = TangentWorld - dot(TangentWorld, N) * N;
	// Meshes without UVs/tangents feed zero (or NaN after normalize) tangents: build any
	// perpendicular frame instead of returning NaN normals (black pixels).
	if (!(dot(T, T) > 1e-8)) {
		T = abs(N.y) < 0.999 ? cross(vec3(0.0, 1.0, 0.0), N) : cross(vec3(1.0, 0.0, 0.0), N);
	}
	T = normalize(T);
	// Keep the mesh's bitangent handedness (mirrored UVs) but always derive B orthogonally
	float handedness = dot(cross(N, T), BitangentWorld) < 0.0 ? -1.0 : 1.0;
	vec3 B = cross(N, T) * handedness;
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
// Displacement maps store height (white = high), but the march walks DOWN into the
// surface, so each sample is converted to depth (1 - h). dx/dy are the derivatives of
// the un-shifted UVs, so the height map is mip-filtered instead of aliasing at LOD 0.
float SampleParallaxDepth(sampler2D heightMap, vec2 uv, vec2 dx, vec2 dy)
{
    return 1.0 - textureGrad(heightMap, uv, dx, dy).r;
}

// viewDirTangent.z is SIGNED: > 0 when the eye is on the outside of the (projected) surface.
// uvWorldSize = world meters per UV unit, so the relief depth can be compared to the view distance.
vec2 CalcParallaxUVs(vec2 texCoords, vec2 dx, vec2 dy, vec3 viewDirTangent, sampler2D heightMap, float heightScale, float uvWorldSize)
{
    // POM is fully on for steep views and fades CONTINUOUSLY to zero at grazing angles or
    // when the eye is behind the projection plane. A hard early-out here (and abs() on z)
    // made the UVs jump at a fixed view angle: on a hill rising in front of the camera the
    // triplanar Y projection crosses that angle exactly at eye level, which showed up as a
    // straight horizontal band across the screen.
    float dotNV = clamp(viewDirTangent.z, 0.0, 1.0);
    float facingFade = smoothstep(0.05, 0.3, dotNV);

    // Distance-based LOD: fade out POM beyond a threshold to prevent GPU overload
    float fragDist = length(eyePosition - FragPos);
    float pomFade = 1.0 - smoothstep(50.0, 150.0, fragDist);

    // POM only holds while the relief is shallow compared to the eye distance: right next to a
    // slope, 0.5 m of relief seen from ~1 m smears into stretched streaks. Cap the world-space
    // depth at POM_MAX_DEPTH_RATIO of the view distance (full depth again a few meters away).
    float worldDepth = heightScale * uvWorldSize;
    float nearFade = clamp(fragDist * POM_MAX_DEPTH_RATIO / max(worldDepth, 1e-5), 0.0, 1.0);

    // Scale displacement with distance, near and view-angle fade
    float effectiveScale = heightScale * pomFade * facingFade * nearFade;
    if (effectiveScale < heightScale * 0.01) return texCoords; // Shift is ~0 here anyway

    // Reduced layer counts: 8-32 instead of 32-128 (4x faster, still looks great)
    const float minLayers = 8.0;
    const float maxLayers = 32.0;
    float numLayers = mix(maxLayers, minLayers, dotNV);
    // Further reduce layers with distance
    numLayers = max(numLayers * pomFade, 4.0);

    float layerDepth = 1.0 / numLayers;
    float currentLayerDepth = 0.0;

    // Shift magnitude along the view vector (z >= 0.05 wherever facingFade > 0)
    float viewZ = max(dotNV, 0.05);
    vec2 p = viewDirTangent.xy / viewZ * effectiveScale;
    
    // Cap the maximum shift to prevent "melting" / extreme stretching at grazing angles
    float maxShift = effectiveScale * 1.5; 
    if (length(p) > maxShift) {
        p = normalize(p) * maxShift;
    }
    
    vec2 deltaTexCoords = p / numLayers;
    
    vec2 currentTexCoords = texCoords;
    float currentDepthMapValue = SampleParallaxDepth(heightMap, currentTexCoords, dx, dy);
    float prevDepthMapValue = currentDepthMapValue;

    // Hard cap on iterations to prevent GPU hang
    int maxSteps = int(numLayers) + 1;
    int step = 0;
    while(currentLayerDepth < currentDepthMapValue && step < maxSteps)
    {
        prevDepthMapValue = currentDepthMapValue;
        currentTexCoords -= deltaTexCoords;
        currentDepthMapValue = SampleParallaxDepth(heightMap, currentTexCoords, dx, dy);
        currentLayerDepth += layerDepth;
        step++;
    }
    if (step == 0) return texCoords; // Surface top: no shift needed

    // Parallax Occlusion Mapping (Interpolation)
    vec2 prevTexCoords = currentTexCoords + deltaTexCoords;
    float afterDepth  = currentDepthMapValue - currentLayerDepth;
    float beforeDepth = prevDepthMapValue - currentLayerDepth + layerDepth;
    float weight = clamp(afterDepth / (afterDepth - beforeDepth), 0.0, 1.0);
    
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
	// Calculate the split distance of this cascade
	float splitDist = cascadeSplits[layer];

	// World-space texel size approximation
	vec2 texSize = vec2(textureSize(directionalShadowMap, 0).xy);
	float texelSizeWS = (splitDist * 2.5) / texSize.x;

	float cosAlpha = clamp(dot(normal, -lightDir), 0.0, 1.0);

	// Normal offset scales with texel size and slope. We keep a 20% minimum offset
	// even when facing the light to prevent acne on flat surfaces under direct light.
	float offsetScale = texelSizeWS * 0.8;
	offsetScale = max(offsetScale, 0.015); // Min 1.5cm offset
	vec3 worldPosWithOffset = FragPos + normal * (offsetScale * (0.2 + 0.8 * (1.0 - cosAlpha)));
	
	vec4 fragPosLightSpace = directionalLightTransform[layer] * vec4(worldPosWithOffset, 1.0);
	vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
	projCoords = (projCoords * 0.5) + 0.5;
	
	if(projCoords.z > 1.0) return 0.0;
	
	float current = projCoords.z;
	
	// Slope-scaled depth bias in NDC space to prevent texel quantization acne
	float tanTheta = sqrt(1.0 - cosAlpha * cosAlpha) / max(cosAlpha, 0.0001);
	
	// Approx radius to find the depth range range of the orthographic projection
	float approxRadius = splitDist * 1.25;
	float zMult = max(2000.0, approxRadius * 4.0);
	float depthRange = 2.0 * zMult;
	
	// World-space bias: starts at 0.4 texels and scales up steeply with slope (2.0x slope scale)
	// to completely cover the depth variation across the PCF footprint at grazing angles.
	float biasWS = texelSizeWS * (0.4 + tanTheta * 2.0);
	
	// Clamp the world-space bias to prevent extreme Peter Panning at grazing angles,
	// while ensuring it's large enough to cover texel quantization.
	biasWS = clamp(biasWS, 0.01, 4.0); // 1cm min, 4m max
	
	float bias = biasWS / depthRange;
	
	float shadow = 0.0;
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
	vec3 normal = normalize(Normal);
	vec3 lightDir = normalize(directionalLight.direction);

	int layer = -1;
	for (int i = 0; i < 4; i++) {
		if (depth < cascadeSplits[i]) {
			layer = i;
			break;
		}
	}
	if (layer == -1) layer = 3;

	// Clamp selected cascade layer to the actual number of layers present in the texture array
	int maxLayer = textureSize(directionalShadowMap, 0).z - 1;
	layer = min(layer, maxLayer);

	float shadow = GetShadowFactorAtLayer(layer, normal, lightDir);

	// Cascade Blending: Smoothly blend between cascades at the split points
	float blendThreshold = 5.0; // 5 meters before split
	if (layer < maxLayer) {
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

	if (isFoliage == 1)
	{
		// Leaves are thin and their card normals are noisy, so Lambert alone turns every crown
		// seen against the light into a black silhouette. Wrapped diffuse keeps the shaded side
		// lit, and light transmitted through the leaves gives the back-lit glow, but only close
		// to the sun's direction (a uniform glow turned whole forests into flat lime blobs).
		vec3 L = normalize(-direction);
		vec3 V = normalize(eyePosition - FragPos);

		// Crown self-occlusion. Beyond the shadow distance nothing darkens the inner leaves,
		// so approximate it from the mesh-space position: leaves near the trunk axis and low in
		// the crown sit inside the canopy; the outer shell and the top are exposed.
		float occlusion = 1.0;
		if (foliageRadius > 0.0) {
			float radial = length(LocalPos.xz) / (foliageRadius * 0.45);
			float height = LocalPos.y / (foliageRadius * 1.6);
			float exposure = max(smoothstep(0.1, 0.75, radial), smoothstep(0.7, 0.95, height));
			occlusion = mix(0.35, 1.0, exposure) * mix(0.7, 1.0, smoothstep(0.0, 0.6, height));
		}
		// Slow brightness variation across the canopy so a crown is not one flat colour
		vec3 q = FragPos * 0.35;
		occlusion *= 0.85 + 0.15 * sin(q.x + 1.7 * sin(q.z)) * sin(q.z * 1.3 + q.y * 0.7);

		float wrapped = max((dot(effectiveNormal, L) + 0.5) / 1.5, 0.0);
		float backLit = pow(max(dot(V, -L), 0.0), 10.0);
		float transmission = (0.06 * max(-dot(effectiveNormal, L), 0.0) + 0.5 * backLit) * occlusion;
		vec3 transmitColor = baseColor * vec3(0.8, 1.6, 0.6); // Chlorophyll passes mostly green
		vec3 lightIn = light.colour * light.diffuseIntensity * (1.0 - shadowFactor * 0.8);
		vec3 skyAmbient = ambientColour * vec3(0.85, 1.0, 1.15); // Fill from the sky dome is cooler than the sun
		diffuseAmbient = baseColor * (skyAmbient * mix(0.6, 1.0, occlusion) + lightIn * wrapped * occlusion)
		               + transmitColor * lightIn * transmission;
		finalSpecular *= 0.3; // Matte leaves: no plastic highlights
	}

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

	// Triplanar frame. Projections run in object space (the texture sticks to the object
	// when it moves/rotates), rescaled so one unit has the same world size on every axis:
	// without that a terrain scaled (1000, 1, 1000) tiled 1000x denser vertically on cliffs
	// (smeared streaks). Normalized to the horizontal scale so flat-ground tiling is unchanged.
	// In this space object->world is a pure rotation, so weights, POM view vectors and the
	// whiteout normal are all computed here and the result is rotated back to world space.
	// (Using the world normal for the weights mismatched the projections on rotated objects.)
	vec3 objScale = vec3(length(ObjectBasis[0]), length(ObjectBasis[1]), length(ObjectBasis[2]));
	mat3 objRot = mat3(ObjectBasis[0] / max(objScale.x, 1e-6),
	                   ObjectBasis[1] / max(objScale.y, 1e-6),
	                   ObjectBasis[2] / max(objScale.z, 1e-6));
	mat3 worldToObj = transpose(objRot);
	float triUnitSize = max(sqrt(objScale.x * objScale.z), 1e-4); // world meters per triPos unit
	vec3 triPos = LocalPos * objScale / triUnitSize;
	vec3 triNormal = normalize(worldToObj * geometryNormal);
	vec3 triViewDir = normalize(worldToObj * (eyePosition - FragPos));
    vec3 triWeights = GetTriplanarWeights(triNormal);
	float totalWeight = 0.0;
	float blendedHeight = 0.5; // Height of the surface built so far, for height-based blending

	for (int i = 0; i < textureLayerCount && i < MAX_TEXTURE_LAYERS; i++)
	{
		vec4 layerSample;
        vec3 layerWorldNorm = geometryNormal;
        float layerHeight = 0.5;
        float tiling = layerData[i].tiling;

		if (layerData[i].blendMode == 0) {
			// Normal mode: use mesh UVs (2D)
			vec2 layerUV = TexCoord * tiling;
			vec2 duv_dx = dFdx(layerUV); vec2 duv_dy = dFdy(layerUV);
			// World meters per UV unit, from the screen-space derivatives of position vs UV
			vec3 dPdx = dFdx(FragPos); vec3 dPdy = dFdy(FragPos);
			float uvWorldSize = sqrt((dot(dPdx, dPdx) + dot(dPdy, dPdy)) / max(dot(duv_dx, duv_dx) + dot(duv_dy, duv_dy), 1e-12));

			if (layerData[i].hasDisplacementMap == 1) {
				vec3 viewDirWorld = normalize(eyePosition - FragPos);
				vec3 viewDirTangent = normalize(transpose(TBN) * viewDirWorld);
				layerUV = CalcParallaxUVs(layerUV, duv_dx, duv_dy, viewDirTangent, layerDisplacementMaps[i], layerData[i].displacementScale, uvWorldSize);
				layerHeight = textureGrad(layerDisplacementMaps[i], layerUV, duv_dx, duv_dy).r;
			}
            
			// Grad sampling with the un-shifted derivatives: POM-shifted UVs jump between
			// neighbouring pixels, which makes implicit mip selection flicker at depth edges.
			layerSample = textureGrad(textureLayers[i], layerUV, duv_dx, duv_dy);

			if (layerData[i].hasNormalMap == 1) {
				vec3 layerNorm = textureGrad(layerNormalMaps[i], layerUV, duv_dx, duv_dy).rgb;
				layerNorm = normalize(layerNorm * 2.0 - 1.0);
				layerWorldNorm = TangentToWorld(layerNorm, TBN);
			}
		} else {
			// Terrain/Structured modes: Triplanar Mapping with per-axis POM
			// Each axis gets independent POM-displaced UVs to prevent seams
			// at the boundary where the dominant projection axis switches.

			// Per-axis base UVs from the isotropic object-space position (see triPos above)
			vec2 uvX = triPos.zy * tiling;
			vec2 uvY = triPos.xz * tiling;
			vec2 uvZ = triPos.xy * tiling;

			// Freeze derivatives from the ORIGINAL (un-displaced) UVs.
			// POM shifts UVs non-linearly, which corrupts hardware dFdx/dFdy
			// and causes mipmap banding at axis transitions.
			vec2 duvX_dx = dFdx(uvX); vec2 duvX_dy = dFdy(uvX);
			vec2 duvY_dx = dFdx(uvY); vec2 duvY_dy = dFdy(uvY);
			vec2 duvZ_dx = dFdx(uvZ); vec2 duvZ_dy = dFdy(uvZ);

			// Apply POM independently per projection axis
			if (layerData[i].hasDisplacementMap == 1) {
				// The view ray enters the REAL surface along its normal, so the depth term is
				// dot(view, surfaceNormal) for every projection; xy is the view projected onto
				// that projection's UV axes. (Measuring depth against the projection axis,
				// e.g. abs(view.y) for the Y plane, goes to 0 at eye level on a hill facing the
				// camera: the shift exploded/flipped there -> a horizontal band across the screen.)
				float vDotN = dot(triViewDir, triNormal);

				// X-plane (cliffs facing X): UV = pos.zy
				if (triWeights.x > 0.01) {
					vec3 vdt = vec3(triViewDir.z, triViewDir.y, vDotN);
					uvX = CalcParallaxUVs(uvX, duvX_dx, duvX_dy, vdt, layerDisplacementMaps[i], layerData[i].displacementScale, triUnitSize / tiling);
				}
				// Y-plane (flat ground): UV = pos.xz
				if (triWeights.y > 0.01) {
					vec3 vdt = vec3(triViewDir.x, triViewDir.z, vDotN);
					uvY = CalcParallaxUVs(uvY, duvY_dx, duvY_dy, vdt, layerDisplacementMaps[i], layerData[i].displacementScale, triUnitSize / tiling);
				}
				// Z-plane (cliffs facing Z): UV = pos.xy
				if (triWeights.z > 0.01) {
					vec3 vdt = vec3(triViewDir.x, triViewDir.y, vDotN);
					uvZ = CalcParallaxUVs(uvZ, duvZ_dx, duvZ_dy, vdt, layerDisplacementMaps[i], layerData[i].displacementScale, triUnitSize / tiling);
				}

				layerHeight = textureGrad(layerDisplacementMaps[i], uvX, duvX_dx, duvX_dy).r * triWeights.x
				            + textureGrad(layerDisplacementMaps[i], uvY, duvY_dx, duvY_dy).r * triWeights.y
				            + textureGrad(layerDisplacementMaps[i], uvZ, duvZ_dx, duvZ_dy).r * triWeights.z;
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

				// Whiteout blend: perturb the REAL surface normal instead of the projection
				// axis, otherwise slopes get axis-snapped normals (faceted, blotchy lighting).
				// Done in the triplanar (object) frame, then rotated back to world space.
				vec3 N = triNormal;
				xNorm = vec3(xNorm.xy + N.zy, abs(xNorm.z) * N.x);
				yNorm = vec3(yNorm.xy + N.xz, abs(yNorm.z) * N.y);
				zNorm = vec3(zNorm.xy + N.xy, abs(zNorm.z) * N.z);

				layerWorldNorm = normalize(objRot * (xNorm.zyx * triWeights.x + yNorm.xzy * triWeights.y + zNorm.xyz * triWeights.z));
			}
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

		// Alpha discard for cutout textures in layered materials — only for layers that are
		// actually visible here, so a masked-out cutout layer doesn't punch holes everywhere.
		if (weight > 0.0 && layerSample.a < 0.5) {
			discard;
		}

		// Height-based blending: inside the transition zone, the displacement map decides
		// which layer wins (stones poke through grass) instead of a flat crossfade.
		// The bias term vanishes at weight 0 and 1, so fully on/off regions are untouched.
		if (layerData[i].hasDisplacementMap == 1 && weight > 0.0 && weight < 1.0) {
			float s = weight + (layerHeight - blendedHeight) * HEIGHT_BLEND_STRENGTH * weight * (1.0 - weight) * 4.0;
			weight = smoothstep(0.0, 1.0, clamp(s, 0.0, 1.0));
		}
		blendedHeight = mix(blendedHeight, layerHeight, weight);

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

	// Resolve the shading normal once: every light (and the reflection) would otherwise
	// re-sample the normal map and rebuild the TBN per call.
	gBlendedNormal = GetEffectiveNormal();
	// Double-sided cutout cards (grass, leaves): light the back face with the flipped normal.
	// Closed meshes never rasterize back faces while culling is on, so they are unaffected.
	if (!gl_FrontFacing) gBlendedNormal = -gBlendedNormal;
	gUseBlendedNormal = true;

	// 2. Compute lighting (uses gBlendedNormal if layers set it)
	vec3 finalLight = CalcDirectionalLight(baseColor);
	finalLight += CalcPointLights(baseColor);
	finalLight += CalcSpotLights(baseColor);

	// 3. Final output
	vec3 finalColor = finalLight;

	// Environment Map Reflections with smooth distance-based fade to prevent far-away parallax warping
	if (material.reflectivity > 0.0) {
		float distToCam = distance(eyePosition, FragPos);
		float fadeFactor = 1.0 - smoothstep(50.0, 150.0, distToCam);
		float activeReflectivity = material.reflectivity * fadeFactor;
		
		if (activeReflectivity > 0.001) {
			vec3 viewDir = normalize(eyePosition - FragPos);
			vec3 reflectDir = reflect(-viewDir, normalize(GetEffectiveNormal()));
			vec3 reflectionColor = texture(skybox, reflectDir).rgb;
			
			// Ensure black/dark materials still show neutral reflections (min 30% intensity)
			vec3 reflectionTint = max(baseColor, vec3(0.3));
			
			// Add the reflection on top of the lighting (tinted and modulated)
			finalColor += reflectionColor * reflectionTint * activeReflectivity;
		}
	}

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