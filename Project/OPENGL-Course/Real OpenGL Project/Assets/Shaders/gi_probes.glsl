#version 430 core

/**
 * @file gi_probes.glsl
 * @brief Updates the global illumination probes (Rendering/GlobalIllumination.h). One workgroup
 * per probe; probes not due this frame (a fixed slice of them is) leave at once, except those that
 * just entered the grid window (a fresh probe is traced right away).
 *
 * A probe whose grid point is inside geometry or touches it (a floor at its height, a wall through
 * it) moves first, up to 0.45 of the spacing, to the nearest place with half a voxel of free space
 * around it (relocation, as in DDGI); with nowhere free it is marked inside and lights nothing.
 *
 * Each lane traces one ray through the voxelized scene (gi_inject.glsl's lit voxels): it marches
 * the finest voxel level that contains it, one voxel per step, accumulating light and opacity front
 * to back, and sees the sky where it gets through. The directions are a spherical Fibonacci set
 * rotated randomly every frame, so successive updates cover the sphere.
 *
 * The rays are then gathered into the probe's ambient cube: for each axis, the light weighted by
 * the cosine around it (what a surface facing that way receives), and the hit distances toward it
 * (with their variance) for the visibility test of the lit shaders. Both blend into the probe's
 * history (hysteresis); a young probe averages its first updates evenly instead, so a new scene,
 * a jump of the camera or light bouncing anew settles within a few updates.
 *
 * The sky that reaches the probes has the brightness of the scene's flat ambient (sun colour x
 * ambient intensity) and the sky's shape (brighter where the sky is); its colour is the ambient's
 * or the sky's own (skyColour, blue shade by day, purple at dusk): the open sky over a surface
 * facing up gives the old flat ambient's luminance, a canopy or a roof only what gets in.
 * One more workgroup, after the probes, fills the sky probe: the
 * ambient cube of the sky alone, the lit shaders' light beyond the probes.
 */

layout(local_size_x = 64) in;

#include "gi_block.glsl"
layout(std430, binding = 10) buffer GIProbes { GIProbe giProbes[]; };

uniform sampler3D voxelRadiance;   ///< rgb light, a opacity; levels stacked along z
uniform usampler3D voxelBricks;    ///< 1 per 4^3 block holding anything (gi_inject.glsl)
uniform int resolution;
uniform int levelCount;
uniform vec4 levelMinSize[4];      ///< xyz world corner of each level, w voxel size

uniform samplerCube skyCube;
uniform float skyLod;              ///< Mip level of about 8x8 texels a face: smooth light (no clouds, stars)
uniform vec3 ambientColour;        ///< Sun colour x ambient intensity: the flat ambient
uniform float skyColour;           ///< 0: the sky light has the ambient's colour, 1: the sky's own colours
uniform mat3 rayRotation;          ///< Random rotation of the ray set, new every frame
uniform int rayCount;              ///< <= 64
uniform int frameSlice;
uniform int sliceCount;
uniform float hysteresis;
uniform float maxRayDistance;

shared vec3 sRadiance[64];
shared vec3 sDirection[64];
shared float sDistance[64];
shared vec3 sSky[64];
shared vec3 sOffset;
shared bool sInside;
shared vec4 sAxis[6];
shared float sAxisVariance[6];

vec3 SphericalFibonacci(float i, float n)
{
    const float PHI = 1.6180339887;
    float phi = 6.2831853 * fract(i * (PHI - 1.0));
    float cosTheta = 1.0 - (2.0 * i + 1.0) / n;
    float sinTheta = sqrt(clamp(1.0 - cosTheta * cosTheta, 0.0, 1.0));
    return vec3(cos(phi) * sinTheta, cosTheta, sin(phi) * sinTheta);
}

// Is p inside level l (one voxel of margin)?
bool InLevel(int l, vec3 p)
{
    vec3 v = (p - levelMinSize[l].xyz) / levelMinSize[l].w;
    return all(greaterThanEqual(v, vec3(1.0))) && all(lessThan(v, vec3(float(resolution) - 1.0)));
}

// The finest level holding p, -1 outside them all
int LevelAt(vec3 p)
{
    for (int l = 0; l < levelCount; l++) if (InLevel(l, p)) return l;
    return -1;
}

vec4 VoxelAt(int l, ivec3 v)
{
    return texelFetch(voxelRadiance, v + ivec3(0, 0, l * resolution), 0);
}

float gSkyScale;   // 1 / luminance of the sky's light over a surface facing up; 0: no sky drawn yet

