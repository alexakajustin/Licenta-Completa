#version 460 core

in vec2 TexCoord;
in vec3 Normal;
in vec3 FragPos;

out vec4 color;

uniform sampler2D theTexture;
uniform bool useDiffuseTexture;

// Material properties (used for material preview thumbnails)
uniform vec3 materialColor;
uniform float specularIntensity;
uniform float shininess;
uniform bool useMaterial;

void main()
{
    vec3 norm = normalize(Normal);
    
    // Three-point studio lighting
    vec3 keyLightDir   = normalize(vec3(0.5, 0.8, 1.0));
    vec3 fillLightDir  = normalize(vec3(-0.7, 0.2, 0.5));
    vec3 rimLightDir   = normalize(vec3(0.0, -0.3, -1.0));
    
    float keyDiff  = max(dot(norm, keyLightDir), 0.0);
    float fillDiff = max(dot(norm, fillLightDir), 0.0) * 0.25;
    float ambient  = 0.15;
    
    // Base color
    vec3 baseColor = vec3(1.0);
    if (useMaterial) {
        baseColor = materialColor;
    }
    
    // Texture
    vec4 texColor = vec4(1.0);
    if (useDiffuseTexture) {
        texColor = texture(theTexture, TexCoord);
        baseColor *= texColor.rgb;
    }
    
    // Specular (Blinn-Phong)
    vec3 viewDir = normalize(vec3(0.0, 0.0, 1.0)); // Thumbnail camera faces along Z
    float spec = 0.0;
    if (useMaterial && shininess > 0.0) {
        vec3 halfDir = normalize(keyLightDir + viewDir);
        spec = pow(max(dot(norm, halfDir), 0.0), max(shininess, 1.0)) * specularIntensity;
    }
    
    // Subtle rim light
    float rim = 1.0 - max(dot(norm, viewDir), 0.0);
    rim = pow(rim, 3.0) * 0.12;
    
    // Combine: key light is main contribution, fill softens shadows
    vec3 result = baseColor * (ambient + keyDiff * 0.7 + fillDiff) + vec3(spec + rim);
    
    // Clamp to prevent blow-out
    result = clamp(result, 0.0, 1.0);
    
    color = vec4(result, 1.0);
}
