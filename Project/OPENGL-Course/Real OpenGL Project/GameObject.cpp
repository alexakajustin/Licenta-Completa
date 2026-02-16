#include "GameObject.h"

GameObject::GameObject()
	: name("GameObject"), model(nullptr), mesh(nullptr), texture(nullptr), normalMap(nullptr), material(nullptr)
{
}

GameObject::GameObject(const std::string& name)
	: name(name), model(nullptr), mesh(nullptr), texture(nullptr), normalMap(nullptr), material(nullptr)
{
}

GameObject::~GameObject()
{
	// Note: We don't delete the pointers here as they may be shared
	// Resource management should be handled by a ResourceManager in the future
}

void GameObject::Render(GLuint uniformModel, GLuint uniformSpecularIntensity, GLuint uniformShininess, GLuint uniformUseNormalMap)
{
	// Apply transform
	glm::mat4 modelMatrix = transform.GetModelMatrix();
	glUniformMatrix4fv(uniformModel, 1, GL_FALSE, glm::value_ptr(modelMatrix));

	// Apply texture if available
	if (texture)
	{
		texture->UseTexture();
	}

	// Render the visual component
	if (model)
	{
		// Model handles per-mesh normal map binding internally
		model->RenderModel(uniformUseNormalMap);
	}
	else if (mesh)
	{
		// Primitive meshes use the GameObject's normal map if available
		if (normalMap)
		{
			glUniform1i(uniformUseNormalMap, 1);
			normalMap->UseNormalMap();
		}
		else
		{
			glUniform1i(uniformUseNormalMap, 0);
		}
		
		mesh->RenderMesh();
	}
}
