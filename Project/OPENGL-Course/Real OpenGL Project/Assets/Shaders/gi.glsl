// =====================================================================
// gi.glsl - ambient light from the global illumination probes (Rendering/GlobalIllumination.h).
// Included by the lit shaders (objects, terrain, grass, tree impostors) and the voxel lighting
// (bounces). GIAmbient replaces the sun's flat ambient term: the sky light that actually reaches
// a point (darker under a canopy, indoors, in creases) plus the light bounced off lit surfaces.
//
// Each cascade interpolates its 8 surrounding probes (DDGI, Majercik et al. 2019): trilinear
// weights, a soft backface term (probes behind the surface count less) and a Chebyshev visibility
// test against the distances the probe saw (a probe on the other side of a wall does not light
// this side). Beyond both cascades, the sky probe (the open sky's light), blended smoothly. While
// TAA is on, overdraw-heavy foliage and the far cascade read one probe picked at random instead
// of eight (GIAmbientTemporal): the frames average the picks.
// =====================================================================

#include "gi_block.glsl"

layout(std430, binding = 10) readonly buffer GIProbes { GIProbe giProbes[]; };

/// Ambient light probe i gives a surface facing N (its ambient cube)
vec3 GIProbeIrradiance(int i, vec3 N)
{
    vec3 n2 = N * N;
    return n2.x * giProbes[i].axis[N.x >= 0.0 ? 0 : 1].rgb
         + n2.y * giProbes[i].axis[N.y >= 0.0 ? 2 : 3].rgb
         + n2.z * giProbes[i].axis[N.z >= 0.0 ? 4 : 5].rgb;
}

/// Distance probe i sees toward direction d (unit), from its six axis means
float GIProbeDistance(int i, vec3 d)
{
    vec3 d2 = d * d;
    return d2.x * giProbes[i].axis[d.x >= 0.0 ? 0 : 1].w
         + d2.y * giProbes[i].axis[d.y >= 0.0 ? 2 : 3].w
         + d2.z * giProbes[i].axis[d.z >= 0.0 ? 4 : 5].w;
}

// coverage: 1 well inside the window, fading to 0 over its last probe and a half
vec3 GISampleCascade(int cascade, vec3 p, vec3 N, out float coverage)
{
    coverage = 0.0;
    float spacing = cascade == 0 ? giSpacing.x : giSpacing.y;
    ivec3 base = cascade == 0 ? giBase0.xyz : giBase1.xyz;
    ivec3 baseSlot = cascade == 0 ? giSlot0.xyz : giSlot1.xyz;
    vec3 g = p / spacing - vec3(base);
    vec3 room = min(g, vec3(giCounts.xyz - 1) - g);
    float edge = min(min(room.x, room.y), room.z);
    if (edge < 0.0) return vec3(0.0);

    ivec3 i0 = ivec3(floor(g));
    vec3 f = g - vec3(i0);
    vec3 sum = vec3(0.0);
    float weightSum = 0.0;
    for (int k = 0; k < 8; k++) {
        ivec3 off = ivec3(k & 1, (k >> 1) & 1, k >> 2);
        vec3 tri = mix(1.0 - f, f, vec3(off));
        float trilinear = tri.x * tri.y * tri.z;
        if (trilinear <= 0.0) continue;
        ivec3 local = min(i0 + off, giCounts.xyz - 1);
        ivec3 w = base + local;
        // Its slot in the toroidal storage, without an integer modulo (slow on GPUs)
        ivec3 slot = baseSlot + local;
        slot -= giCounts.xyz * ivec3(greaterThanEqual(slot, giCounts.xyz));
        int index = GIIndex(cascade, slot);
        uint key = giProbes[index].key;
        if ((key & (GI_VALID | GI_INSIDE)) != GI_VALID || (key & 0x3FFFFFFFu) != GIKey(w)) continue;

        vec4 offset = giProbes[index].offset; // Moved out of geometry: seen from where it really is
        vec3 toProbe = vec3(w) * spacing + offset.xyz - p;
        float dist = length(toProbe);
        vec3 dir = toProbe / max(dist, 1e-4);
        // Smooth backface term: probes behind the surface see the other side
        float backface = (dot(dir, N) + 1.0) * 0.5;
        float weight = backface * backface + 0.2;
        // Chebyshev: can the probe see this point, or is something in between?
        float mean = GIProbeDistance(index, -dir);
        if (dist > mean) {
            float variance = max(offset.w, 0.0) * mean * mean + 0.0004 * spacing * spacing;
            float d = dist - mean;
            float chebyshev = variance / (variance + d * d);
            weight *= max(chebyshev * chebyshev * chebyshev, 0.0);
        }
        // Tiny weights would let a single leaking probe dominate: crush them (DDGI)
        const float crush = 0.2;
        if (weight < crush) weight *= weight * weight / (crush * crush);
        weight *= trilinear;

        sum += GIProbeIrradiance(index, N) * weight;
        weightSum += weight;
    }
    if (weightSum < 1e-5) return vec3(0.0);
    coverage = clamp(edge / 1.5, 0.0, 1.0);
    return sum / weightSum;
}

