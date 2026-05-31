#pragma once

#include <string>
#include <GL/glew.h>
#include <glm/gtc/type_ptr.hpp>

#include "Core/Transform.h"
#include "Rendering/Model.h"
#include "Rendering/Mesh.h"
#include "Rendering/Texture.h"
#include "Rendering/Material.h"
#include <memory>
#include <vector>
#include "Rendering/MeshData.h"
#include "Rendering/TextureLayer.h"
#include "Component.h"

struct Frustum;
struct GraphicsSettings;

/**
 * @class GameObject
 * @brief Represents a node in the scene tree containing transformation, meshes, materials, and hierarchy links.
 */
class GameObject
{
public:
	/**
	 * @brief Default constructor creating a game object named "Unnamed Object".
	 */
	GameObject();

	/**
	 * @brief Custom constructor specifying the object name.
	 * @param name Desired object name.
	 */
	GameObject(const std::string& name);

	/**
	 * @brief Virtual destructor releasing custom children and resources.
	 */
	virtual ~GameObject();

	/**
	 * @brief Performs deep clone replication of this GameObject.
	 * @param newName Name of the cloned instance.
	 * @return Pointer to cloned GameObject.
	 */
	GameObject* Clone(const std::string& newName);

	// Getters
	
	/**
	 * @brief Retrieves the object's string name.
	 * @return Object name.
	 */
	std::string GetName() const { return name; }

	/**
	 * @brief Retrieves reference to local transform matrix controls.
	 * Marks hierarchy dirtiness state upon retrieval.
	 * @return Reference to local Transform.
	 */
	Transform& GetTransform() { SetDirty(); return transform; }

	/**
	 * @brief Retrieves constant reference to local transform.
	 * @return Constant reference to Transform.
	 */
	const Transform& GetTransform() const { return transform; }

	/**
	 * @brief Computes combined world matrix including parent hierarchies.
	 * @return 4x4 Transformation matrix.
	 */
	glm::mat4 GetWorldMatrix();

	// Setters for components
	
	/**
	 * @brief Sets the object's name.
	 * @param newName New string name.
	 */
	void SetName(const std::string& newName) { name = newName; }

	/**
	 * @brief Links a loaded 3D Model resource component.
	 * @param mdl Pointer to Model.
	 */
	void SetModel(Model* mdl);

	/**
	 * @brief Links a single procedural Mesh component.
	 * @param msh Pointer to Mesh.
	 */
	void SetMesh(Mesh* msh);

	/**
	 * @brief Sets the diffuse texture.
	 * @param tex Pointer to Texture.
	 */
	void SetTexture(Texture* tex) { texture = tex; }

	/**
	 * @brief Sets the tangent-space normal map.
	 * @param normal Pointer to Normal map Texture.
	 */
	void SetNormalMap(Texture* normal) { normalMap = normal; }

	/**
	 * @brief Sets the custom rendering material parameters.
	 * @param mat Pointer to Material.
	 */
	void SetMaterial(Material* mat) { material = mat; }

	// Getters for components
	
	/**
	 * @brief Gets linked Model.
	 */
	Model* GetModel() const { return model; }

	/**
	 * @brief Gets linked procedural Mesh.
	 */
	Mesh* GetMesh() const { return mesh; }

	/**
	 * @brief Gets linked diffuse texture.
	 */
	Texture* GetTexture() const { return texture; }

	/**
	 * @brief Gets linked normal map.
	 */
	Texture* GetNormalMap() const { return normalMap; }

	/**
	 * @brief Gets linked rendering material.
	 */
	Material* GetMaterial() const { return material; }
	
	// LOD Support
	
	/**
	 * @brief Binds a specific mesh representation level for distance-based LOD culling.
	 * @param level Target level index (0 to 2).
	 * @param msh Pointer to level Mesh.
	 */
	void SetLODMesh(int level, Mesh* msh);

	/**
	 * @brief Gets a specific mesh LOD representation level.
	 * @param level Target level index (0 to 2).
	 * @return Pointer to target level Mesh.
	 */
	Mesh* GetLODMesh(int level) const;

	/**
	 * @brief Gets the total registered LOD levels.
	 */
	int GetLODCount() const { return lodCount; }

	// Hierarchy
	
	/**
	 * @brief Re-parents this node within the scene tree hierarchy.
	 * @param newParent Target parent node.
	 */
	void SetParent(GameObject* newParent);

	/**
	 * @brief Gets the current parent node.
	 * @return Parent pointer or nullptr.
	 */
	GameObject* GetParent() const { return parent; }

	/**
	 * @brief Gets list of active children nodes.
	 * @return Vector of GameObject pointers.
	 */
	const std::vector<GameObject*>& GetChildren() const { return children; }

	/**
	 * @brief Adds a child node to this node.
	 * @param child Pointer to child GameObject.
	 */
	void AddChild(GameObject* child);

	/**
	 * @brief Removes a child node from this node.
	 * @param child Pointer to child GameObject.
	 */
	void RemoveChild(GameObject* child);

	/**
	 * @brief Disconnects this node from its parent hierarchy without deleting it.
	 */
	void Orphan();

