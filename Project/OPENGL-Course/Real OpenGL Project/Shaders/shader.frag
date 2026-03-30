#version 330				

in vec4 vertex_color;	
in vec2 TexCoord;
in vec3 Normal;
in vec3 FragPos;
in vec4 DirectionalLightSpacePos;

// TBN vectors from vertex shader
in vec3 TangentWorld;
in vec3 BitangentWorld;
in vec3 NormalWorld;

// Object-space position for height-based layer blending
in vec3 LocalPos;

out vec4 colour;	

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
	 vec3 baseColor;
	 vec2 tiling;
	 vec2 offset;
};

struct OmniShadowMap
{
	samplerCube shadowMap;
	float farPlane;
};

uniform int pointLightCount;
uniform int spotLightCount;

uniform DirectionalLight directionalLight;
uniform PointLight pointLights[MAX_POINT_LIGHTS];
uniform SpotLight spotLights[MAX_SPOT_LIGHTS];


uniform sampler2D theTexture;
uniform int useDiffuseTexture;
uniform sampler2D normalMap;
uniform int useNormalMap;
uniform sampler2D directionalShadowMap;
uniform OmniShadowMap omniShadowMaps[MAX_POINT_LIGHTS + MAX_SPOT_LIGHTS];

uniform Material material;

// camera position
uniform vec3 eyePosition;

// ========== Texture Layers ==========
const int MAX_TEXTURE_LAYERS = 4;

struct TextureLayerData {
	int blendMode;       // 0=Normal, 1=Height, 2=Slope, 3=HeightSlope
	float opacity;
	float tiling;
	float heightMin;
	float heightMax;
	float slopeMin;
	float slopeMax;
	int invert;
	int hasNormalMap;       // 1 if this layer has a normal map
	int hasDisplacementMap; // 1 if this layer has a displacement/height map
	float displacementScale; // 0.1 default
};

uniform int textureLayerCount;
uniform sampler2D textureLayers[MAX_TEXTURE_LAYERS];         // Diffuse samplers
uniform sampler2D layerNormalMaps[MAX_TEXTURE_LAYERS];       // Normal map samplers
uniform sampler2D layerDisplacementMaps[MAX_TEXTURE_LAYERS]; // Displacement samplers
uniform TextureLayerData layerData[MAX_TEXTURE_LAYERS];

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
    vec3 xWorld = vec3(xNorm.z, xNorm.y, xNorm.x) * sign(surfaceNorm.x);
    // Y-plane: Normal points in Y (standard X-Z mapping), so tangent is X, bitangent is Z
    vec3 yWorld = vec3(yNorm.x, yNorm.z, yNorm.y) * sign(surfaceNorm.y);
    // Z-plane: Normal points in Z, so tangent is X, bitangent is Y
    vec3 zWorld = vec3(zNorm.x, zNorm.y, zNorm.z) * sign(surfaceNorm.z);

    return normalize(xWorld * weights.x + yWorld * weights.y + zWorld * weights.z);
}

