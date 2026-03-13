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
	if (parent) {
		parent->RemoveChild(this);
	}
	// Note: We don't delete children here because SceneManager owns them in the 'objects' list.
	// We just need to make sure they are orphaned if the parent is deleted alone, 
	// OR better, SceneManager should delete them recursively.
	for (auto* child : children) {
		child->parent = nullptr;
	}
}

void GameObject::SetParent(GameObject* newParent)
{
	if (parent == newParent) return;

	// Calculate current world matrix to maintain world position
	glm::mat4 worldMat = GetWorldMatrix();

	// Remove from old parent
	if (parent) {
		parent->RemoveChild(this);
	}

	parent = newParent;

	// Add to new parent
	if (parent) {
		parent->AddChild(this);
	}

	// Update local transform to maintain world position, rotation, and scale
	if (parent) {
		glm::mat4 invParent = glm::inverse(parent->GetWorldMatrix());
		glm::mat4 localMat = invParent * worldMat;
		transform.SetFromMatrix(localMat);
	} else {
		transform.SetFromMatrix(worldMat);
	}
}

glm::mat4 GameObject::GetWorldMatrix() const
{
	if (parent) {
		return parent->GetWorldMatrix() * transform.GetModelMatrix();
	}
	return transform.GetModelMatrix();
}

void GameObject::AddChild(GameObject* child)
{
	if (!child) return;
	
	// Ensure not already a child
	for (auto* c : children) if (c == child) return;

	children.push_back(child);
	child->parent = this;
}

void GameObject::RemoveChild(GameObject* child)
{
	for (auto it = children.begin(); it != children.end(); ++it) {
		if (*it == child) {
			child->parent = nullptr;
			children.erase(it);
			return;
		}
	}
}

void GameObject::Render(GLint uniformModel, GLint uniformSpecularIntensity, GLint uniformShininess, GLint uniformMaterialColor, GLint uniformUseNormalMap, GLint uniformUseDiffuseTexture, const glm::mat4& parentMatrix)
{
	// Apply transform relative to parent
	glm::mat4 modelMatrix = parentMatrix * transform.GetModelMatrix();
	glUniformMatrix4fv(uniformModel, 1, GL_FALSE, glm::value_ptr(modelMatrix));

	// Apply material if available
	if (material)
	{
		material->UseMaterial(uniformSpecularIntensity, uniformShininess, uniformMaterialColor);
	}
	else
	{
		glUniform1f(uniformSpecularIntensity, 0.0f);
		glUniform1f(uniformShininess, 1.0f);
		glUniform3f(uniformMaterialColor, 1.0f, 1.0f, 1.0f);
	}

	// Render the visual component
	if (model)
	{
		bool hasOverrideTex = (texture != nullptr);
		bool hasOverrideNorm = (normalMap != nullptr);

		if (hasOverrideTex || hasOverrideNorm) {
			// Inspector overrides: bind our textures, skip model's own
			if (hasOverrideTex) {
				glUniform1i(uniformUseDiffuseTexture, 1);
				texture->UseTexture();
			} else {
				glUniform1i(uniformUseDiffuseTexture, 0);
			}

			if (hasOverrideNorm) {
				glUniform1i(uniformUseNormalMap, 1);
				normalMap->UseNormalMap();
			} else {
				glUniform1i(uniformUseNormalMap, 0);
			}
			model->RenderModelGeometryOnly();
		} else {
			// No overrides — model uses its own per-mesh textures
			// Note: model->RenderModel handles its own uniformUseDiffuseTexture/uniformUseNormalMap if needed,
			// but we should ensure it has access to the locations or set a default.
			// Actually, model->RenderModel takes uniformUseNormalMap as argument currently.
			// We should probably update Model::RenderModel too to be consistent.
			model->RenderModel(uniformUseNormalMap, uniformUseDiffuseTexture);
		}
	}
	else if (mesh)
	{
		if (texture) {
			glUniform1i(uniformUseDiffuseTexture, 1);
			texture->UseTexture();
		} else {
			glUniform1i(uniformUseDiffuseTexture, 0);
		}

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

	// Recursive render for children
	for (auto* child : children)
	{
		child->Render(uniformModel, uniformSpecularIntensity, uniformShininess, uniformMaterialColor, uniformUseNormalMap, uniformUseDiffuseTexture, modelMatrix);
	}
}
