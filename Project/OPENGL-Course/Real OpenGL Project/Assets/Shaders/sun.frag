#version 410 core

out vec4 FragColor;

in vec3 WorldPos;
in vec2 TexCoord;
in vec3 Normal;
in vec3 LocalPos; // Normalized

uniform vec3 eyePosition;
uniform float time;
uniform int seed;

// Panteleymonov A K 2015 Sun Shader logic adapted for GLSL 410 and Mesh
vec4 hash4(vec4 n) { 
    return fract(sin(n) * 1399763.5453123); 
}

float noise4q(vec4 x)
{
    vec4 n3 = vec4(0.0, 0.25, 0.5, 0.75);
    vec4 p2 = floor(x.wwww + n3);
    vec4 b = floor(x.xxxx + n3) + floor(x.yyyy + n3) * 157.0 + floor(x.zzzz + n3) * 113.0;
    vec4 p1 = b + fract(p2 * 0.00390625) * vec4(164352.0, -164352.0, 163840.0, -163840.0);
    p2 = b + fract((p2 + vec4(1.0)) * 0.00390625) * vec4(164352.0, -164352.0, 163840.0, -163840.0);
    vec4 f1 = fract(x.xxxx + n3);
    vec4 f2 = fract(x.yyyy + n3);
    f1 = f1 * f1 * (3.0 - 2.0 * f1);
    f2 = f2 * f2 * (3.0 - 2.0 * f2);
    vec4 n1 = vec4(0.0, 1.0, 157.0, 158.0);
    vec4 n2 = vec4(113.0, 114.0, 270.0, 271.0);	
    vec4 vs1 = mix(hash4(p1), hash4(n1.yyyy + p1), f1);
    vec4 vs2 = mix(hash4(n1.zzzz + p1), hash4(n1.wwww + p1), f1);
    vec4 vs3 = mix(hash4(p2), hash4(n1.yyyy + p2), f1);
    vec4 vs4 = mix(hash4(n1.zzzz + p2), hash4(n1.wwww + p2), f1);	
    vs1 = mix(vs1, vs2, f2);
    vs3 = mix(vs3, vs4, f2);
    vs2 = mix(hash4(n2.xxxx + p1), hash4(n2.yyyy + p1), f1);
    vs4 = mix(hash4(n2.zzzz + p1), hash4(n2.wwww + p1), f1);
    vs2 = mix(vs2, vs4, f2);
    vs4 = mix(hash4(n2.xxxx + p2), hash4(n2.yyyy + p2), f1);
    vec4 vs5 = mix(hash4(n2.zzzz + p2), hash4(n2.wwww + p2), f1);
    vs4 = mix(vs4, vs5, f2);
    f1 = fract(x.zzzz + n3);
    f2 = fract(x.wwww + n3);
    f1 = f1 * f1 * (3.0 - 2.0 * f1);
    f2 = f2 * f2 * (3.0 - 2.0 * f2);
    vs1 = mix(vs1, vs2, f1);
    vs3 = mix(vs3, vs4, f1);
    vs1 = mix(vs1, vs3, f2);
    float r = dot(vs1, vec4(0.25));
    return r * r * (3.0 - 2.0 * r);
}

float getPlasma(vec3 p, float anim) {
    float s = 0.0;
    float d = 0.03125;
    float ar = 5.0;
    float zoom = 0.5;
    for (int i = 0; i < 3; i++) {
        s += abs(noise4q(vec4(p * zoom / (d * d) + vec3(ar), anim * ar)) * d);
        ar -= 2.0;
        d *= 4.0;
    }
    return s;
}

void main()
{
    vec3 n = normalize(Normal);
    vec3 v = normalize(eyePosition - WorldPos);
    vec3 lPos = normalize(LocalPos);
    
    float anim = time * 0.4;
    
    // Normalized Core Plasma
    float s1 = clamp(getPlasma(lPos, anim), 0.0, 1.0);
    float s2 = clamp(getPlasma(lPos + vec3(83.23, 34.34, 67.453), anim * 1.2), 0.0, 1.0);
    
    s1 = pow(s1 * 2.4, 2.0);
    s2 = s2 * 2.2;

    // Panteleymonov's Color Palettes
    vec3 yellowBody = mix(vec3(1.0, 1.0, 0.0), vec3(1.0, 1.0, 1.0), pow(s1, 60.0)) * s1;
    vec3 redBody = mix(mix(vec3(1.0, 0.0, 0.0), vec3(1.0, 0.0, 1.0), pow(s2, 2.0)), vec3(1.0, 1.0, 1.0), pow(s2, 10.0)) * s2;

    vec3 finalColor = yellowBody + redBody;

    // --- CORONA AURA ---
    float fresnel = 1.0 - max(dot(n, v), 0.0);
    float ring = max(0.0, 1.0 - abs(0.5 - fresnel) * 2.0);
    
    // Radial Rays
    float nd = pow(noise4q(vec4(lPos * 2.0, -anim + fresnel)) * 2.0, 2.0);
    float ns = noise4q(vec4(lPos * 10.0, -anim * 2.5 + fresnel * 2.0)) * 2.0;
    float s3 = pow(ring, 4.0) + pow(ring, 2.0) * nd * ns;

    vec3 rayColor = mix(vec3(1.0, 0.6, 0.1), vec3(1.0, 0.95, 1.0), pow(s3, 3.0));
    finalColor += rayColor * s3 * 0.7;

    finalColor = clamp(finalColor, 0.0, 1.0);
    FragColor = vec4(finalColor, 1.0);
}