// ========== Parallax Occlusion Mapping ==========
// Returns shifted UVs based on view direction and height map
vec2 CalcParallaxUVs(vec2 texCoords, vec3 viewDirTangent, sampler2D heightMap, float heightScale)
{
    // Number of depth layers (dynamic based on angle)
    const float minLayers = 32.0;
    const float maxLayers = 128.0;
    float numLayers = mix(maxLayers, minLayers, abs(dot(vec3(0.0, 0.0, 1.0), viewDirTangent)));
    
    float layerDepth = 1.0 / numLayers;
    float currentLayerDepth = 0.0;
    
    // Shift magnitude along the view vector
    vec2 p = viewDirTangent.xy / viewDirTangent.z * heightScale;
    vec2 deltaTexCoords = p / numLayers;
    
    vec2 currentTexCoords = texCoords;
    float currentDepthMapValue = texture(heightMap, currentTexCoords).r;
    
    while(currentLayerDepth < currentDepthMapValue)
    {
        currentTexCoords -= deltaTexCoords;
        currentDepthMapValue = texture(heightMap, currentTexCoords).r;
        currentLayerDepth += layerDepth;
    }
    
    // Parallax Occlusion Mapping (Interpolation)
    vec2 prevTexCoords = currentTexCoords + deltaTexCoords;
    float afterDepth  = currentDepthMapValue - currentLayerDepth;
    float beforeDepth = texture(heightMap, prevTexCoords).r - currentLayerDepth + layerDepth;
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

float CalcDirectionalShadowFactor(DirectionalLight light)
{
	vec3 projCoords = DirectionalLightSpacePos.xyz / DirectionalLightSpacePos.w;
	projCoords = (projCoords * 0.5) + 0.5;
	
	float current = projCoords.z;
	
	vec3 normal = GetEffectiveNormal();
	vec3 lightDir = normalize(directionalLight.direction);
	
	// Lowered bias for focused frustum (30 units area) to fix peter panning
	// Using -lightDir (Fragment -> Light) as per standard bias calculation
	float bias = max(0.002 * (1.0 - dot(normal, -lightDir)), 0.0002);
	
	float shadow = 0.0;
	vec2 texelSize = 1.0 / vec2(textureSize(directionalShadowMap, 0));
	
	// 3x3 Grid-based PCF sampling as per reference course
	for(int x = -1; x <= 1; ++x)
	{
		for(int y = -1; y <= 1; ++y)
		{
			float pcfDepth = texture(directionalShadowMap, projCoords.xy + vec2(x, y) * texelSize).r;
			shadow += current - bias > pcfDepth ? 1.0 : 0.0;
		}
	}

	shadow /= 9.0;
	
	if(projCoords.z > 1.0)
	{
		shadow = 0.0;
	}									
	
	return shadow;
}

float CalcOmniShadowFactor(PointLight light, int shadowIndex)
{
	// get fragment to light ( depth ) POSITION
	vec3 fragToLight = FragPos - light.position;
	 // find the closest point that can block out the current point and cast shadow
	float currentDepth = length(fragToLight);

	float shadow = 0.0;
	float bias = 0.05;
	int samples = 20;
	
	float viewDistance = length(eyePosition - FragPos);
	float diskRadius = (1.0 + (viewDistance / omniShadowMaps[shadowIndex].farPlane)) / 75.0; 

	for(int i = 0; i < samples; i++)
	{
		float closestDepth = texture(omniShadowMaps[shadowIndex].shadowMap, fragToLight + sampleOffsetDirections[i] * diskRadius).r;
		closestDepth *= omniShadowMaps[shadowIndex].farPlane;

		if(currentDepth - bias > closestDepth) 
		{
			shadow += 1.0;
		}
	}
	shadow /= float(samples);

	return shadow;
}
vec3 CalcLightByDirection(Light light, vec3 direction, float shadowFactor) 
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
			specularFactor = pow(specularFactor, material.shininess);
			specularColour = light.colour * material.specularIntensity * specularFactor * light.diffuseIntensity;
		}
	}

	return (ambientColour + (1.0 - shadowFactor) * (diffuseColor + specularColour));
}

vec3 CalcDirectionalLight() 
{
	float shadowFactor = CalcDirectionalShadowFactor(directionalLight);
	return CalcLightByDirection(directionalLight.base, directionalLight.direction, shadowFactor);
}

vec3 CalcPointLight(PointLight pLight, int shadowIndex) 
{
	vec3 direction = FragPos - pLight.position; // get the vector from point light to fragment = direction
	float distance = length(direction);
	direction = normalize(direction);

	float shadowFactor = CalcOmniShadowFactor(pLight, shadowIndex);

	vec3 lightFinal = CalcLightByDirection(pLight.base, direction, shadowFactor);
	float attenuation = pLight.exponent * distance * distance + pLight.linear * distance + pLight.constant;

	return (lightFinal / attenuation);
}

