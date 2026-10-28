#version 460

/**
 * @file volumetric_sky.frag
 * @brief Sky gradient + sun + ray-marched volumetric cumulus layer.
 *
 * Clouds live in a shell 1.2-3.4 km above a spherical ground (so the layer bends down to the
 * horizon). Per pixel the view ray is marched through the shell; at every sample:
 *  - density = tileable Perlin-Worley base shape (3D texture) shaped by coverage and a cumulus
 *    height profile, edges eroded by a Worley detail volume (Schneider, "The Real-time Volumetric
 *    Cloudscapes of Horizon Zero Dawn", 2015),
 *  - sun light = short march toward the sun (self-shadowing, Beer) x "powder" darkening of thin
 *    edges x two-lobe Henyey-Greenstein phase (bright silver lining when looking at the sun),
 *  - sky light = ambient from the sky colours, stronger at the top of the cloud,
 *  - energy-conserving integration (Hillaire, "Physically Based Sky, Atmosphere and Cloud
 *    Rendering in Frostbite", 2016).
 * The march start is jittered per pixel and per frame; sky_temporal.frag accumulates the frames,
 * so the result converges to a smooth image instead of showing the per-pixel noise.
 * Output alpha = cloud transmittance (used to mask the god rays).
 */

out vec4 FragColor;

in vec2 TexCoord;
in vec3 WorldDir; ///< Ray direction mapping into 3D space.

uniform float time;            ///< Current animation timer.
uniform int cloudsEnabled;     ///< Set to 1 if clouds are drawn.
uniform float cloudsDensity;   ///< Sky coverage (0.1 = few clouds, 0.9 = overcast).
uniform float cloudsSpeed;     ///< Wind speed multiplier.
uniform float cloudsSharpness; ///< Edge detail / erosion strength.
uniform int jitterFrame;       ///< Frame counter for the temporal jitter (constant = static pattern).

uniform sampler3D cloudShapeNoise;  ///< 128^3 RGBA: Perlin-Worley + Worley fBm (cloud_noise.glsl)
uniform sampler3D cloudDetailNoise; ///< 64^3 RGB: Worley fBm (edge erosion)

uniform vec3 sunDir;   ///< Direction vector pointing towards the sun.
uniform vec3 sunColor; ///< RGB intensity color of the sun light source.
uniform mat4 invView;  ///< Camera-to-world (translation = camera position).

uniform vec3 zenithDay;
uniform vec3 horizonDay;
uniform vec3 zenithSunset;
uniform vec3 horizonSunset;
uniform vec3 zenithNight;
uniform vec3 horizonNight;

const float PI = 3.14159265;
const float EARTH_RADIUS = 6360000.0;
const float CLOUD_BOTTOM = 1200.0;
const float CLOUD_TOP = 3400.0;
const int   VIEW_STEPS_MIN = 64;  // Looking up (short path through the shell)
const int   VIEW_STEPS_MAX = 128; // Grazing rays near the horizon (long path)
const int   LIGHT_STEPS = 6;
const float SHAPE_TILE = 6500.0;   // World size (m) of one repeat of the shape volume
const float DETAIL_TILE = 1100.0;  // World size (m) of one repeat of the detail volume
const float WEATHER_TILE = 60000.0;

float Remap(float v, float lo, float hi, float newLo, float newHi)
{
    return newLo + (clamp(v, lo, hi) - lo) / max(hi - lo, 1e-5) * (newHi - newLo);
}

// ----------------------------------------------------------------------------------------
// Cloud density
// ----------------------------------------------------------------------------------------
vec3 gCamPos;
vec3 gWind;

float HeightFraction(vec3 p)
{
    float altitude = length(p - vec3(0.0, -EARTH_RADIUS, 0.0)) - EARTH_RADIUS;
    return (altitude - CLOUD_BOTTOM) / (CLOUD_TOP - CLOUD_BOTTOM);
}

// Cumulus: a low-frequency "weather" field groups clouds into clusters with blue gaps; the
// Perlin-Worley base is eroded by its own Worley fBm, then thresholded by the local coverage.
// The threshold rises with height, so towers have wide bases and narrowing rounded tops.
float CloudShape(vec3 p, float h)
{
    // Flat-ish base, rounded top
    float profile = Remap(h, 0.0, 0.08, 0.0, 1.0) * Remap(h, 0.45, 1.0, 1.0, 0.0);
    if (profile <= 0.0) return 0.0;

    vec3 q = p + gWind;
    // Tops lean downwind
    q.xz += vec2(1.0, 0.35) * h * 350.0;

    float weather = texture(cloudShapeNoise, vec3(q.xz / WEATHER_TILE, 0.37).xzy).g;
    float coverage = clamp(clamp(cloudsDensity, 0.05, 0.95) * Remap(weather, 0.25, 0.85, 0.55, 1.45), 0.0, 1.0);

    vec4 n = texture(cloudShapeNoise, q / SHAPE_TILE);
    float lowFbm = n.g * 0.625 + n.b * 0.25 + n.a * 0.125;
    float base = Remap(n.r, lowFbm - 1.0, 1.0, 0.0, 1.0);

    base *= profile;
    float threshold = 1.0 - coverage;
    return clamp(Remap(base, threshold, 1.0, 0.0, 1.0) * coverage * 1.4, 0.0, 1.0);
}

