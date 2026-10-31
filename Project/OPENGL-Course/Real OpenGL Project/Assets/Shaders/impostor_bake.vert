#version 430 core

/**
 * @file impostor_bake.vert
 * @brief Renders the full LOD0 of a tree into one view of its impostor atlas (Impostor.cpp).
 *
 * Orthographic projection along the view direction frameDir (toward the viewer), framed on the
 * tree's bounding sphere: x/y = position on the view's plane, z = depth (nearest = smallest).
 */

layout (location = 0) in vec3 pos;
layout (location = 1) in vec2 tex;
layout (location = 2) in vec3 norm;

uniform vec3 frameRight;
uniform vec3 frameUp;
uniform vec3 frameDir;      ///< From the tree toward the viewer of this view
uniform vec3 frameCenter;   ///< Bounding sphere centre (mesh space)
uniform float frameRadius;  ///< Half-size of the view
uniform vec2 uvTiling;
uniform vec2 uvOffset;

out vec2 TexCoord;
out vec3 MeshNormal;
out vec3 MeshPos;

void main()
{
    vec3 p = pos - frameCenter;
    gl_Position = vec4(dot(p, frameRight), dot(p, frameUp), -dot(p, frameDir), frameRadius);
    TexCoord = tex * uvTiling + uvOffset;
    MeshNormal = norm;
    MeshPos = pos;
}
