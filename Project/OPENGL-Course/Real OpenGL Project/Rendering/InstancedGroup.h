#pragma once

#include <vector>
#include <string>
#include "Core/Frustum.h"
#include "Rendering/TextureLayer.h"
#include <GL/glew.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

class Mesh;
class Material;
class Texture;
class Shader;
struct Frustum;
struct GraphicsSettings;

// =====================================================================
// InstancedGroup — GPU-driven instanced rendering for millions of objects
//
//   1. All instance transforms live in an SSBO (32 bytes each); groups over 1M
//      instances are split into spatial chunks the CPU can skip whole.
//   2. Every pass (camera, reflection, shadow cascade, omni face, water foam)
//      describes itself with a CullPass and runs the same compute shader
//      (compute_cull.glsl): frustum planes, Hi-Z occlusion, screen-size LOD
//      (LODPolicy.h) with dithered cross-fades, detail-foliage thinning.
//   3. One glDrawElementsIndirect per LOD level, zero CPU work per instance.
//
// Usage:
//   group->Setup(mesh, transforms, material, texture, normalMap);
//   group->SetLODMesh(1, lod1); group->SetLODMesh(2, lod2);
//   // Each frame:
//   group->CullAndDraw(cullShader, renderShader, InstancedGroup::CameraPass(...), proj, view, gs);
//   group->CullAndDrawShadow(cullShader, shadowShader, cascadePass, lightViewProj, time);
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

	// One culling pass: the frustum it draws into and how much detail it may use.
	// LOD and draw distances are always measured from the camera, so every pass agrees on
	// what an instance looks like (a shadow comes from the LOD the camera sees, or a cheaper one).
	struct CullPass {
		glm::mat4 viewProj = glm::mat4(1.0f);   // Frustum of the pass (camera, reflection, cascade...)
		glm::vec3 cameraPos = glm::vec3(0.0f);
		float lodProjScale = 1.732f;            // projection[1][1] of the camera x LOD bias
		float maxDrawDistance = 2000.0f;        // Before DrawDistanceScale()
		bool sizeScaledDistance = true;         // Big objects are drawn further away (DrawDistanceScale)
		int lodMin = 0;                         // Most detailed LOD allowed (far shadow cascades: > 0)
		float crossfade = 0.0f;                 // Relative width of the dithered LOD transitions
		float minScreenSize = 0.0f;             // Instances smaller on screen are not drawn
		float minCasterSize = 0.0f;             // Shadow cascades: diameter of one shadow texel
		glm::vec2 detailRange = glm::vec2(45.0f, 150.0f); // Detail foliage: full density / a quarter
		float detailDensity = 1.0f;
		float detailMaxDistance = 1.0e9f;       // Detail foliage ends here (shadows, reflections)
		int phase = 2;                          // 0/1: two-phase occlusion culling, 2: single pass
		bool sphereCull = false;                // Omni shadows: distance to lightPos instead of the frustum
		glm::vec3 lightPos = glm::vec3(0.0f);
		GLuint hizTexture = 0;                  // Camera pass occlusion (0 = off)
		glm::vec2 screenSize = glm::vec2(0.0f);
		float nearPlane = 0.1f, farPlane = 20000.0f;
	};

	// Pass of a camera (main view, reflection) from the graphics settings. lodBias < 1 switches
	// to cheaper LODs sooner, detailScale shortens the detail-foliage distances.
	static CullPass CameraPass(const glm::mat4& projection, const glm::mat4& view, const glm::vec3& cameraPos,
		const GraphicsSettings* gs, float lodBias = 1.0f, float detailScale = 1.0f);

	InstancedGroup(const std::string& name = "InstancedGroup");
	~InstancedGroup();

	// Setup: upload instance data to GPU. Call once (or when transforms change).
	void Setup(Mesh* sharedMesh,
		const std::vector<PackedInstance>& instances,
		Material* material = nullptr,
		Texture* texture = nullptr,
		Texture* normalMap = nullptr,
		const std::vector<TextureLayer>& textureLayers = {});

	// Camera passes: cull on the GPU, then draw every LOD with the material's shader
	void CullAndDraw(GLuint cullShaderID, Shader& renderShader, const CullPass& pass,
		const glm::mat4& projection, const glm::mat4& view, const GraphicsSettings* gs);

	// Depth-only passes (directional shadow cascades, water foam obstacles): cull, then draw
	// every LOD with a shared shader that takes "directionalLightTransform"
	void CullAndDrawShadow(GLuint cullShaderID, Shader& shadowShader, const CullPass& pass,
		const glm::mat4& lightViewProj, float time);

	// Per-frame: cull against light sphere and draw into omni (point/spot) shadow map
	void CullAndDrawShadowOmni(GLuint cullShaderID, Shader& shadowShader,
		const glm::vec3& lightPos, float farPlane,
		const glm::vec3& cameraPos, const GraphicsSettings* gs, float time);

	// LOD configuration (level 1 or 2; LOD0 is the shared mesh)
	void SetLODMesh(int level, Mesh* mesh);
	int GetLODCount() const { return lodCount; }
	Mesh* GetLODMesh(int level) const { return (level >= 0 && level < lodCount && lodLevels[level].mesh) ? lodLevels[level].mesh : sharedMesh; }

	// Grass and other cheap, numerous things without LOD meshes thin out with distance instead
	bool IsDetailFoliage() const;

	// Instances drawn at a LOD by the last pass (GPU readback: stalls, debugging/statistics only)
	uint32_t ReadDrawnCount(int lod) const;

	// Accessors
	const std::string& GetName() const { return name; }
	uint32_t GetTotalCount() const { return totalCount; }
	uint32_t GetVisibleCount() const { return lastVisibleCount; }
	float GetMeshBoundRadius() const { return meshBoundRadius; }

	// Camera draw distance multiplier: bigger objects stay visible further away, like a
	// screen-size cull. Grass/ferns/rocks (radius <= 2 m) keep the global render distance,
	// a 20 m tree is drawn 5x further, capped at 10x.
	float DrawDistanceScale() const { return glm::clamp(meshBoundRadius / 2.0f, 1.0f, 10.0f); }

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
	void DeleteSelectedInstances(class SceneManager* scene = nullptr);
	void ExtractInstance(int index, class SceneManager* scene, bool skipReuploadAndSelect = false);
	void ExtractInstances(const std::vector<int>& indices, class SceneManager* scene, class GameObject* parentGroup = nullptr, bool skipReuploadAndSelect = false);
	void ReuploadGPU(); // Re-upload after batch extraction

	// Cleanup
	void Release();

