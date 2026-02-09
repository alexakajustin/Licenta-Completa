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

	// Setters for components
	void SetName(const std::string& newName) { name = newName; }
	void SetModel(Model* mdl) { model = mdl; }
	void SetMesh(Mesh* msh) { mesh = msh; }
	void SetTexture(Texture* tex) { texture = tex; }
	void SetMaterial(Material* mat) { material = mat; }

	// Getters for components
	Model* GetModel() const { return model; }
	Mesh* GetMesh() const { return mesh; }
	Texture* GetTexture() const { return texture; }
	Material* GetMaterial() const { return material; }

	// Render this object
	void Render(GLuint uniformModel, GLuint uniformSpecularIntensity, GLuint uniformShininess);

private:
	std::string name;
	Transform transform;

	// Visual components (use one or the other)
	Model* model;      // For loaded .obj models
	Mesh* mesh;        // For primitive meshes

	// Appearance
	Texture* texture;
	Material* material;
};
