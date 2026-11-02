// =====================================================================
// cloud_shadow.glsl - shadows of the volumetric clouds on everything the sun lights
// (Rendering/CloudShadows.h). Included by the lit shaders (objects, terrain, grass, impostors,
// water) and the volumetric fog, so the ground, the lake and the air under a cloud all darken
// together and the shadows drift with the clouds.
//
// The map holds, for each point of a square around the camera on the plane y = 0, how much sun
// gets through the clouds. A point at height y looks it up where its own sun ray crosses that
// plane, so a hill top and the valley below it get the right patch.
//
// The map lives in the uniform block itself (4 bits per texel, two per byte, copied there on
// the GPU), not in a texture: the main shader already uses every texture unit a fragment shader
// may have, and the block must fit the 16 KB every GPU guarantees.
// =====================================================================

const int CLOUD_SHADOW_SIZE = 180; // Texels along a side (CloudShadowMap::SIZE)

layout(std140) uniform CloudShadowBlock {
    vec4 cloudShadowArea;        ///< xy: world xz of the map's corner, z: its size (m), w: 1 = on
    vec4 cloudShadowSun;         ///< xy: toSun.xz / toSun.y (xz shift per metre of height), z: strength
    uvec4 cloudShadowData[1013]; ///< SIZE x SIZE nibbles, row by row from the corner: sun transmittance
};

float CloudShadowTexel(ivec2 c)
{
    c = clamp(c, ivec2(0), ivec2(CLOUD_SHADOW_SIZE - 1));
    int i = c.y * CLOUD_SHADOW_SIZE + c.x; // Texel; its byte is i / 2 (rows have an even length)
    int byteIndex = i >> 1;
    uint word = cloudShadowData[byteIndex >> 4][(byteIndex >> 2) & 3];
    uint byteValue = (word >> uint((byteIndex & 3) * 8)) & 255u;
    uint nibble = (i & 1) == 0 ? (byteValue & 15u) : (byteValue >> 4u);
    return float(nibble) / 15.0;
}

/// Sun light reaching worldPos through the clouds: 1 = clear sky, 1 - strength under a cloud
float CloudShadow(vec3 worldPos)
{
    if (cloudShadowArea.w < 0.5) return 1.0;
    // The point on y = 0 whose sun ray passes through worldPos (same clouds above both)
    vec2 ground = worldPos.xz - cloudShadowSun.xy * worldPos.y;
    vec2 uv = (ground - cloudShadowArea.xy) / cloudShadowArea.z;
    // Past the map's edge: no shadow (faded, so the edge never shows as a line)
    vec2 e = min(uv, 1.0 - uv);
    float inside = smoothstep(0.0, 0.1, min(e.x, e.y));
    if (inside <= 0.0) return 1.0;

    // Bilinear between the four texels around the point
    vec2 st = uv * float(CLOUD_SHADOW_SIZE) - 0.5;
    ivec2 c = ivec2(floor(st));
    vec2 f = st - vec2(c);
    float transmittance = mix(mix(CloudShadowTexel(c), CloudShadowTexel(c + ivec2(1, 0)), f.x),
                              mix(CloudShadowTexel(c + ivec2(0, 1)), CloudShadowTexel(c + ivec2(1, 1)), f.x), f.y);
    return mix(1.0, transmittance, cloudShadowSun.z * inside);
}
