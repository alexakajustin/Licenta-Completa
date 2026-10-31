#version 430 core

/**
 * @file impostor.vert
 * @brief Octahedral impostor card (Impostor.h): one quad per tree facing the viewer, with the three
 *        baked views closest to the view direction and where this corner lands in each of them.
 *
 * The full LOD0 of the tree was baked from impostorFrames x impostorFrames directions over the upper
 * hemisphere, laid out on a hemi-octahedral grid. The direction to the viewer, in the tree's own
 * space, falls inside one triangle of that grid: its three corner views are blended with the
 * barycentric weights (Brucks, "Octahedral Impostors", 2018). Each view is a projection along its
 * own direction, so a point of the card is placed into each view by following the view ray onto
 * that view's plane through the tree centre, which keeps the three images aligned.
 *
 * The card covers only the outline of the tree's box seen from the viewer (a tall cedar is much
 * narrower than its bounding sphere: half the fragments), and small cards narrow the blend down to
 * the nearest view, so most of the far forest reads one view instead of three.
 *
 * Shared by the camera passes (impostor.frag) and the shadow cascades (impostor_shadow.frag); the
 * cascades are orthographic, every ray runs along impostorViewDir.
 */

layout (location = 0) in vec3 pos; ///< Corner of the card, xy in [-1, 1]

struct PackedInstance {
    vec4 posAndScale;     ///< xyz = position, w = uniform scale
    vec4 rotAndFlags;     ///< xyz = euler degrees, w = fade factor + 10 * selected
};

layout(std430, binding = 1) readonly buffer VisibleInstances {
    PackedInstance instances[];
};

uniform mat4 impostorViewProj;
uniform vec3 impostorViewPos;      ///< Perspective passes: the camera
uniform vec3 impostorViewDir;      ///< Orthographic passes: toward the light
uniform int impostorOrthographic;
uniform vec3 impostorCenter;       ///< Bounding sphere of the tree (mesh space)
uniform float impostorRadius;      ///< Half-size of a baked view (mesh space)
uniform int impostorFrames;        ///< Views per side of the grid
uniform vec3 impostorBoundsMin;    ///< Box of the tree (mesh space)
uniform vec3 impostorBoundsMax;
uniform float impostorPixelScale;  ///< Screen pixels of one unit of radius at distance 1 (0: always blend)
uniform vec4 clipPlane;

const float BLEND_PIXELS = 32.0;   ///< Cards with a smaller radius on screen fade to the nearest view

out vec4 FrameUV01;                ///< Position of this corner inside views 0 and 1 (0-1)
out vec2 FrameUV2;                 ///< ... and view 2
out vec3 CardPos;                  ///< World position on the card
flat out vec4 FrameCell01;         ///< Grid cells of views 0 and 1
flat out vec2 FrameCell2;
flat out vec3 FrameWeights;
flat out mat3 InstanceRotation;
flat out vec3 InstancePos;
flat out float InstanceScale;
flat out vec3 ToViewer;            ///< Unit direction from the tree toward the viewer
flat out float vFadeFactor;
flat out float vIsSelected;

/// Same rotation as the instanced mesh shaders (instanced_object.vert)
mat3 eulerToMat3(vec3 euler) {
    float cx = cos(radians(euler.x)), sx = sin(radians(euler.x));
    float cy = cos(radians(euler.y)), sy = sin(radians(euler.y));
    float cz = cos(radians(euler.z)), sz = sin(radians(euler.z));
    mat3 rx = mat3(1.0, 0.0, 0.0, 0.0, cx, -sx, 0.0, sx, cx);
    mat3 ry = mat3(cy, 0.0, sy, 0.0, 1.0, 0.0, -sy, 0.0, cy);
    mat3 rz = mat3(cz, -sz, 0.0, sz, cz, 0.0, 0.0, 0.0, 1.0);
    return rz * ry * rx;
}

/// Grid position in [-1, 1]^2 -> direction on the upper hemisphere (same as Impostor.cpp)
vec3 HemiOctDecode(vec2 e) {
    vec2 xz = vec2(e.x + e.y, e.x - e.y) * 0.5;
    return normalize(vec3(xz.x, 1.0 - abs(xz.x) - abs(xz.y), xz.y));
}

vec2 HemiOctEncode(vec3 d) {
    d /= abs(d.x) + abs(d.y) + abs(d.z);
    return vec2(d.x + d.z, d.x - d.z);
}

/// Axes of the view baked along d (same as Impostor.cpp)
void FrameBasis(vec3 d, out vec3 right, out vec3 up) {
    vec3 reference = abs(d.y) > 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(0.0, 1.0, 0.0);
    right = normalize(cross(reference, d));
    up = cross(d, right);
}

/// Where the ray through rayOrigin meets the plane of the view of a grid cell, in that view (0-1)
vec2 ProjectIntoFrame(vec2 cell, vec3 rayOrigin, vec3 rayDir) {
    vec3 d = HemiOctDecode(cell / float(impostorFrames - 1) * 2.0 - 1.0);
    vec3 right, up;
    FrameBasis(d, right, up);
    float denom = dot(rayDir, d);
    float t = abs(denom) > 1e-4 ? dot(impostorCenter - rayOrigin, d) / denom : 0.0;
    vec3 rel = rayOrigin + rayDir * t - impostorCenter;
    return vec2(dot(rel, right), dot(rel, up)) / (2.0 * impostorRadius) + 0.5;
}

