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
    float sssScale;
    float sssDistortion;
    vec4 baseColor;
    vec2 tiling;
    vec2 offset;
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
uniform mat4 dirLightMatrices[4];
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
    
    // 3. Fake Subsurface Scattering (Translucency)
    float sssDistortion = max(material.sssDistortion, 0.2); 
    float sssScale = max(material.sssScale, 2.0);
    vec3 backLightDir = normalize(lightDir + normal * sssDistortion);
    float sssPower = pow(clamp(dot(viewDir, -backLightDir), 0.0, 1.0), 4.0) * sssScale;
    vec3 sss = base.colour * base.diffuseIntensity * sssPower * 0.5;
    
    return ambient + diffuse + sss;
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
	float offsetScale = 0.2 * (layer + 1); 
	vec3 worldPosWithOffset = FragPos + normal * (offsetScale * (1.0 - dot(normal, -lightDir)));
	
	vec4 fragPosLightSpace = dirLightMatrices[layer] * vec4(worldPosWithOffset, 1.0);
	vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
	projCoords = (projCoords * 0.5) + 0.5;
	
	if(projCoords.z > 1.0) return 0.0;
	
	float current = projCoords.z;
	float bias = max(0.005 * (1.0 - dot(normal, -lightDir)), 0.001);
	
	float shadow = 0.0;
	vec2 texSize = vec2(textureSize(directionalShadowMap, 0).xy);
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

float CalcDirectionalShadowFactor()
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

	vec3 lightDir = normalize(directionalLight.direction);
	return GetShadowFactorAtLayer(layer, normalize(Normal), lightDir);
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

    // 1. Sample Base Texture
    vec4 texColor = texture(theTexture, TexCoord);
    if(texColor.a < 0.1) discard;

    vec3 norm = normalize(Normal);
    vec3 viewDir = normalize(eyePosition - FragPos);
    
    // 2. Accumulate Lighting from all sources
    float shadowFactor = CalcDirectionalShadowFactor();
    vec3 totalLight = CalcGrassLightByDirection(directionalLight.base, directionalLight.direction, norm, viewDir) * (1.0 - shadowFactor);
    
    for(int i = 0; i < pointLightCount; i++) {
        totalLight += CalcPointLight(pointLights[i], norm, viewDir);
    }
    
    for(int i = 0; i < spotLightCount; i++) {
        totalLight += CalcSpotLight(spotLights[i], norm, viewDir);
    }
    
    // 3. Compute Surface properties (Root-to-Tip Gradient + Patch variation)
    float heightWgt = clamp(TexCoord.y, 0.0, 1.0); 
    vec3 rootColor = material.baseColor.rgb * 0.25; 
    vec3 tipColor = material.baseColor.rgb;
    vec3 gradientColor = mix(rootColor, tipColor, heightWgt);
    
    float noise = fract(sin(dot(floor(FragPos.xz * 0.2), vec2(12.9898, 78.233))) * 43758.5453);
    gradientColor *= mix(0.85, 1.15, noise);

    // 4. Final Output
	vec3 finalColor = texColor.rgb * totalLight * gradientColor;

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

	colour = vec4(finalColor, texColor.a);
}
