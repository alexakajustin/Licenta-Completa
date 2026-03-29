#pragma once

#include <string>
#include <GL/glew.h>
#include <glm/gtc/type_ptr.hpp>

#include "Transform.h"
#include "Model.h"
#include "Mesh.h"
#include "Texture.h"
#include "Material.h"
#include <memory>
#include "MeshData.h"
#include "TextureLayer.h"

struct Frustum;

class GameObject
{
public:
	GameObject();
	GameObject(const std::string& name);
	~GameObject();

	// Getters
	std::string GetName() const { return name; }
	Transform& GetTransform() { SetDirty(); return transform; }
	const Transform& GetTransform() const { return transform; }
	glm::mat4 GetWorldMatrix();

	// Setters for components
	void SetName(const std::string& newName) { name = newName; }
	void SetModel(Model* mdl) { model = mdl; }
	void SetMesh(Mesh* msh);
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
	void Render(GLint uniformModel, GLint uniformSpecularIntensity, GLint uniformShininess, GLint uniformMaterialColor, 
		GLint uniformTiling, GLint uniformOffset,
		GLint uniformUseNormalMap, GLint uniformUseDiffuseTexture, GLint uniformDiffuseTexture, GLint uniformNormalMap, 
		const glm::mat4& parentMatrix = glm::mat4(1.0f), const Frustum* frustum = nullptr);

	// Separate render for a single object (used by SceneManager batching/loop)
	void RenderSingle(GLint uniformModel, GLint uniformSpecularIntensity, GLint uniformShininess, GLint uniformMaterialColor,
		GLint uniformTiling, GLint uniformOffset,
		GLint uniformUseNormalMap, GLint uniformUseDiffuseTexture, GLint uniformDiffuseTexture, GLint uniformNormalMap,
		GLuint shaderID = 0);

	// Texture layers
	std::vector<TextureLayer>& GetTextureLayers() { return textureLayers; }
	const std::vector<TextureLayer>& GetTextureLayers() const { return textureLayers; }
	void AddTextureLayer(const TextureLayer& layer);
	void RemoveTextureLayer(int index);

	// Mesh Persistence
	void SetCPUMeshData(const MeshData& data);
	void SetCPUMeshData(std::shared_ptr<MeshData> data);
	const MeshData& GetCPUMeshData() const;
	bool HasCustomMesh() const { return hasCustomMesh; }
	void ClearCustomMesh() { hasCustomMesh = false; cpuMeshData.reset(); }

	// Serialization helpers (track how the object was created)
	void SetPrimitiveType(const std::string& type) { primitiveType = type; }
	const std::string& GetPrimitiveType() const { return primitiveType; }
	void SetModelSourcePath(const std::string& path) { modelSourcePath = path; }
	const std::string& GetModelSourcePath() const { return modelSourcePath; }

	void GetWorldBounds(glm::vec3& min, glm::vec3& max);
	
	void SetDirty(); // Dirties this and all children recursively

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
	std::vector<TextureLayer> textureLayers;

	// Persistent mesh data for procedural generation
	std::shared_ptr<MeshData> cpuMeshData;
	bool hasCustomMesh = false;

	// Serialization: track creation source
	std::string primitiveType;    // "Plane", "Cube", "Sphere", "Empty", or ""
	std::string modelSourcePath;  // File path for model-based objects

	// CPU Caching for Performance
	glm::mat4 cachedWorldMatrix = glm::mat4(1.0f);
	bool worldDirty = true;

	glm::vec3 cachedWorldMin = glm::vec3(0.0f);
	glm::vec3 cachedWorldMax = glm::vec3(0.0f);
	bool boundsDirty = true;
};
