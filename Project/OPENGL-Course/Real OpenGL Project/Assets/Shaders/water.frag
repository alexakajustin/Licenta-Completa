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
 *  - wind ripples from flow noise (FlowRipples) over the Gerstner swell of water.vert,
 *  - planar reflection (sky cubemap where it has nothing), blurred by the ripple roughness and
 *    hazed by the fog between the water and what it mirrors, Schlick Fresnel,
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
uniform sampler2D waterFoamPattern; // Tileable lacy foam pattern (water_foam_pattern.glsl)
uniform sampler2D waterFoamMap;     // Foam around obstacles + their wakes (WaterFoamMap)
uniform vec2  waterFoamOrigin;      // World xz of the foam map window's corner
uniform float waterFoamSize;        // World size of the window (0 = no foam map)
uniform vec3 eyePosition;
uniform float selectionTint;
uniform sampler2D fogReflectionDepth;   // Depth of the planar reflection (mirrored camera)
uniform int fogReflectionDepthValid;

// ===================== SHARED WATER MODEL (keep identical in river.frag) =====================
// Ripples, glint and reflections after "Where the River Goes" by Paul Malin
// (shadertoy.com/view/Xl2XRW, CC BY-NC-SA 3.0), rewritten for world units and the engine's light:
//  - ripple normals from a value-noise fBm with analytic derivatives; each octave is advected by
//    the flow in alternating directions and warped by the slope of the octaves below, so the
//    surface churns and boils instead of sliding (FlowNoise),
//  - two copies advected over staggered cycles and cross-faded, each restarting at a random
//    offset while invisible: the advection never stretches nor visibly repeats (FlowRipples),
//  - octaves finer than the pixel fade out and the slope they carried becomes specular roughness:
//    filtered at any distance instead of pixelated sparkle,
//  - GGX sun glint, reflections blurred by the same roughness (mip-mapped reflection textures).

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

// fBm of NoiseD with its octaves advected by 'flow' in alternating directions (each one a bit
// slower) and warped by the slope of the octaves below ('warp' > 0 sharpens the crests). Octaves
// finer than ~2 pixels ('footprint' = noise units per pixel) fade out; 'lost' is the share of
// slope they would have added. Returns (slope x, slope z, height).
vec3 FlowNoise(vec2 p, vec2 flow, float warp, float footprint, out float lost)
{
    vec3 sum = vec3(0.0);
    float amp = 1.0, total = 0.0, scale = 1.0;
    lost = 0.0;
    for (int i = 0; i < 4; i++) {
        p += flow;
        flow *= -0.75;
        vec3 n = NoiseD(p);
        float keep = 1.0 - smoothstep(0.3, 0.6, footprint * scale);
        sum += vec3(n.xy * keep, mix(0.5, n.z, keep)) * amp;
        lost += amp * (1.0 - keep);
        p += n.xy * warp;
        p *= 2.0;
        scale *= 2.0;
        total += amp;
        amp *= 0.75;
    }
    lost /= total;
    return sum / total;
}

vec2 CycleOffset(float cycle, float salt)
{
    return vec2(Hash21(vec2(cycle, salt)), Hash21(vec2(cycle, salt + 57.0))) * 97.0;
}

struct Ripples { vec3 normal; float height; float roughness; };

// Surface of water flowing at 'flow' (noise units per cycle) at 'p' (noise units), 't' in
// cycles: two FlowNoise copies over staggered cycles, cross-faded (Valve's flow maps), each
// restarting at a random offset while invisible. 'flatness' sets how gentle the slopes are.
// Must be called in uniform control flow (screen-space derivatives).
Ripples FlowRipples(vec2 p, vec2 flow, float warp, float flatness, float t)
{
    // Pixel footprint as an area (geometric mean): at grazing angles the depth axis is stretched
    // and its max would wipe out every octave, while the detail across the view still resolves
    vec2 fp = fwidth(p);
    float footprint = sqrt(max(fp.x * fp.y, 1e-12));
    float t0 = fract(t), t1 = fract(t + 0.5);
    float lost0, lost1;
    vec3 n0 = FlowNoise(p + CycleOffset(floor(t), 1.0), flow * (t0 - 0.5), warp, footprint, lost0);
    vec3 n1 = FlowNoise(p + CycleOffset(floor(t + 0.5), 31.0), flow * (t1 - 0.5), warp, footprint, lost1);
    float w = abs(t0 - 0.5) * 2.0; // Copy 0 is fully faded out when it restarts
    vec3 n = mix(n0, n1, w);
    Ripples r;
    r.normal = normalize(vec3(n.x, flatness, n.y));
    r.height = n.z;
    // Slope detail below the pixel (and far, grazing views) becomes roughness, above a calm minimum
    r.roughness = clamp(0.06 + mix(lost0, lost1, w) * 0.35 / flatness + footprint * 0.03, 0.06, 0.5);
    return r;
}

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
    float pattern = texture(waterFoamPattern, p * 0.25).r * 0.6 + texture(waterFoamPattern, p * 0.61 + 0.37).g * 0.4;
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
    return texture(waterFoamMap, uv).r * smoothstep(0.0, 0.1, edge);
}

