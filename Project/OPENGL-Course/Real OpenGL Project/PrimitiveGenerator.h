#pragma once

#include "Mesh.h"
#include "MeshData.h"
#include <vector>
#include <glm/glm.hpp>

class PrimitiveGenerator
{
public:
    static Mesh* CreatePlane(int resolutionX = 200, int resolutionZ = 200);
    static Mesh* CreateCube();
    static Mesh* CreateSphere(unsigned int rings = 20, unsigned int sectors = 20);
    static Mesh* CreateIcosphere(int subdivisions = 2);

    static MeshData GetPlaneData(int resolutionX = 1, int resolutionZ = 1);
    static MeshData GetCubeData();
    static MeshData GetSphereData(unsigned int rings = 20, unsigned int sectors = 20);
    static MeshData GetIcosphereData(int subdivisions = 2);

private:
    static void CalcNormals(GLfloat* vertices, unsigned int verticeCount, unsigned int* indices, unsigned int indiceCount);
};
