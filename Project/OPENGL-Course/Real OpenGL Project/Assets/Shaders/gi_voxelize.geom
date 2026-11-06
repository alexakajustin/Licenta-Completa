#version 430 core

/**
 * @file gi_voxelize.geom
 * @brief Projects each triangle onto the side of the voxel level's box it faces the most, so it
 * covers as many voxels as possible when rasterized at one pixel per voxel column (the fragment
 * shader finds the voxel from the interpolated world position).
 */

layout(triangles) in;
layout(triangle_strip, max_vertices = 3) out;

in VS_OUT {
    vec3 worldPos;
    vec3 normal;
    vec2 uv;
} gsIn[];

out GS_OUT {
    vec3 worldPos;
    vec3 normal;
    vec2 uv;
} gsOut;

uniform vec3 levelMin;   ///< World corner of the voxel level
uniform float levelSize; ///< Its edge length (m)

void main()
{
    vec3 faceNormal = abs(cross(gsIn[1].worldPos - gsIn[0].worldPos, gsIn[2].worldPos - gsIn[0].worldPos));
    int axis = (faceNormal.x >= faceNormal.y && faceNormal.x >= faceNormal.z) ? 0 : (faceNormal.y >= faceNormal.z ? 1 : 2);
    vec2 projected[3];
    for (int i = 0; i < 3; i++) {
        vec3 ndc = (gsIn[i].worldPos - levelMin) / levelSize * 2.0 - 1.0;
        projected[i] = axis == 0 ? ndc.yz : (axis == 1 ? ndc.xz : ndc.xy);
    }
    // Always counter-clockwise on screen: face culling (left on by some callers) keeps it
    vec2 e1 = projected[1] - projected[0], e2 = projected[2] - projected[0];
    bool flip = e1.x * e2.y - e1.y * e2.x < 0.0;
    for (int k = 0; k < 3; k++) {
        int i = flip ? (3 - k) % 3 : k; // 0, 2, 1 when flipped
        gl_Position = vec4(projected[i], 0.0, 1.0);
        gsOut.worldPos = gsIn[i].worldPos;
        gsOut.normal = gsIn[i].normal;
        gsOut.uv = gsIn[i].uv;
        EmitVertex();
    }
    EndPrimitive();
}
