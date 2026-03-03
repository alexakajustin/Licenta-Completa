#version 330

in vec3 FragPos;
in vec3 Normal;
in vec2 TexCoord;
in mat3 TBN;

out vec4 color;

uniform float specularIntensity;
uniform float shininess;
uniform vec3 materialColor;

uniform sampler2D diffuseMap;
uniform sampler2D normalMap;
uniform int hasDiffuse;
uniform int hasNormal;

void main()
{
    vec3 norm;
    if (hasNormal > 0) {
        norm = texture(normalMap, TexCoord).rgb;
        norm = norm * 2.0 - 1.0;
        norm = normalize(TBN * norm);
    } else {
        norm = normalize(Normal);
    }
    
    // Camera is at (0, 0, 3), sphere at origin
    vec3 viewDir = normalize(vec3(0.0, 0.0, 3.0) - FragPos);
    
    // Key light: FROM the camera position (dead front)
    vec3 lightPos = vec3(0.0, 0.5, 3.0);
    vec3 lightDir1 = normalize(lightPos - FragPos);
    
    // Fill light: from the left
    vec3 lightDir2 = normalize(vec3(-1.0, 0.3, 0.5) - FragPos);
    
    // Ambient
    float ambient = 0.2;
    
    // Diffuse
    float diff1 = max(dot(norm, lightDir1), 0.0);
    float diff2 = max(dot(norm, lightDir2), 0.0) * 0.3;
    
    // Specular (Blinn-Phong) — from key light at camera = highlight dead center
    vec3 halfDir = normalize(lightDir1 + viewDir);
    float spec = pow(max(dot(norm, halfDir), 0.0), max(shininess, 1.0)) * specularIntensity;
    
    // Base color
    vec3 baseColor;
    if (hasDiffuse > 0) {
        baseColor = texture(diffuseMap, TexCoord).rgb * materialColor;
    } else {
        baseColor = materialColor;
    }
    
    // Rim
    float rim = 1.0 - max(dot(norm, viewDir), 0.0);
    rim = pow(rim, 3.5) * 0.15;
    
    vec3 result = baseColor * (ambient + diff1 + diff2) + vec3(spec + rim);
    
    // Apply gamma correction (approximate sRGB) for accurate colors
    result = pow(result, vec3(1.0 / 2.2));
    
    color = vec4(result, 1.0);
}
