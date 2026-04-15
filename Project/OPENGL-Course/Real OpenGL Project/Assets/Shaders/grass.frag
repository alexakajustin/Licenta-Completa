#version 330

in vec2 TexCoord;
in vec3 Normal;
in vec3 FragPos;

out vec4 colour;

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
uniform vec3 eyePosition;

// --- Specialized Grass Lighting ---
vec3 CalcGrassLighting(vec3 normal, vec3 viewDir)
{
    // 1. Ambient Lighting
    vec3 ambient = directionalLight.base.colour * directionalLight.base.ambientIntensity;
    
    // 2. Wrapped Diffuse
    // Standard dot(N,L) creates harsh shadows on flat grassy planes.
    // Wrapped lighting softens it, mimicking light scattered through the blades.
    vec3 lightDir = normalize(-directionalLight.direction);
    float diffuseFactor = dot(normal, lightDir);
    float wrappedDiffuse = max(0.0, (diffuseFactor + 0.5) / 1.5);
    vec3 diffuse = directionalLight.base.colour * directionalLight.base.diffuseIntensity * wrappedDiffuse;
    
    // 3. Fake Subsurface Scattering (Translucency/Backlighting)
    // Makes grass glow when the sun is behind it
    float sssDistortion = max(material.sssDistortion, 0.2); // safe default
    float sssScale = max(material.sssScale, 2.0);
    vec3 backLightDir = normalize(lightDir + normal * sssDistortion);
    float sssPower = pow(clamp(dot(viewDir, -backLightDir), 0.0, 1.0), 4.0) * sssScale;
    vec3 sss = directionalLight.base.colour * sssPower * 0.5; // Soft glow
    
    // 4. Procedural Root-to-Tip Color Gradient
    // Assuming TexCoord.y goes 0.0 (bottom/root) to 1.0 (top/tip)
    float heightWgt = clamp(TexCoord.y, 0.0, 1.0); 
    
    // Roots are darker and more desaturated
    vec3 rootColor = material.baseColor.rgb * 0.25; 
    vec3 tipColor = material.baseColor.rgb;
    vec3 gradientColor = mix(rootColor, tipColor, heightWgt);
    
    // 5. Procedural Patch Variation (Breaks up tiling)
    // Uses FragPos.xz to create subtle color changes in large fields
    float noise = fract(sin(dot(floor(FragPos.xz * 0.2), vec2(12.9898, 78.233))) * 43758.5453);
    gradientColor *= mix(0.85, 1.15, noise); // Random brightness per patch

    // Combine Lighting & Albedo
    return (ambient + diffuse + sss) * gradientColor;
}

void main()
{
    // Sample Base Texture
    vec4 texColor = texture(theTexture, TexCoord);
    
    // Alpha discard for grass cards (billboards)
    if(texColor.a < 0.1) discard;

    vec3 norm = normalize(Normal);
    vec3 viewDir = normalize(eyePosition - FragPos);
    
    // Apply Grass Illumination Model
    vec3 finalLight = CalcGrassLighting(norm, viewDir);
    
    // Output
	colour = vec4(texColor.rgb * finalLight, texColor.a);
}