// Full density: shape + erosion of the edges by the detail volume (wispy at the bottom,
// cauliflower billows higher up)
float CloudDensity(vec3 p)
{
    float h = HeightFraction(p);
    if (h < 0.0 || h > 1.0) return 0.0;
    float d = CloudShape(p, h);
    if (d <= 0.0) return 0.0;

    float sharp = clamp(cloudsSharpness, 0.05, 0.5) * 2.0;
    vec3 dq = (p + gWind * 1.15) / DETAIL_TILE + vec3(0.0, time * 0.004, 0.0);
    vec3 dn = texture(cloudDetailNoise, dq).rgb;
    float hf = dn.r * 0.625 + dn.g * 0.25 + dn.b * 0.125;
    float hfMod = mix(1.0 - hf, hf, clamp(h * 6.0, 0.0, 1.0));
    return clamp(Remap(d, hfMod * 0.5 * sharp, 1.0, 0.0, 1.0) * 2.0, 0.0, 1.0);
}

// Cheap density for the light march (no detail): self-shadowing only needs the bulk
float CloudDensityCoarse(vec3 p)
{
    float h = HeightFraction(p);
    if (h < 0.0 || h > 1.0) return 0.0;
    return CloudShape(p, h);
}

// ----------------------------------------------------------------------------------------
// Geometry / lighting helpers
// ----------------------------------------------------------------------------------------
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

float HenyeyGreenstein(float cosTheta, float g)
{
    float g2 = g * g;
    return (1.0 - g2) / (4.0 * PI * pow(1.0 + g2 - 2.0 * g * cosTheta, 1.5));
}

// Sun light after crossing the atmosphere: low sun -> long air path -> red/orange
vec3 SunTransmittance(vec3 toSun)
{
    float airMass = 1.0 / max(toSun.y + 0.06, 0.035);
    return exp(-vec3(0.05, 0.14, 0.24) * airMass);
}

vec3 SkyGradient(vec3 dir, vec3 toSun, float sunsetFactor, float nightFactor)
{
    float h = max(dir.y, 0.0);
    vec3 zenithColor = mix(mix(zenithDay, zenithSunset, sunsetFactor), zenithNight, nightFactor);
    vec3 horizonColor = mix(mix(horizonDay, horizonSunset, sunsetFactor), horizonNight, nightFactor);
    vec3 sky = mix(horizonColor, zenithColor, pow(h, 0.45));
    // Warm glow around the sun (strong at sunset), and below the horizon fade to haze
    float mu = max(dot(dir, toSun), 0.0);
    sky += SunTransmittance(toSun) * sunColor * (pow(mu, 6.0) * 0.35 + pow(mu, 64.0) * 0.6) * (1.0 - nightFactor);
    sky = mix(sky, horizonColor * 0.85, smoothstep(0.0, 0.25, -dir.y));
    return sky;
}

