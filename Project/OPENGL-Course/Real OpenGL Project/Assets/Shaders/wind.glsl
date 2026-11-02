// =====================================================================
// wind.glsl — the scene's wind (Rendering/Wind.h, Graphics Settings > Wind)
//
// Included by every shader that moves with the wind: grass, plants and trees (shader.vert,
// instanced_object.vert, the instanced shadow shaders), impostor cards, the lake, the clouds.
// One uniform block, written once per frame: direction, speed, gusts sweeping across the land
// downwind, turbulence, and phases integrated on the CPU (changing the speed never jumps).
//
// Plants answer in layers (Crytek, "Vegetation Procedural Animation and Shading in Crysis",
// GPU Gems 3 ch. 16; SpeedTree's wind):
//   - trunk: the whole plant leans downwind with the drag (~ speed^2) and sways around that
//     lean at its own slow rhythm - bigger trees slower - bending quadratically with the height
//     above its base and keeping its length, so the crown moves on an arc (WindTrunk: impostors);
//   - branches: away from the trunk they bob and swing with their own rhythm, coherent along
//     each branch, more toward the tips;
//   - leaves: leaf cards flutter quickly along their normal, leaf by leaf (WindPlant: all three,
//     the wind at the plant computed once per vertex);
//   - WindGrass: blades bend over with the gusts rolling across a field, flick around that pose
//     and tilt their normals, so the gusts show as waves of light over the grass.
// Bark and leaves of one plant get the same trunk and branch motion (functions of the mesh
// position), so the leaves stay on their twigs.
// =====================================================================

layout(std140) uniform WindBlock {
    vec4 windDirSpeed;  ///< xy: where the wind blows to (world xz, unit), z: speed (m/s), w: 1 = blowing
    vec4 windGust;      ///< xy: drift of the gust field (m), z: gustiness, w: turbulence
    vec4 windPhase;     ///< Integrated phases (radians): x trunks, y branches, z leaves, w grass
    vec4 windResponse;  ///< x: tree sway, y: leaf flutter, z: grass bend (Graphics Settings)
    vec4 windClouds;    ///< xy: cloud drift (sky units)
    // The same four for the previous frame: where a vertex was (motion vectors, TemporalAA.h)
    vec4 windPrevDirSpeed;
    vec4 windPrevGust;
    vec4 windPrevPhase;
    vec4 windPrevResponse;
};

const float WIND_TAU = 6.2831853;

/// The wind of one frame: this one (WindNow) or the previous one (WindPrev). Every function
/// below has a *P form taking it; the plain forms use this frame's.
struct WindParams { vec4 dirSpeed; vec4 gust; vec4 phase; vec4 response; };
WindParams WindNow()  { return WindParams(windDirSpeed, windGust, windPhase, windResponse); }
WindParams WindPrev() { return WindParams(windPrevDirSpeed, windPrevGust, windPrevPhase, windPrevResponse); }

/// The block has been written (a unit direction): false in tools that never update the wind
bool WindActive() { return dot(windDirSpeed.xy, windDirSpeed.xy) > 0.5; }

float WindHash(ivec2 c)
{
    uvec2 q = uvec2(c + ivec2(1 << 20));
    uint h = (q.x * 1597334677u) ^ (q.y * 3812015801u);
    h ^= h >> 16;
    h *= 2246822519u;
    h ^= h >> 13;
    return float(h) * (1.0 / 4294967296.0);
}

/// Stable random number in [0, 1) for a position (per-plant phases; 25 cm cells)
float WindRandom(vec2 p) { return WindHash(ivec2(floor(p * 4.0))); }

/// Smooth value noise in [0, 1]
float WindNoise(vec2 p)
{
    ivec2 i = ivec2(floor(p));
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(WindHash(i), WindHash(i + ivec2(1, 0)), u.x),
               mix(WindHash(i + ivec2(0, 1)), WindHash(i + ivec2(1, 1)), u.x), u.y);
}

/// Wind speed relative to a fresh breeze (10 m/s = 1)
float WindStrengthP(WindParams P) { return P.dirSpeed.z * 0.1; }
float WindStrength() { return WindStrengthP(WindNow()); }