vec3 Sky(vec3 d)
{
    if (d.y < -0.05) return ambientColour * 0.3; // Below the horizon and nothing hit: the ground's bounce
    if (gSkyScale <= 0.0) return ambientColour;
    vec3 sky = min(textureLod(skyCube, d, skyLod).rgb * gSkyScale, vec3(3.0)); // Luminance 1 on average
    float ambientLuminance = dot(ambientColour, vec3(0.2126, 0.7152, 0.0722));
    return mix(ambientColour * dot(sky, vec3(0.2126, 0.7152, 0.0722)), ambientLuminance * sky, skyColour);
}

// Is there a surface at p (an opaque voxel of the finest level holding it)?
bool Solid(vec3 p)
{
    int l = LevelAt(p);
    return l >= 0 && VoxelAt(l, ivec3((p - levelMinSize[l].xyz) / levelMinSize[l].w)).a > 0.5;
}

// Room for a probe at p: no surface there nor half a voxel away along the axes
bool Clear(vec3 p)
{
    int l = LevelAt(p);
    float h = (l >= 0 ? levelMinSize[l].w : 1.0) * 0.5;
    return !Solid(p) && !Solid(p + vec3(h, 0.0, 0.0)) && !Solid(p - vec3(h, 0.0, 0.0))
        && !Solid(p + vec3(0.0, h, 0.0)) && !Solid(p - vec3(0.0, h, 0.0))
        && !Solid(p + vec3(0.0, 0.0, h)) && !Solid(p - vec3(0.0, 0.0, h));
}

// Front-to-back march: light, and the distance where the ray became mostly blocked. Through the
// finest level holding the ray (outward only: rays start near the camera), jumping over empty
// 4^3 blocks in one step, else one voxel at a time (nearest voxel: the probes average many rays)
vec3 Trace(vec3 origin, vec3 dir, out float hitDistance)
{
    vec4 acc = vec4(0.0);
    hitDistance = maxRayDistance;
    int l = LevelAt(origin);
    if (l < 0) return Sky(dir);
    vec3 invDir = 1.0 / mix(dir, vec3(1e-6), lessThan(abs(dir), vec3(1e-6)));
    vec3 toFar = step(vec3(0.0), dir); // 1 where the ray leaves a block through its upper side
    float t = levelMinSize[l].w * 0.75;
    for (int i = 0; i < 384; i++) {
        if (t >= maxRayDistance) break;
        vec3 p = origin + dir * t;
        while (l < levelCount && !InLevel(l, p)) l++;
        if (l >= levelCount) break;
        float voxel = levelMinSize[l].w;
        vec3 v = (p - levelMinSize[l].xyz) / voxel;
        ivec3 cell = ivec3(v);
        ivec3 brick = cell >> 2;
        if (texelFetch(voxelBricks, brick + ivec3(0, 0, l * (resolution >> 2)), 0).r == 0u) {
            vec3 exitV = (vec3(brick * 4) + 4.0 * toFar - v) * invDir;
            t += max(min(min(exitV.x, exitV.y), exitV.z), 0.0) * voxel + voxel * 0.02;
            continue;
        }
        vec4 s = VoxelAt(l, cell);
        if (s.a > 0.001) {
            acc.rgb += (1.0 - acc.a) * s.a * s.rgb;
            acc.a += (1.0 - acc.a) * s.a;
            if (acc.a > 0.5 && hitDistance >= maxRayDistance) hitDistance = t;
            if (acc.a > 0.97) break;
        }
        t += voxel * 0.75;
    }
    return acc.rgb + (1.0 - acc.a) * Sky(dir);
}