// One probe of the 8, picked at random (u in [0, 1)) in proportion to the weight it would get
// from its place alone (trilinear x backface): the blend on average, for an eighth of the reads.
// A probe that cannot light anything (inside geometry, not traced yet) passes the pick on to the
// next one. No visibility test: for grass, leaf cards and impostors (open ground or inside a
// canopy), where the overdraw multiplies the cost and TAA averages the picks over the frames.
vec3 GISampleCascadeStochastic(int cascade, vec3 p, vec3 N, float u, out float coverage)
{
    coverage = 0.0;
    float spacing = cascade == 0 ? giSpacing.x : giSpacing.y;
    ivec3 base = cascade == 0 ? giBase0.xyz : giBase1.xyz;
    ivec3 baseSlot = cascade == 0 ? giSlot0.xyz : giSlot1.xyz;
    vec3 g = p / spacing - vec3(base);
    vec3 room = min(g, vec3(giCounts.xyz - 1) - g);
    float edge = min(min(room.x, room.y), room.z);
    if (edge < 0.0) return vec3(0.0);

    ivec3 i0 = ivec3(floor(g));
    vec3 f = g - vec3(i0);
    float weights[8];
    float total = 0.0;
    for (int k = 0; k < 8; k++) {
        ivec3 off = ivec3(k & 1, (k >> 1) & 1, k >> 2);
        vec3 tri = mix(1.0 - f, f, vec3(off));
        vec3 toProbe = vec3(base + min(i0 + off, giCounts.xyz - 1)) * spacing - p;
        float backface = (dot(toProbe, N) * inversesqrt(max(dot(toProbe, toProbe), 1e-8)) + 1.0) * 0.5;
        weights[k] = (backface * backface + 0.2) * tri.x * tri.y * tri.z;
        total += weights[k];
    }
    float target = u * total, run = 0.0;
    int pick = 7;
    for (int k = 0; k < 8; k++) {
        run += weights[k];
        if (run > target) { pick = k; break; }
    }
    for (int n = 0; n < 8; n++) {
        int k = (pick + n) & 7;
        if (weights[k] <= 0.0) continue;
        ivec3 local = min(i0 + ivec3(k & 1, (k >> 1) & 1, k >> 2), giCounts.xyz - 1);
        ivec3 slot = baseSlot + local;
        slot -= giCounts.xyz * ivec3(greaterThanEqual(slot, giCounts.xyz));
        int index = GIIndex(cascade, slot);
        uint key = giProbes[index].key;
        if ((key & (GI_VALID | GI_INSIDE)) != GI_VALID || (key & 0x3FFFFFFFu) != GIKey(base + local)) continue;
        coverage = clamp(edge / 1.5, 0.0, 1.0);
        return GIProbeIrradiance(index, N);
    }
    return vec3(0.0);
}

// Both cascades blended (near inside its window, far around it, the sky probe beyond); each one
// reads every probe around the point, or one picked at random (u)
vec3 GIAmbientBlend(vec3 worldPos, vec3 N, vec3 V, vec3 fallback, float u, bool nearRandom, bool farRandom)
{
    if (giSpacing.w < 0.5) return fallback;
    // Pushed off the surface (toward the normal and the eye) so it does not see itself
    float coverageNear, coverageFar = 0.0;
    vec3 pNear = worldPos + (N * 0.3 + V * 0.1) * giSpacing.x;
    vec3 near = nearRandom ? GISampleCascadeStochastic(0, pNear, N, u, coverageNear)
                           : GISampleCascade(0, pNear, N, coverageNear);
    if (coverageNear >= 1.0) return near * giSpacing.z;
    vec3 pFar = worldPos + (N * 0.3 + V * 0.1) * giSpacing.y;
    vec3 far = farRandom ? GISampleCascadeStochastic(1, pFar, N, u, coverageFar)
                         : GISampleCascade(1, pFar, N, coverageFar);
    vec3 outer = mix(GIProbeIrradiance(GISkyProbe(), N), far, coverageFar);
    return mix(outer, near, coverageNear) * giSpacing.z;
}

/// Ambient light at worldPos for a surface with normal N seen from direction V (toward the eye):
/// every probe around it, with the visibility test. fallback: the flat ambient, used while the
/// global illumination is off. Beyond the probes, the open sky's light (what a probe with
/// nothing around would see).
vec3 GIAmbient(vec3 worldPos, vec3 N, vec3 V, vec3 fallback)
{
    return GIAmbientBlend(worldPos, N, V, fallback, 0.0, false, false);
}

/// While TAA averages the frames (u in [0, 1), new every pixel and frame: GINoise). Surfaces read
/// every probe of the near cascade (visibility test included) and one probe picked at random of
/// the far one (8 m apart: they differ little, the noise is faint and averages out); foliage
/// (leaf cards, grass, impostors: layers of overdraw, alpha tested) one probe of each.
vec3 GIAmbientTemporal(vec3 worldPos, vec3 N, vec3 V, vec3 fallback, float u, bool foliage)
{
    return foliage ? GIAmbientBlend(worldPos, N, V, fallback, u, true, true)
                   : GIAmbientBlend(worldPos, N, V, fallback, u, false, true);
}

/// A random number for the pick: interleaved gradient noise (Jimenez 2014) of the pixel, shifted
/// by the frame's dither offset (frame.glsl: frameJitter.z) so it changes every frame
float GINoise(vec2 pixel, float frameOffset)
{
    return fract(52.9829189 * fract(dot(pixel, vec2(0.06711056, 0.00583715))) + frameOffset);
}
