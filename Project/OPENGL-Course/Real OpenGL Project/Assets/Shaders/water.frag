#version 330

/**
 * @file water.frag
 * @brief Lake / still water.
 *
 * Shares its shading model with river.frag (keep the "SHARED WATER MODEL" sections identical) so a
 * river flowing into a lake has the same colour, reflection and foam on both sides of the junction:
 *  - water thickness along the view ray from the scene depth, Beer-Lambert absorption per channel
 *    (red is absorbed first, so shallow water is clear, deep water turns teal/blue),
 *  - refraction of the opaque scene with depth-aware distortion,
 *  - sky reflection (scene cubemap) + planar reflection, Schlick Fresnel,
 *  - sun glint with specular anti-aliasing (no per-pixel sparkle at distance),
 *  - shoreline foam and a soft shoreline (alpha from water depth).
 */

in vec4 vertex_color;
in vec2 TexCoord;
in vec3 Normal;
in vec3 FragPos;
in vec3 TangentWorld;
in vec3 BitangentWorld;
in vec3 NormalWorld;
in vec3 LocalPos;
in float vIsSelected;
in float vFadeFactor;
in vec4 clipSpaceCoords;
in float vObjectScale;

out vec4 colour;

struct Light { vec3 colour; float ambientIntensity; float diffuseIntensity; };
struct DirectionalLight { Light base; vec3 direction; };
struct PointLight { Light base; vec3 position; float constant; float linear; float exponent; };

uniform DirectionalLight directionalLight;
uniform PointLight pointLights[3];
uniform int pointLightCount;

struct Material {
    float specularIntensity;
    float shininess;
    vec4 baseColor;
    vec2 tiling;
    vec2 offset;
    float reflectivity;
};
uniform Material material;

uniform sampler2D material_waterNormalMap;
uniform sampler2D material_causticsMap;
uniform sampler2D material_dudvMap;
uniform vec4  material_waterColorDeep;     // In-scattering colour of deep water
uniform vec4  material_waterColorShallow;  // In-scattering colour of shallow water
uniform float material_waterDepthScale;    // Absorption multiplier (0.3 = clear mountain water)
uniform float material_fresnelPower;
uniform float material_waveSpeed;
uniform float material_waveScale;
uniform vec4  material_foamColor;
uniform float material_foamDistance;       // Shoreline foam width (meters of water depth)
uniform float material_specularIntensityOverride;
uniform float material_shininessOverride;
uniform float material_normalStrength;     // Ripple normal strength (0 = mirror flat)

uniform sampler2D refractionMap;  // Opaque scene colour (captured before water)
uniform sampler2D reflectionMap;  // Planar reflection (lake height)
uniform sampler2D sceneDepthMap;  // Opaque scene depth (captured before water)
uniform samplerCube skybox;       // Scene / sky cubemap
uniform mat4 projection;
uniform float time;
uniform vec2 screenSize;
uniform vec3 eyePosition;
uniform float selectionTint;

// ===================== SHARED WATER MODEL (keep identical in river.frag) =====================

float LinearDepth(float depth)
{
    float ndcZ = depth * 2.0 - 1.0;
    return projection[3][2] / (ndcZ + projection[2][2]);
}

float Hash21(vec2 p)
{
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

float ValueNoise(vec2 p)
{
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(Hash21(i), Hash21(i + vec2(1, 0)), f.x), mix(Hash21(i + vec2(0, 1)), Hash21(i + vec2(1, 1)), f.x), f.y);
}

// Tangent-space normal map sample -> world-space perturbation on a horizontal surface.
// Falls back to a procedural ripple when no normal map is bound (black texture).
vec3 SampleRipple(vec2 uv)
{
    vec3 n = texture(material_waterNormalMap, uv).rgb;
    if (dot(n, n) < 0.001) {
        float e = 0.05;
        float h  = ValueNoise(uv * 8.0);
        float hx = ValueNoise(uv * 8.0 + vec2(e, 0.0));
        float hz = ValueNoise(uv * 8.0 + vec2(0.0, e));
        return normalize(vec3(-(hx - h) / e * 0.15, -(hz - h) / e * 0.15, 1.0));
    }
    return n * 2.0 - 1.0;
}

// Combine tangent-space ripples into a world normal around 'up' and flatten them with distance:
// far ripples are sub-pixel and only produce aliasing (sparkle), not visible detail.
vec3 RippleToWorld(vec3 tn, float strength, float dist)
{
    strength *= mix(1.0, 0.3, smoothstep(40.0, 400.0, dist));
    return normalize(vec3(tn.x * strength, 1.0, tn.y * strength));
}

