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

void GameObject::Render(GLint uniformModel, GLint uniformSpecularIntensity, GLint uniformShininess, GLint uniformMaterialColor, GLint uniformUseNormalMap, GLint uniformUseDiffuseTexture)
{
	// Apply transform
	glm::mat4 modelMatrix = transform.GetModelMatrix();
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
}
