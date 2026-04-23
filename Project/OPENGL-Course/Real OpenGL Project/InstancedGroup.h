#pragma once

#include <vector>
#include <string>
#include "Frustum.h"
#include "TextureLayer.h"
#include <GL/glew.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

class Mesh;
class Material;
class Texture;
class Shader;
struct Frustum;

// =====================================================================
// InstancedGroup — GPU-Driven Instanced Rendering for Millions of Objects
//
// Architecture (v2 — 10M+ scale):
//   1. Stores all instance transforms in a GPU-side SSBO (32 bytes each)
//   2. Partitions instances into spatial chunks for CPU-side pre-culling
//   3. Runs a compute shader for frustum + distance + LOD culling on GPU
//   4. Issues multi-LOD glDrawElementsIndirect calls (zero CPU draw overhead)
//   5. Dedicated shadow pass with instanced shadow vertex shader
//
// Usage:
//   auto* group = new InstancedGroup("grass_field");
//   group->Setup(mesh, transforms, material, texture, normalMap);
//   // Each frame:
//   group->CullAndDraw(cullShader, renderShader, proj, view, camPos, ...);
//   group->CullAndDrawShadow(cullShader, shadowShader, lightVP, camPos, ...);
// =====================================================================

// Maximum LOD levels supported
static const int MAX_LOD_LEVELS = 3;

class InstancedGroup
{
public:
	// Packed instance data — 32 bytes per instance (vs 64 for mat4, vs ~700 for GameObject)
	struct PackedInstance {
		glm::vec4 positionAndScale;   // xyz = world position, w = uniform scale
		glm::vec4 rotationAndFlags;   // xyz = euler degrees, w = flags/padding
	};

	InstancedGroup(const std::string& name = "InstancedGroup");
	~InstancedGroup();

	// Setup: upload instance data to GPU. Call once (or when transforms change).
	void Setup(Mesh* sharedMesh,
		const std::vector<PackedInstance>& instances,
		Material* material = nullptr,
		Texture* texture = nullptr,
		Texture* normalMap = nullptr,
		const std::vector<TextureLayer>& textureLayers = {});

	// Per-frame: cull on GPU and draw visible instances (camera pass)
	void CullAndDraw(GLuint cullShaderID, Shader& renderShader,
		const glm::mat4& projection, const glm::mat4& view,
		const glm::vec3& cameraPos, float maxDrawDistance,
		bool isShadowPass = false);

	// Per-frame: cull against light frustum and draw into shadow map
	void CullAndDrawShadow(GLuint cullShaderID, Shader& shadowShader,
		const glm::mat4& lightViewProj, const glm::vec3& cameraPos,
		float shadowDrawDistance, float time);

	// LOD configuration
	void SetLODMesh(int level, Mesh* mesh, float maxDistance);
	int GetLODCount() const { return lodCount; }

	// Accessors
	const std::string& GetName() const { return name; }
	uint32_t GetTotalCount() const { return totalCount; }
	uint32_t GetVisibleCount() const { return lastVisibleCount; }
	float GetMeshBoundRadius() const { return meshBoundRadius; }
	void SetMaxDrawDistance(float dist) { defaultMaxDrawDistance = dist; }
	float GetMaxDrawDistance() const { return defaultMaxDrawDistance; }
	void SetShadowDistance(float dist) { shadowDrawDistance = dist; }
	float GetShadowDistance() const { return shadowDrawDistance; }

	// Material/texture access for rendering pipeline
	Material* GetMaterial() const { return material; }
	Texture* GetTexture() const { return texture; }
	Texture* GetNormalMap() const { return normalMap; }
	Mesh* GetMesh() const { return sharedMesh; }
	const std::vector<TextureLayer>& GetTextureLayers() const { return textureLayers; }

	// Serialization Helper
	std::string sourceObjectName;
	const std::string& GetSourceObjectName() const { return sourceObjectName; }
	void SetSourceObjectName(const std::string& name) { sourceObjectName = name; }

	// Smart Instance Extraction & Selection
	std::vector<PackedInstance> cpuInstances;
	std::vector<int> selectedInstanceIndices;
	bool Raycast(glm::vec3 rayOrigin, glm::vec3 rayDir, int& outIndex, float& outDist);

