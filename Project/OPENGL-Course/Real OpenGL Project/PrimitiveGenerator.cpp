#include "PrimitiveGenerator.h"
#include <cmath>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

Mesh* PrimitiveGenerator::CreatePlane()
{
    // x, y, z, u, v, nx, ny, nz, tx, ty, tz, bx, by, bz
    GLfloat vertices[] = {
        -1.0f, 0.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f,  1.0f, 0.0f, 0.0f,  0.0f, 0.0f, 1.0f,
         1.0f, 0.0f, -1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f,  1.0f, 0.0f, 0.0f,  0.0f, 0.0f, 1.0f,
        -1.0f, 0.0f,  1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f,  1.0f, 0.0f, 0.0f,  0.0f, 0.0f, 1.0f,
         1.0f, 0.0f,  1.0f, 1.0f, 1.0f, 0.0f, 1.0f, 0.0f,  1.0f, 0.0f, 0.0f,  0.0f, 0.0f, 1.0f
    };

    unsigned int indices[] = {
        0, 2, 1,
        1, 2, 3
    };

    Mesh* mesh = new Mesh();
    mesh->CreateMesh(vertices, indices, 4 * 14, 6);
    return mesh;
}

Mesh* PrimitiveGenerator::CreateCube()
{
    GLfloat vertices[] = {
        // Front face: normal (0,0,1), tangent (1,0,0), bitangent (0,1,0)
        -0.5f, -0.5f,  0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f,  1.0f, 0.0f, 0.0f,  0.0f, 1.0f, 0.0f,
         0.5f, -0.5f,  0.5f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f,  1.0f, 0.0f, 0.0f,  0.0f, 1.0f, 0.0f,
         0.5f,  0.5f,  0.5f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f,  1.0f, 0.0f, 0.0f,  0.0f, 1.0f, 0.0f,
        -0.5f,  0.5f,  0.5f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f,  1.0f, 0.0f, 0.0f,  0.0f, 1.0f, 0.0f,
        // Back face: normal (0,0,-1), tangent (-1,0,0), bitangent (0,1,0)
        -0.5f, -0.5f, -0.5f, 1.0f, 0.0f, 0.0f, 0.0f, -1.0f, -1.0f, 0.0f, 0.0f,  0.0f, 1.0f, 0.0f,
        -0.5f,  0.5f, -0.5f, 1.0f, 1.0f, 0.0f, 0.0f, -1.0f, -1.0f, 0.0f, 0.0f,  0.0f, 1.0f, 0.0f,
         0.5f,  0.5f, -0.5f, 0.0f, 1.0f, 0.0f, 0.0f, -1.0f, -1.0f, 0.0f, 0.0f,  0.0f, 1.0f, 0.0f,
         0.5f, -0.5f, -0.5f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, -1.0f, 0.0f, 0.0f,  0.0f, 1.0f, 0.0f,
        // Top face: normal (0,1,0), tangent (1,0,0), bitangent (0,0,-1)
        -0.5f,  0.5f, -0.5f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f,  1.0f, 0.0f, 0.0f,  0.0f, 0.0f, -1.0f,
        -0.5f,  0.5f,  0.5f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f,  1.0f, 0.0f, 0.0f,  0.0f, 0.0f, -1.0f,
         0.5f,  0.5f,  0.5f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f,  1.0f, 0.0f, 0.0f,  0.0f, 0.0f, -1.0f,
         0.5f,  0.5f, -0.5f, 1.0f, 1.0f, 0.0f, 1.0f, 0.0f,  1.0f, 0.0f, 0.0f,  0.0f, 0.0f, -1.0f,
        // Bottom face: normal (0,-1,0), tangent (1,0,0), bitangent (0,0,1)
        -0.5f, -0.5f, -0.5f, 1.0f, 1.0f, 0.0f, -1.0f, 0.0f,  1.0f, 0.0f, 0.0f,  0.0f, 0.0f, 1.0f,
         0.5f, -0.5f, -0.5f, 0.0f, 1.0f, 0.0f, -1.0f, 0.0f,  1.0f, 0.0f, 0.0f,  0.0f, 0.0f, 1.0f,
         0.5f, -0.5f,  0.5f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f,  1.0f, 0.0f, 0.0f,  0.0f, 0.0f, 1.0f,
        -0.5f, -0.5f,  0.5f, 1.0f, 0.0f, 0.0f, -1.0f, 0.0f,  1.0f, 0.0f, 0.0f,  0.0f, 0.0f, 1.0f,
        // Left face: normal (-1,0,0), tangent (0,0,1), bitangent (0,1,0)
        -0.5f, -0.5f, -0.5f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f,  0.0f, 0.0f, 1.0f,  0.0f, 1.0f, 0.0f,
        -0.5f, -0.5f,  0.5f, 1.0f, 0.0f, -1.0f, 0.0f, 0.0f,  0.0f, 0.0f, 1.0f,  0.0f, 1.0f, 0.0f,
        -0.5f,  0.5f,  0.5f, 1.0f, 1.0f, -1.0f, 0.0f, 0.0f,  0.0f, 0.0f, 1.0f,  0.0f, 1.0f, 0.0f,
        -0.5f,  0.5f, -0.5f, 0.0f, 1.0f, -1.0f, 0.0f, 0.0f,  0.0f, 0.0f, 1.0f,  0.0f, 1.0f, 0.0f,
        // Right face: normal (1,0,0), tangent (0,0,-1), bitangent (0,1,0)
         0.5f, -0.5f, -0.5f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f,  0.0f, 0.0f, -1.0f,  0.0f, 1.0f, 0.0f,
         0.5f,  0.5f, -0.5f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f,  0.0f, 0.0f, -1.0f,  0.0f, 1.0f, 0.0f,
         0.5f,  0.5f,  0.5f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f,  0.0f, 0.0f, -1.0f,  0.0f, 1.0f, 0.0f,
         0.5f, -0.5f,  0.5f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,  0.0f, 0.0f, -1.0f,  0.0f, 1.0f, 0.0f
    };

    unsigned int indices[] = {
        0, 1, 2, 2, 3, 0,
        4, 5, 6, 6, 7, 4,
        8, 9, 10, 10, 11, 8,
        12, 13, 14, 14, 15, 12,
        16, 17, 18, 18, 19, 16,
        20, 21, 22, 22, 23, 20
    };

    Mesh* mesh = new Mesh();
    mesh->CreateMesh(vertices, indices, 24 * 14, 36);
    return mesh;
}

