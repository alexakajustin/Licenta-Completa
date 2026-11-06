#version 430 core

/**
 * @file water.frag
 * @brief Lake / still water.
 *
 * Shares its shading model with river.frag (keep the "SHARED WATER MODEL" sections identical) so a
 * river flowing into a lake has the same colour, reflection and foam on both sides of the junction:
 *  - water thickness along the view ray from the scene depth, Beer-Lambert absorption per channel
 *    (red is absorbed first, so shallow water is clear, deep water turns teal/blue),
 *  - refraction of the opaque scene with depth-aware distortion,
 *  - ripples (FlowingRipples): driven by the wind (drift, chop, gusts) and by the simulated
 *    current where a river's plume spreads into the lake, over the simulated waves (rings around
 *    rocks, the swash) and the Gerstner swell of water.vert,
 *  - planar reflection, the opaque scene by screen-space reflection where it has nothing, the
 *    sky cubemap beyond, blurred by the ripple roughness and hazed by the fog between the water
 *    and what it mirrors, Schlick Fresnel,
 *  - GGX sun glint; sub-pixel ripple detail turns into roughness (no sparkle at distance),
 *  - foam: shoreline wave fronts and contact line (from the water depth), froth around objects
 *    standing in the water (WaterFoamMap), all eroding one lacy bubble pattern,
 *  - a soft shoreline (alpha from water depth).
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
in float LakeMouthFade;   // RiverNode lakes fade out up the river channels they reach into

out vec4 colour;

struct Light { vec3 colour; float ambientIntensity; float diffuseIntensity; };
struct DirectionalLight { Light base; vec3 direction; };
struct PointLight { Light base; vec3 position; float constant; float linear; float exponent; };

uniform DirectionalLight directionalLight;
#include "cloud_shadow.glsl"     // Shadows of the volumetric clouds (Rendering/CloudShadows.h)
#include "gi.glsl"               // Light reaching the foam (Rendering/GlobalIllumination.h)
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
uniform sampler2D waterFoamPattern; // Tileable lacy foam pattern (water_foam_pattern.glsl)
uniform sampler2D waterFoamMap;     // Foam around obstacles + their wakes (WaterFoamMap)
uniform vec2  waterFoamOrigin;      // World xz of the foam map window's corner
uniform float waterFoamSize;        // World size of the window (0 = no foam map)
uniform sampler2D waterSimFoam;     // Foam carried by the simulated current (water_sim.glsl)
uniform vec3 eyePosition;
uniform float selectionTint;
#include "wind.glsl"           // The scene's wind: the lake's ripples drift with it and grow with it
uniform sampler2D fogReflectionDepth;   // Depth of the planar reflection (mirrored camera)
uniform int fogReflectionDepthValid;

// ===================== SHARED WATER MODEL (keep identical in river.frag) =====================
// One surface model for rivers and lakes, so a river's mouth blends into the lake without a seam
// (it started from "Where the River Goes" by Paul Malin, shadertoy.com/view/Xl2XRW, CC BY-NC-SA 3.0):
//  - ripples: a tileable ripple spectrum (water_ripple_normal.glsl) cut into cells, each turned to
//    the local current plus the wind's drift and carried along it (directional flow maps, Valve /
//    Catlike Coding), steeper with the wind's chop and the current's turbulence; the current is
//    the simulated one around the camera (water_sim.glsl): a river's plume spreads into a lake,
//    eddies spin behind rocks,
//  - mips filter the ripples far away and the lost slope becomes specular roughness; at grazing
//    angles they flatten into a blurred mirror,
//  - reflections: the planar reflection at the lake's height, the opaque scene by screen-space
//    reflection elsewhere, the sky cubemap where nothing is hit, hazed by the fog in between,
//  - GGX sun glint, Beer-Lambert water colour, refraction, foam (ShadeWater).

float LinearDepth(float depth)
{
    float ndcZ = depth * 2.0 - 1.0;
    return projection[3][2] / (ndcZ + projection[2][2]);
}

// Integer lattice hash: stable at any world coordinate (fract-of-product hashes lose precision
// hundreds of metres from the origin and start to show patterns)
float Hash21(vec2 p)
{
    uvec2 q = uvec2(ivec2(floor(p))) * uvec2(1597334673u, 3812015801u);
    uint n = (q.x ^ q.y) * 1597334673u;
    return float(n) * (1.0 / 4294967295.0);
}

float ValueNoise(vec2 p)
{
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(Hash21(i), Hash21(i + vec2(1, 0)), f.x), mix(Hash21(i + vec2(0, 1)), Hash21(i + vec2(1, 1)), f.x), f.y);
}

// Value noise with its analytic derivatives: (d/dx, d/dy, value)
vec3 NoiseD(vec2 p)
{
    vec2 i = floor(p), f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    vec2 du = 6.0 * f * (1.0 - f);
    float a = Hash21(i), b = Hash21(i + vec2(1.0, 0.0)), c = Hash21(i + vec2(0.0, 1.0)), d = Hash21(i + vec2(1.0, 1.0));
    float k = a - b - c + d;
    return vec3(du.x * (b - a + k * u.y), du.y * (c - a + k * u.x), a + (b - a) * u.x + (c - a) * u.y + k * u.x * u.y);
}

// Graphics Settings > Water > Detail Distance: how far the fine ripples and foam bubbles reach.
// 1 = up to where they would start to shimmer; 2 = twice as far. Every detail fade below reads
// the pixel footprint through this.
uniform float waterDetail;

float DetailFootprint(float footprint)
{
    return footprint / (waterDetail > 0.0 ? waterDetail : 1.0);
}

vec2 CycleOffset(float cycle, float salt)
{
    return vec2(Hash21(vec2(cycle, salt)), Hash21(vec2(cycle, salt + 57.0))) * 97.0;
}

struct Ripples { vec3 normal; float height; float roughness; };

// GGX sun glint: Trowbridge-Reitz distribution, Smith-Schlick visibility, Schlick Fresnel with
// water's F0 = 0.02. Reflected radiance per unit of light radiance.
float Glint(vec3 N, vec3 V, vec3 L, float roughness)
{
    vec3 H = normalize(V + L);
    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 1e-3);
    float NdotH = max(dot(N, H), 0.0);
    float a = max(roughness * roughness, 0.003);
    float a2 = a * a;
    float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
    float D = a2 / (3.14159265 * d * d);
    float k = a * 0.5;
    float vis = 0.25 / ((NdotL * (1.0 - k) + k) * (NdotV * (1.0 - k) + k));
    float F = 0.02 + 0.98 * pow(1.0 - max(dot(V, H), 0.0), 5.0);
    return D * vis * F * NdotL;
}

// Vertical water depth under this pixel (metres), from the opaque scene depth
float WaterDepthBelow()
{
    vec2 screenUV = gl_FragCoord.xy / max(screenSize, vec2(1.0));
    float surfaceZ = LinearDepth(gl_FragCoord.z);
    float sceneZ = LinearDepth(texture(sceneDepthMap, screenUV).r);
    float dist = length(eyePosition - FragPos);
    float thickness = max(sceneZ - surfaceZ, 0.0) * dist / max(surfaceZ, 1e-3);
    return thickness * max(normalize(eyePosition - FragPos).y, 0.05);
}

// Lacy foam pattern at world xz (the caller offsets p to advect it): two layers of the pattern
// texture at different scales and channels, so their repeats never line up. Bubbles smaller
// than the pixel ('footprint', metres per pixel) average out to the pattern's mean.
float FoamPattern(vec2 p, float footprint)
{
    // Three layers at unrelated scales and angles, read through slowly warped coordinates: one
    // layer alone repeats every few metres (a printed look)
    vec2 q = p + vec2(ValueNoise(p * 0.11), ValueNoise(p * 0.11 + 17.3)) * 2.5;
    float a = texture(waterFoamPattern, q * 0.23).r;
    float b = texture(waterFoamPattern, mat2(0.80, 0.60, -0.60, 0.80) * q * 0.57 + 0.37).g;
    float c = texture(waterFoamPattern, mat2(0.38, -0.92, 0.92, 0.38) * q * 1.31 + 0.71).r;
    float pattern = 0.58 + (a * 0.45 + b * 0.35 + c * 0.2 - 0.58) * 1.35; // Three layers average out: contrast back
    return mix(pattern, 0.58, smoothstep(0.03, 0.12, footprint) * 0.7);
}

// Foam generated by obstacles and carried downstream as wakes (top-down foam map around the
// camera, see WaterFoamMap), faded out at the edges of its window.
float SampleFoamMap(vec3 p)
{
    if (waterFoamSize <= 0.0) return 0.0;
    vec2 uv = (p.xz - waterFoamOrigin) / waterFoamSize;
    float edge = min(min(uv.x, 1.0 - uv.x), min(uv.y, 1.0 - uv.y));
    if (edge <= 0.0) return 0.0;
    // The simulated foam: born where the water hits objects, in eddies, on breaking crests, and
    // carried off by the current (the foam map's static rings only as a faint base)
    return texture(waterSimFoam, uv).r * smoothstep(0.0, 0.1, edge);
}

// Foam cover for a foam mask in [0, 1] (Waterways-style erosion): the mask lowers the threshold
// the pattern must pass. Where foam is generated it is dense froth with bubble holes; as the
// mask fades it thins into a network of bubble walls, then isolated specks - never a flat blob.
// 'blur' (0..1, bubbles shrinking below the pixel) widens the threshold so the cover tends to
// the share of the surface the froth covers instead of speckling or vanishing.
float FoamCoverage(float mask, float pattern, float blur)
{
    float t = mix(1.0, 0.5, mask); // Even dense froth stays lacy (a solid sheet looks painted on)
    float w = mix(0.16, 0.5, blur); // Soft-edged bubbles, never cut-out blobs
    return smoothstep(t - w, t + w, pattern) * smoothstep(0.0, 0.3, mask);
}

float FoamBlur(float footprint)
{
    return smoothstep(0.03, 0.12, footprint);
}

// The sun's shadow on the water: the cascaded shadow map (SceneManager binds it to every scene
// shader that declares it), four taps. The shade of trees and banks takes the sunlit scattering,
// the glint and the sunlight on the foam
uniform sampler2DArray directionalShadowMap;
uniform mat4 directionalLightTransform[4];
uniform float cascadeSplits[4];
uniform int shadowCascadeCount;
uniform float shadowCascadeDepthRange[4];
uniform mat4 viewMatrix;

float SunVisibility(vec3 p)
{
    float viewDepth = -(viewMatrix * vec4(p, 1.0)).z;
    int layer = -1;
    for (int i = 0; i < shadowCascadeCount; i++) {
        if (viewDepth < cascadeSplits[i]) { layer = i; break; }
    }
    if (layer < 0) return 1.0;
    vec4 ls = directionalLightTransform[layer] * vec4(p, 1.0);
    vec3 c = ls.xyz / ls.w * 0.5 + 0.5;
    if (any(lessThan(c, vec3(0.0))) || any(greaterThan(c, vec3(1.0)))) return 1.0;
    float bias = 0.05 / max(shadowCascadeDepthRange[layer], 1.0);
    vec2 texel = 1.5 / vec2(textureSize(directionalShadowMap, 0).xy);
    float lit = 0.0;
    for (int k = 0; k < 4; k++) {
        vec2 o = (vec2(float(k & 1), float(k >> 1)) - 0.5) * texel;
        lit += (c.z - bias > texture(directionalShadowMap, vec3(c.xy + o, float(layer))).r) ? 0.0 : 1.0;
    }
    return lit * 0.25;
}

struct WaterSample { vec3 color; float alpha; };

WaterSample ShadeWater(vec3 N, float roughness, vec3 reflectionColor, float extraFoam, float pattern, float shoreWaves)
{
    WaterSample ws;
    vec3 viewDir = normalize(eyePosition - FragPos);
    float dist = length(eyePosition - FragPos);
    vec2 screenUV = gl_FragCoord.xy / max(screenSize, vec2(1.0));

    // --- Water thickness along the view ray, and vertical depth below the surface ---
    float surfaceZ = LinearDepth(gl_FragCoord.z);
    float sceneZ = LinearDepth(texture(sceneDepthMap, screenUV).r);
    float rayScale = dist / max(surfaceZ, 1e-3);                   // view-z -> ray length
    float depthBelow = max(sceneZ - surfaceZ, 0.0) * rayScale * max(viewDir.y, 0.05); // ~ vertical water depth

    // --- Refraction (distortion fades out in very shallow water, no foreground bleeding) ---
    vec2 offset = N.xz * 0.05 * clamp(depthBelow * 0.5, 0.0, 1.0) / max(1.0, dist * 0.02);
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
    // In the shade of a cloud, a tree or a bank the water loses its sun glint and sunlit colour
    float cloudSun = CloudShadow(FragPos) * SunVisibility(FragPos);
    float skyLight = directionalLight.base.ambientIntensity + directionalLight.base.diffuseIntensity * max(sunDir.y, 0.0) * cloudSun;
    vec3 underwater = refracted * transmittance + scatter * directionalLight.base.colour * skyLight * (1.0 - transmittance);

    // --- Fresnel (Schlick, F0 = 0.02); rough water reflects a little less at grazing angles ---
    float fPow = material_fresnelPower > 0.0 ? material_fresnelPower : 5.0;
    float fresnel = 0.02 + 0.98 * pow(1.0 - clamp(dot(N, viewDir), 0.0, 1.0), fPow) * mix(1.0, 0.6, smoothstep(0.1, 0.5, roughness));
    vec3 color = mix(underwater, reflectionColor, fresnel);

    // --- GGX glint (3.0, the materials' value, is physically based) ---
    float glint = material_specularIntensityOverride > 0.0 ? material_specularIntensityOverride / 3.0 : 1.0;
    color += directionalLight.base.colour * directionalLight.base.diffuseIntensity * Glint(N, viewDir, sunDir, roughness) * glint * cloudSun;
    for (int i = 0; i < pointLightCount; i++) {
        vec3 toLight = pointLights[i].position - FragPos;
        float d = length(toLight);
        float att = pointLights[i].constant + pointLights[i].linear * d + pointLights[i].exponent * d * d;
        color += pointLights[i].base.colour * pointLights[i].base.diffuseIntensity * Glint(N, viewDir, toLight / d, roughness) * glint / max(att, 1e-3);
    }

    // --- Foam ---
    // Foam needs water under it: the meshes run into the banks, where the depth is ~0 (foam
    // drawn there stood out of the water and ended on the meshes' straight edges)
    float foamWidth = material_foamDistance > 0.0 ? material_foamDistance : 0.3;
    float wet = smoothstep(0.005, 0.03, depthBelow);
    // Shore: the swash. Waves reach the shore one after another, every stretch of shore on its
    // own phase (they arrive at an angle, never all at once). Each runs in as a line of white
    // water to the waterline and leaves froth behind that thins into lace while it drains
    // back, over the next waves. s: 0 at the waterline, 1 at the outer edge of the swash zone.
    // shoreWaves scales it (weak on rivers: no breakers there).
    const float SWASH_PERIOD = 5.0, RUN_IN = 0.55;
    float s = depthBelow / max(foamWidth, 0.01);
    float shorePhase = fract(time / SWASH_PERIOD + ValueNoise(FragPos.xz * 0.07) * 1.6 + ValueNoise(FragPos.xz * 0.23) * 0.4);
    float front = 1.0 - clamp(shorePhase / RUN_IN, 0.0, 1.0);
    float frontLine = shorePhase < RUN_IN ? exp(-pow((s - front) / 0.1, 2.0)) * (1.0 - 0.5 * shorePhase / RUN_IN) : 0.0;
    float sincePassed = fract(shorePhase - RUN_IN * (1.0 - clamp(s, 0.0, 1.0))) * SWASH_PERIOD; // Seconds
    float trail = exp(-sincePassed / 1.4) * 0.75;
    float shoreMask = max(frontLine, trail) * (1.0 - smoothstep(0.6, 1.0, s)) * shoreWaves;
    // Weak sources (a little turbulence) only cloud the water; froth needs real churning
    float mask = smoothstep(0.06, 1.0, max(shoreMask, max(SampleFoamMap(FragPos), extraFoam))) * wet;
    vec2 fpw = fwidth(FragPos.xz);
    float foam = FoamCoverage(mask, pattern, FoamBlur(sqrt(max(fpw.x * fpw.y, 1e-12))));
    vec3 foamCol = material_foamColor.rgb == vec3(0.0) ? vec3(1.0) : material_foamColor.rgb;
    // Froth is a rough diffuse layer (albedo ~0.75), lit like everything around it: the sky and
    // bounced light that actually reach it (GI probes: dim under trees, in a gorge) and the sun
    // where it reaches it (its shadows, the clouds). Lit by the flat ambient it glowed in the shade.
    vec3 foamN = normalize(vec3(N.x * 3.0, 1.0, N.z * 3.0));
    vec3 foamAmbient = GIAmbient(FragPos, vec3(0.0, 1.0, 0.0), viewDir, directionalLight.base.colour * directionalLight.base.ambientIntensity);
    vec3 foamSun = directionalLight.base.colour * directionalLight.base.diffuseIntensity
                 * (0.3 + 0.7 * max(dot(foamN, sunDir), 0.0)) * max(sunDir.y, 0.0) * cloudSun;
    vec3 foamLit = foamCol * 0.75 * (foamAmbient + foamSun);
    // Aerated water: air bubbles under the froth whiten the water itself (milky white water)
    color = mix(color, foamLit * 0.8, mask * 0.3);
    color = mix(color, foamLit * (0.85 + 0.15 * pattern), foam * 0.8);

    // --- Soft shoreline: the surface fades into the terrain over the first few cm of depth ---
    // (foam floats on top, so it keeps the surface visible right up to the contact line)
    ws.alpha = max(smoothstep(0.0, 0.12, depthBelow), foam * 0.7);
    ws.color = color;
    return ws;
}

// --- Fog along reflections ---
// The volumetric fog (VolumetricFog) covers the way from the camera to the water surface, but the
// light a reflection shows also crossed the fog from the surface to what is mirrored. This is the
// same medium in closed form: height fog (full density below the base height, exponential falloff
// above) lit by the sun with the Henyey-Greenstein phase and by the sky, without shadows or wisps.
uniform int   fogEnabled;
uniform float fogDensity;
uniform float fogBaseHeight;
uniform float fogHeightFalloff;
uniform float fogRange;
uniform float fogAnisotropy;
uniform vec3  fogAlbedo;
uniform vec3  fogSunRadiance;     // Same values as the volumetric fog (VolumetricFog::LightRadiance)
uniform vec3  fogAmbientRadiance;

// Optical depth of the fog along start + dir * t, t in [0, len]
float FogOpticalDepth(vec3 start, vec3 dir, float len)
{
    float h0 = start.y - fogBaseHeight;
    float k = max(fogHeightFalloff, 1e-5);
    if (abs(dir.y) < 1e-4) return fogDensity * len * (h0 <= 0.0 ? 1.0 : exp(-k * h0));
    float crossing = -h0 / dir.y;                               // Where the ray passes the base height
    float ta = dir.y > 0.0 ? clamp(crossing, 0.0, len) : 0.0;   // Part of the ray above it
    float tb = dir.y > 0.0 ? len : clamp(crossing, 0.0, len);
    float above = tb > ta ? (exp(-k * (h0 + dir.y * ta)) - exp(-k * (h0 + dir.y * tb))) / (k * dir.y) : 0.0;
    return fogDensity * (above + (len - max(tb - ta, 0.0)));
}

// A reflection after crossing len metres of fog from the water surface along dir
// How bright the sky is for this scene's light: the sky shaders draw it at full strength, the
// rest of the scene is lit by the sun's ambient + diffuse. Under a dimmed sun (dusk, a moody
// scene) the mirrored sky dims with everything else instead of glowing; at or above the default
// light (0.4 + 0.6) it stays as drawn.
float SkyReflectionLevel()
{
    return clamp(directionalLight.base.ambientIntensity + directionalLight.base.diffuseIntensity, 0.0, 1.0);
}

vec3 FogReflection(vec3 reflected, vec3 start, vec3 dir, float len)
{
    if (fogEnabled == 0 || len <= 0.0) return reflected;
    float transmittance = exp(-FogOpticalDepth(start, dir, len));
    float g = fogAnisotropy;
    float cosTheta = dot(dir, normalize(-directionalLight.direction));
    float phase = (1.0 - g * g) / pow(max(1.0 + g * g - 2.0 * g * cosTheta, 1e-4), 1.5);
    vec3 light = fogAlbedo * (fogSunRadiance * phase + fogAmbientRadiance) * (1.0 - transmittance);
    // Same roll-off as fog_apply.frag: the scene is lit straight into LDR
    float peak = max(max(light.r, light.g), light.b);
    if (peak > 0.7) light *= (0.7 + 0.3 * (1.0 - exp(-(peak - 0.7) / 0.3))) / peak;
    return reflected * transmittance + light;
}

// --- The water's surface: current, ripples, reflections (rivers and lakes alike) ---
uniform sampler2D waterSimVelocity;   // Simulated current (m/s, xz) around the camera (water_sim.glsl)
uniform sampler2D waterSimWave;       // Simulated surface: height, rate
uniform sampler2D waterRipples;       // Tileable ripple slopes (water_ripple_normal.glsl)
uniform mat4 view;
uniform float waterReflectionHeight;  // Height the planar reflection was rendered for (the lake)
uniform int waterReflectionValid;

// Where xz is in the simulation's window (uv) and how far inside it (0 at its edge, < 0 outside)
float SimWindow(vec2 xz, out vec2 uv)
{
    uv = waterFoamSize > 0.0 ? (xz - waterFoamOrigin) / waterFoamSize : vec2(-1.0);
    return min(min(uv.x, 1.0 - uv.x), min(uv.y, 1.0 - uv.y));
}

// The current at a point: the simulated one inside its window (not on a bank or in a rock: no
// water there), else 'fallback'
vec2 CurrentAt(vec2 xz, vec2 fallback)
{
    vec2 uv;
    float edge = SimWindow(xz, uv);
    if (edge <= 0.0) return fallback;
    vec2 v = textureLod(waterSimVelocity, uv, 0.0).xy;
    if (dot(v, v) < 1e-6) return fallback;
    return mix(fallback, v, smoothstep(0.02, 0.1, edge));
}

// Slope of the simulated surface at xz: the waves rocks shed, a river's plume, the swash
vec2 SimulatedSlope(vec2 xz)
{
    vec2 uv;
    float edge = SimWindow(xz, uv);
    if (edge <= 0.0) return vec2(0.0);
    float texel = 1.0 / float(textureSize(waterSimWave, 0).x);
    float hx = textureLod(waterSimWave, uv + vec2(texel, 0.0), 0.0).x - textureLod(waterSimWave, uv - vec2(texel, 0.0), 0.0).x;
    float hz = textureLod(waterSimWave, uv + vec2(0.0, texel), 0.0).x - textureLod(waterSimWave, uv - vec2(0.0, texel), 0.0).x;
    return vec2(hx, hz) / (2.0 * waterFoamSize * texel) * smoothstep(0.0, 0.08, edge);
}

// The wind's drift of the surface (m/s): ripples run downwind; a fast current's own turbulence
// hides it
vec2 WindDrift(vec2 current)
{
    vec2 windDir = WindActive() ? windDirSpeed.xy : normalize(vec2(0.32, 0.19));
    float windMs = WindActive() ? windDirSpeed.z : 4.0;
    return windDir * (0.12 + 0.05 * windMs) * (1.0 - smoothstep(0.2, 0.6, length(current)));
}

// How steep the ripples are: the wind's chop (glassy in a calm, choppy in a gale) and the
// current's turbulence. The wind touches still water in patches tens of metres across, drifting
// downwind ("cat's paws"): rippled there, glassy between them - the mirror a lake is known for
float RippleGain(vec2 xz, float currentSpeed, float strength)
{
    vec2 windDir = WindActive() ? windDirSpeed.xy : normalize(vec2(0.32, 0.19));
    float windMs = WindActive() ? windDirSpeed.z * WindGust(xz) : 4.0;
    vec2 q = (xz - windDir * time * (0.6 + 0.2 * windMs)) / 45.0;
    float paws = ValueNoise(q) * 0.65 + ValueNoise(q * 2.3 + 7.1) * 0.35;
    float patches = mix(0.2, 1.35, smoothstep(0.35, 0.7, paws));
    float wind = (mix(0.05, 0.22, smoothstep(0.0, 8.0, windMs)) + 0.1 * smoothstep(10.0, 20.0, windMs)) * patches;
    return strength * (wind + 0.2 * clamp(currentSpeed / 1.8, 0.0, 1.0));
}

// Ripples on moving water at p: 'current' is the current there (m/s; the cells read the simulated
// one at their centres where it reaches), the wind's drift is added. The ripple spectrum is cut
// into cells, each rotated about its own centre to its velocity (one rotation about a fixed point
// would swirl the pattern around bends) and the four nearest blended ("directional flow", Catlike
// Coding), each with a random offset; each is carried along its velocity over two staggered
// cycles cross-faded (Valve's flow maps). A current draws its ripples out along itself; wind
// waves run downwind with their crests across it: the pattern turns with the wind's share of the
// velocity. 'gain': RippleGain. Must be called in uniform control flow (derivatives).
Ripples FlowingRipples(vec2 p, vec2 current, vec2 fallbackDir, float scale, float gain)
{
    const float CELL = 3.0;                             // Metres per direction cell
    const float CYCLE = 1.6;                            // Seconds per advection cycle
    const float TILE_LARGE = 3.2, TILE_SMALL = 0.9;     // Metres per ripple tile
    const vec2 STRETCH = vec2(1.0, 1.0 / 2.2);          // Streaks along the flow (large scale)
    const vec2 STRETCH_SMALL = vec2(1.0, 1.0 / 1.5);
    float t = time / CYCLE;
    float t0 = fract(t), t1 = fract(t + 0.5);
    float phase = abs(t0 - 0.5) * 2.0;                  // Copy 0 is invisible when it restarts
    vec2 o0 = CycleOffset(floor(t), 3.0), o1 = CycleOffset(floor(t + 0.5), 41.0);
    vec2 dpdx = dFdx(p), dpdy = dFdy(p);
    vec2 kl = STRETCH * (scale / TILE_LARGE), ks = STRETCH_SMALL * (scale / TILE_SMALL);

    vec2 g = p / CELL - 0.5;
    vec2 i0 = floor(g), f = g - i0;
    f = f * f * (3.0 - 2.0 * f);                        // Mostly the nearest cell: less blur
    vec2 slope = vec2(0.0);
    float weight2 = 0.0;
    for (int k = 0; k < 4; k++) {
        vec2 off = vec2(float(k & 1), float(k >> 1));
        float w = mix(1.0 - f.x, f.x, off.x) * mix(1.0 - f.y, f.y, off.y);
        vec2 cellId = i0 + off;
        vec2 centre = (cellId + 0.5) * CELL;
        vec2 c = CurrentAt(centre, current);
        vec2 drift = WindDrift(c);
        vec2 v = c + drift;
        float speed = length(v);
        vec2 dir = speed > 0.02 ? v / speed : fallbackDir;
        // The pattern's long axis: along a current, across the wind (its crests)
        float windShare = length(drift) / max(length(c) + length(drift), 1e-4);
        float turn = 1.5707963 * smoothstep(0.3, 0.7, windShare);
        vec2 along = cos(turn) * dir + sin(turn) * vec2(-dir.y, dir.x);
        vec2 across = vec2(-along.y, along.x);
        // Metres across / along the pattern's axis, from the cell's centre, plus the cell's offset
        vec2 d = p - centre;
        vec2 q = vec2(dot(d, across), dot(d, along)) + vec2(Hash21(cellId), Hash21(cellId + vec2(531.0, 77.0))) * 37.0;
        vec2 qdx = vec2(dot(dpdx, across), dot(dpdx, along)), qdy = vec2(dot(dpdy, across), dot(dpdy, along));
        vec2 travel = vec2(dot(v, across), dot(v, along)) * CYCLE;
        vec2 qa = q - travel * (t0 - 0.5), qb = q - travel * (t1 - 0.5);
        // Explicit gradients: the neighbouring pixel may sit in another cell (another rotation)
        vec2 sl = mix(textureGrad(waterRipples, qa * kl + o0, qdx * kl, qdy * kl).rg,
                      textureGrad(waterRipples, qb * kl + o1, qdx * kl, qdy * kl).rg, phase) * STRETCH;
        vec2 ss = mix(textureGrad(waterRipples, qa * ks + o0 * 1.7, qdx * ks, qdy * ks).rg,
                      textureGrad(waterRipples, qb * ks + o1 * 1.7, qdx * ks, qdy * ks).rg, phase) * STRETCH_SMALL;
        vec2 s = sl * 0.75 + ss * 0.3;                  // Across, along this cell's velocity
        slope += w * (s.x * across + s.y * along);
        weight2 += w * w;
    }
    slope /= sqrt(max(weight2, 1e-4));                  // Uncorrelated cells blended: one's contrast
    vec2 world = slope * gain;
    Ripples r;
    r.normal = normalize(vec3(-world.x, 1.0, -world.y));
    r.height = 0.5;
    vec2 fp = fwidth(p);
    float footprint = DetailFootprint(sqrt(max(fp.x * fp.y, 1e-12)));
    r.roughness = clamp(0.035 + footprint * 0.06 + gain * 0.05, 0.035, 0.45);
    return r;
}

// At a grazing angle the eye sees only the faces of the ripples turned up toward it, the troughs
// hide behind the crests: the surface reads as a blurred mirror. Their slope fades, its detail
// becomes roughness (else rays dip under the horizon in bands). Returns the factor (the caller
// flattens the larger waves with it too).
float FlattenAtGrazing(inout Ripples r, vec3 viewDir)
{
    float graze = clamp(viewDir.y * 4.0, 0.15, 1.0);
    r.normal = normalize(vec3(r.normal.x * graze, r.normal.y, r.normal.z * graze));
    r.roughness = min(r.roughness + (1.0 - graze) * 0.1, 0.45);
    return graze;
}

// Screen-space reflection: the reflected ray marched through the opaque scene's depth
// (sceneDepthMap) in growing steps, refined by bisection where it passes behind a surface, which
// gives its colour (refractionMap: the opaque scene). a = confidence (0 off screen, behind the
// camera, in the sky, or near the screen's edge: there the sky reflection stays).
vec4 ScreenSpaceReflection(vec3 origin, vec3 dir, float roughness)
{
    mat4 viewProj = projection * view;
    float t = 0.1, stepLength = 0.25;
    for (int i = 0; i < 24; i++) {
        vec4 clip = viewProj * vec4(origin + dir * t, 1.0);
        if (clip.w <= 0.05) break;
        vec2 uv = clip.xy / clip.w * 0.5 + 0.5;
        if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))) break;
        float sceneDistance = LinearDepth(textureLod(sceneDepthMap, uv, 0.0).r);
        float behind = clip.w - sceneDistance;
        if (behind > 0.0 && behind < max(stepLength * 2.5, 0.5)) {
            float a = t - stepLength, b = t;
            for (int k = 0; k < 5; k++) {
                float m = (a + b) * 0.5;
                vec4 c = viewProj * vec4(origin + dir * m, 1.0);
                vec2 u = c.xy / c.w * 0.5 + 0.5;
                if (c.w > LinearDepth(textureLod(sceneDepthMap, u, 0.0).r)) b = m; else a = m;
            }
            vec4 hit = viewProj * vec4(origin + dir * b, 1.0);
            uv = hit.xy / hit.w * 0.5 + 0.5;
            if (textureLod(sceneDepthMap, uv, 0.0).r >= 1.0) break; // The sky
            float edge = min(min(uv.x, 1.0 - uv.x), min(uv.y, 1.0 - uv.y));
            float confidence = smoothstep(0.0, 0.06, edge) * (1.0 - smoothstep(0.7, 1.0, float(i) / 24.0));
            // Rough water blurs it: a few taps around the hit
            vec2 r = vec2(roughness * 0.025) * vec2(screenSize.y / max(screenSize.x, 1.0), 1.0);
            vec3 colour = textureLod(refractionMap, uv, 0.0).rgb * 0.4
                        + (textureLod(refractionMap, uv + vec2(r.x, 0.0), 0.0).rgb + textureLod(refractionMap, uv - vec2(r.x, 0.0), 0.0).rgb
                         + textureLod(refractionMap, uv + vec2(0.0, r.y), 0.0).rgb + textureLod(refractionMap, uv - vec2(0.0, r.y), 0.0).rgb) * 0.15;
            return vec4(colour, confidence);
        }
        stepLength *= 1.22;
        t += stepLength;
    }
    return vec4(0.0);
}

// What the water mirrors for normal N: the sky cubemap along the reflected ray (kept just above
// the horizon: a ripple turned toward the eye at a grazing angle would send it down into the
// water, where it would see the next wave, as good as the sky just above the horizon); where the
// water lies at (or near) the height the planar reflection was rendered for, that mirror (the
// whole scene, off screen too); elsewhere the opaque scene by screen-space reflection. Hazed by
// the fog between the water and what it mirrors (the view's fog stops at fogRange, so does this).
vec3 WaterReflection(vec3 N, vec3 viewDir, float roughness, float dist)
{
    vec3 reflected = reflect(-viewDir, N);
    reflected = normalize(vec3(reflected.x, max(reflected.y, 0.03), reflected.z));
    float skyFog = max(fogRange - dist, 0.0);
    vec3 reflection = FogReflection(textureLod(skybox, reflected, 1.5 + roughness * 6.0).rgb * SkyReflectionLevel(), FragPos, reflected, skyFog);

    float planarWeight = waterReflectionValid == 1 ? 1.0 - smoothstep(0.5, 3.0, abs(FragPos.y - waterReflectionHeight)) : 0.0;
    if (planarWeight > 0.0) {
        vec2 screenUV = gl_FragCoord.xy / max(screenSize, vec2(1.0));
        vec2 reflUV = clamp(screenUV + N.xz * 0.04, vec2(0.001), vec2(0.999));
        vec3 planar = textureLod(reflectionMap, reflUV, 0.5 + roughness * 5.0).rgb;
        if (dot(planar, planar) > 0.0001) {
            float fogLength = skyFog;
            if (fogReflectionDepthValid == 1) {
                float mirroredDepth = textureLod(fogReflectionDepth, reflUV, 0.0).r;
                if (mirroredDepth >= 1.0) planar *= SkyReflectionLevel(); // The sky part
                if (mirroredDepth < 1.0) {
                    // The mirrored camera sees the object at this view depth; the same pixel turns
                    // view depth into distance the same way, its first 'dist' metres end at the water
                    float rayScale = dist / max(LinearDepth(gl_FragCoord.z), 1e-3);
                    fogLength = min(max(LinearDepth(mirroredDepth) * rayScale - dist, 0.0), skyFog);
                }
            }
            reflection = mix(reflection, FogReflection(planar, FragPos, reflect(-viewDir, vec3(0.0, 1.0, 0.0)), fogLength), planarWeight);
        } else {
            planarWeight = 0.0; // Nothing rendered there
        }
    }

    // Banks, rocks, trees where the planar mirror does not reach (skipped where the reflection
    // barely shows: seen from above, Fresnel ~2 %, or far away)
    float reflectance = 0.02 + 0.98 * pow(1.0 - max(dot(N, viewDir), 0.0), 5.0);
    if (planarWeight < 0.99 && reflectance > 0.06 && dist < 150.0) {
        vec4 ssr = ScreenSpaceReflection(FragPos + N * 0.02, reflected, roughness);
        ssr.a *= (1.0 - smoothstep(100.0, 150.0, dist)) * (1.0 - planarWeight);
        reflection = mix(reflection, ssr.rgb, ssr.a);
    }
    return reflection;
}

// The foam pattern carried by 'velocity' (m/s): two copies over staggered cycles like the ripples,
// each restarting at a random spot while invisible, so it never shears or visibly repeats
float FlowingFoamPattern(vec2 p, vec2 velocity, float footprint)
{
    const float FOAM_CYCLE = 4.0;
    float c0 = fract(time / FOAM_CYCLE), c1 = fract(time / FOAM_CYCLE + 0.5);
    vec2 travel = velocity * FOAM_CYCLE;
    float a = FoamPattern(p - travel * c0 + CycleOffset(floor(time / FOAM_CYCLE), 5.0), footprint);
    float b = FoamPattern(p - travel * c1 + CycleOffset(floor(time / FOAM_CYCLE + 0.5), 71.0), footprint);
    float w = 1.0 - abs(1.0 - 2.0 * c0);
    // Averaging two patterns compresses their contrast mid-cycle (the foam would pulse):
    // renormalize around the pattern's mean
    return 0.58 + (mix(b, a, w) - 0.58) / sqrt(w * w + (1.0 - w) * (1.0 - w));
}

// =================================== END SHARED WATER MODEL ===================================

void main()
{
    if (vFadeFactor > 0.001) {
        float bayer[16] = float[16](0.0, 0.5, 0.125, 0.625, 0.75, 0.25, 0.875, 0.375, 0.1875, 0.6875, 0.0625, 0.5625, 0.9375, 0.4375, 0.8125, 0.3125);
        if (vFadeFactor > bayer[int(mod(gl_FragCoord.y, 4.0)) * 4 + int(mod(gl_FragCoord.x, 4.0))]) discard;
    }

    float dist = length(eyePosition - FragPos);
    float scale = material_waveScale > 0.0 ? material_waveScale : 1.0;
    float strength = (material_normalStrength > 0.0 ? material_normalStrength : 0.35) / 0.35;

    // The current: a river's plume spreading into the lake and slowing, eddies around rocks (the
    // simulation, around the camera); still water elsewhere. The wind drifts the ripples downwind
    // and sets their chop (glassy in a calm, cat's paws in the gusts)
    vec2 current = CurrentAt(FragPos.xz, vec2(0.0));
    float currentSpeed = length(current);
    vec2 windDir = WindActive() ? windDirSpeed.xy : normalize(vec2(0.32, 0.19));
    Ripples r = FlowingRipples(FragPos.xz, current, windDir, scale, RippleGain(FragPos.xz, currentSpeed, strength));
    vec3 viewDir = normalize(eyePosition - FragPos);
    float graze = FlattenAtGrazing(r, viewDir);
    // Under the ripples: the simulated waves (rings around rocks, the plume's chop, the swash)
    // and the swell of water.vert
    vec2 simSlope = SimulatedSlope(FragPos.xz);
    vec3 swell = normalize(NormalWorld) - vec3(0.0, 1.0, 0.0);
    vec3 N = normalize(r.normal + (vec3(-simSlope.x, 0.0, -simSlope.y) + swell) * graze);

    vec3 reflection = WaterReflection(N, viewDir, r.roughness, dist);

    // Foam carried by the current and the wind's drift
    vec2 fp = fwidth(FragPos.xz);
    float pattern = FlowingFoamPattern(FragPos.xz, current + WindDrift(current), DetailFootprint(max(fp.x, fp.y)));
    WaterSample ws = ShadeWater(N, r.roughness, reflection, 0.0, pattern, 1.0);

    vec3 finalColor = ws.color;
    float selected = max(selectionTint, vIsSelected > 0.5 ? 1.0 : 0.0);
    finalColor += vec3(0.4, 0.3, 0.0) * selected;
    // Where the lake runs up a river channel it fades out over the river: no line where they meet
    colour = vec4(finalColor, ws.alpha * smoothstep(0.0, 1.0, LakeMouthFade));
}
