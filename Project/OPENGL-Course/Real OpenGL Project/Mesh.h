#pragma once

#include <GL\glew.h>

class Mesh
{
public:
	Mesh();

	void CreateMesh(GLfloat* vertices, unsigned int* indices, unsigned int numberOfVertices, unsigned int numberOfIndices);
	void RenderMesh();
	void ClearMesh();

	GLuint GetVAO() { return VAO; }
	GLuint GetIBO() { return IBO; }
	GLuint GetIndexCount() { return indexCount; }

	~Mesh();

private:
	GLuint VAO, VBO, IBO;
	GLsizei indexCount;
};