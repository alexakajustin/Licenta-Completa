// =====================================================================
// atmosphere_common.glsl - a physically based atmosphere (Hillaire, "A Scalable and Production
// Ready Sky and Atmosphere Rendering Technique", EGSR 2020; the model of Unreal's SkyAtmosphere).
//
// The planet is a sphere under the world origin (the same one the cloud shell bends around):
// world height y is the altitude in metres. The air holds three things:
//  - Rayleigh (molecules): scatters blue far more than red - the blue sky, the red sunset,
//  - Mie (aerosols, haze): scatters all colours alike, mostly forward - the white glow around the
//    sun, the haze that fades distant hills. Its amount is the scene's visibility,
//  - ozone: absorbs orange - the deep blue-violet of the zenith at twilight.
// Three look-up textures (Rendering/Atmosphere.cpp) hold what is expensive:
//  - transmittance: how much sunlight is left at an altitude, for a sun elevation,
//  - multiple scattering: the light scattered more than once (the sky lit by the sky),
//  - sky view: the sky's colour in every direction from the camera.
// All of it in metres; the result is radiance per unit sun illuminance (times sunIlluminance).
// The engine's scene colours are display-encoded (post_tonemap.frag decodes them with a 2.2 power
// before its curve): what the atmosphere computes in linear light is encoded with AtmoEncode
// before it meets them, so its ratios (the sky 6 EV darker at the blue hour) stay true on screen.
// =====================================================================

const float ATMO_PI = 3.14159265;
const float ATMO_BOTTOM = 6360000.0;
const float ATMO_TOP = 6460000.0;

uniform vec3 rayleighScattering;   ///< Per metre at sea level (5.8, 13.6, 33.1) e-6
uniform float rayleighScaleHeight; ///< 8000 m
uniform float mieScattering;       ///< Per metre at sea level (from the visibility)
uniform float mieExtinction;
uniform float mieScaleHeight;      ///< 1200 m: haze stays low
uniform float mieG;                ///< 0.8: strongly forward
uniform vec3 ozoneAbsorption;      ///< Per metre at the layer's peak (0.65, 1.88, 0.085) e-6
uniform vec3 groundAlbedo;
uniform vec3 sunIlluminance;       ///< The sun's colour and strength above the atmosphere
uniform float sunAngularRadius;    ///< Radians (0.0047: the real sun)

uniform sampler2D transmittanceLUT;  ///< 256 x 64
uniform sampler2D multiScatterLUT;   ///< 32 x 32
uniform sampler2D skyViewLUT;        ///< 192 x 108, around the camera

struct AtmoSample { vec3 scattering; vec3 extinction; vec3 rayleigh; float mie; };

// More air per metre (aerial perspective over a small map: its distances count this many times)
float gAtmoDensityScale = 1.0;

AtmoSample AtmoMedium(float altitude)
{
    altitude = max(altitude, 0.0);
    float r = exp(-altitude / rayleighScaleHeight);
    float m = exp(-altitude / mieScaleHeight);
    float o = max(0.0, 1.0 - abs(altitude - 25000.0) / 15000.0);
    AtmoSample s;
    s.rayleigh = rayleighScattering * r;
    s.mie = mieScattering * m;
    s.scattering = s.rayleigh + vec3(s.mie);
    s.extinction = s.rayleigh + vec3(mieExtinction * m) + ozoneAbsorption * o;
    s.rayleigh *= gAtmoDensityScale;
    s.mie *= gAtmoDensityScale;
    s.scattering *= gAtmoDensityScale;
    s.extinction *= gAtmoDensityScale;
    return s;
}

float AtmoRayleighPhase(float c) { return 3.0 / (16.0 * ATMO_PI) * (1.0 + c * c); }
float AtmoMiePhase(float c)
{
    // Cornette-Shanks
    float g = mieG, g2 = g * g;
    float k = 3.0 / (8.0 * ATMO_PI) * (1.0 - g2) / (2.0 + g2);
    return k * (1.0 + c * c) / pow(max(1.0 + g2 - 2.0 * g * c, 1e-4), 1.5);
}

// Distance along a ray from radius r (direction mu = cos to the zenith) to a sphere; < 0: none
float AtmoRaySphere(float r, float mu, float R)
{
    float d = r * r * (mu * mu - 1.0) + R * R;
    if (d < 0.0) return -1.0;
    float s = sqrt(d);
    float t0 = -r * mu - s, t1 = -r * mu + s;
    return t0 > 0.0 ? t0 : t1;
}

// Transmittance LUT parametrisation (Bruneton): (r, mu) <-> uv, finest near the horizon
vec2 AtmoTransmittanceUV(float r, float mu)
{
    float H = sqrt(ATMO_TOP * ATMO_TOP - ATMO_BOTTOM * ATMO_BOTTOM);
    float rho = sqrt(max(r * r - ATMO_BOTTOM * ATMO_BOTTOM, 0.0));
    float disc = r * r * (mu * mu - 1.0) + ATMO_TOP * ATMO_TOP;
    float d = max(0.0, -r * mu + sqrt(max(disc, 0.0)));
    float dMin = ATMO_TOP - r, dMax = rho + H;
    return vec2((d - dMin) / max(dMax - dMin, 1e-3), rho / H);
}