vec3 CalcPointLights() 
{
	vec3 totalColour = vec3(0, 0, 0);

	for(int i = 0; i < pointLightCount; i++) 
	{
		totalColour += CalcPointLight(pointLights[i], i);
	}

	return totalColour;
}

vec3 CalcSpotLight(SpotLight sLight, int shadowIndex) 
{
	vec3 rayDirection = normalize(FragPos - sLight.base.position);
	float slFactor = dot(rayDirection, sLight.direction);

	if(slFactor > sLight.edge) 
	{
		vec3 lightFinal = CalcPointLight(sLight.base, shadowIndex);

		return lightFinal * (1.0f - (1.0f - slFactor) * (1.0f / (1.0f - sLight.edge)));
	} 
	else 
	{	
		return vec3(0, 0, 0);
	}

}

vec3 CalcSpotLights() 
{
	vec3 totalColour = vec3(0, 0, 0);

	for(int i = 0; i < spotLightCount; i++) 
	{
	
		totalColour += CalcSpotLight(spotLights[i], i + pointLightCount);
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
	vec3 baseColor = material.baseColor;
	vec3 blendedNorm = geometryNormal;
    vec3 triWeights = GetTriplanarWeights(geometryNormal);
	float totalWeight = 0.0;

	for (int i = 0; i < textureLayerCount && i < MAX_TEXTURE_LAYERS; i++)
	{
		vec4 layerSample;
        vec3 layerWorldNorm = geometryNormal;
        float tiling = layerData[i].tiling;

		if (layerData[i].blendMode == 0) {
			// Normal mode: use mesh UVs (2D)
			vec2 layerUV = TexCoord * tiling;
            
            if (layerData[i].hasDisplacementMap == 1) {
                vec3 viewDirWorld = normalize(eyePosition - FragPos);
                vec3 viewDirTangent = normalize(transpose(TBN) * viewDirWorld);
                layerUV = CalcParallaxUVs(layerUV, viewDirTangent, layerDisplacementMaps[i], layerData[i].displacementScale);
            }
            
			layerSample = texture(textureLayers[i], layerUV);
            
            if (layerData[i].hasNormalMap == 1) {
                vec3 layerNorm = texture(layerNormalMaps[i], layerUV).rgb;
                layerNorm = normalize(layerNorm * 2.0 - 1.0);
                layerWorldNorm = TangentToWorld(layerNorm, TBN);
            }
		} else {
			// Terrain/Structured modes: use Triplanar Mapping to fix "melting"
			layerSample.rgb = SampleTriplanarDiffuse(textureLayers[i], LocalPos, triWeights, tiling);
            layerSample.a = 1.0; // Assume opaque for triplanar layers
            
            if (layerData[i].hasNormalMap == 1) {
                layerWorldNorm = SampleTriplanarNormal(layerNormalMaps[i], LocalPos, triWeights, tiling, geometryNormal);
            }
            
            // Note: POM is omitted for triplanar mapping as it's extremely expensive 
            // and usually unnecessary when triplanar mapping is present.
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

	outColor = baseColor;
	gBlendedNormal = normalize(blendedNorm);
	gUseBlendedNormal = true;
}

void main()								         
{
	// 1. Compute surface color (and blended normal if layers active)
	vec3 baseColor;

	if (textureLayerCount > 0) {
		// Layer system: computes color + blended normal BEFORE lighting
		CalcLayeredSurface(baseColor);
	} else {
		// Legacy single-texture path (for models, old objects)
		vec4 texColor = (useDiffuseTexture == 1) ? texture(theTexture, TexCoord) : vec4(1.0);
		baseColor = mix(material.baseColor, texColor.rgb, texColor.a);
	}

	// 2. Compute lighting (uses gBlendedNormal if layers set it)
	vec3 finalLight = CalcDirectionalLight();
	finalLight += CalcPointLights();
	finalLight += CalcSpotLights();

	// 3. Final output
	colour = vec4(baseColor * finalLight, 1.0);
}