struct WaterSample { vec3 color; float alpha; };

WaterSample ShadeWater(vec3 N, vec3 reflectionColor, float extraFoam, vec2 foamFlow)
{
    WaterSample ws;
    vec3 viewDir = normalize(eyePosition - FragPos);
    float dist = length(eyePosition - FragPos);
    vec2 screenUV = gl_FragCoord.xy / max(screenSize, vec2(1.0));

    // --- Water thickness along the view ray, and vertical depth below the surface ---
    float surfaceZ = LinearDepth(gl_FragCoord.z);
    float sceneZ = LinearDepth(texture(sceneDepthMap, screenUV).r);
    float rayScale = dist / max(surfaceZ, 1e-3);                   // view-z -> ray length
    float thickness = max(sceneZ - surfaceZ, 0.0) * rayScale;
    float depthBelow = thickness * max(viewDir.y, 0.05);           // ~ vertical water depth

    // --- Refraction (distortion fades out in very shallow water, no foreground bleeding) ---
    vec2 offset = N.xz * 0.035 * clamp(depthBelow * 0.5, 0.0, 1.0) / max(1.0, dist * 0.02);
    vec2 refrUV = clamp(screenUV + offset, vec2(0.001), vec2(0.999));
    float refrSceneZ = LinearDepth(texture(sceneDepthMap, refrUV).r);
    if (refrSceneZ < surfaceZ) { refrUV = screenUV; refrSceneZ = sceneZ; }
    vec3 refracted = texture(refractionMap, refrUV).rgb;
    // Clamped: over the map edge / open sky there is nothing below and the thickness is "infinite"
    float refrThickness = min(max(refrSceneZ - surfaceZ, 0.0) * rayScale, 40.0);

    // --- Beer-Lambert absorption + in-scattering ---
    float absorbScale = (material_waterDepthScale > 0.0 ? material_waterDepthScale : 0.3) / 0.3;
    vec3 absorption = vec3(0.45, 0.075, 0.055) * absorbScale;      // per meter: red dies first
    vec3 transmittance = exp(-absorption * refrThickness);
    vec3 shallowCol = material_waterColorShallow.rgb == vec3(0.0) ? vec3(0.06, 0.32, 0.36) : material_waterColorShallow.rgb;
    vec3 deepCol = material_waterColorDeep.rgb == vec3(0.0) ? vec3(0.01, 0.07, 0.10) : material_waterColorDeep.rgb;
    float deepness = 1.0 - exp(-refrThickness * 0.08 * absorbScale);
    vec3 scatter = mix(shallowCol, deepCol, deepness);
    // In-scattered light is lit by the sun/sky like everything else
    vec3 sunDir = normalize(-directionalLight.direction);
    float skyLight = directionalLight.base.ambientIntensity + directionalLight.base.diffuseIntensity * max(sunDir.y, 0.0);
    vec3 underwater = refracted * transmittance + scatter * directionalLight.base.colour * skyLight * (1.0 - transmittance);

    // --- Fresnel (Schlick, F0 = 0.02 for water) ---
    float fPow = material_fresnelPower > 0.0 ? material_fresnelPower : 5.0;
    float fresnel = 0.02 + 0.98 * pow(1.0 - clamp(dot(N, viewDir), 0.0, 1.0), fPow);
    vec3 color = mix(underwater, reflectionColor, fresnel);

    // --- Sun glint with specular anti-aliasing ---
    // Normal variance across the pixel lowers the exponent (Toksvig-style): a rough-looking
    // highlight instead of a field of single-pixel sparkles.
    float shininess = material_shininessOverride > 0.0 ? material_shininessOverride : 256.0;
    vec3 dN = fwidth(N);
    float variance = clamp(dot(dN, dN), 0.0, 1.0);
    float effShine = shininess / (1.0 + shininess * variance * 4.0);
    vec3 H = normalize(sunDir + viewDir);
    float specI = material_specularIntensityOverride > 0.0 ? material_specularIntensityOverride : 1.0;
    float spec = pow(max(dot(N, H), 0.0), effShine) * (effShine + 8.0) / 25.13 * specI * 0.25;
    color += directionalLight.base.colour * directionalLight.base.diffuseIntensity * spec * fresnel * 4.0;

    for (int i = 0; i < pointLightCount; i++) {
        vec3 toLight = pointLights[i].position - FragPos;
        float d = length(toLight);
        vec3 Hp = normalize(toLight / d + viewDir);
        float att = pointLights[i].constant + pointLights[i].linear * d + pointLights[i].exponent * d * d;
        color += pointLights[i].base.colour * pointLights[i].base.diffuseIntensity * pow(max(dot(N, Hp), 0.0), effShine) * specI * 0.25 / max(att, 1e-3);
    }

    // --- Foam ---
    // Shoreline: wave fronts (bands of equal depth) run in toward the shore over time and are
    // broken up by drifting noise, plus a thin contact line where the water meets the bank.
    // foamFlow drifts the noise (downstream for rivers) so the foam is carried by the water.
    float foamWidth = material_foamDistance > 0.0 ? material_foamDistance : 0.3;
    vec2 fp = FragPos.xz - foamFlow;
    float breakup = ValueNoise(fp * 0.9) * 0.55 + ValueNoise(fp * 3.1 + 7.3) * 0.3 + ValueNoise(fp * 9.0 - 2.1) * 0.15;
    float shoreZone = 1.0 - smoothstep(0.0, foamWidth, depthBelow);
    float waveFront = sin(depthBelow / max(foamWidth, 0.01) * 9.0 + time * 1.6 + breakup * 5.0) * 0.5 + 0.5;
    float lapping = smoothstep(0.55, 0.9, waveFront) * shoreZone * smoothstep(0.35, 0.6, breakup);
    float contact = 1.0 - smoothstep(0.0, foamWidth * 0.12, depthBelow);
    float foam = max(lapping * 0.8, contact * smoothstep(0.25, 0.55, breakup));
    foam = clamp(foam + extraFoam * smoothstep(0.45, 0.75, breakup), 0.0, 1.0);
    vec3 foamCol = material_foamColor.rgb == vec3(0.0) ? vec3(1.0) : material_foamColor.rgb;
    float foamLight = directionalLight.base.ambientIntensity + directionalLight.base.diffuseIntensity * max(sunDir.y, 0.0);
    color = mix(color, foamCol * foamLight, foam * 0.7);

    // --- Soft shoreline: the surface fades into the terrain over the first few cm of depth ---
    ws.alpha = smoothstep(0.0, 0.12, depthBelow);
    ws.color = color;
    return ws;
}
// =================================== END SHARED WATER MODEL ===================================

