#version 460

/**
 * @file volumetric_sky.frag
 * @brief Sky gradient + sun + ray-marched volumetric cumulus layer.
 *
 * Clouds live in a shell 1.2-3.4 km above a spherical ground (so the layer bends down to the
 * horizon). Per pixel the view ray is marched through the shell:
 *  - density = tileable Perlin-Worley base shape (3D texture) shaped by coverage and a cumulus
 *    height profile, edges eroded by a Worley detail volume (Schneider, "The Real-time Volumetric
 *    Cloudscapes of Horizon Zero Dawn", 2015),
 *  - adaptive march: long steps through empty sky with the cheap shape only; at the first hit the
 *    march backs up and continues with short steps (they grow with distance), and goes back to
 *    long steps after a run of empty samples. A step is always much shorter than the distance
 *    light needs to fade inside a cloud, so the surface of a cloud is resolved instead of being
 *    sliced into the step positions (which, being the same for every pixel of a screen row, made
 *    horizontal lines across distant clouds),
 *  - every lookup picks its mip from the pixel's footprint there, stretched along grazing rays
 *    (textureLod: implicit derivatives are undefined inside the march's divergent loop),
 *  - sun light = short jittered march toward the sun (self-shadowing, Beer) x "powder" darkening
 *    of thin edges x two-lobe Henyey-Greenstein phase with multiple-scattering octaves,
 *  - sky light = ambient from the sky colours, stronger at the top of the cloud,
 *  - energy-conserving integration (Hillaire, "Physically Based Sky, Atmosphere and Cloud
 *    Rendering in Frostbite", 2016),
 *  - aerial perspective from the transmittance-weighted cloud distance.
 * The march start is jittered per pixel and per frame; sky_temporal.frag accumulates the frames.
 * Output alpha = cloud transmittance (used to mask the god rays).
 */

out vec4 FragColor;

in vec2 TexCoord;
in vec3 WorldDir; ///< Ray direction mapping into 3D space.

uniform int cloudsEnabled;     ///< Set to 1 if clouds are drawn.
uniform int jitterFrame;       ///< Frame counter for the temporal jitter (constant = static pattern).
#include "cloud_common.glsl"   // Density of the cloud layer (shared with the cloud shadow map)

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
const float MAX_MARCH = 40000.0;    // Along the ray through the shell (grazing rays cross far more)
const int   MAX_ITERATIONS = 170;
const int   LIGHT_STEPS = 5;
const float DETAIL_FADE = 25000.0;  // Beyond this the erosion detail is sub-pixel: skipped

// ----------------------------------------------------------------------------------------
// Cloud density
// ----------------------------------------------------------------------------------------
vec3 gCamPos;
float gPixelAngle; // Radians covered by one pixel (sets the mip of every lookup)

// How much cloud one pixel covers at distance `dist` of point p: grazing rays near the horizon
// cross the layer at a shallow angle, so a pixel spans far more along the ray than across it.
// Sampled finer than that, the noise aliases into bright horizontal streaks.
float Footprint(vec3 p, vec3 dir, float dist)
{
    float grazing = abs(dot(dir, normalize(p - vec3(0.0, -EARTH_RADIUS, 0.0))));
    return dist * gPixelAngle * min(1.0 / max(grazing, 1e-3), 3.0);
}

// Mip level whose texels match a footprint
float Lod(float footprint, float texelSize)
{
    return clamp(log2(max(footprint, 1e-3) / texelSize), 0.0, 6.0);
}

// Full density: shape + erosion of the edges by the detail volume (wispy at the bottom,
// cauliflower billows higher up). The erosion fades out with distance (sub-pixel there).
float CloudDensity(vec3 p, float h, float dist, float footprint)
{
    float d = CloudShape(p, h, Lod(footprint, SHAPE_TEXEL));
    if (d <= 0.0) return 0.0;

    float detailWeight = 1.0 - smoothstep(DETAIL_FADE * 0.5, DETAIL_FADE, dist);
    if (detailWeight > 0.0) {
        float sharp = clamp(cloudsSharpness, 0.05, 0.5) * 2.0;
        vec3 dq = (p + gWind * 1.15) / DETAIL_TILE + vec3(0.0, time * 0.004, 0.0);
        vec3 dn = textureLod(cloudDetailNoise, dq, Lod(footprint, DETAIL_TEXEL)).rgb;
        float hf = dn.r * 0.625 + dn.g * 0.25 + dn.b * 0.125;
        float hfMod = mix(1.0 - hf, hf, clamp(h * 6.0, 0.0, 1.0));
        float eroded = clamp(Remap(d, hfMod * 0.5 * sharp, 1.0, 0.0, 1.0) * 2.0, 0.0, 1.0);
        // Far away: the eroded field's average instead
        return mix(CloudErodedAverage(d), eroded, detailWeight);
    }
    return CloudErodedAverage(d);
}

// ----------------------------------------------------------------------------------------
// Geometry / lighting helpers
// ----------------------------------------------------------------------------------------
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

// Interleaved gradient noise (Jimenez 2014), a different offset per frame and per use
float Jitter(float salt)
{
    vec2 jp = gl_FragCoord.xy + vec2(5.588238, 3.7342) * (float(jitterFrame % 64) + salt * 17.0);
    return fract(52.9829189 * fract(dot(jp, vec2(0.06711056, 0.00583715))));
}

