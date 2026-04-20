#version 330

in vec2 TexCoord;
in vec3 Normal;
in vec3 FragPos;

out vec4 colour;

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

uniform vec3 eyePosition;

// Selection highlight (0.0 = not selected, > 0 = selected)
uniform float selectionTint;

// --- Specialized Grass Lighting Model ---
// Applies wrapped diffuse and subsurface scattering for any light source.
vec3 CalcGrassLightByDirection(Light base, vec3 direction, vec3 normal, vec3 viewDir)
{
    // 1. Ambient Lighting (usually only from directional light, but included for completeness)
    vec3 ambient = base.colour * base.ambientIntensity;
    
    // 2. Wrapped Diffuse
    // Standard dot(N,L) creates harsh shadows on flat grassy planes.
    // Wrapped lighting softens it, mimicking light scattered through the blades.
    vec3 lightDir = normalize(-direction);
    float diffuseFactor = dot(normal, lightDir);
    float wrappedDiffuse = max(0.0, (diffuseFactor + 0.5) / 1.5);
    vec3 diffuse = base.colour * base.diffuseIntensity * wrappedDiffuse;
    
    // 3. Fake Subsurface Scattering (Translucency/Backlighting)
    // Makes grass glow when the light is behind it
    float sssDistortion = max(material.sssDistortion, 0.2); // safe default
    float sssScale = max(material.sssScale, 2.0);
    vec3 backLightDir = normalize(lightDir + normal * sssDistortion);
    float sssPower = pow(clamp(dot(viewDir, -backLightDir), 0.0, 1.0), 4.0) * sssScale;
    vec3 sss = base.colour * base.diffuseIntensity * sssPower * 0.5; // Soft glow
    
    return ambient + diffuse + sss;
}

vec3 CalcDirectionalLight(vec3 normal, vec3 viewDir)
{
    return CalcGrassLightByDirection(directionalLight.base, directionalLight.direction, normal, viewDir);
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

void main()
{
    // 1. Sample Base Texture
    vec4 texColor = texture(theTexture, TexCoord);
    if(texColor.a < 0.1) discard;

    vec3 norm = normalize(Normal);
    vec3 viewDir = normalize(eyePosition - FragPos);
    
    // 2. Accumulate Lighting from all sources
    vec3 totalLight = CalcDirectionalLight(norm, viewDir);
    
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
	if (selectionTint > 0.0) {
		finalColor += vec3(0.35, 0.25, 0.0) * selectionTint;
	}

	colour = vec4(finalColor, texColor.a);
}
