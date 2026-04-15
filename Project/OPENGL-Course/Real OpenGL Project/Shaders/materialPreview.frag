#version 460 core

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
    
    // Match thumbnail.frag lighting exactly
    vec3 keyLightDir  = normalize(vec3(0.5, 0.8, 1.0));
    vec3 fillLightDir = normalize(vec3(-0.7, 0.2, 0.5));
    
    float keyDiff  = max(dot(norm, keyLightDir), 0.0);
    float fillDiff = max(dot(norm, fillLightDir), 0.0) * 0.25;
    float ambient  = 0.15;
    
    // Base color
    vec3 baseColor = materialColor;
    if (hasDiffuse > 0) {
        baseColor *= texture(diffuseMap, TexCoord).rgb;
    }
    
    // Specular (Blinn-Phong) — same as thumbnail
    vec3 viewDir = normalize(vec3(0.0, 0.0, 1.0));
    float spec = 0.0;
    if (shininess > 0.0) {
        vec3 halfDir = normalize(keyLightDir + viewDir);
        spec = pow(max(dot(norm, halfDir), 0.0), max(shininess, 1.0)) * specularIntensity;
    }
    
    // Subtle rim light
    float rim = 1.0 - max(dot(norm, viewDir), 0.0);
    rim = pow(rim, 3.0) * 0.12;
    
    vec3 result = baseColor * (ambient + keyDiff * 0.7 + fillDiff) + vec3(spec + rim);
    result = clamp(result, 0.0, 1.0);
    
    color = vec4(result, 1.0);
}
