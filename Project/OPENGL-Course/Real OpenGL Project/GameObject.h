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
struct GraphicsSettings;

class GameObject
{
public:
	GameObject();
	GameObject(const std::string& name);
	~GameObject();

	GameObject* Clone(const std::string& newName);

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
	
	// LOD Support
	void SetLODMesh(int level, Mesh* msh);
	Mesh* GetLODMesh(int level) const;
	int GetLODCount() const { return lodCount; }

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
		const glm::vec3& cameraPos,
		const GraphicsSettings* gs = nullptr,
		const glm::mat4& parentMatrix = glm::mat4(1.0f), const Frustum* frustum = nullptr);

	// Separate render for a single object (used by SceneManager batching/loop)
	void RenderSingle(GLint uniformModel, GLint uniformSpecularIntensity, GLint uniformShininess, GLint uniformMaterialColor,
		GLint uniformTiling, GLint uniformOffset,
		GLint uniformUseNormalMap, GLint uniformUseDiffuseTexture, GLint uniformDiffuseTexture, GLint uniformNormalMap,
		const glm::vec3& cameraPos,
		const GraphicsSettings* gs = nullptr,
		GLuint shaderID = 0);

	// Texture layers
	std::vector<TextureLayer>& GetTextureLayers() { return textureLayers; }
	const std::vector<TextureLayer>& GetTextureLayers() const { return textureLayers; }
	void AddTextureLayer(const TextureLayer& layer);
	void RemoveTextureLayer(int index);

	// GPU Tessellation
	void SetUseTessellation(bool val) { useTessellation = val; }
	bool GetUseTessellation() const { return useTessellation; }
	void SetTessLevel(float val) { tessLevel = val; }
	float GetTessLevel() const { return tessLevel; }
	void SetTessDistance(float val) { tessDistance = val; }
	float GetTessDistance() const { return tessDistance; }
	void SetTessDisplacementScale(float val) { tessDisplacementScale = val; }
	float GetTessDisplacementScale() const { return tessDisplacementScale; }
	void SetTessDisplacementBias(float val) { tessDisplacementBias = val; }
	float GetTessDisplacementBias() const { return tessDisplacementBias; }

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
	void GetWorldBoundingSphere(glm::vec3& center, float& radius);
	
	void SetDirty(); // Dirties this and all children recursively

private:
	std::string name;
	Transform transform;

	GameObject* parent = nullptr;
	std::vector<GameObject*> children;
	bool inheritScale = true;

	Model* model;      // For loaded .obj models
	Mesh* mesh;        // For primitive meshes (LOD 0)
	Mesh* lodMeshes[3] = { nullptr, nullptr, nullptr }; // Support for 3 LOD levels
	int lodCount = 1;

	// Appearance
	Texture* texture;
	Texture* normalMap;
	Material* material;
	std::vector<TextureLayer> textureLayers;

	// Persistent mesh data for procedural generation
	std::shared_ptr<MeshData> cpuMeshData;
	bool hasCustomMesh = false;
	// Cached bounds for custom CPU mesh data
	glm::vec3 customMeshMin = glm::vec3(0.0f);
	glm::vec3 customMeshMax = glm::vec3(0.0f);
	bool customBoundsDirty = true;

	// GPU Tessellation settings
	bool useTessellation = false;
	float tessLevel = 8.0f;              // Max tessellation subdivision level
	float tessDistance = 50.0f;          // Distance at which tessellation fades to minimum
	float tessDisplacementScale = 1.0f;  // World-space displacement height
	float tessDisplacementBias = -0.5f;  // Offset (centers displacement around surface)

	// Serialization: track creation source
	std::string primitiveType;    // "Plane", "Cube", "Sphere", "Empty", or ""
	std::string modelSourcePath;  // File path for model-based objects

	// CPU Caching for Performance
	glm::mat4 cachedWorldMatrix = glm::mat4(1.0f);
	bool worldDirty = true;

	glm::vec3 cachedWorldMin = glm::vec3(0.0f);
	glm::vec3 cachedWorldMax = glm::vec3(0.0f);
	glm::vec3 cachedSphereCenter = glm::vec3(0.0f);
	float cachedSphereRadius = 0.0f;
	bool boundsDirty = true;
};
