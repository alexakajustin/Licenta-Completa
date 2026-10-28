#version 330

in vec2 TexCoord;
in vec3 Normal;
in vec3 FragPos;

out vec4 colour;
in float vIsSelected;
in float vFadeFactor;

// Constants
const int MAX_POINT_LIGHTS = 3;
const int MAX_SPOT_LIGHTS = 3;

// Standard Light Structs
struct Light {
	vec3 colour;
	float ambientIntensity;
	float diffuseIntensity;
};

struct DirectionalLight {
	Light base;
	vec3 direction;
};

struct PointLight {
	Light base;
	vec3 position;
	float constant;
	float linear;
	float exponent;
};

struct SpotLight {
	PointLight base;
	vec3 direction;
	float edge;
};

struct Material {
    float specularIntensity;
    float shininess;
    vec4 baseColor;
    vec2 tiling;
    vec2 offset;
    float reflectivity;
};

uniform Material material;
uniform sampler2D theTexture;

// Lighting Uniforms automatically passed by Renderer
uniform DirectionalLight directionalLight;
uniform PointLight pointLights[MAX_POINT_LIGHTS];
uniform SpotLight spotLights[MAX_SPOT_LIGHTS];
uniform int pointLightCount;
uniform int spotLightCount;

uniform sampler2DArray directionalShadowMap;
uniform sampler2DArray directionalShadowColorMap;
uniform mat4 directionalLightTransform[4];
uniform float cascadeSplits[4];
uniform mat4 viewMatrix;
uniform float shadowDistance;
uniform float lodDistances[3];

uniform bool debugLODColoring;
uniform vec3 lodDebugColor;

float random(vec3 seed, int i) {
	vec4 seed4 = vec4(seed, i);
	float dot_product = dot(seed4, vec4(12.9898, 78.233, 45.164, 94.673));
	return fract(sin(dot_product) * 43758.5453);
}

vec2 poissonDisk[16] = vec2[](
	vec2(-0.94201624, -0.39906216), vec2(0.94558609, -0.76890725),
	vec2(-0.094184101, -0.92938870), vec2(0.34495938, 0.29387760),
	vec2(-0.91588581, 0.45771432), vec2(-0.81544232, -0.87912464),
	vec2(-0.38277543, 0.27676845), vec2(0.97484398, 0.75648379),
	vec2(0.44323325, -0.97511554), vec2(0.53742981, -0.47373420),
	vec2(-0.26496911, -0.41893023), vec2(0.79197514, 0.19090188),
	vec2(-0.24188840, 0.99706507), vec2(-0.81409955, 0.91437590),
	vec2(0.19984126, 0.78641367), vec2(0.14383161, -0.14100790)
);

uniform vec3 eyePosition;
uniform float selectionTint;

// --- Specialized Grass Lighting Model ---
// Applies wrapped diffuse and subsurface scattering for any light source.
vec3 CalcGrassLightByDirection(Light base, vec3 direction, vec3 normal, vec3 viewDir)
{
    // 1. Ambient Lighting
    vec3 ambient = base.colour * base.ambientIntensity;
    
    // 2. Wrapped Diffuse
    vec3 lightDir = normalize(-direction);
    float diffuseFactor = dot(normal, lightDir);
    float wrappedDiffuse = max(0.0, (diffuseFactor + 0.5) / 1.5);
    vec3 diffuse = base.colour * base.diffuseIntensity * wrappedDiffuse;
    
    return ambient + diffuse;
}

vec3 CalcPointLight(PointLight pLight, vec3 normal, vec3 viewDir)
{
    vec3 direction = FragPos - pLight.position;
    float distance = length(direction);
    
    vec3 lightFinal = CalcGrassLightByDirection(pLight.base, direction, normal, viewDir);
    float attenuation = pLight.exponent * distance * distance + pLight.linear * distance + pLight.constant;
    
    return (lightFinal / attenuation);
}

vec3 CalcSpotLight(SpotLight sLight, vec3 normal, vec3 viewDir)
{
    vec3 rayDirection = normalize(FragPos - sLight.base.position);
    float slFactor = dot(rayDirection, sLight.direction);
    
    if (slFactor > sLight.edge) {
        vec3 lightFinal = CalcPointLight(sLight.base, normal, viewDir);
        return lightFinal * (1.0 - (1.0 - slFactor) * (1.0 / (1.0 - sLight.edge)));
    } else {
        return vec3(0.0);
    }
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

	for(int i = 0; i < 4; i++) // Highly optimized for 1M+ grass instances
	{
		vec2 offset = rot * poissonDisk[i] * texelSize * 1.2;
		vec3 samplePos = vec3(projCoords.xy + offset, layer);
		float pcfDepth = texture(directionalShadowMap, samplePos).r;
		float occluderAlpha = texture(directionalShadowColorMap, samplePos).r;
		
		if (current - bias > pcfDepth) {
			shadow += 1.0 * occluderAlpha;
		}
	}

	return shadow / 4.0;
}

float CalcDirectionalShadowFactor(vec3 normal)
{
	vec4 fragPosViewSpace = viewMatrix * vec4(FragPos, 1.0);
	float depth = abs(fragPosViewSpace.z);
    
    if (depth > shadowDistance) return 0.0;

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

	vec3 lightDir = normalize(directionalLight.direction);
	return GetShadowFactorAtLayer(layer, normal, lightDir);
}

