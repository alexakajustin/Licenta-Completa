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

// Compute the effective normal: either from normal map or from vertex normal
vec3 GetEffectiveNormal()
{
	if(useNormalMap == 1)
	{
		// Sample normal map and convert from [0,1] to [-1,1]
		vec3 sampledNormal = texture(normalMap, TexCoord).rgb;
		sampledNormal = sampledNormal * 2.0 - 1.0;

		// Remapped normal must be normalized again to ensure it's a unit vector
		sampledNormal = normalize(sampledNormal);

		// Re-orthogonalize TBN matrix using Gram-Schmidt process
		// We use the interpolated vectors from the vertex shader.
		// Using the provided BitangentWorld instead of cross(N, T) preserves handedness/mirrored UVs.
		vec3 N = normalize(NormalWorld);
		vec3 T = normalize(TangentWorld);
		vec3 B = normalize(BitangentWorld);

		// Re-orthogonalize T and B to N
		T = normalize(T - dot(T, N) * N);
		B = normalize(B - dot(B, N) * N - dot(B, T) * T);
		
		mat3 TBN = mat3(T, B, N);

		// Transform from tangent space to world space
		return normalize(TBN * sampledNormal);
	}
	else
	{
		return normalize(Normal);
	}
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
	
	// Correct bias: use -lightDir (surface to light) for dot product
	float bias = max(0.005 * (1.0 - dot(normal, -lightDir)), 0.0005);

	
	float shadow = 0.0;
	vec2 texelSize = 1.0 / vec2(textureSize(directionalShadowMap, 0));
	
	// Disk-based PCF sampling (20 samples) for smoother shadows
	int samples = 20;
	float diskRadius = 0.5; // Lower = sharper, higher = softer
	
	for(int i = 0; i < samples; i++)
	{
		vec2 offset = sampleOffsetDirections[i].xy * texelSize * diskRadius;
		float pcfDepth = texture(directionalShadowMap, projCoords.xy + offset).r;
		shadow += current - bias > pcfDepth ? 1.0 : 0.0;
	}

	shadow /= float(samples);
	
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



void main()								         
{									
	vec3 finalLight = CalcDirectionalLight();
	finalLight += CalcPointLights();
	finalLight += CalcSpotLights();
	
	vec4 texColor = (useDiffuseTexture == 1) ? texture(theTexture, TexCoord) : vec4(1.0);
	
	// DEBUG: Output TexCoords as color
	// colour = vec4(TexCoord.x, TexCoord.y, 0.0, 1.0); return;

	// Normal result
	vec3 finalRGB = mix(material.baseColor, texColor.rgb, texColor.a) * finalLight;
	colour = vec4(finalRGB, 1.0);          
}