#include "GameObject.h"

GameObject::GameObject()
	: name("Unnamed"), model(nullptr), mesh(nullptr), texture(nullptr), material(nullptr)
{
}

GameObject::GameObject(const std::string& name)
	: name(name), model(nullptr), mesh(nullptr), texture(nullptr), material(nullptr)
{
}

GameObject::~GameObject()
{
	// Note: We don't delete the pointers here as they may be shared
	// Resource management should be handled by a ResourceManager in the future
}

void GameObject::Render(GLuint uniformModel, GLuint uniformSpecularIntensity, GLuint uniformShininess)
{
	// Apply transform
	glm::mat4 modelMatrix = transform.GetModelMatrix();
	glUniformMatrix4fv(uniformModel, 1, GL_FALSE, glm::value_ptr(modelMatrix));

	// Apply texture if available
	if (texture)
	{
		texture->UseTexture();
	}

	// Apply material if available
	if (material)
	{
		material->UseMaterial(uniformSpecularIntensity, uniformShininess);
	}

	// Render the visual component
	if (model)
	{
		model->RenderModel();
	}
	else if (mesh)
	{
		mesh->RenderMesh();
	}
}
