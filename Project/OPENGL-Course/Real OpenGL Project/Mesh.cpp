#include "Mesh.h"
#include "DebugOverlay.h"

Mesh::Mesh()
{
	VAO = 0;
	VBO = 0;
	IBO = 0;
	instanceVBO = 0;
	indexCount = 0;
	refCount = 0;
}

void Mesh::CreateMesh(GLfloat* vertices, unsigned int* indices, unsigned int numberOfVertices, unsigned int numberOfIndices)
{
	ClearMesh();
	
	indexCount = numberOfIndices;

	// generate the vertex array object (LAYOUT/METADATA FOR THE VBO)
	glGenVertexArrays(1, &VAO);
	// any opengl functions that involve VAOS are now using that id
	glBindVertexArray(VAO); 

	// generate the index buffer object and bind it
	glGenBuffers(1, &IBO);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, IBO);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices[0]) * numberOfIndices, indices, GL_STATIC_DRAW);

	// generate vertex buffer object(the DATA itself)
	glGenBuffers(1, &VBO); 
	glBindBuffer(GL_ARRAY_BUFFER, VBO);
	// add the vertices that i have to the vbo
	// static = never changing vertices data
	glBufferData(GL_ARRAY_BUFFER, sizeof(vertices[0]) * numberOfVertices, vertices, GL_STATIC_DRAW); 

	// function that tells the GPU how to interpret vertex data stored in a vertex buffer object (VBO)
	// glVertexAttribPointer parameters:
	// index -> shader attribute location (layout(location = X)) by default 0
	// size -> number of components per vertex attribute (1-4, e.g., x,y,z = 3)
	// type -> data type of each component (e.g., GL_FLOAT)
	// normalized -> whether to normalize integer data (GL_FALSE for raw values)
	// stride -> byte offset between consecutive vertices (total size of one vertex in bytes)
	// pointer -> byte offset of the first component of this attribute within the vertex
	// Layout: pos(3) + uv(2) + normal(3) + tangent(3) + bitangent(3) = 14 floats per vertex
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(vertices[0]) * 14, 0);

	// ts just tells the gpu how you lay out data at location index 0
	glEnableVertexAttribArray(0); 

	// texture coordinates (uv coordinates)
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(vertices[0]) * 14, (void*)(sizeof(vertices[0]) * 3));
	glEnableVertexAttribArray(1);

	// normal coords
	glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(vertices[0]) * 14, (void*)(sizeof(vertices[0]) * 5));
	glEnableVertexAttribArray(2);

	// tangent
	glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(vertices[0]) * 14, (void*)(sizeof(vertices[0]) * 8));
	glEnableVertexAttribArray(3);

	// bitangent
	glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, sizeof(vertices[0]) * 14, (void*)(sizeof(vertices[0]) * 11));
	glEnableVertexAttribArray(4);

	// unbinds — ORDER MATTERS!
	// The GL_ELEMENT_ARRAY_BUFFER binding is part of VAO state.
	// We must unbind the VAO FIRST, then unbind the IBO *after*.
	// Unbinding IBO while VAO is still bound would detach it from the VAO on NVIDIA.
	glBindBuffer(GL_ARRAY_BUFFER, 0);         // unbind VBO (not part of VAO state, safe anytime)
	glBindVertexArray(0);                     // unbind VAO (this "locks in" the IBO association)
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0); // now safe to unbind IBO (no VAO is bound)
}