void main()
{
    PackedInstance inst = instances[gl_InstanceID];
    float rawW = inst.rotAndFlags.w;
    vIsSelected = rawW > 5.0 ? 1.0 : 0.0;
    vFadeFactor = rawW > 5.0 ? rawW - 10.0 : rawW;

    vec3 position = inst.posAndScale.xyz;
    float scale = inst.posAndScale.w;
    mat3 R = eulerToMat3(inst.rotAndFlags.xyz);
    mat3 Rt = transpose(R);
    vec3 centerW = position + R * impostorCenter * scale;

    vec3 V = impostorOrthographic == 1 ? normalize(impostorViewDir) : normalize(impostorViewPos - centerW);

    // The three views around the view direction (seen from below: the horizon views)
    vec3 viewLocal = Rt * V;
    vec3 hemi = normalize(vec3(viewLocal.x, max(viewLocal.y, 0.0) + 1e-4, viewLocal.z));
    float lastCell = float(impostorFrames - 1);
    vec2 grid = (HemiOctEncode(hemi) * 0.5 + 0.5) * lastCell;
    vec2 cell = clamp(floor(grid), vec2(0.0), vec2(lastCell - 1.0));
    vec2 f = clamp(grid - cell, 0.0, 1.0);
    vec2 cell1 = cell + (f.x > f.y ? vec2(1.0, 0.0) : vec2(0.0, 1.0));
    vec2 cell2 = cell + vec2(1.0);
    vec3 weights = vec3(min(1.0 - f.x, 1.0 - f.y), abs(f.x - f.y), min(f.x, f.y));

    // Small on screen: the weaker views fade out (radius BLEND_PIXELS -> half of it), their texture
    // reads are skipped (impostor_common.glsl); a far tree reads only its nearest view
    if (impostorOrthographic == 0 && impostorPixelScale > 0.0) {
        float radiusPixels = impostorRadius * scale / max(distance(impostorViewPos, centerW), 1e-3) * impostorPixelScale;
        float t = clamp((BLEND_PIXELS - radiusPixels) / (BLEND_PIXELS * 0.5), 0.0, 1.0);
        // Subtracting t x the second strongest weight: at t = 1 only the strongest view is left
        float strongest = max(weights.x, max(weights.y, weights.z));
        float weakest = min(weights.x, min(weights.y, weights.z));
        float second = weights.x + weights.y + weights.z - strongest - weakest;
        vec3 sharp = max(weights - t * second, 0.0);
        float sum = sharp.x + sharp.y + sharp.z;
        weights = sum > 1e-5 ? sharp / sum
                : (weights.x >= strongest ? vec3(1.0, 0.0, 0.0) : (weights.y >= strongest ? vec3(0.0, 1.0, 0.0) : vec3(0.0, 0.0, 1.0)));
    }
    FrameWeights = weights;
    FrameCell01 = vec4(cell, cell1);
    FrameCell2 = cell2;

    // Upright card facing the viewer, covering the outline of the tree's box: each corner of the box
    // is carried along its view ray onto the card's plane (through the centre)
    vec3 reference = abs(V.y) > 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(0.0, 1.0, 0.0);
    vec3 right = normalize(cross(reference, V));
    vec3 up = cross(V, right);
    float planeDist = dot(centerW - impostorViewPos, -V);
    vec2 lo = vec2(1e9), hi = vec2(-1e9);
    for (int i = 0; i < 8; i++) {
        vec3 c = vec3((i & 1) != 0 ? impostorBoundsMax.x : impostorBoundsMin.x,
                      (i & 2) != 0 ? impostorBoundsMax.y : impostorBoundsMin.y,
                      (i & 4) != 0 ? impostorBoundsMax.z : impostorBoundsMin.z);
        vec3 w = centerW + R * (c - impostorCenter) * scale;
        vec3 onCard;
        if (impostorOrthographic == 1) {
            onCard = w - V * dot(w - centerW, V);
        } else {
            vec3 rel = w - impostorViewPos;
            onCard = impostorViewPos + rel * (planeDist / max(dot(rel, -V), 0.05 * planeDist));
        }
        vec2 q = vec2(dot(onCard - centerW, right), dot(onCard - centerW, up));
        lo = min(lo, q);
        hi = max(hi, q);
    }
    vec2 cardPos = mix(lo, hi, pos.xy * 0.5 + 0.5);
    vec3 world = centerW + right * cardPos.x + up * cardPos.y;

    // The view ray through this corner, in the tree's space
    vec3 cornerLocal = Rt * (world - position) / scale;
    vec3 rayDir = impostorOrthographic == 1 ? -viewLocal
                                            : normalize(cornerLocal - Rt * (impostorViewPos - position) / scale);
    FrameUV01 = vec4(ProjectIntoFrame(cell, cornerLocal, rayDir), ProjectIntoFrame(cell1, cornerLocal, rayDir));
    FrameUV2 = ProjectIntoFrame(cell2, cornerLocal, rayDir);

    CardPos = world;
    InstanceRotation = R;
    InstancePos = position;
    InstanceScale = scale;
    ToViewer = V;

    gl_ClipDistance[0] = dot(vec4(world, 1.0), clipPlane);
    gl_Position = impostorViewProj * vec4(world, 1.0);
}