void main()
{
    vec3 dir = normalize(WorldDir);
    // Before any branch: derivatives are only defined in uniform control flow
    gPixelAngle = max(length(fwidth(dir)), 1e-5);
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
        tEnd = min(tEnd, tStart + MAX_MARCH);

        if (tEnd > tStart && tStart < 150000.0)
        {
            CloudSetup();

            // Sky light: the cloud top sees the zenith, the base sees the (darker) haze/ground bounce
            vec3 ambientTop = SkyGradient(vec3(0.0, 1.0, 0.0), toSun, sunsetFactor, nightFactor) * 1.4
                            + SkyGradient(normalize(vec3(-toSun.x, 0.3, -toSun.z)), toSun, sunsetFactor, nightFactor) * 0.6;
            vec3 ambientBottom = SkyGradient(normalize(vec3(-toSun.x, 0.05, -toSun.z)), toSun, sunsetFactor, nightFactor) * 0.45;
            // The phase does not change along the ray: once per pixel
            float phase[3];
            for (int o = 0; o < 3; o++) {
                float c = pow(0.5, float(o));
                phase[o] = max(mix(HenyeyGreenstein(mu, 0.7 * c), HenyeyGreenstein(mu, -0.25 * c), 0.25) * 4.0 * PI, 0.4);
            }

            float jitter = Jitter(0.0);
            float lightJitter = Jitter(1.0);

            vec3 scattered = vec3(0.0);
            float transmittance = 1.0;
            float depthSum = 0.0, depthWeight = 0.0; // Where the cloud is (for the haze)

            // Short steps where the cloud is (a fraction of light's 1/SIGMA = 33 m fade length,
            // growing with distance where a pixel covers more), three times longer through empty sky
            float fineStep = clamp(40.0 + tStart * 0.005, 40.0, 350.0);
            float t = tStart + fineStep * 3.0 * jitter;
            bool inCloud = false;
            int emptyRun = 0;

            for (int i = 0; i < MAX_ITERATIONS; i++)
            {
                if (t > tEnd || transmittance < 0.02) break;
                fineStep = clamp(40.0 + t * 0.005, 40.0, 350.0);
                vec3 p = gCamPos + dir * t;
                float h = HeightFraction(p);
                float footprint = Footprint(p, dir, t);

                if (!inCloud) {
                    // Empty sky: the cheap shape only. A hit backs up one long step so the fine
                    // march starts just before the cloud's surface.
                    if (h >= 0.0 && h <= 1.0 && CloudShape(p, h, Lod(footprint, SHAPE_TEXEL)) > 0.0) {
                        inCloud = true;
                        emptyRun = 0;
                        t = max(t - fineStep * 3.0, tStart) + fineStep * jitter;
                        continue;
                    }
                    t += fineStep * 3.0;
                    continue;
                }

                float density = (h >= 0.0 && h <= 1.0) ? CloudDensity(p, h, t, footprint) : 0.0;
                if (density <= 0.001) {
                    // Out of the cloud for a while: back to long steps
                    if (++emptyRun > 4) inCloud = false;
                    t += fineStep;
                    continue;
                }
                emptyRun = 0;

                // Light march toward the sun (growing, jittered steps, no erosion detail)
                float lightDepth = 0.0;
                // Longer light steps far away (smoothly: a switch would draw a line across the sky)
                float ls = mix(40.0, 90.0, smoothstep(4000.0, 30000.0, t));
                float lightLod = Lod(footprint, SHAPE_TEXEL) + 1.0;
                vec3 lp = p + toSun * ls * lightJitter;
                for (int j = 0; j < LIGHT_STEPS; j++) {
                    lp += toSun * ls;
                    float lh = HeightFraction(lp);
                    if (lh > 1.0) break;
                    if (lh >= 0.0) lightDepth += CloudShape(lp, lh, lightLod) * ls;
                    ls *= 1.8;
                }

                // Multiple scattering approximation (Wrenninge 2013): sum of attenuated octaves,
                // each with weaker extinction and a flatter phase. Thick clouds stay luminous
                // inside instead of turning into grey/black blobs.
                vec3 sunScatter = vec3(0.0);
                float a = 1.0, b = 1.0;
                for (int o = 0; o < 3; o++) {
                    sunScatter += sunLight * a * exp(-lightDepth * SIGMA * b) * phase[o];
                    a *= 0.5; b *= 0.3;
                }
                // Powder: thin cloud edges facing away from the sun look darker (in-scattering deficit)
                float powder = 1.0 - exp(-density * 8.0 - lightDepth * SIGMA * 0.5);

                vec3 S = sunScatter * mix(0.55, 1.0, powder)
                       + mix(ambientBottom, ambientTop, h) * (0.75 + 0.5 * h);
                float stepT = exp(-density * SIGMA * fineStep);
                // Energy-conserving integration of in-scattering over the step
                float absorbed = transmittance * (1.0 - stepT);
                scattered += absorbed * S;
                depthSum += absorbed * t;
                depthWeight += absorbed;
                transmittance *= stepT;
                t += fineStep;
            }

            // Aerial perspective: distant clouds fade into the horizon haze, by where they are
            float cloudDist = depthWeight > 1e-4 ? depthSum / depthWeight : tStart;
            float haze = 1.0 - exp(-cloudDist * 0.000022);
            vec3 horizonColor = mix(mix(horizonDay, horizonSunset, sunsetFactor), horizonNight, nightFactor);
            scattered = mix(scattered, horizonColor * (1.0 - transmittance), haze * 0.85);
            // The farthest clouds melt into the horizon instead of ending on a hard line
            float horizonFade = smoothstep(0.0, 0.06, dir.y);
            transmittance = mix(1.0, transmittance, horizonFade);
            scattered *= horizonFade;
            color = color * transmittance + scattered;
            cloudTransmittance = transmittance;
        }
    }

    // Gentle tone curve keeps bright cloud tops from clipping to flat white
    color = color / (1.0 + max(max(color.r, color.g), color.b) * 0.15);
    FragColor = vec4(color, cloudTransmittance);
}