/// Gusts at a world position: a coarse noise field drifting downwind, stretched across the wind
/// so gust fronts are wide bands that sweep over a field or a forest. ~0.35 in a lull, ~1.7 in a
/// gust at full gustiness; 1 everywhere when the wind is steady.
float WindGustP(WindParams P, vec2 xz)
{
    vec2 dir = P.dirSpeed.xy;
    vec2 p = xz - P.gust.xy;
    vec2 q = vec2(dot(p, dir), dot(p, vec2(-dir.y, dir.x)));    // Along, across the wind
    float n = WindNoise(q * vec2(1.0 / 45.0, 1.0 / 110.0)) * 0.7
            + WindNoise(q * vec2(1.0 / 17.0, 1.0 / 40.0) + vec2(17.3, 5.1)) * 0.3;
    float gust = smoothstep(0.32, 0.78, n);
    return mix(1.0, 0.35 + 1.35 * gust, P.gust.z);
}
float WindGust(vec2 xz) { return WindGustP(WindNow(), xz); }

/// Turbulence: finer, faster swirls on top of the gusts (0.4 .. 1.6 at full turbulence)
float WindTurbulenceP(WindParams P, vec2 xz)
{
    float n = WindNoise((xz - P.gust.xy * 1.7) * (1.0 / 7.0) + vec2(31.7, 11.3));
    return 1.0 + (n * 2.0 - 1.0) * 0.6 * P.gust.w;
}
float WindTurbulence(vec2 xz) { return WindTurbulenceP(WindNow(), xz); }

/// What every part of one plant shares, computed once per vertex: the wind at the plant
/// (gust x turbulence at its root), its size class and its random seed
struct WindPlantState {
    vec3 dir;       ///< Downwind (world xz)
    vec3 across;    ///< Across the wind
    float s;        ///< Wind strength (10 m/s = 1)
    float gust;     ///< Gust x turbulence at the plant
    float size;     ///< 1 for a 12 m tree: bigger plants are stiffer and slower
    float seed;     ///< Random in [0, 1) per plant
};

WindPlantState WindPlantSetupP(WindParams P, vec3 root, float height)
{
    WindPlantState w;
    w.dir = vec3(P.dirSpeed.x, 0.0, P.dirSpeed.y);
    w.across = vec3(-w.dir.z, 0.0, w.dir.x);
    w.s = WindStrengthP(P);
    w.gust = WindGustP(P, root.xz) * WindTurbulenceP(P, root.xz * 0.5);
    w.size = clamp(height / 12.0, 0.25, 3.0);
    w.seed = WindRandom(root.xz);
    return w;
}
WindPlantState WindPlantSetup(vec3 root, float height) { return WindPlantSetupP(WindNow(), root, height); }

/// Trunk: the whole plant leans downwind with the drag (~ speed^2, saturating in a storm) and
/// sways around that lean, slower and stiffer when bigger, a little across the wind too;
/// quadratic in the height above the base and renormalised, so the crown moves on an arc.
vec3 WindTrunkWithP(WindParams P, WindPlantState w, vec3 world, vec3 root, float height)
{
    vec3 fromRoot = world - root;
    float len = length(fromRoot);
    if (len < 1e-4) return world;
    float drag = w.s * w.s * w.gust * w.gust;
    float lean = 0.055 * drag / (1.0 + 0.3 * drag);                        // Radians at the top
    float phase = P.phase.x / sqrt(w.size) + w.seed * WIND_TAU;
    float swing = (0.012 + 0.032 * w.s * w.gust) / sqrt(w.size);
    float along = lean + swing * (sin(phase) + 0.35 * sin(phase * 2.17 + 1.1));
    float side = swing * 0.5 * sin(phase * 0.79 + 2.3);
    float rel = clamp(fromRoot.y / height, 0.0, 1.3);
    vec3 offset = (w.dir * along + w.across * side) * height * rel * rel * P.response.x;
    return root + normalize(fromRoot + offset) * len;
}
vec3 WindTrunkWith(WindPlantState w, vec3 world, vec3 root, float height) { return WindTrunkWithP(WindNow(), w, world, root, height); }

/// Branches: away from the trunk axis they bob and swing with their own rhythm, coherent along
/// each branch (the phase is a coarse noise of the mesh position), more toward the tips.
/// radial: distance from the trunk axis (world m); local: mesh-space position.
vec3 WindBranchesWithP(WindParams P, WindPlantState w, vec3 world, float radial, vec3 local)
{
    float reach = max(radial - 0.25 * w.size, 0.0);                        // The trunk core stays put
    float phase = P.phase.y / sqrt(w.size)
                + WindNoise(local.xz * 0.5 + vec2(local.y * 0.35, w.seed * 37.0)) * WIND_TAU * 1.5;
    float amp = (0.010 + 0.040 * w.s * w.gust) * pow(reach, 1.25) * P.response.x;
    vec3 move = vec3(0.0, 0.75 * sin(phase), 0.0) + w.dir * (0.45 * w.gust * w.s + 0.35 * sin(phase * 0.83 + 2.1));
    return world + move * amp;
}
vec3 WindBranchesWith(WindPlantState w, vec3 world, float radial, vec3 local) { return WindBranchesWithP(WindNow(), w, world, radial, local); }