void Mesh::CreateInstancedMesh(GLfloat* vertices, unsigned int* indices, unsigned int numberOfVertices, unsigned int numberOfIndices, unsigned int maxInstances)
{
	CreateMesh(vertices, indices, numberOfVertices, numberOfIndices);
	maxInstanceCount = maxInstances;

	glBindVertexArray(VAO);

	glGenBuffers(1, &instanceVBO);
	glBindBuffer(GL_ARRAY_BUFFER, instanceVBO);
	glBufferData(GL_ARRAY_BUFFER, sizeof(glm::mat4) * maxInstanceCount, NULL, GL_DYNAMIC_DRAW);

	// A mat4 is 4 vec4s. Each vec4 takes one attribute slot.
	// Layout location 5, 6, 7, 8 for instance matrix
	for (int i = 0; i < 4; i++) {
		glEnableVertexAttribArray(5 + i);
		glVertexAttribPointer(5 + i, 4, GL_FLOAT, GL_FALSE, sizeof(glm::mat4), (void*)(sizeof(glm::vec4) * i));
		glVertexAttribDivisor(5 + i, 1); // Per-instance
	}

	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindVertexArray(0);
}

void Mesh::RenderMesh()
{
	// use this VAO
	glBindVertexArray(VAO);      
	// bind index buffer object
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, IBO); 
	// draw the object stored in the VAO
	glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, 0); 
	
	// Track stats
	if (DebugOverlay::GetInstance()) {
		DebugOverlay::GetInstance()->CountDrawCall();
		DebugOverlay::GetInstance()->CountTriangles(indexCount / 3);
	}

	// unbind the VAO
	glBindVertexArray(0);                    
	// unbind the IBO
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0); 
}

void Mesh::RenderMeshTessellated(bool canTessellate)
{
	// Set patch size to 3 vertices per patch (triangles)
	glPatchParameteri(GL_PATCH_VERTICES, 3);

	glBindVertexArray(VAO);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, IBO);

	if (canTessellate) {
		// Draw with GL_PATCHES — the tessellation hardware will subdivide each patch
		glDrawElements(GL_PATCHES, indexCount, GL_UNSIGNED_INT, 0);
	} else {
		// Fallback: Draw as regular triangles if the shader doesn't support tessellation
		// (e.g. during a standard shadow pass or fallback rendering)
		glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, 0);
	}

	// Track stats (tessellated triangle count is unknown at CPU side, count input patches)
	if (DebugOverlay::GetInstance()) {
		DebugOverlay::GetInstance()->CountDrawCall();
		DebugOverlay::GetInstance()->CountTriangles(indexCount / 3);
	}

	glBindVertexArray(0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}

void Mesh::RenderInstancedMesh(unsigned int instanceCount, const glm::mat4* instanceMatrices)
{
	if (instanceCount == 0) return;
	
	// Safety cap to avoid buffer overflow if we exceed initial allocation
	unsigned int count = (instanceCount > maxInstanceCount) ? maxInstanceCount : instanceCount;

	glBindBuffer(GL_ARRAY_BUFFER, instanceVBO);
	glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(glm::mat4) * count, instanceMatrices);

	glBindVertexArray(VAO);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, IBO);
	glDrawElementsInstanced(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, 0, count);

	// Track stats
	if (DebugOverlay::GetInstance()) {
		DebugOverlay::GetInstance()->CountDrawCall();
		DebugOverlay::GetInstance()->CountTriangles((indexCount / 3) * count);
	}

	glBindVertexArray(0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}

void Mesh::RenderIndirect(GLuint indirectBufferID)
{
	glBindVertexArray(VAO);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, IBO);
	glBindBuffer(GL_DRAW_INDIRECT_BUFFER, indirectBufferID);

	glDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT, nullptr);

	// Track stats
	if (DebugOverlay::GetInstance()) {
		DebugOverlay::GetInstance()->CountDrawCall();
	}

	glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
	glBindVertexArray(0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}

void Mesh::ClearMesh()
{
	if (IBO != 0)
	{
		glDeleteBuffers(1, &IBO);
		IBO = 0;
	}

	if (VBO != 0)
	{
		glDeleteBuffers(1, &VBO);
		VBO = 0;
	}

	if (VAO != 0)
	{
		glDeleteVertexArrays(1, &VAO);
		VAO = 0;
	}

	if (instanceVBO != 0)
	{
		glDeleteBuffers(1, &instanceVBO);
		instanceVBO = 0;
	}

	indexCount = 0;
}

Mesh::~Mesh()
{
	ClearMesh();
}