void main()
{
    vec3 dir = normalize(WorldDir);
    vec3 toSun = normalize(sunDir);
    gCamPos = vec3(invView[3]);
    gCamPos.y = max(gCamPos.y, 0.0);

    // smoothstep is undefined for edge0 >= edge1, so the descending ramps are written as 1 - smoothstep
    float sunsetFactor = 1.0 - smoothstep(-0.1, 0.5, toSun.y);
    float nightFactor = 1.0 - smoothstep(-0.5, -0.1, toSun.y);

    vec3 sky = SkyGradient(dir, toSun, sunsetFactor, nightFactor);
    vec3 sunLight = sunColor * SunTransmittance(toSun) * (1.0 - nightFactor) * 3.0;

    // Sun disk (behind the clouds)
    float mu = dot(dir, toSun);
    vec3 sunDisk = sunLight * smoothstep(0.99985, 0.99993, mu) * 8.0;

    vec3 color = sky + sunDisk;
    float cloudTransmittance = 1.0;

    if (cloudsEnabled == 1)
    {
        // March segment through the cloud shell
        vec2 inner = RaySphere(gCamPos, dir, EARTH_RADIUS + CLOUD_BOTTOM);
        vec2 outer = RaySphere(gCamPos, dir, EARTH_RADIUS + CLOUD_TOP);
        float tStart = max(inner.y, 0.0);
        float tEnd = outer.y;
        // Below the ground horizon the ray hits the planet before the clouds
        vec2 ground = RaySphere(gCamPos, dir, EARTH_RADIUS);
        if (ground.x > 0.0) tEnd = -1.0;
        // Grazing rays cross tens of km of shell; cap the segment so the steps stay bounded
        tEnd = min(tEnd, tStart + 20000.0);

        if (tEnd > tStart && tStart < 120000.0)
        {
            gWind = vec3(1.0, 0.0, 0.35) * time * max(cloudsSpeed, 0.0) * 100.0;

            // Interleaved gradient noise, shifted every frame (Jimenez 2014): the per-pixel start
            // offset hides step banding, and the temporal pass averages the frames into a smooth image
            vec2 jp = gl_FragCoord.xy + 5.588238 * float(jitterFrame % 64);
            float jitter = fract(52.9829189 * fract(dot(jp, vec2(0.06711056, 0.00583715))));
            int viewSteps = int(mix(float(VIEW_STEPS_MAX), float(VIEW_STEPS_MIN), clamp(dir.y * 3.0, 0.0, 1.0)));
            float stepLen = (tEnd - tStart) / float(viewSteps);
            float t = tStart + stepLen * jitter;

            // Sky light: the cloud top sees the zenith, the base sees the (darker) haze/ground bounce
            vec3 ambientTop = SkyGradient(vec3(0.0, 1.0, 0.0), toSun, sunsetFactor, nightFactor) * 1.4
                            + SkyGradient(normalize(vec3(-toSun.x, 0.3, -toSun.z)), toSun, sunsetFactor, nightFactor) * 0.6;
            vec3 ambientBottom = SkyGradient(normalize(vec3(-toSun.x, 0.05, -toSun.z)), toSun, sunsetFactor, nightFactor) * 0.45;

            vec3 scattered = vec3(0.0);
            float transmittance = 1.0;
            const float SIGMA = 0.03; // Extinction per meter at density 1

            for (int i = 0; i < viewSteps; i++)
            {
                if (t > tEnd) break;
                vec3 p = gCamPos + dir * t;
                float density = CloudDensity(p);
                if (density <= 0.001) {
                    t += stepLen;
                    continue;
                }

                // Light march toward the sun (growing steps, coarse density)
                float lightDepth = 0.0;
                float ls = 40.0;
                vec3 lp = p;
                for (int j = 0; j < LIGHT_STEPS; j++) {
                    lp += toSun * ls;
                    lightDepth += CloudDensityCoarse(lp) * ls;
                    ls *= 1.6;
                }

                // Multiple scattering approximation (Wrenninge 2013): sum of attenuated octaves,
                // each with weaker extinction and a flatter phase. Thick clouds stay luminous
                // inside instead of turning into grey/black blobs.
                vec3 sunScatter = vec3(0.0);
                float a = 1.0, b = 1.0, c = 1.0;
                for (int o = 0; o < 3; o++) {
                    float ph = mix(HenyeyGreenstein(mu, 0.7 * c), HenyeyGreenstein(mu, -0.25 * c), 0.25) * 4.0 * PI;
                    sunScatter += sunLight * a * exp(-lightDepth * SIGMA * b) * max(ph, 0.4);
                    a *= 0.5; b *= 0.3; c *= 0.5;
                }
                // Powder: thin cloud edges facing away from the sun look darker (in-scattering deficit)
                float powder = 1.0 - exp(-density * 8.0 - lightDepth * SIGMA * 0.5);
                float h = HeightFraction(p);

                vec3 S = sunScatter * mix(0.55, 1.0, powder)
                       + mix(ambientBottom, ambientTop, h) * (0.75 + 0.5 * h);
                float stepT = exp(-density * SIGMA * stepLen);
                // Energy-conserving integration of in-scattering over the step
                scattered += transmittance * (S - S * stepT);
                transmittance *= stepT;
                if (transmittance < 0.02) break;
                t += stepLen;
            }

            // Aerial perspective: distant clouds fade into the horizon haze
            float haze = 1.0 - exp(-tStart * 0.000022);
            vec3 horizonColor = mix(mix(horizonDay, horizonSunset, sunsetFactor), horizonNight, nightFactor);
            scattered = mix(scattered, horizonColor * (1.0 - transmittance), haze * 0.85);
            color = color * transmittance + scattered;
            cloudTransmittance = transmittance;
        }
    }

    // Gentle tone curve keeps bright cloud tops from clipping to flat white
    color = color / (1.0 + max(max(color.r, color.g), color.b) * 0.15);
    FragColor = vec4(color, cloudTransmittance);
}