void main()
{
    // Distance dithered fade
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

    // 1. Sample Base Texture — alpha-tested cutout, never blended.
    // The main pass runs with GL_BLEND on and the GPU culler emits instances in a different
    // order every frame. Outputting the texture alpha made every soft edge (and most of the
    // field at distance, where mips average alpha down) blend with whatever happened to be
    // drawn first while still writing depth -> grass behind grass flickered frame to frame.
    vec4 texColor = texture(theTexture, TexCoord);

    // Thin blades stay visible with distance: cutout textures get coverage-preserving mips
    // (Texture.cpp), so no per-pixel alpha boost is needed here.
    if (texColor.a < 0.5) discard; // Same cutoff as the shadow passes

    vec3 norm = normalize(Normal);
    // Blades are drawn double-sided (culling off): light the back face with the flipped
    // normal, otherwise every blade seen from behind is dark.
    if (!gl_FrontFacing) norm = -norm;
    vec3 viewDir = normalize(eyePosition - FragPos);
    
    // 2. Accumulate Lighting from all sources
    // Ambient is sky light: a blade in the shadow of a tree still receives it. Only the sun's
    // direct light is attenuated by the shadow map (not fully: blades scatter light into each other).
    float shadowFactor = CalcDirectionalShadowFactor(norm);
    vec3 sunDir = normalize(-directionalLight.direction);
    vec3 sunIn = directionalLight.base.colour * directionalLight.base.diffuseIntensity * (1.0 - shadowFactor * 0.8);
    // Sky fill is cooler than the sun (same as foliage in shader.frag)
    vec3 totalLight = directionalLight.base.colour * directionalLight.base.ambientIntensity * vec3(1.0, 1.2, 1.35)
                    + sunIn * max(0.0, (dot(norm, sunDir) + 0.5) / 1.5);
    
    for(int i = 0; i < pointLightCount; i++) {
        totalLight += CalcPointLight(pointLights[i], norm, viewDir);
    }
    
    for(int i = 0; i < spotLightCount; i++) {
        totalLight += CalcSpotLight(spotLights[i], norm, viewDir);
    }
    
    // 3. Compute Surface properties (Root-to-Tip Gradient + Patch variation)
    // Card textures have the blade tips at the top of the image, which is v = 0 after Assimp's
    // FlipUVs, so the tip weight is 1 - v (using v directly made the tips the darkest part).
    float heightWgt = clamp(1.0 - TexCoord.y, 0.0, 1.0);
    vec3 rootColor = material.baseColor.rgb * 0.45; // Self-occlusion at the base
    vec3 tipColor = material.baseColor.rgb;
    vec3 gradientColor = mix(rootColor, tipColor, heightWgt);
    
    // Smooth value noise for patch variation. Hashing floor(pos) directly gave hard-edged
    // 5m squares of different grass colour across the field.
    vec2 cell = FragPos.xz * 0.2;
    vec2 ci = floor(cell);
    vec2 cf = fract(cell);
    cf = cf * cf * (3.0 - 2.0 * cf);
    float n00 = fract(sin(dot(ci,                   vec2(12.9898, 78.233))) * 43758.5453);
    float n10 = fract(sin(dot(ci + vec2(1.0, 0.0),  vec2(12.9898, 78.233))) * 43758.5453);
    float n01 = fract(sin(dot(ci + vec2(0.0, 1.0),  vec2(12.9898, 78.233))) * 43758.5453);
    float n11 = fract(sin(dot(ci + vec2(1.0, 1.0),  vec2(12.9898, 78.233))) * 43758.5453);
    float noise = mix(mix(n00, n10, cf.x), mix(n01, n11, cf.x), cf.y);
    gradientColor *= mix(0.85, 1.15, noise);

    // 4. Final Output
    // Light transmitted through the blades: strongest looking toward the sun, so back-lit
    // tips glow at sunset. Roots get none (same model as foliage in shader.frag).
    vec3 albedo = texColor.rgb * gradientColor;
    // Blades are much thinner than needle sprays, so they also transmit sideways light.
    float backLit = pow(max(dot(viewDir, -sunDir), 0.0), 6.0);
    float transmission = (0.3 * max(-dot(norm, sunDir), 0.0) + 0.6 * backLit) * heightWgt;
	vec3 finalColor = albedo * totalLight + albedo * vec3(0.8, 1.6, 0.6) * sunIn * transmission;

	// Selection highlight: additive yellow tint
	float selectedVal = max(selectionTint, vIsSelected > 0.5 ? 1.0 : 0.0);
	if (selectedVal > 0.0) {
		finalColor += vec3(0.35, 0.25, 0.0) * selectedVal;
	}
    
    // LOD debug coloring: Show distance-based buckets (RGB)
	if (debugLODColoring) {
		float distToCam = distance(FragPos, eyePosition);
		vec3 distColor = vec3(1.0, 0.2, 0.2); // Red (LOD 0)
		if (distToCam > lodDistances[1]) distColor = vec3(0.2, 0.2, 1.0); // Blue (LOD 2)
		else if (distToCam > lodDistances[0]) distColor = vec3(0.2, 1.0, 0.2); // Green (LOD 1)
		
		finalColor = mix(finalColor, distColor, 0.4);
	}

	colour = vec4(finalColor, 1.0); // Opaque cutout: result must not depend on draw order
}
