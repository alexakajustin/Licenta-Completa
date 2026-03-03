#include "PrimitiveGenerator.h"
#include <cmath>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

MeshData PrimitiveGenerator::GetPlaneData(int resolutionX, int resolutionZ)
{
    MeshData data;
    
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
    float const R = 1.0f / (float)(rings - 1);
    float const S = 1.0f / (float)(sectors - 1);

    for (unsigned int r = 0; r < rings; r++) {
        for (unsigned int s = 0; s < sectors; s++) {
            float y = sin(-M_PI / 2 + M_PI * r * R);
            float x = cos(2 * M_PI * s * S) * sin(M_PI * r * R);
            float z = sin(2 * M_PI * s * S) * sin(M_PI * r * R);

            float u = s * S;
            float v = r * R;
            float nx = x, ny = y, nz = z;

            float tx = -sin(2 * M_PI * s * S);
            float ty = 0.0f;
            float tz = cos(2 * M_PI * s * S);
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