private:
	std::string name;

	// Upload the wind uniforms of this group's material to an instanced shadow shader
	void ApplyShadowWind(GLuint shadowShaderID, float time) const;

	// GPU Buffers (main instance data)
	GLuint instanceSSBO = 0;       // All instance transforms (input to compute shader)

	// Per-LOD rendering resources, refilled by every pass (each pass draws before the next culls)
	struct LODLevel {
		Mesh* mesh = nullptr;
		GLuint visibleSSBO = 0;    // Visible instance transforms for this LOD
		GLuint indirectBuffer = 0; // GL_DRAW_INDIRECT_BUFFER for this LOD
	};
	LODLevel lodLevels[MAX_LOD_LEVELS];
	int lodCount = 1;  // LOD meshes in use (LOD0 = shared mesh)

	// Two-phase occlusion culling: per-instance visibility from the previous frame
	GLuint visibilitySSBO = 0;

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
	glm::vec3 groupBoundsMin = glm::vec3(0.0f);   // Overall group AABB min
	glm::vec3 groupBoundsMax = glm::vec3(0.0f);   // Overall group AABB max

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
	void ReleaseLODBuffers();
	void ReleaseChunks();

	// Runs the compute culler for one pass into the per-LOD lists. False when the whole
	// group (or every chunk) was rejected on the CPU and nothing needs drawing.
	bool Cull(GLuint cullShaderID, const CullPass& pass);

	// Draw every LOD list with the material (camera passes)
	void DrawLODs(Shader& renderShader, const glm::mat4& projection,
		const glm::mat4& view, const glm::vec3& cameraPos, const GraphicsSettings* gs);

	// Draw every LOD list with a depth-only shader whose uniforms are already set
	void DrawDepthLODs();
};