void main()
{
    int probe = int(gl_WorkGroupID.x);
    bool skyProbe = probe == GISkyProbe(); // Sees the sky only (fixed directions, no history)
    int cascade = skyProbe ? 0 : probe / giCounts.w;
    int localIndex = probe - cascade * giCounts.w;
    ivec3 counts = giCounts.xyz;
    ivec3 slot = ivec3(localIndex % counts.x, (localIndex / counts.x) % counts.y, localIndex / (counts.x * counts.y));
    ivec3 base = cascade == 0 ? giBase0.xyz : giBase1.xyz;
    ivec3 w = base + (((slot - base) % counts) + counts) % counts; // The coordinate this slot holds now
    uint key = GIKey(w);
    bool fresh = true;
    if (!skyProbe) {
        uint oldKey = giProbes[probe].key;
        fresh = (oldKey & 0x3FFFFFFFu) != key || (oldKey & GI_VALID) == 0u;
        bool due = ((localIndex + frameSlice) % sliceCount) == 0;
        if (!fresh && !due) return; // Uniform across the workgroup
    }

    float spacing = cascade == 0 ? giSpacing.x : giSpacing.y;
    vec3 probePos = vec3(w) * spacing;
    uint lane = gl_LocalInvocationIndex;

    // Relocation: the grid point if it has room, else the nearest free place along the axes (up
    // first: probes on a floor), up to 0.45 of the spacing
    if (lane == 0u) {
        vec3 offset = vec3(0.0);
        bool inside = false;
        if (!skyProbe && !Clear(probePos)) {
            inside = true;
            const vec3 dirs[6] = vec3[6](vec3(0, 1, 0), vec3(0, -1, 0), vec3(1, 0, 0), vec3(-1, 0, 0), vec3(0, 0, 1), vec3(0, 0, -1));
            const float steps[3] = float[3](0.15, 0.3, 0.45);
            for (int s = 0; s < 3 && inside; s++) {
                for (int d = 0; d < 6; d++) {
                    vec3 o = dirs[d] * (steps[s] * spacing);
                    if (Clear(probePos + o)) { offset = o; inside = false; break; }
                }
            }
        }
        sOffset = offset;
        sInside = inside;
    }

    // How the sky's colours map to the flat ambient: its light over a surface facing up (cosine
    // weighted upper hemisphere, a Vogel spiral of 64 directions) has a luminance of 1
    {
        float r = sqrt((float(lane) + 0.5) / 64.0);
        float phi = 6.2831853 * fract(float(lane) * 0.6180339887);
        vec3 d = vec3(r * cos(phi), sqrt(max(1.0 - r * r, 0.0)), r * sin(phi));
        sSky[lane] = textureLod(skyCube, d, skyLod).rgb;
    }
    barrier();
    vec3 skySum = vec3(0.0);
    for (int k = 0; k < 64; k++) skySum += sSky[k];
    float skyLuminance = dot(skySum, vec3(0.2126, 0.7152, 0.0722)) / 64.0;
    gSkyScale = skyLuminance > 1e-4 ? 1.0 / skyLuminance : 0.0;

    if (int(lane) < rayCount) {
        vec3 dir = SphericalFibonacci(float(lane), float(rayCount));
        float dist = maxRayDistance;
        if (skyProbe) {
            sRadiance[lane] = Sky(dir);
        } else {
            dir = normalize(rayRotation * dir);
            sRadiance[lane] = Trace(probePos + sOffset, dir, dist);
        }
        sDirection[lane] = dir;
        sDistance[lane] = dist;
    }
    barrier();

    // The ambient cube, one lane per axis: the light a surface facing it receives (the rays weighted
    // by the cosine), and how far the probe sees toward it (sharper weights, with the variance)
    if (lane < 6u) {
        int a = int(lane);
        vec3 axisDir = vec3(0.0);
        axisDir[a >> 1] = (a & 1) == 0 ? 1.0 : -1.0;
        vec3 light = vec3(0.0);
        float lightWeight = 0.0, distSum = 0.0, dist2Sum = 0.0, distWeight = 0.0;
        for (int i = 0; i < rayCount; i++) {
            float c = max(dot(sDirection[i], axisDir), 0.0);
            light += sRadiance[i] * c;
            lightWeight += c;
            float wgt = c * c * c * c;
            distSum += wgt * sDistance[i];
            dist2Sum += wgt * sDistance[i] * sDistance[i];
            distWeight += wgt;
        }
        float m = distWeight > 1e-4 ? distSum / distWeight : maxRayDistance;
        float m2 = distWeight > 1e-4 ? dist2Sum / distWeight : m * m;
        sAxis[a] = vec4(lightWeight > 1e-4 ? light / lightWeight : vec3(0.0), m);
        sAxisVariance[a] = max(m2 - m * m, 0.0) / max(m * m, 1e-4);
    }
    barrier();
    if (lane != 0u) return;

    float relVar = (sAxisVariance[0] + sAxisVariance[1] + sAxisVariance[2]
                  + sAxisVariance[3] + sAxisVariance[4] + sAxisVariance[5]) / 6.0;

    bool inside = sInside; // Nowhere free to go: it would light nothing correctly

    // A fresh probe takes this update as it is (its slot held another probe, or nothing); the next
    // ones average evenly (weight 1 / age) until that is less than the hysteresis allows
    GIProbe p;
    p.age = fresh ? 1.0 : min(giProbes[probe].age + 1.0, 255.0);
    float h = fresh ? 0.0 : min(hysteresis, 1.0 - 1.0 / p.age);
    for (int a = 0; a < 6; a++) p.axis[a] = fresh ? sAxis[a] : mix(sAxis[a], giProbes[probe].axis[a], h);
    p.offset = vec4(sOffset, fresh ? relVar : mix(relVar, giProbes[probe].offset.w, h));
    p.key = key | GI_VALID | (inside ? GI_INSIDE : 0u);
    p.pad0 = 0.0;
    p.pad1 = 0.0;
    giProbes[probe] = p;
}