	// GPU Selection Pipeline
	void SelectInstances(const std::vector<int>& indices, bool additive);
	void ClearSelection();
	void DeleteSelectedInstances();
	void ExtractInstance(int index, class SceneManager* scene, bool skipReuploadAndSelect = false);
	void ExtractInstances(const std::vector<int>& indices, class SceneManager* scene, bool skipReuploadAndSelect = false);
	void ReuploadGPU(); // Re-upload after batch extraction

	// Cleanup
	void Release();

private:
	std::string name;

	// GPU Buffers (main instance data)
	GLuint instanceSSBO = 0;       // All instance transforms (input to compute shader)

	// Per-LOD rendering resources
	struct LODLevel {
		Mesh* mesh = nullptr;
		float maxDistance = 0.0f;// Max distance for this LOD (0 = use full draw distance)
		GLuint visibleSSBO = 0;    // Visible instance transforms for this LOD
		GLuint indirectBuffer = 0; // GL_DRAW_INDIRECT_BUFFER for this LOD
	};
	LODLevel lodLevels[MAX_LOD_LEVELS];
	int lodCount = 1;  // How many LOD levels are active

	// Shadow pass resources (separate from camera LOD)
	GLuint shadowVisibleSSBO = 0;
	GLuint shadowIndirectBuffer = 0;

	// Shared resources
	Mesh* sharedMesh = nullptr;
	Material* material = nullptr;
	Texture* texture = nullptr;
	Texture* normalMap = nullptr;
	std::vector<TextureLayer> textureLayers;

	uint32_t totalCount = 0;
	uint32_t lastVisibleCount = 0;
	float meshBoundRadius = 1.0f;
	glm::vec3 meshBoundsCenter = glm::vec3(0.0f); // Center of mesh AABB relative to origin
	float defaultMaxDrawDistance = 200.0f;
	float shadowDrawDistance = 30.0f;  // Only cast shadows within this range

	// Spatial chunking for 10M+ scale
	struct Chunk {
		GLuint ssbo = 0;           // SSBO containing this chunk's instances
		uint32_t instanceCount = 0;
		glm::vec3 boundsMin;       // AABB min
		glm::vec3 boundsMax;       // AABB max
	};
	std::vector<Chunk> chunks;
	float chunkSize = 50.0f;      // World-space chunk grid cell size
	bool useChunking = false;     // Only enabled for very large counts

	// Indirect draw command structure (matches OpenGL spec)
	struct DrawElementsIndirectCommand {
		GLuint count;          // Number of indices per instance
		GLuint instanceCount;  // Set by compute shader (visible count)
		GLuint firstIndex;     // Offset into index buffer
		GLuint baseVertex;     // Added to each index
		GLuint baseInstance;   // First instance ID
	};

	// Internal helpers
	void SetupChunking(const std::vector<PackedInstance>& instances);
	void SetupFlat(const std::vector<PackedInstance>& instances);
	void AllocateLODBuffers();
	void AllocateShadowBuffers();
	void ReleaseLODBuffers();
	void ReleaseShadowBuffers();
	void ReleaseChunks();

	// Helpers for chunked rendering
	void CullAndDrawChunked(GLuint cullShaderID, Shader& renderShader,
		const glm::mat4& projection, const glm::mat4& view,
		const glm::vec3& cameraPos, float maxDrawDistance,
		bool isShadowPass);
	void CullAndDrawFlat(GLuint cullShaderID, Shader& renderShader,
		const glm::mat4& projection, const glm::mat4& view,
		const glm::vec3& cameraPos, float maxDrawDistance,
		bool isShadowPass);

	// Dispatch compute cull for a given SSBO of instances
	void DispatchCull(GLuint cullShaderID, GLuint inputSSBO, uint32_t inputCount,
		const glm::mat4& viewProj, const glm::vec3& cameraPos,
		float maxDrawDistance);

	// Render all LOD levels after culling
	void RenderLODs(Shader& renderShader, const glm::mat4& projection,
		const glm::mat4& view, const glm::vec3& cameraPos,
		bool isShadowPass);
};
