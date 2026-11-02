// =====================================================================
// cloud_common.glsl - the volumetric cloud layer's density, shared by the sky
// (volumetric_sky.frag) and the cloud shadow map (cloud_shadow_map.frag), so the shadows
// on the ground fall exactly under the clouds that are drawn.
//
// Clouds live in a shell 1.2-3.4 km above a spherical ground centred under the world origin.
// Density = tileable Perlin-Worley base shape (3D texture) shaped by coverage and a cumulus
// height profile (Schneider, "The Real-time Volumetric Cloudscapes of Horizon Zero Dawn", 2015).
// Call CloudSetup() once before any lookup (it sets the wind offset).
// =====================================================================

uniform float time;            ///< Current animation timer.
uniform float cloudsDensity;   ///< Sky coverage (0.1 = few clouds, 0.9 = overcast).
uniform float cloudsSpeed;     ///< Wind speed multiplier.
uniform float cloudsSharpness; ///< Edge detail / erosion strength.
#include "wind.glsl"           // The clouds drift in the scene's wind direction (Wind.h integrates it)

uniform sampler3D cloudShapeNoise;  ///< 128^3 RGBA: Perlin-Worley + Worley fBm (cloud_noise.glsl)
uniform sampler3D cloudDetailNoise; ///< 64^3 RGB: Worley fBm (edge erosion)

const float EARTH_RADIUS = 6360000.0;
const float CLOUD_BOTTOM = 1200.0;
const float CLOUD_TOP = 3400.0;
const float SHAPE_TILE = 6500.0;    // World size (m) of one repeat of the shape volume
const float DETAIL_TILE = 1100.0;   // World size (m) of one repeat of the detail volume
const float WEATHER_TILE = 60000.0;
const float SHAPE_TEXEL = SHAPE_TILE / 128.0;
const float DETAIL_TEXEL = DETAIL_TILE / 64.0;
const float SIGMA = 0.03;           // Extinction per meter at density 1

vec3 gWind;

void CloudSetup()
{
    gWind = WindActive() ? -vec3(windClouds.x, 0.0, windClouds.y)
                         : vec3(1.0, 0.0, 0.35) * time * max(cloudsSpeed, 0.0) * 100.0;
}

float Remap(float v, float lo, float hi, float newLo, float newHi)
{
    return newLo + (clamp(v, lo, hi) - lo) / max(hi - lo, 1e-5) * (newHi - newLo);
}

float HeightFraction(vec3 p)
{
    float altitude = length(p - vec3(0.0, -EARTH_RADIUS, 0.0)) - EARTH_RADIUS;
    return (altitude - CLOUD_BOTTOM) / (CLOUD_TOP - CLOUD_BOTTOM);
}

// Distances (near, far) along the ray to a sphere of radius r centred on the planet; -1 = miss
vec2 RaySphere(vec3 ro, vec3 rd, float r)
{
    vec3 oc = ro - vec3(0.0, -EARTH_RADIUS, 0.0);
    float b = dot(oc, rd);
    float c = dot(oc, oc) - r * r;
    float disc = b * b - c;
    if (disc < 0.0) return vec2(-1.0);
    float s = sqrt(disc);
    return vec2(-b - s, -b + s);
}

// Cumulus: a low-frequency "weather" field groups clouds into clusters with blue gaps; the
// Perlin-Worley base is eroded by its own Worley fBm, then thresholded by the local coverage.
// The threshold rises with height, so towers have wide bases and narrowing rounded tops.
float CloudShape(vec3 p, float h, float lod)
{
    // Flat-ish base, rounded top
    float profile = Remap(h, 0.0, 0.08, 0.0, 1.0) * Remap(h, 0.45, 1.0, 1.0, 0.0);
    if (profile <= 0.0) return 0.0;

    vec3 q = p + gWind;
    // Tops lean downwind
    q.xz += vec2(1.0, 0.35) * h * 350.0;

    float weather = textureLod(cloudShapeNoise, vec3(q.x / WEATHER_TILE, 0.37, q.z / WEATHER_TILE), 0.0).g;
    float coverage = clamp(clamp(cloudsDensity, 0.05, 0.95) * Remap(weather, 0.25, 0.85, 0.55, 1.45), 0.0, 1.0);

    vec4 n = textureLod(cloudShapeNoise, q / SHAPE_TILE, lod);
    float lowFbm = n.g * 0.625 + n.b * 0.25 + n.a * 0.125;
    float base = Remap(n.r, lowFbm - 1.0, 1.0, 0.0, 1.0);

    base *= profile;
    float threshold = 1.0 - coverage;
    return clamp(Remap(base, threshold, 1.0, 0.0, 1.0) * coverage * 1.4, 0.0, 1.0);
}

// The erosion detail's average effect on a base density (CloudDensity in volumetric_sky.frag
// with the detail noise at its mean): the thin outskirts vanish, the cores stay. For lookups
// where the detail itself is far below a pixel / texel: distant clouds and the shadow map.
float CloudErodedAverage(float d)
{
    float sharp = clamp(cloudsSharpness, 0.05, 0.5) * 2.0;
    return clamp(Remap(d, 0.25 * sharp, 1.0, 0.0, 1.0) * 2.0, 0.0, 1.0);
}
