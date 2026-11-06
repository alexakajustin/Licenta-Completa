#version 430 core

/**
 * @file water_sim.glsl
 * @brief Water simulation around the camera (Rendering/WaterFoamMap.h): how the water moves
 * around everything standing in it.
 *
 * A 2D grid over the foam map's window (top-down, 25 cm cells), stepped once per frame:
 *  - currents: "Stable Fluids" (Stam 1999; GPU: Harris, GPU Gems ch. 38). The velocity is carried
 *    along itself (semi-Lagrangian), pulled toward the river's own current (RiverNode's flow,
 *    fastest mid-stream), kept spinning by vorticity confinement (Fedkiw et al. 2001: the eddies
 *    shed behind rocks) and made divergence-free with a pressure solve where banks and objects
 *    crossing the surface are walls: the current parts around a rock, speeds up on its flanks
 *    and leaves a slow, swirling wake,
 *  - surface: the wave equation on a height field carried by that current (the 2D shallow
 *    surface of Still Wakes the Deep, UE5): water running into an object rises against it and
 *    sheds waves that the current bends into a V, waves bounce off rocks and banks, and run out
 *    of depth on the shore (the waterline moves up and down the bank),
 *  - foam: a quantity carried by the current, born where the water hits an object, in strong
 *    eddies, on steep crests and where waves break on the shore, fading over seconds.
 * Every field is carried from the last frame by world position, so the window may follow the
 * camera (it moves in whole cells).
 *
 * Modes (one dispatch each): 0 domain, 1 curl, 2 velocity, 3 divergence, 4 pressure (Jacobi),
 * 5 projection, 6 waves, 7 foam.
 */

layout(local_size_x = 8, local_size_y = 8) in;

uniform int mode;
uniform int size;          // Cells per side
uniform float dx;          // Metres per cell
uniform float dt;          // Seconds this step
uniform float time;
uniform vec2 origin;       // World xz of this frame's window corner
uniform vec2 prevOrigin;   // ... and of the last frame's (the fields carried from it)
uniform int reset;         // 1: no history (start, jump): the current starts as the river's

layout(binding = 0) uniform sampler2D tex0;
layout(binding = 1) uniform sampler2D tex1;
layout(binding = 2) uniform sampler2D tex2;
layout(binding = 3) uniform sampler2D tex3;
layout(binding = 4) uniform sampler2D tex4;

layout(rgba16f, binding = 0) writeonly uniform image2D outRGBA;
layout(rg16f, binding = 1) writeonly uniform image2D outRG;
layout(r16f, binding = 2) writeonly uniform image2D outR;

ivec2 cell;

vec4 At(sampler2D t, ivec2 c) { return texelFetch(t, clamp(c, ivec2(0), ivec2(size - 1)), 0); }
vec2 WorldOf(ivec2 c) { return origin + (vec2(c) + 0.5) * dx; }
vec2 PrevUV(vec2 world) { return (world - prevOrigin) / (float(size) * dx); }
bool Inside(vec2 uv) { return all(greaterThanEqual(uv, vec2(0.0))) && all(lessThanEqual(uv, vec2(1.0))); }

// Domain (mode 0's output, tex1 in the later modes): x = 1 water / 0 wall, yz = the river's own
// current there (m/s), w = water depth (m)
bool Fluid(sampler2D domain, ivec2 c)
{
    if (any(lessThan(c, ivec2(0))) || any(greaterThanEqual(c, ivec2(size)))) return true; // Open window edge
    return texelFetch(domain, c, 0).x > 0.5;
}

float Hash(vec3 p)
{
    p = fract(p * vec3(0.1031, 0.1030, 0.0973));
    p += dot(p, p.yzx + 33.33);
    return fract((p.x + p.y) * p.z);
}

// Noise in time per cell, smooth (turbulent pushes that come and go)
float Flicker(ivec2 c, float rate)
{
    float t = time * rate;
    float a = Hash(vec3(vec2(c), floor(t))), b = Hash(vec3(vec2(c), floor(t) + 1.0));
    return mix(a, b, smoothstep(0.0, 1.0, fract(t))) * 2.0 - 1.0;
}

// Smooth noise over the world (cells of 'scale' metres), changing at 'rate' per second
float SmoothNoise(vec2 world, float scale, float rate)
{
    vec2 p = world / scale;
    vec2 i = floor(p), f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    float t = time * rate, tf = fract(t), ti = floor(t);
    float n0 = mix(mix(Hash(vec3(i, ti)), Hash(vec3(i + vec2(1, 0), ti)), u.x), mix(Hash(vec3(i + vec2(0, 1), ti)), Hash(vec3(i + vec2(1, 1), ti)), u.x), u.y);
    float n1 = mix(mix(Hash(vec3(i, ti + 1.0)), Hash(vec3(i + vec2(1, 0), ti + 1.0)), u.x), mix(Hash(vec3(i + vec2(0, 1), ti + 1.0)), Hash(vec3(i + vec2(1, 1), ti + 1.0)), u.x), u.y);
    return mix(n0, n1, smoothstep(0.0, 1.0, tf)) * 2.0 - 1.0;
}