void main()
{
    if (vFadeFactor > 0.001) {
        float bayer[16] = float[16](0.0, 0.5, 0.125, 0.625, 0.75, 0.25, 0.875, 0.375, 0.1875, 0.6875, 0.0625, 0.5625, 0.9375, 0.4375, 0.8125, 0.3125);
        if (vFadeFactor > bayer[int(mod(gl_FragCoord.y, 4.0)) * 4 + int(mod(gl_FragCoord.x, 4.0))]) discard;
    }

    float dist = length(eyePosition - FragPos);
    float t = time * (material_waveSpeed > 0.0 ? material_waveSpeed : 1.0);
    float scale = material_waveScale > 0.0 ? material_waveScale : 1.0;

    // Two world-space ripple layers drifting in different directions (no visible tiling direction)
    vec2 uv = FragPos.xz * 0.06 * scale;
    vec3 r1 = SampleRipple(uv + vec2(0.021, 0.013) * t);
    vec3 r2 = SampleRipple(uv * 2.3 + vec2(-0.017, 0.029) * t);
    vec3 tn = normalize(vec3(r1.xy + r2.xy, r1.z * r2.z)); // UDN blend
    float strength = material_normalStrength > 0.0 ? material_normalStrength : 0.35;
    vec3 N = RippleToWorld(tn, strength, dist);
    // Large Gerstner swell from water.vert under the ripples
    vec3 geo = normalize(NormalWorld);
    N = normalize(N + (geo - vec3(0.0, 1.0, 0.0)));

    // Reflection: planar reflection of the lake height when available, sky cubemap otherwise
    vec3 viewDir = normalize(eyePosition - FragPos);
    vec3 sky = texture(skybox, reflect(-viewDir, N)).rgb;
    vec2 screenUV = gl_FragCoord.xy / max(screenSize, vec2(1.0));
    vec3 planar = texture(reflectionMap, clamp(screenUV + N.xz * 0.03, vec2(0.001), vec2(0.999))).rgb;
    vec3 reflection = dot(planar, planar) > 0.0001 ? planar : sky;

    WaterSample ws = ShadeWater(N, reflection, 0.0, vec2(0.12, 0.07) * t);

    vec3 finalColor = ws.color;
    float selected = max(selectionTint, vIsSelected > 0.5 ? 1.0 : 0.0);
    finalColor += vec3(0.4, 0.3, 0.0) * selected;
    colour = vec4(finalColor, ws.alpha);
}
