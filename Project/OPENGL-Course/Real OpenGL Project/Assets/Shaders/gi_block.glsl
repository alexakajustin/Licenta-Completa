// =====================================================================
// gi_block.glsl - the global illumination probe grids (Rendering/GlobalIllumination.h): their
// placement (one uniform block, every program) and the layout of one probe. Included by gi.glsl
// (lit shaders: reading) and gi_probes.glsl (the update: writing).
//
// Two cascades of probes follow the camera: a fine grid close by, a coarse one further out. A
// probe at integer grid coordinate w sits at w * spacing; the grid window starts at giBase and is
// stored toroidally (slot = w mod counts), so moving the camera only re-initialises the probes
// that enter the window.
// =====================================================================

layout(std140) uniform GIBlock {
    vec4 giSpacing;   ///< x: near spacing (m), y: far spacing, z: intensity, w: 1 = on
    ivec4 giBase0;    ///< xyz: grid coordinate of the near window's first probe
    ivec4 giBase1;    ///< xyz: same for the far window
    ivec4 giCounts;   ///< xyz: probes along each axis (both cascades), w: probes per cascade
    ivec4 giSlot0;    ///< xyz: slot of the near window's first probe (giBase0 mod giCounts)
    ivec4 giSlot1;    ///< xyz: same for the far window
};

/// One probe. The ambient light a surface facing n receives (irradiance / pi, per channel), kept
/// for the six axis directions and blended by n^2 (an "ambient cube", as in Half-Life 2): exact for
/// floors and walls, no ringing, three reads per probe. (L1 spherical harmonics let light from the
/// horizon leak into surfaces facing up or down: too bright under a roof or a canopy.)
/// w: mean distance the probe sees toward that axis (leak prevention, Chebyshev test as in DDGI).
struct GIProbe {
    vec4 axis[6];   ///< +x, -x, +y, -y, +z, -z
    vec4 offset;    ///< xyz: where it sits from its grid point (moved out of geometry: a floor, a
                    ///< wall), w: relative variance of the hit distances
    uint key;       ///< Bit 31: valid, bit 30: inside geometry (nowhere free to go), bits 0-29: the grid coordinate it holds
    float age;      ///< Updates blended into it (a young probe averages them evenly: it settles fast)
    float pad0;
    float pad1;
};

/// After the probes of both cascades: the open sky's light (an ambient cube that sees only the
/// sky), what the lit shaders use beyond the probes
int GISkyProbe() { return 2 * giCounts.w; }

const uint GI_VALID = 0x80000000u;
const uint GI_INSIDE = 0x40000000u;

uint GIKey(ivec3 w)
{
    uvec3 u = uvec3(w) & 1023u;
    return u.x | (u.y << 10) | (u.z << 20);
}

ivec3 GISlot(ivec3 w)
{
    return ((w % giCounts.xyz) + giCounts.xyz) % giCounts.xyz;
}

int GIIndex(int cascade, ivec3 slot)
{
    return cascade * giCounts.w + slot.x + giCounts.x * (slot.y + giCounts.y * slot.z);
}
