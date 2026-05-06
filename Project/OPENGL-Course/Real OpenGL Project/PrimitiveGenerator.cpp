#include "PrimitiveGenerator.h"
#include <cmath>
#include <vector>
#include <map>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

MeshData PrimitiveGenerator::GetPlaneData(int resolutionX, int resolutionZ)
{
    MeshData data;
    
    int totalVerts = (resolutionX + 1) * (resolutionZ + 1);
    int totalIndices = resolutionX * resolutionZ * 6;
    data.vertices.reserve(totalVerts * 14);
    data.indices.reserve(totalIndices);

    float stepX = 2.0f / (float)resolutionX;
    float stepZ = 2.0f / (float)resolutionZ;

    for (int z = 0; z <= resolutionZ; z++)
    {
        for (int x = 0; x <= resolutionX; x++)
        {
            float posX = -1.0f + (float)x * stepX;
            float posZ = -1.0f + (float)z * stepZ;
            float u = (float)x / (float)resolutionX;
            float v = (float)z / (float)resolutionZ;

            // Normal: (0, 1, 0), Tangent: (1, 0, 0), Bitangent: (0, 0, 1)
            data.AddVertex(posX, 0.0f, posZ, u, v, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f);
        }
    }

    for (int z = 0; z < resolutionZ; z++)
    {
        for (int x = 0; x < resolutionX; x++)
        {
            unsigned int topLeft = z * (resolutionX + 1) + x;
            unsigned int topRight = topLeft + 1;
            unsigned int bottomLeft = (z + 1) * (resolutionX + 1) + x;
            unsigned int bottomRight = bottomLeft + 1;

            data.AddTriangle(topLeft, bottomLeft, topRight);
            data.AddTriangle(topRight, bottomLeft, bottomRight);
        }
    }

    return data;
}

Mesh* PrimitiveGenerator::CreatePlane(int resolutionX, int resolutionZ)
{
    MeshData data = GetPlaneData(resolutionX, resolutionZ);
    return data.ToMesh();
}

MeshData PrimitiveGenerator::GetCubeData()
{
    MeshData data;
    // We'll manually add vertices and indices based on the original table to preserve normals/tangents
    // Front face
    data.AddVertex(-0.5f, -0.5f, 0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f);
    data.AddVertex(0.5f, -0.5f, 0.5f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f);
    data.AddVertex(0.5f, 0.5f, 0.5f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f);
    data.AddVertex(-0.5f, 0.5f, 0.5f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f);
    // Back face
    data.AddVertex(-0.5f, -0.5f, -0.5f, 1.0f, 0.0f, 0.0f, 0.0f, -1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f);
    data.AddVertex(-0.5f, 0.5f, -0.5f, 1.0f, 1.0f, 0.0f, 0.0f, -1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f);
    data.AddVertex(0.5f, 0.5f, -0.5f, 0.0f, 1.0f, 0.0f, 0.0f, -1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f);
    data.AddVertex(0.5f, -0.5f, -0.5f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f);
    // Top
    data.AddVertex(-0.5f, 0.5f, -0.5f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f);
    data.AddVertex(-0.5f, 0.5f, 0.5f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f);
    data.AddVertex(0.5f, 0.5f, 0.5f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f);
    data.AddVertex(0.5f, 0.5f, -0.5f, 1.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f);
    // Bottom
    data.AddVertex(-0.5f, -0.5f, -0.5f, 1.0f, 1.0f, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f);
    data.AddVertex(0.5f, -0.5f, -0.5f, 0.0f, 1.0f, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f);
    data.AddVertex(0.5f, -0.5f, 0.5f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f);
    data.AddVertex(-0.5f, -0.5f, 0.5f, 1.0f, 0.0f, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f);
    // Left
    data.AddVertex(-0.5f, -0.5f, -0.5f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f);
    data.AddVertex(-0.5f, -0.5f, 0.5f, 1.0f, 0.0f, -1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f);
    data.AddVertex(-0.5f, 0.5f, 0.5f, 1.0f, 1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f);
    data.AddVertex(-0.5f, 0.5f, -0.5f, 0.0f, 1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f);
    // Right
    data.AddVertex(0.5f, -0.5f, -0.5f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f);
    data.AddVertex(0.5f, 0.5f, -0.5f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f);
    data.AddVertex(0.5f, 0.5f, 0.5f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f);
    data.AddVertex(0.5f, -0.5f, 0.5f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f);

    unsigned int indices[] = {
        0, 1, 2, 2, 3, 0,
        4, 5, 6, 6, 7, 4,
        8, 9, 10, 10, 11, 8,
        12, 13, 14, 14, 15, 12,
        16, 17, 18, 18, 19, 16,
        20, 21, 22, 22, 23, 20
    };
    for (int i = 0; i < 36; i++) data.indices.push_back(indices[i]);

    return data;
}

Mesh* PrimitiveGenerator::CreateCube()
{
    MeshData data = GetCubeData();
    return data.ToMesh();
}

