#version 460

/**
 * @file post_tonemap.frag
 * @brief Final image (Rendering/PostProcess.h): exposure, tone mapping and contrast-adaptive
 * sharpening of the HDR scene, written into the viewport.
 *
 * The scene is lit straight into display values (the materials are tuned that way), so the
 * default curve keeps everything up to 0.75 exactly as it was and only rolls the highlights
 * above it off smoothly toward white, where they used to clip. AgX and ACES work on the values
 * linearised with a 2.2 gamma and give their own (filmic) look.
 *
 * Exposure: the eye's adapted luminance (post_adapt.frag) against a target brightness, applied
 * partially (strength) and within a range, plus a manual compensation in EV.
 *
 * Sharpening: AMD FidelityFX CAS (5-tap form) on the tone-mapped values; it sharpens less where
 * the neighbourhood already has strong contrast, so edges do not ring.
 */

out vec4 FragColor;
in vec2 TexCoord;

uniform sampler2D source;     ///< HDR scene (TAA result or the plain frame)
uniform sampler2D adapted;    ///< Adapted log2 luminance (1x1)
uniform int autoExposure;
uniform float exposureKey;    ///< Target average brightness (display value)
uniform float exposureStrength; ///< 0 = no adaptation, 1 = full
uniform float exposureRange;  ///< Max adaptation (EV, both ways)
uniform float exposureBias;   ///< Manual compensation (EV)
uniform int tonemapper;       ///< 0 neutral, 1 AgX, 2 ACES
uniform float sharpness;      ///< 0 = none, 1 = strongest

vec3 NeutralShoulder(vec3 x)
{
    const float knee = 0.75;
    vec3 over = max(x - knee, 0.0);
    vec3 rolled = knee + (1.0 - knee) * (1.0 - exp(-over / (1.0 - knee)));
    return mix(x, rolled, step(knee, x));
}

// AgX (Sobotka), minimal fit by B. Wrensch: log2 encoding in the AgX inset space + sigmoid
vec3 AgX(vec3 linear)
{
    const mat3 inset = mat3(0.842479062253094, 0.0423282422610123, 0.0423756549057051,
                            0.0784335999999992, 0.878468636469772, 0.0784336,
                            0.0792237451477643, 0.0791661274605434, 0.879142973793104);
    const mat3 outset = mat3(1.19687900512017, -0.0528968517574562, -0.0529716355144438,
                             -0.0980208811401368, 1.15190312990417, -0.0980434501171241,
                             -0.0990297440797205, -0.0989611768448433, 1.15107367264116);
    const float minEv = -12.47393, maxEv = 4.026069;
    vec3 v = inset * max(linear, vec3(1e-10));
    v = clamp(log2(v), minEv, maxEv);
    v = (v - minEv) / (maxEv - minEv);
    vec3 v2 = v * v, v4 = v2 * v2;
    v = 15.5 * v4 * v2 - 40.14 * v4 * v + 31.96 * v4 - 6.868 * v2 * v + 0.4298 * v2 + 0.1191 * v - 0.00232;
    return clamp(outset * v, 0.0, 1.0); // Display-encoded
}

// ACES filmic fit (Narkowicz 2015), linear in, linear out
vec3 ACES(vec3 x)
{
    x *= 0.6;
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

float gExposure;

vec3 Tonemap(vec3 c)
{
    c = max(c, vec3(0.0)) * gExposure;
    if (tonemapper == 1) return AgX(pow(c, vec3(2.2)));
    if (tonemapper == 2) return pow(ACES(pow(c, vec3(2.2))), vec3(1.0 / 2.2));
    return clamp(NeutralShoulder(c), 0.0, 1.0);
}

void main()
{
    float ev = exposureBias;
    if (autoExposure == 1) {
        float avgLog = texelFetch(adapted, ivec2(0), 0).r;
        ev += clamp((log2(exposureKey) - avgLog) * exposureStrength, -exposureRange, exposureRange);
    }
    gExposure = exp2(ev);

    ivec2 p = ivec2(gl_FragCoord.xy);
    ivec2 maxP = textureSize(source, 0) - 1;
    vec3 c = Tonemap(texelFetch(source, p, 0).rgb);
    if (sharpness <= 0.001) {
        FragColor = vec4(c, 1.0);
        return;
    }
    vec3 n = Tonemap(texelFetch(source, clamp(p + ivec2(0, 1), ivec2(0), maxP), 0).rgb);
    vec3 s = Tonemap(texelFetch(source, clamp(p - ivec2(0, 1), ivec2(0), maxP), 0).rgb);
    vec3 e = Tonemap(texelFetch(source, clamp(p + ivec2(1, 0), ivec2(0), maxP), 0).rgb);
    vec3 w = Tonemap(texelFetch(source, clamp(p - ivec2(1, 0), ivec2(0), maxP), 0).rgb);
    vec3 mn = min(c, min(min(n, s), min(e, w)));
    vec3 mx = max(c, max(max(n, s), max(e, w)));
    vec3 amp = sqrt(clamp(min(mn, 1.0 - mx) / max(mx, vec3(1e-4)), 0.0, 1.0));
    float peak = -1.0 / mix(8.0, 5.0, sharpness);
    vec3 wgt = amp * peak;
    vec3 result = (c + (n + s + e + w) * wgt) / (1.0 + 4.0 * wgt);
    FragColor = vec4(clamp(result, 0.0, 1.0), 1.0);
}