// Foam cover for a foam mask in [0, 1] (Waterways-style erosion): the mask lowers the threshold
// the pattern must pass. Where foam is generated it is dense froth with bubble holes; as the
// mask fades it thins into a network of bubble walls, then isolated specks - never a flat blob.
// 'blur' (0..1, bubbles shrinking below the pixel) widens the threshold so the cover tends to
// the share of the surface the froth covers instead of speckling or vanishing.
float FoamCoverage(float mask, float pattern, float blur)
{
    float t = mix(1.0, 0.25, mask);
    float w = mix(0.08, 0.45, blur);
    return smoothstep(t - w, t + w, pattern) * smoothstep(0.0, 0.15, mask);
}

float FoamBlur(float footprint)
{
    return smoothstep(0.03, 0.12, footprint);
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
    float skyLight = directionalLight.base.ambientIntensity + directionalLight.base.diffuseIntensity * max(sunDir.y, 0.0);
    vec3 underwater = refracted * transmittance + scatter * directionalLight.base.colour * skyLight * (1.0 - transmittance);

    // --- Fresnel (Schlick, F0 = 0.02); rough water reflects a little less at grazing angles ---
    float fPow = material_fresnelPower > 0.0 ? material_fresnelPower : 5.0;
    float fresnel = 0.02 + 0.98 * pow(1.0 - clamp(dot(N, viewDir), 0.0, 1.0), fPow) * mix(1.0, 0.6, smoothstep(0.1, 0.5, roughness));
    vec3 color = mix(underwater, reflectionColor, fresnel);

    // --- GGX glint (3.0, the materials' value, is physically based) ---
    float glint = material_specularIntensityOverride > 0.0 ? material_specularIntensityOverride / 3.0 : 1.0;
    color += directionalLight.base.colour * directionalLight.base.diffuseIntensity * Glint(N, viewDir, sunDir, roughness) * glint;
    for (int i = 0; i < pointLightCount; i++) {
        vec3 toLight = pointLights[i].position - FragPos;
        float d = length(toLight);
        float att = pointLights[i].constant + pointLights[i].linear * d + pointLights[i].exponent * d * d;
        color += pointLights[i].base.colour * pointLights[i].base.diffuseIntensity * Glint(N, viewDir, toLight / d, roughness) * glint / max(att, 1e-3);
    }

    // --- Foam ---
    // Foam masks (0..1) say where foam is generated: shoreline wave fronts running in toward
    // the shore and the contact line (from the water depth), obstacles and their wakes (foam
    // map), rapids (extraFoam, from the caller). The strongest mask erodes the lacy pattern.
    // shoreWaves scales the wave fronts (weak on rivers, where they read as stripes).
    float foamWidth = material_foamDistance > 0.0 ? material_foamDistance : 0.3;
    float shoreZone = 1.0 - smoothstep(0.0, foamWidth, depthBelow);
    float waveFront = sin(depthBelow / max(foamWidth, 0.01) * 9.0 + time * 1.6 + ValueNoise(FragPos.xz * 0.7) * 5.0) * 0.5 + 0.5;
    float shoreMask = smoothstep(0.45, 0.9, waveFront) * shoreZone * shoreWaves * 0.8;
    // Contact band: a few cm of depth wide, so it frays into lace instead of drawing a line
    float contactMask = (1.0 - smoothstep(0.0, foamWidth * 0.6, depthBelow)) * 0.55;
    float mask = max(max(shoreMask, contactMask), max(SampleFoamMap(FragPos), extraFoam));
    vec2 fpw = fwidth(FragPos.xz);
    float foam = FoamCoverage(mask, pattern, FoamBlur(sqrt(max(fpw.x * fpw.y, 1e-12))));
    vec3 foamCol = material_foamColor.rgb == vec3(0.0) ? vec3(1.0) : material_foamColor.rgb;
    // Froth is a rough diffuse layer: lit by the sun on its bumps and by the sky
    vec3 foamN = normalize(vec3(N.x * 3.0, 1.0, N.z * 3.0));
    float foamLight = directionalLight.base.ambientIntensity + directionalLight.base.diffuseIntensity * (0.35 + 0.65 * max(dot(foamN, sunDir), 0.0)) * max(sunDir.y + 0.1, 0.0);
    color = mix(color, foamCol * foamLight * (0.8 + 0.2 * pattern), foam * 0.9);

    // --- Soft shoreline: the surface fades into the terrain over the first few cm of depth ---
    // (foam floats on top, so it keeps the surface visible right up to the contact line)
    ws.alpha = max(smoothstep(0.0, 0.12, depthBelow), foam * 0.75);
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

// =================================== END SHARED WATER MODEL ===================================

void main()
{
    if (vFadeFactor > 0.001) {
        float bayer[16] = float[16](0.0, 0.5, 0.125, 0.625, 0.75, 0.25, 0.875, 0.375, 0.1875, 0.6875, 0.0625, 0.5625, 0.9375, 0.4375, 0.8125, 0.3125);
        if (vFadeFactor > bayer[int(mod(gl_FragCoord.y, 4.0)) * 4 + int(mod(gl_FragCoord.x, 4.0))]) discard;
    }

    float dist = length(eyePosition - FragPos);
    float speed = material_waveSpeed > 0.0 ? material_waveSpeed : 1.0;
    float scale = material_waveScale > 0.0 ? material_waveScale : 1.0;
    float strength = material_normalStrength > 0.0 ? material_normalStrength : 0.35;

    // Wind ripples: a steady breeze carries them slowly while the octaves churn against each other
    const float CYCLE = 3.0;                 // Seconds per advection cycle
    float freq = 0.55 * scale;               // Noise cells per metre (first octave)
    vec2 wind = vec2(0.32, 0.19) * speed;    // m/s
    Ripples r = FlowRipples(FragPos.xz * freq, wind * freq * CYCLE, 0.3, 0.8 / strength, time / CYCLE);
    // Large Gerstner swell from water.vert under the ripples
    vec3 geo = normalize(NormalWorld);
    vec3 N = normalize(r.normal + (geo - vec3(0.0, 1.0, 0.0)));

    // Reflection: planar reflection of the lake height when available, sky cubemap otherwise, both
    // blurred by the roughness (mip-mapped), plus the fog between the water and what it mirrors.
    // The view's fog stops at fogRange too.
    vec3 viewDir = normalize(eyePosition - FragPos);
    vec2 screenUV = gl_FragCoord.xy / max(screenSize, vec2(1.0));
    vec2 reflUV = clamp(screenUV + N.xz * 0.04, vec2(0.001), vec2(0.999));
    vec3 planar = textureLod(reflectionMap, reflUV, 0.5 + r.roughness * 5.0).rgb;
    float skyFog = max(fogRange - dist, 0.0);
    vec3 reflection;
    if (dot(planar, planar) > 0.0001) {
        float fogLength = skyFog;
        if (fogReflectionDepthValid == 1) {
            float mirroredDepth = texture(fogReflectionDepth, reflUV).r;
            if (mirroredDepth < 1.0) {
                // The mirrored camera sees the object at this view depth; the same pixel turns view
                // depth into distance the same way, and its first 'dist' metres end at the water
                float rayScale = dist / max(LinearDepth(gl_FragCoord.z), 1e-3);
                fogLength = min(max(LinearDepth(mirroredDepth) * rayScale - dist, 0.0), skyFog);
            }
        }
        reflection = FogReflection(planar, FragPos, reflect(-viewDir, vec3(0.0, 1.0, 0.0)), fogLength);
    } else {
        vec3 skyDir = reflect(-viewDir, N);
        reflection = FogReflection(textureLod(skybox, skyDir, 1.5 + r.roughness * 6.0).rgb, FragPos, skyDir, skyFog);
    }

    // Uniform slow drift: the same offset everywhere, so the foam pattern is never sheared
    vec2 fp = fwidth(FragPos.xz);
    float pattern = FoamPattern(FragPos.xz - vec2(0.12, 0.07) * time * speed, max(fp.x, fp.y));
    WaterSample ws = ShadeWater(N, r.roughness, reflection, 0.0, pattern, 1.0);

    vec3 finalColor = ws.color;
    float selected = max(selectionTint, vIsSelected > 0.5 ? 1.0 : 0.0);
    finalColor += vec3(0.4, 0.3, 0.0) * selected;
    colour = vec4(finalColor, ws.alpha);
}
