#pragma once

#include <GL\glew.h>
#include <glm/glm.hpp>
#include <vector>

class Mesh
{
public:
	Mesh();

	void CreateMesh(GLfloat* vertices, unsigned int* indices, unsigned int numberOfVertices, unsigned int numberOfIndices);
	void CreateInstancedMesh(GLfloat* vertices, unsigned int* indices, unsigned int numberOfVertices, unsigned int numberOfIndices, unsigned int maxInstances);
	void RenderMesh();
	void RenderMeshTessellated(bool canTessellate = true);
	void RenderInstancedMesh(unsigned int instanceCount, const glm::mat4* instanceMatrices);
	void RenderIndirect(GLuint indirectBuffer, bool useTessellation = false); // GPU-driven indirect draw
	void ClearMesh();

	bool IsInstanced() const { return instanceVBO != 0; }

	void SetBounds(glm::vec3 min, glm::vec3 max) { minBound = min; maxBound = max; }
	void GetBounds(glm::vec3& min, glm::vec3& max) const { min = minBound; max = maxBound; }

	void AddRef() { refCount++; }
	void Release() { if (--refCount <= 0) delete this; }

	~Mesh();

	// Accessors for GPU-driven rendering (InstancedGroup needs these)
	GLuint GetVAO() const { return VAO; }
	GLuint GetIBO() const { return IBO; }
	GLsizei GetIndexCount() const { return indexCount; }

private:
	GLuint VAO, VBO, IBO, instanceVBO;
	GLsizei indexCount;
	int refCount;
	unsigned int maxInstanceCount = 0;

	glm::vec3 minBound = glm::vec3(-1.0f);
	glm::vec3 maxBound = glm::vec3(1.0f);
};