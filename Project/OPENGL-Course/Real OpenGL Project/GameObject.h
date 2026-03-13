#pragma once

#include <string>
#include <GL/glew.h>
#include <glm/gtc/type_ptr.hpp>

#include "Transform.h"
#include "Model.h"
#include "Mesh.h"
#include "Texture.h"
#include "Material.h"

class GameObject
{
public:
	GameObject();
	GameObject(const std::string& name);
	~GameObject();

	// Getters
	std::string GetName() const { return name; }
	Transform& GetTransform() { return transform; }
	const Transform& GetTransform() const { return transform; }
	glm::mat4 GetWorldMatrix() const;

	// Setters for components
	void SetName(const std::string& newName) { name = newName; }
	void SetModel(Model* mdl) { model = mdl; }
	void SetMesh(Mesh* msh) { mesh = msh; }
	void SetTexture(Texture* tex) { texture = tex; }
	void SetNormalMap(Texture* normal) { normalMap = normal; }
	void SetMaterial(Material* mat) { material = mat; }

	// Getters for components
	Model* GetModel() const { return model; }
	Mesh* GetMesh() const { return mesh; }
	Texture* GetTexture() const { return texture; }
	Texture* GetNormalMap() const { return normalMap; }
	Material* GetMaterial() const { return material; }

	// Hierarchy
	void SetParent(GameObject* newParent);
	GameObject* GetParent() const { return parent; }
	const std::vector<GameObject*>& GetChildren() const { return children; }
	void AddChild(GameObject* child);
	void RemoveChild(GameObject* child);

	void SetInheritScale(bool inherit) { inheritScale = inherit; }
	bool GetInheritScale() const { return inheritScale; }

	// Render this object
	void Render(GLint uniformModel, GLint uniformSpecularIntensity, GLint uniformShininess, GLint uniformMaterialColor, GLint uniformUseNormalMap, GLint uniformUseDiffuseTexture, const glm::mat4& parentMatrix = glm::mat4(1.0f));

private:
	std::string name;
	Transform transform;

	GameObject* parent = nullptr;
	std::vector<GameObject*> children;
	bool inheritScale = true;

	Model* model;      // For loaded .obj models
	Mesh* mesh;        // For primitive meshes

	// Appearance
	Texture* texture;
	Texture* normalMap;
	Material* material;
};