/// Leaves: quick, small flutter along the card normal (world), leaf by leaf, more outside
vec3 WindLeavesWithP(WindParams P, WindPlantState w, vec3 world, float radial, vec3 local, vec3 normal)
{
    float phase = P.phase.z + WindNoise(local.xz * 2.3 + vec2(local.y * 1.7, 0.0)) * WIND_TAU * 3.0;
    float amp = (0.003 + 0.020 * w.s * w.gust) * clamp(0.4 + radial * 0.2, 0.4, 1.0) * P.response.y;
    return world + (normal * sin(phase) + w.dir * (0.2 + 0.3 * sin(phase * 1.31 + 0.7))) * amp;
}
vec3 WindLeavesWith(WindPlantState w, vec3 world, float radial, vec3 local, vec3 normal) { return WindLeavesWithP(WindNow(), w, world, radial, local, normal); }

/// Trunk only (impostor cards: the whole tree as one card). world: the vertex after the
/// instance transform; root: base of the trunk (world); height: the plant's height (world m).
vec3 WindTrunkP(WindParams P, vec3 world, vec3 root, float height)
{
    if (P.dirSpeed.w < 0.5 || height < 0.05) return world;
    return WindTrunkWithP(P, WindPlantSetupP(P, root, height), world, root, height);
}
vec3 WindTrunk(vec3 world, vec3 root, float height) { return WindTrunkP(WindNow(), world, root, height); }

/// A whole plant: trunk, branches and (leaves = true) leaf flutter
vec3 WindPlantP(WindParams P, vec3 world, vec3 root, float height, float radial, vec3 local, vec3 normal, bool leaves)
{
    if (P.dirSpeed.w < 0.5 || height < 0.05) return world;
    WindPlantState w = WindPlantSetupP(P, root, height);
    vec3 p = WindTrunkWithP(P, w, world, root, height);
    p = WindBranchesWithP(P, w, p, radial, local);
    if (leaves) p = WindLeavesWithP(P, w, p, radial, local, normal);
    return p;
}
vec3 WindPlant(vec3 world, vec3 root, float height, float radial, vec3 local, vec3 normal, bool leaves)
{
    return WindPlantP(WindNow(), world, root, height, radial, local, normal, leaves);
}

/// Grass blades bending with the gusts. root: the clump's origin (world), height: blade height
/// (world m). Tilts 'normal' (world) with the bend, so the gusts show as waves of light.
vec3 WindGrassP(WindParams P, vec3 world, vec3 root, float height, inout vec3 normal)
{
    vec3 fromRoot = world - root;
    float len = length(fromRoot);
    if (P.dirSpeed.w < 0.5 || height < 0.01 || len < 1e-4) return world;

    vec3 dir = vec3(P.dirSpeed.x, 0.0, P.dirSpeed.y);
    vec3 across = vec3(-dir.z, 0.0, dir.x);
    float s = WindStrengthP(P) * P.response.z;
    float gust = WindGustP(P, root.xz);
    float seed = WindRandom(root.xz);
    // Turbulence per clump (a per-vertex noise for millions of blades would cost more than it shows)
    float turb = 1.0 + (seed * 2.0 - 1.0) * 0.6 * P.gust.w;

    // Drag ~ speed^2, saturating toward ~65 degrees; the blades flick around that pose, the
    // phase following the gust field so neighbours move together but never in lockstep
    float drag = s * s * gust * gust;
    float bend = 1.1 * (1.0 - exp(-1.8 * drag));
    float flick = (0.05 + 0.22 * s * turb) * sin(P.phase.w + seed * WIND_TAU + gust * 2.5);
    float side = (0.03 + 0.10 * s * turb) * sin(P.phase.w * 0.77 + seed * 8.2);

    float rel = clamp(fromRoot.y / height, 0.0, 1.2);
    vec3 offset = (dir * (bend + flick) + across * side) * height * rel * rel;
    normal = normalize(normal + dir * (bend + flick) * 0.7 * rel);
    return root + normalize(fromRoot + offset) * len;
}
vec3 WindGrass(vec3 world, vec3 root, float height, inout vec3 normal) { return WindGrassP(WindNow(), world, root, height, normal); }