	/**
	 * @brief Configures whether parent scaling is inherited.
	 */
	void SetInheritScale(bool inherit) { inheritScale = inherit; }

	/**
	 * @brief Gets parent scale inheritance setting.
	 */
	bool GetInheritScale() const { return inheritScale; }

	/**
	 * @brief Sets rendering visibility flag.
	 */
	void SetVisible(bool visible) { isVisible = visible; }

	/**
	 * @brief Gets rendering visibility flag.
	 */
	bool GetVisible() const { return isVisible; }

	/**
	 * @brief Draws this object and all its children recursively.
	 */
	void Render(GLint uniformModel, GLint uniformSpecularIntensity, GLint uniformShininess, GLint uniformMaterialColor, 
		GLint uniformTiling, GLint uniformOffset,
		GLint uniformUseNormalMap, GLint uniformUseDiffuseTexture, GLint uniformDiffuseTexture, GLint uniformNormalMap, 
		const glm::vec3& cameraPos,
		const GraphicsSettings* gs = nullptr,
		const glm::mat4& parentMatrix = glm::mat4(1.0f), const Frustum* frustum = nullptr);

	/**
	 * @brief Draws this single object without traversing its children.
	 */
	void RenderSingle(GLint uniformModel, GLint uniformSpecularIntensity, GLint uniformShininess, GLint uniformMaterialColor,
		GLint uniformTiling, GLint uniformOffset,
		GLint uniformUseNormalMap, GLint uniformUseDiffuseTexture, GLint uniformDiffuseTexture, GLint uniformNormalMap,
		const glm::vec3& cameraPos,
		const GraphicsSettings* gs = nullptr,
		GLuint shaderID = 0,
		bool shaderSupportsTessellation = false);

	// Texture layers
	
	/**
	 * @brief Gets the list of texture layers.
	 */
	std::vector<TextureLayer>& GetTextureLayers() { return textureLayers; }
	const std::vector<TextureLayer>& GetTextureLayers() const { return textureLayers; }
	
	/**
	 * @brief Appends a new texture layer.
	 */
	void AddTextureLayer(const TextureLayer& layer);

	/**
	 * @brief Removes a texture layer by index.
	 */
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

	// --- Component System ---
	template<typename T, typename... Args>
	T* AddComponent(Args&&... args) {
		auto component = std::make_unique<T>(this, std::forward<Args>(args)...);
		T* ptr = component.get();
		components.push_back(std::move(component));
		ptr->Start();
		return ptr;
	}

	template<typename T>
	T* GetComponent() const {
		for (const auto& comp : components) {
			if (T* ptr = dynamic_cast<T*>(comp.get())) {
				return ptr;
			}
		}
		return nullptr;
	}

	template<typename T>
	void RemoveComponent() {
		for (auto it = components.begin(); it != components.end(); ++it) {
			if (dynamic_cast<T*>(it->get())) {
				components.erase(it);
				break;
			}
		}
	}

	void RemoveComponent(Component* comp) {
		for (auto it = components.begin(); it != components.end(); ++it) {
			if (it->get() == comp) {
				components.erase(it);
				break;
			}
		}
	}

	const std::vector<std::unique_ptr<Component>>& GetComponents() const {
		return components;
	}

	// Will call Update on all components
	void UpdateComponents(float deltaTime);
	// ------------------------


	// Mesh Persistence
	void SetCPUMeshData(const MeshData& data);
	void SetCPUMeshData(std::shared_ptr<MeshData> data);
	const MeshData& GetCPUMeshData() const;
	bool HasCustomMesh() const { return hasCustomMesh; }
	void ClearCustomMesh() { hasCustomMesh = false; cpuMeshData.reset(); }

	// Serialization helpers
	void SetPrimitiveType(const std::string& type) { primitiveType = type; }
	const std::string& GetPrimitiveType() const { return primitiveType; }
	void SetModelSourcePath(const std::string& path) { modelSourcePath = path; }
	const std::string& GetModelSourcePath() const { return modelSourcePath; }

	void SetSaveInScene(bool save) { saveInScene = save; }
	bool GetSaveInScene() const { return saveInScene; }

	/**
	 * @brief Calculates the world-space bounding box.
	 */
	void GetWorldBounds(glm::vec3& min, glm::vec3& max);

	/**
	 * @brief Calculates the world-space bounding sphere.
	 */
	void GetWorldBoundingSphere(glm::vec3& center, float& radius);
	
	/**
	 * @brief Dirties this and all children's cached transformations.
	 */
	void SetDirty();


private:
	std::string name;
	Transform transform;

	GameObject* parent = nullptr;
	std::vector<GameObject*> children;
	bool inheritScale = true;

	std::vector<std::unique_ptr<Component>> components;

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
	
	bool saveInScene = true;
	bool isVisible = true;

	// CPU Caching for Performance
	glm::mat4 cachedWorldMatrix = glm::mat4(1.0f);
	bool worldDirty = true;

	glm::vec3 cachedWorldMin = glm::vec3(0.0f);
	glm::vec3 cachedWorldMax = glm::vec3(0.0f);
	glm::vec3 cachedSphereCenter = glm::vec3(0.0f);
	float cachedSphereRadius = 0.0f;
	bool boundsDirty = true;
};
