#pragma once

#include <GL\glew.h>

class Mesh
{
public:
	Mesh();

	void CreateMesh(GLfloat* vertices, unsigned int* indices, unsigned int numberOfVertices, unsigned int numberOfIndices);
	void RenderMesh();
	void ClearMesh();

	void AddRef() { refCount++; }
	void Release() { if (--refCount <= 0) delete this; }

	~Mesh();

private:
	GLuint VAO, VBO, IBO;
	GLsizei indexCount;
	int refCount;
};