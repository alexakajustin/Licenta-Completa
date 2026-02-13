#pragma once

#include "Mesh.h"
#include <vector>
#include <glm/glm.hpp>

class PrimitiveGenerator
{
public:
    static Mesh* CreatePlane();
    static Mesh* CreateCube();
    static Mesh* CreateSphere(unsigned int rings = 20, unsigned int sectors = 20);

private:
    static void CalcNormals(GLfloat* vertices, unsigned int verticeCount, unsigned int* indices, unsigned int indiceCount);
};