// Unit vector from a water cell toward the walls next to it (0 if none), how many there are and
// how many of them are objects standing in the water (not banks)
vec2 WallNormal(sampler2D domain, ivec2 c, out float walls, out float objects)
{
    vec2 n = vec2(0.0);
    walls = 0.0;
    objects = 0.0;
    const ivec2 off[8] = ivec2[8](ivec2(1, 0), ivec2(-1, 0), ivec2(0, 1), ivec2(0, -1), ivec2(1, 1), ivec2(-1, 1), ivec2(1, -1), ivec2(-1, -1));
    for (int i = 0; i < 8; i++) {
        ivec2 o = c + off[i];
        if (Fluid(domain, o)) continue;
        n += normalize(vec2(off[i]));
        walls += 1.0;
        if (texelFetch(domain, clamp(o, ivec2(0), ivec2(size - 1)), 0).x < -0.5) objects += 1.0;
    }
    return walls > 0.0 ? normalize(n) : vec2(0.0);
}

void main()
{
    cell = ivec2(gl_GlobalInvocationID.xy);
    if (any(greaterThanEqual(cell, ivec2(size)))) return;
    vec2 world = WorldOf(cell);

    if (mode == 0) {
        // tex0: water surfaces (r height, gb current direction, a = 1 + speed), tex1: highest ground
        // (r, below -1e5 = none), tex2: objects crossing the surface
        vec4 water = At(tex0, cell);
        float ground = At(tex1, cell).r;
        float object = At(tex2, cell).r;
        bool hasGround = ground > -1.0e5;
        float depth = hasGround ? water.r - ground : 3.0;
        bool fluid = water.a > 0.5 && object < 0.5 && depth > 0.03;
        // x: 1 water, 0 bank / dry land, -1 an object standing in the water
        imageStore(outRGBA, cell, vec4(fluid ? 1.0 : (object >= 0.5 && water.a > 0.5 ? -1.0 : 0.0), fluid ? water.gb * max(water.a - 1.0, 0.0) : vec2(0.0), max(depth, 0.0)));
        return;
    }

    if (mode == 1) {
        // Curl of last frame's current (tex0), on last frame's cells
        float dvdx = At(tex0, cell + ivec2(1, 0)).y - At(tex0, cell - ivec2(1, 0)).y;
        float dudz = At(tex0, cell + ivec2(0, 1)).x - At(tex0, cell - ivec2(0, 1)).x;
        imageStore(outR, cell, vec4((dvdx - dudz) / (2.0 * dx)));
        return;
    }

    vec4 domain = At(tex1, cell);
    bool fluid = domain.x > 0.5;

    if (mode == 2) {
        // tex0: last frame's current, tex1: domain, tex2: last frame's curl
        vec2 target = domain.yz;
        if (!fluid) { imageStore(outRG, cell, vec4(0.0)); return; }
        vec2 v;
        vec2 here = PrevUV(world);
        if (reset == 1 || !Inside(here)) {
            v = target;
        } else {
            // Carried along itself: where this water was a step ago
            vec2 vNow = texture(tex0, here).xy;
            vec2 back = PrevUV(world - vNow * dt);
            v = Inside(back) ? texture(tex0, back).xy : target;
            // Vorticity confinement: keeps the eddies the coarse grid would smear out
            vec2 e = vec2(1.0 / float(size), 0.0);
            float w = texture(tex2, here).r;
            vec2 grad = vec2(abs(texture(tex2, here + e.xy).r) - abs(texture(tex2, here - e.xy).r),
                             abs(texture(tex2, here + e.yx).r) - abs(texture(tex2, here - e.yx).r));
            float g = length(grad);
            if (g > 1e-5) v += 0.08 * (grad / g).yx * vec2(1.0, -1.0) * w * dt;
            // The river keeps driving its current (slower to take over in a wake); still water
            // (a lake: no current of its own) only slows what flows in, over many seconds - a
            // river's plume carries far out before it spreads and fades
            float settle = dot(target, target) > 0.0025 ? 2.5 : 25.0;
            v = mix(v, target, 1.0 - exp(-dt / settle));
        }
        // Into the window the river flows as it is
        int edge = min(min(cell.x, cell.y), min(size - 1 - cell.x, size - 1 - cell.y));
        if (edge < 6) v = target;
        // Bounded (a stray cell must not blow the step up)
        float s = length(v);
        if (s > 8.0) v *= 8.0 / s;
        imageStore(outRG, cell, vec4(v, 0.0, 0.0));
        return;
    }

    if (mode == 3) {
        // Divergence of the carried current (tex0); walls have no flow through them
        vec2 vL = Fluid(tex1, cell - ivec2(1, 0)) ? At(tex0, cell - ivec2(1, 0)).xy : vec2(0.0);
        vec2 vR = Fluid(tex1, cell + ivec2(1, 0)) ? At(tex0, cell + ivec2(1, 0)).xy : vec2(0.0);
        vec2 vB = Fluid(tex1, cell - ivec2(0, 1)) ? At(tex0, cell - ivec2(0, 1)).xy : vec2(0.0);
        vec2 vT = Fluid(tex1, cell + ivec2(0, 1)) ? At(tex0, cell + ivec2(0, 1)).xy : vec2(0.0);
        float div = fluid ? ((vR.x - vL.x) + (vT.y - vB.y)) / (2.0 * dx) : 0.0;
        imageStore(outR, cell, vec4(div));
        return;
    }

    if (mode == 4) {
        // Jacobi iteration: tex0 pressure, tex2 divergence; a wall reflects the pressure
        float p = At(tex0, cell).r;
        if (!fluid) { imageStore(outR, cell, vec4(0.0)); return; }
        float pL = Fluid(tex1, cell - ivec2(1, 0)) ? At(tex0, cell - ivec2(1, 0)).r : p;
        float pR = Fluid(tex1, cell + ivec2(1, 0)) ? At(tex0, cell + ivec2(1, 0)).r : p;
        float pB = Fluid(tex1, cell - ivec2(0, 1)) ? At(tex0, cell - ivec2(0, 1)).r : p;
        float pT = Fluid(tex1, cell + ivec2(0, 1)) ? At(tex0, cell + ivec2(0, 1)).r : p;
        float div = At(tex2, cell).r;
        imageStore(outR, cell, vec4((pL + pR + pB + pT - dx * dx * div) * 0.25));
        return;
    }

    if (mode == 5) {
        // Projection: tex0 carried current, tex2 pressure -> divergence-free current
        if (!fluid) { imageStore(outRG, cell, vec4(0.0)); return; }
        float p = At(tex2, cell).r;
        float pL = Fluid(tex1, cell - ivec2(1, 0)) ? At(tex2, cell - ivec2(1, 0)).r : p;
        float pR = Fluid(tex1, cell + ivec2(1, 0)) ? At(tex2, cell + ivec2(1, 0)).r : p;
        float pB = Fluid(tex1, cell - ivec2(0, 1)) ? At(tex2, cell - ivec2(0, 1)).r : p;
        float pT = Fluid(tex1, cell + ivec2(0, 1)) ? At(tex2, cell + ivec2(0, 1)).r : p;
        vec2 v = At(tex0, cell).xy - vec2(pR - pL, pT - pB) / (2.0 * dx);
        imageStore(outRG, cell, vec4(v, 0.0, 0.0));
        return;
    }

    if (mode == 6) {
        // Surface: tex0 last step's (height, rate) on its cells, tex2 this frame's current, tex4
        // last frame's curl.
        // Shallow water stills the waves (they run out of depth on the shore)
        if (!fluid) { imageStore(outRG, cell, vec4(0.0)); return; }
        vec2 v = At(tex2, cell).xy;
        vec2 back = PrevUV(world - v * dt);
        if (reset == 1 || !Inside(back)) { imageStore(outRG, cell, vec4(0.0)); return; }
        vec2 hw = texture(tex0, back).xy;
        // Neighbours of that point; a wall mirrors the surface (waves bounce off it)
        vec2 e = vec2(1.0 / float(size), 0.0);
        float hL = Fluid(tex1, cell - ivec2(1, 0)) ? texture(tex0, back - e.xy).x : hw.x;
        float hR = Fluid(tex1, cell + ivec2(1, 0)) ? texture(tex0, back + e.xy).x : hw.x;
        float hB = Fluid(tex1, cell - ivec2(0, 1)) ? texture(tex0, back - e.yx).x : hw.x;
        float hT = Fluid(tex1, cell + ivec2(0, 1)) ? texture(tex0, back + e.yx).x : hw.x;
        float laplacian = (hL + hR + hB + hT - 4.0 * hw.x) / (dx * dx);
        float depth = domain.w;
        // Wave speed: gravity waves of about a metre (1.25 m/s), slower where it gets shallow
        float c = 1.25 * clamp(sqrt(depth / 0.4), 0.3, 1.0);
        float rate = hw.y + c * c * laplacian * dt;

        // Where the current runs into an object the water piles up to its stagnation head
        // (v^2 / 2g: 7 cm at 1.2 m/s, 20 cm at 2 m/s) and sheds waves as it surges and falls
        // back; behind it the surface dips
        // (The approaching current: right at the face the water has stopped - stagnation)
        float walls, objects;
        vec2 n = WallNormal(tex1, cell, walls, objects);
        vec2 approach = domain.yz;
        float into = dot(approach, n);
        if (objects > 0.0 && depth > 0.12) {
            float head = dot(approach, approach) / (2.0 * 9.81);
            float surge = 1.0 + 0.6 * SmoothNoise(world, 1.0, 2.5);
            float target = into > 0.0 ? head * surge * clamp(into / max(length(approach), 1e-3), 0.0, 1.0) : -0.35 * head;
            rate += (target - hw.x) * 18.0 * dt;
        }
        // Eddies (shed behind obstacles: last frame's curl, tex4) boil the surface
        vec2 here = PrevUV(world);
        float eddy = Inside(here) ? min(abs(texture(tex4, here).r), 3.0) : 0.0;
        rate += SmoothNoise(world, 0.8, 2.0) * eddy * 0.12 * dt;
        // Fast water stirs the surface a little (smooth turbulence a couple of metres across)
        rate += SmoothNoise(world, 2.0, 0.8) * length(v) * 0.03 * dt;

        // Damping: viscosity, more in shallow water; the window's border absorbs
        int edge = min(min(cell.x, cell.y), min(size - 1 - cell.x, size - 1 - cell.y));
        float damping = 0.6 + 3.0 * (1.0 - smoothstep(0.05, 0.35, depth)) + (edge < 24 ? (24.0 - float(edge)) * 0.4 : 0.0);
        rate *= exp(-damping * dt);
        float h = (hw.x + rate * dt) * exp(-0.15 * dt); // Slowly back to the mean level
        h = clamp(h, -0.35, 0.35);
        imageStore(outRG, cell, vec4(h, rate, 0.0, 0.0));
        return;
    }

    if (mode == 7) {
        // Foam: tex0 last frame's foam (its cells), tex2 current, tex3 surface (h, rate), tex4
        // last frame's curl (its cells)
        if (!fluid) { imageStore(outR, cell, vec4(0.0)); return; }
        vec2 v = At(tex2, cell).xy;
        vec2 back = PrevUV(world - v * dt);
        float foam = 0.0;
        if (reset == 0 && Inside(back)) {
            // Carried by the current and dispersing: a trail widens and softens downstream
            // instead of staying a thin bright line
            vec2 e = vec2(0.7 / float(size), 0.0);
            foam = texture(tex0, back).r * 0.6 + (texture(tex0, back + e.xy).r + texture(tex0, back - e.xy).r
                 + texture(tex0, back + e.yx).r + texture(tex0, back - e.yx).r) * 0.1;
        }
        foam *= exp(-dt / 2.5); // Bubbles burst over a few seconds

        float walls, objects;
        vec2 n = WallNormal(tex1, cell, walls, objects);
        float depth = domain.w;
        float into = max(dot(domain.yz, n), 0.0); // The approaching current (stopped at the face)
        // Where the water hits an object (its upstream face: 'into'), not along the banks
        foam += objects > 0.0 && depth > 0.1 ? into * into * 3.0 * dt : 0.0;
        // Strong eddies (shed behind obstacles) and fast, churning water
        vec2 here = PrevUV(world);
        float curl = Inside(here) ? abs(texture(tex4, here).r) : 0.0;
        // (Not along walls: there the curl is the shear of water sliding past a bank or a rock)
        foam += walls == 0.0 ? max(curl - 1.2, 0.0) * 0.35 * dt : 0.0;
        // Steep crests and waves breaking on the shore
        vec4 s = At(tex3, cell);
        float hx = At(tex3, cell + ivec2(1, 0)).x - At(tex3, cell - ivec2(1, 0)).x;
        float hz = At(tex3, cell + ivec2(0, 1)).x - At(tex3, cell - ivec2(0, 1)).x;
        float steep = length(vec2(hx, hz)) / (2.0 * dx);
        foam += max(steep - 0.3, 0.0) * 2.0 * dt;
        if (walls > 0.0 && depth < 0.35) foam += max(abs(s.y) - 0.12, 0.0) * 4.0 * dt; // Only waves that break
        imageStore(outR, cell, vec4(clamp(foam, 0.0, 1.0)));
        return;
    }
}