MeshData PrimitiveGenerator::GetSphereData(unsigned int rings, unsigned int sectors)
{
    MeshData data;
    
    int totalVerts = rings * sectors;
    int totalIndices = (rings - 1) * (sectors - 1) * 6;
    data.vertices.reserve(totalVerts * 14);
    data.indices.reserve(totalIndices);

    float const R = 1.0f / (float)(rings - 1);
    float const S = 1.0f / (float)(sectors - 1);

    for (unsigned int r = 0; r < rings; r++) {
        for (unsigned int s = 0; s < sectors; s++) {
            float y = sin(-3.14159265f / 2.0f + 3.14159265f * (float)r * R);
            float x = cos(2.0f * 3.14159265f * (float)s * S) * sin(3.14159265f * (float)r * R);
            float z = sin(2.0f * 3.14159265f * (float)s * S) * sin(3.14159265f * (float)r * R);

            float u = s * S;
            float v = r * R;
            float nx = x, ny = y, nz = z;

            float tx = -sin(2.0f * 3.14159265f * (float)s * S);
            float ty = 0.0f;
            float tz = cos(2.0f * 3.14159265f * (float)s * S);
            float tlen = sqrt(tx * tx + ty * ty + tz * tz);
            if (tlen > 0.0f) { tx /= tlen; ty /= tlen; tz /= tlen; }

            float bx = y * tz - z * ty;
            float by = z * tx - x * tz;
            float bz = x * ty - y * tx;
            float blen = sqrt(bx * bx + by * by + bz * bz);
            if (blen > 0.0f) { bx /= blen; by /= blen; bz /= blen; }

            data.AddVertex(x, y, z, u, v, nx, ny, nz, tx, ty, tz, bx, by, bz);
        }
    }

    for (unsigned int r = 0; r < rings - 1; r++) {
        for (unsigned int s = 0; s < sectors - 1; s++) {
            data.AddTriangle(r * sectors + s, (r + 1) * sectors + s, (r + 1) * sectors + (s + 1));
            data.AddTriangle(r * sectors + s, (r + 1) * sectors + (s + 1), r * sectors + (s + 1));
        }
    }
    return data;
}

Mesh* PrimitiveGenerator::CreateSphere(unsigned int rings, unsigned int sectors)
{
    MeshData data = GetSphereData(rings, sectors);
    return data.ToMesh();
}

MeshData PrimitiveGenerator::GetIcosphereData(int subdivisions)
{
    MeshData data;
    
    // Phi = (1 + sqrt(5)) / 2
    const float t = (1.0f + sqrt(5.0f)) / 2.0f;

    // Initial 12 vertices of an icosahedron
    std::vector<glm::vec3> verts = {
        glm::normalize(glm::vec3(-1,  t,  0)),
        glm::normalize(glm::vec3( 1,  t,  0)),
        glm::normalize(glm::vec3(-1, -t,  0)),
        glm::normalize(glm::vec3( 1, -t,  0)),

        glm::normalize(glm::vec3( 0, -1,  t)),
        glm::normalize(glm::vec3( 0,  1,  t)),
        glm::normalize(glm::vec3( 0, -1, -t)),
        glm::normalize(glm::vec3( 0,  1, -t)),

        glm::normalize(glm::vec3( t,  0, -1)),
        glm::normalize(glm::vec3( t,  0,  1)),
        glm::normalize(glm::vec3(-t,  0, -1)),
        glm::normalize(glm::vec3(-t,  0,  1))
    };

    struct Triangle { unsigned int v1, v2, v3; };
    std::vector<Triangle> faces = {
        {0, 11, 5}, {0, 5, 1}, {0, 1, 7}, {0, 7, 10}, {0, 10, 11},
        {1, 5, 9}, {5, 11, 4}, {11, 10, 2}, {10, 7, 6}, {7, 1, 8},
        {3, 9, 4}, {3, 4, 2}, {3, 2, 6}, {3, 6, 8}, {3, 8, 9},
        {4, 9, 5}, {2, 4, 11}, {6, 2, 10}, {8, 6, 7}, {9, 8, 1}
    };

    auto getMidpoint = [&](unsigned int v1, unsigned int v2, std::map<std::pair<unsigned int, unsigned int>, unsigned int>& cache) {
        if (v1 > v2) std::swap(v1, v2);
        auto key = std::make_pair(v1, v2);
        if (cache.count(key)) return cache[key];

        glm::vec3 mid = glm::normalize((verts[v1] + verts[v2]) * 0.5f);
        verts.push_back(mid);
        cache[key] = (unsigned int)verts.size() - 1;
        return (unsigned int)verts.size() - 1;
    };

    for (int i = 0; i < subdivisions; i++) {
        std::vector<Triangle> newFaces;
        std::map<std::pair<unsigned int, unsigned int>, unsigned int> midpointCache;
        for (auto& f : faces) {
            unsigned int a = getMidpoint(f.v1, f.v2, midpointCache);
            unsigned int b = getMidpoint(f.v2, f.v3, midpointCache);
            unsigned int c = getMidpoint(f.v3, f.v1, midpointCache);

            newFaces.push_back({f.v1, a, c});
            newFaces.push_back({f.v2, b, a});
            newFaces.push_back({f.v3, c, b});
            newFaces.push_back({a, b, c});
        }
        faces = std::move(newFaces);
    }

    // Convert to MeshData
    for (const auto& v : verts) {
        // Calculate UVs (spherical mapping)
        float u = 0.5f + (atan2(v.z, v.x) / (2.0f * (float)M_PI));
        float v_coord = 0.5f - (asin(v.y) / (float)M_PI);

        // Normals for a sphere are just the normalized position
        glm::vec3 n = v;

        // Tangents and bitangents
        glm::vec3 t_vec;
        if (abs(n.y) > 0.999f) t_vec = glm::vec3(1, 0, 0);
        else t_vec = glm::normalize(glm::cross(glm::vec3(0, 1, 0), n));
        glm::vec3 b_vec = glm::cross(n, t_vec);

        data.AddVertex(v.x, v.y, v.z, u, v_coord, n.x, n.y, n.z, t_vec.x, t_vec.y, t_vec.z, b_vec.x, b_vec.y, b_vec.z);
    }

    for (const auto& f : faces) {
        data.AddTriangle(f.v1, f.v2, f.v3);
    }

    return data;
}

Mesh* PrimitiveGenerator::CreateIcosphere(int subdivisions)
{
    MeshData data = GetIcosphereData(subdivisions);
    return data.ToMesh();
}