void AtmoTransmittanceRMu(vec2 uv, out float r, out float mu)
{
    float H = sqrt(ATMO_TOP * ATMO_TOP - ATMO_BOTTOM * ATMO_BOTTOM);
    float rho = H * uv.y;
    r = sqrt(rho * rho + ATMO_BOTTOM * ATMO_BOTTOM);
    float dMin = ATMO_TOP - r, dMax = rho + H;
    float d = dMin + uv.x * (dMax - dMin);
    mu = d == 0.0 ? 1.0 : (H * H - rho * rho - d * d) / (2.0 * r * d);
    mu = clamp(mu, -1.0, 1.0);
}

// Sunlight left at radius r toward a direction at mu from the zenith (0 behind the planet)
vec3 AtmoTransmittance(float r, float mu)
{
    return texture(transmittanceLUT, AtmoTransmittanceUV(r, mu)).rgb;
}

// The sun's light reaching a point, with the planet's shadow softened over the sun's disc
vec3 AtmoSunlight(float r, float muSun)
{
    float horizonMu = -sqrt(max(1.0 - (ATMO_BOTTOM / r) * (ATMO_BOTTOM / r), 0.0));
    float visible = smoothstep(-sunAngularRadius, sunAngularRadius, muSun - horizonMu);
    return AtmoTransmittance(r, muSun) * visible;
}

vec3 AtmoMultiScatter(float r, float muSun)
{
    vec2 uv = vec2(muSun * 0.5 + 0.5, (r - ATMO_BOTTOM) / (ATMO_TOP - ATMO_BOTTOM));
    uv = clamp(uv, vec2(0.5 / 32.0), vec2(1.0 - 0.5 / 32.0));
    return texture(multiScatterLUT, uv).rgb;
}

// World position -> radius from the planet's centre and the local up
float AtmoRadius(vec3 p) { return length(p + vec3(0.0, ATMO_BOTTOM, 0.0)); }
vec3 AtmoUp(vec3 p) { return normalize(p + vec3(0.0, ATMO_BOTTOM, 0.0)); }

// Light scattered toward the eye along a segment from `start` in direction `dir`, `len` metres
// long (ray-marched, single scattering from the sun + the multiple-scattering LUT), and its
// transmittance. Per unit sun illuminance.
void AtmoIntegrate(vec3 start, vec3 dir, float len, vec3 toSun, int steps, float jitter,
                   out vec3 inscatter, out vec3 transmittance)
{
    float c = dot(dir, toSun);
    float pr = AtmoRayleighPhase(c), pm = AtmoMiePhase(c);
    inscatter = vec3(0.0);
    transmittance = vec3(1.0);
    float prevT = 0.0;
    for (int i = 0; i < steps; i++) {
        // Steps grow along the ray (the air is densest near the start, for a low camera)
        float f = (float(i) + jitter) / float(steps);
        float t = len * f * f;
        float f1 = (float(i) + 1.0) / float(steps);
        float t1 = len * f1 * f1;
        float dt = t1 - prevT;
        prevT = t1;
        vec3 p = start + dir * t;
        float r = AtmoRadius(p);
        vec3 up = AtmoUp(p);
        float muSun = dot(up, toSun);
        AtmoSample m = AtmoMedium(r - ATMO_BOTTOM);
        vec3 sun = AtmoSunlight(r, muSun);
        vec3 S = (m.rayleigh * pr + m.mie * pm) * sun + m.scattering * AtmoMultiScatter(r, muSun);
        vec3 stepT = exp(-m.extinction * dt);
        // Energy-conserving: what scatters over the step, integrated against its own fade
        vec3 integ = (S - S * stepT) / max(m.extinction, vec3(1e-12));
        inscatter += transmittance * integ;
        transmittance *= stepT;
    }
}

// Sky view LUT: azimuth around the sun x elevation, the horizon at the middle row with more rows
// near it (Hillaire's non-linear mapping)
vec2 AtmoSkyViewUV(vec3 dir, vec3 up, vec3 toSun, float r)
{
    float horizon = acos(clamp(sqrt(max(r * r - ATMO_BOTTOM * ATMO_BOTTOM, 0.0)) / r, -1.0, 1.0)); // Below 90 deg from zenith
    float zenithAngle = acos(clamp(dot(dir, up), -1.0, 1.0));
    float horizonAngle = 0.5 * ATMO_PI + horizon; // Zenith angle of the ground's horizon
    float v;
    if (zenithAngle < horizonAngle) {
        float x = 1.0 - zenithAngle / horizonAngle;
        v = 0.5 * (1.0 - sqrt(max(x, 0.0)));
    } else {
        float x = (zenithAngle - horizonAngle) / max(ATMO_PI - horizonAngle, 1e-4);
        v = 0.5 * (1.0 + sqrt(max(x, 0.0)));
    }
    vec3 dirH = dir - up * dot(dir, up);
    vec3 sunH = toSun - up * dot(toSun, up);
    float az = 0.0;
    if (dot(dirH, dirH) > 1e-8 && dot(sunH, sunH) > 1e-8)
        az = acos(clamp(dot(normalize(dirH), normalize(sunH)), -1.0, 1.0));
    float u = sqrt(az / ATMO_PI); // More columns toward the sun
    return vec2(u, v);
}

vec3 AtmoEncode(vec3 linear) { return pow(max(linear, vec3(0.0)), vec3(1.0 / 2.2)); }

vec3 AtmoSkyRadiance(vec3 dir, vec3 camPos, vec3 toSun)
{
    float r = AtmoRadius(camPos);
    vec2 uv = AtmoSkyViewUV(dir, AtmoUp(camPos), toSun, r);
    return texture(skyViewLUT, uv).rgb;
}
