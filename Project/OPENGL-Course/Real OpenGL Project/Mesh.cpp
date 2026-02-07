#include "Mesh.h"

Mesh::Mesh()
{
	VAO = 0;
	VBO = 0;
	IBO = 0;
	indexCount = 0;
}

void Mesh::CreateMesh(GLfloat* vertices, unsigned int* indices, unsigned int numberOfVertices, unsigned int numberOfIndices)
{
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
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(vertices[0]) * 8, 0);

	// ts just tells the gpu how you lay out data at location index 0
	glEnableVertexAttribArray(0); 

	// texture coordinates (uv coordinates)
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(vertices[0]) * 8, (void*)(sizeof(vertices[0]) * 3));
	glEnableVertexAttribArray(1);

	// normal coords
	glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(vertices[0]) * 8, (void*)(sizeof(vertices[0]) * 5));
	glEnableVertexAttribArray(2);

	// unbinds
	glBindBuffer(GL_ARRAY_BUFFER, 0); // unbind vbo 
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0); // unbind IBO
	glBindVertexArray(0); // unbind vao
}

void Mesh::RenderMesh()
{
	// use this VAO
	glBindVertexArray(VAO);      
	// bind index buffer object
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, IBO); 
	// draw the object stored in the VAO
	glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, 0); 
	// unbind the IBO
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0); 
	// unbind the VAO
	glBindVertexArray(0);                    

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

	indexCount = 0;
}

Mesh::~Mesh()
{
	ClearMesh();
}