Mesh* PrimitiveGenerator::CreateSphere(unsigned int rings, unsigned int sectors)
{
    std::vector<GLfloat> vertices;
    std::vector<unsigned int> indices;

    float const R = 1.0f / (float)(rings - 1);
    float const S = 1.0f / (float)(sectors - 1);

    for (unsigned int r = 0; r < rings; r++) {
        for (unsigned int s = 0; s < sectors; s++) {
            float y = sin(-M_PI / 2 + M_PI * r * R);
            float x = cos(2 * M_PI * s * S) * sin(M_PI * r * R);
            float z = sin(2 * M_PI * s * S) * sin(M_PI * r * R);

            // Position
            vertices.push_back(x);
            vertices.push_back(y);
            vertices.push_back(z);
            // UV
            vertices.push_back(s * S);
            vertices.push_back(r * R);
            // Normal (same as position for unit sphere)
            vertices.push_back(x);
            vertices.push_back(y);
            vertices.push_back(z);

            // Tangent: derivative of position w.r.t. s (longitude)
            float tx = -sin(2 * M_PI * s * S);
            float ty = 0.0f;
            float tz = cos(2 * M_PI * s * S);
            float tlen = sqrt(tx * tx + ty * ty + tz * tz);
            if (tlen > 0.0f) { tx /= tlen; ty /= tlen; tz /= tlen; }
            vertices.push_back(tx);
            vertices.push_back(ty);
            vertices.push_back(tz);

            // Bitangent: cross(normal, tangent)
            float bx = y * tz - z * ty;
            float by = z * tx - x * tz;
            float bz = x * ty - y * tx;
            float blen = sqrt(bx * bx + by * by + bz * bz);
            if (blen > 0.0f) { bx /= blen; by /= blen; bz /= blen; }
            vertices.push_back(bx);
            vertices.push_back(by);
            vertices.push_back(bz);
        }
    }

    for (unsigned int r = 0; r < rings - 1; r++) {
        for (unsigned int s = 0; s < sectors - 1; s++) {
            indices.push_back(r * sectors + s);
            indices.push_back(r * sectors + (s + 1));
            indices.push_back((r + 1) * sectors + (s + 1));
            indices.push_back((r + 1) * sectors + (s + 1));
            indices.push_back((r + 1) * sectors + s);
            indices.push_back(r * sectors + s);
        }
    }

    Mesh* mesh = new Mesh();
    mesh->CreateMesh(vertices.data(), indices.data(), (unsigned int)vertices.size(), (unsigned int)indices.size());
    return mesh;
}
