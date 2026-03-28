#pragma once

#include "Mesh.h"
#include "MeshData.h"
#include <vector>
#include <glm/glm.hpp>

class PrimitiveGenerator
{
public:
    static Mesh* CreatePlane(int resolutionX = 250, int resolutionZ = 250);
    static Mesh* CreateCube();
    static Mesh* CreateSphere(unsigned int rings = 20, unsigned int sectors = 20);

    static MeshData GetPlaneData(int resolutionX = 250, int resolutionZ = 250);
    static MeshData GetCubeData();
    static MeshData GetSphereData(unsigned int rings = 20, unsigned int sectors = 20);

private:
    static void CalcNormals(GLfloat* vertices, unsigned int verticeCount, unsigned int* indices, unsigned int indiceCount);
};
