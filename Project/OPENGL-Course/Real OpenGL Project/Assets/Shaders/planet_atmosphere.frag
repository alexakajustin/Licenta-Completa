#version 410

in vec3 WorldPos;
in vec3 Normal;

out vec4 fragColor;

uniform vec3 eyePosition;
uniform vec3 lightDir = vec3(1.0, 1.0, 1.0);
uniform vec3 atmosphereColor = vec3(0.3, 0.6, 1.0);
uniform float atmosphereStrength = 1.0;
uniform float atmospherePower = 4.0;

void main()
{
    vec3 n = normalize(Normal);
    vec3 v = normalize(eyePosition - WorldPos);
    
    // Fresnel effect for the atmosphere glow
    float fresnel = 1.0 - max(dot(v, n), 0.0);
    fresnel = pow(fresnel, atmospherePower);
    
    // Light attenuation (glow only on lit side)
    float lightEffect = max(dot(n, normalize(lightDir)), 0.0);
    
    float alpha = fresnel * atmosphereStrength * (lightEffect + 0.2);
    
    fragColor = vec4(atmosphereColor, alpha);
}
