#include "InstancedGroup.h"
#include "Mesh.h"
#include "Material.h"
#include "Texture.h"
#include "Shader.h"
#include "GraphicsSettings.h"
#include "Application.h"
#include "DebugOverlay.h"
#include "Frustum.h"
#include "GameObject.h"
#include "SceneManager.h"
#include "NodeGraph.h"
#include "ScatterNode.h"
#include <cstdio>
#include <cmath>
#include <map>
#include <algorithm>
#include <execution>
#include <numeric>

InstancedGroup::InstancedGroup(const std::string& name)
	: name(name)
{
}

InstancedGroup::~InstancedGroup()
{
	Release();
}

// =====================================================================
// Setup: Upload all instance data to GPU
// Automatically enables chunking for very large instance counts
// =====================================================================
void InstancedGroup::Setup(Mesh* mesh,
	const std::vector<PackedInstance>& instances,
	Material* mat, Texture* tex, Texture* norm,
	const std::vector<TextureLayer>& layers)
{
	Release(); // Clean up any previous buffers
	if (instances.empty()) return;

	sharedMesh = mesh;
	if (sharedMesh) sharedMesh->AddRef();

	material = mat;
	texture = tex;
	normalMap = norm;
	textureLayers = layers;
	cpuInstances = instances; // Store on CPU for Raycast/Extraction
	totalCount = (uint32_t)instances.size();

	if (totalCount == 0 || !sharedMesh) return;

	// Compute bounding radius and center from mesh bounds
	glm::vec3 minB, maxB;
	sharedMesh->GetBounds(minB, maxB);
	glm::vec3 extents = (maxB - minB) * 0.5f;
	meshBoundRadius = glm::length(extents);
	meshBoundsCenter = (minB + maxB) * 0.5f; // Center of AABB relative to mesh origin

	// Initialize all LOD levels — LOD1/LOD2 start as nullptr (fall back to sharedMesh)
	// MeshSimplifier will override them with simplified meshes if the mesh is complex enough.
	// For simple meshes (grass quads), density-based culling in the compute shader handles LOD.
	lodLevels[0].mesh = sharedMesh;
	lodCount = 3;

	// Decide whether to use chunking
	const uint32_t CHUNK_THRESHOLD = 1000000; // 1M instances → enable chunking
	if (totalCount >= CHUNK_THRESHOLD) {
		useChunking = true;
		SetupChunking(instances);
	} else {
		useChunking = false;
		SetupFlat(instances);
	}

	// Allocate LOD output buffers and shadow buffers
	AllocateLODBuffers();
	AllocateShadowBuffers();

	float totalVRAM_KB = (float)(sizeof(PackedInstance) * totalCount) / 1024.0f;
	printf("[InstancedGroup] '%s': Uploaded %u instances (%.1f KB VRAM, %s)\n",
		name.c_str(), totalCount, totalVRAM_KB,
		useChunking ? "CHUNKED" : "FLAT");
}

// =====================================================================
// Flat setup: Single SSBO for all instances (< 1M)
// =====================================================================
void InstancedGroup::SetupFlat(const std::vector<PackedInstance>& instances)
{
	glGenBuffers(1, &instanceSSBO);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, instanceSSBO);
	glBufferData(GL_SHADER_STORAGE_BUFFER,
		sizeof(PackedInstance) * totalCount,
		instances.data(),
		GL_STATIC_DRAW);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}

// =====================================================================
// Chunked setup: Partition instances into spatial grid cells
// =====================================================================
void InstancedGroup::SetupChunking(const std::vector<PackedInstance>& instances)
{
	// Determine world bounds
	glm::vec3 worldMin(FLT_MAX), worldMax(-FLT_MAX);
	for (const auto& inst : instances) {
		glm::vec3 pos(inst.positionAndScale.x, inst.positionAndScale.y, inst.positionAndScale.z);
		worldMin = glm::min(worldMin, pos);
		worldMax = glm::max(worldMax, pos);
	}

	// Calculate grid dimensions
	glm::vec3 worldSize = worldMax - worldMin;
	int gridX = std::max(1, (int)std::ceil(worldSize.x / chunkSize));
	int gridZ = std::max(1, (int)std::ceil(worldSize.z / chunkSize));

	// Bin instances into chunks
	std::map<int, std::vector<PackedInstance>> bins;

	for (const auto& inst : instances) {
		float px = inst.positionAndScale.x - worldMin.x;
		float pz = inst.positionAndScale.z - worldMin.z;
		int cx = std::min((int)(px / chunkSize), gridX - 1);
		int cz = std::min((int)(pz / chunkSize), gridZ - 1);
		int key = cz * gridX + cx;
		bins[key].push_back(inst);
	}

	// Create GPU buffers for each chunk
	for (auto& [key, bin] : bins) {
		if (bin.empty()) continue;

		Chunk chunk;
		chunk.instanceCount = (uint32_t)bin.size();

		// Compute chunk AABB
		chunk.boundsMin = glm::vec3(FLT_MAX);
		chunk.boundsMax = glm::vec3(-FLT_MAX);
		for (const auto& inst : bin) {
			glm::vec3 pos(inst.positionAndScale.x, inst.positionAndScale.y, inst.positionAndScale.z);
			float r = meshBoundRadius * inst.positionAndScale.w;
			chunk.boundsMin = glm::min(chunk.boundsMin, pos - glm::vec3(r));
			chunk.boundsMax = glm::max(chunk.boundsMax, pos + glm::vec3(r));
		}

		// Upload to GPU
		glGenBuffers(1, &chunk.ssbo);
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, chunk.ssbo);
		glBufferData(GL_SHADER_STORAGE_BUFFER,
			sizeof(PackedInstance) * chunk.instanceCount,
			bin.data(),
			GL_STATIC_DRAW);
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

		chunks.push_back(chunk);
	}

	// Also create a flat SSBO for the shadow pass (shadows only need nearby instances)
	// We'll reuse chunk-based culling for shadows too
	instanceSSBO = 0; // Not used in chunked mode

	printf("[InstancedGroup] Chunked into %d cells (%dx%d grid, %.0fm cell size)\n",
		(int)chunks.size(), gridX, gridZ, chunkSize);
}

// =====================================================================
// Allocate per-LOD output buffers
// =====================================================================
void InstancedGroup::AllocateLODBuffers()
{
	for (int lod = 0; lod < MAX_LOD_LEVELS; lod++) {
		// Visible SSBO — sized to totalCount (worst case all visible at this LOD)
		glGenBuffers(1, &lodLevels[lod].visibleSSBO);
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, lodLevels[lod].visibleSSBO);
		glBufferData(GL_SHADER_STORAGE_BUFFER,
			sizeof(PackedInstance) * totalCount,
			nullptr,
			GL_DYNAMIC_DRAW);
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

		// Indirect draw buffer
		DrawElementsIndirectCommand cmd = {};
		glGenBuffers(1, &lodLevels[lod].indirectBuffer);
		glBindBuffer(GL_DRAW_INDIRECT_BUFFER, lodLevels[lod].indirectBuffer);
		glBufferData(GL_DRAW_INDIRECT_BUFFER, sizeof(DrawElementsIndirectCommand),
			&cmd, GL_DYNAMIC_DRAW);
		glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
	}
}

// =====================================================================
// Allocate shadow pass buffers
// =====================================================================
void InstancedGroup::AllocateShadowBuffers()
{
	// Shadow visible SSBO
	glGenBuffers(1, &shadowVisibleSSBO);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, shadowVisibleSSBO);
	glBufferData(GL_SHADER_STORAGE_BUFFER,
		sizeof(PackedInstance) * totalCount,
		nullptr,
		GL_DYNAMIC_DRAW);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

	// Shadow indirect draw buffer
	DrawElementsIndirectCommand cmd = {};
	glGenBuffers(1, &shadowIndirectBuffer);
	glBindBuffer(GL_DRAW_INDIRECT_BUFFER, shadowIndirectBuffer);
	glBufferData(GL_DRAW_INDIRECT_BUFFER, sizeof(DrawElementsIndirectCommand),
		&cmd, GL_DYNAMIC_DRAW);
	glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
}

// =====================================================================
// LOD Configuration
// =====================================================================
void InstancedGroup::SetLODMesh(int level, Mesh* mesh, float maxDistance)
{
	if (level < 0 || level >= MAX_LOD_LEVELS) return;

	lodLevels[level].mesh = mesh;
	lodLevels[level].maxDistance = maxDistance;

	// Update lodCount to reflect highest configured level
	if (level >= lodCount) lodCount = level + 1;
}

// =====================================================================
// CullAndDraw — Camera Pass (main rendering)
// =====================================================================
void InstancedGroup::CullAndDraw(GLuint cullShaderID, Shader& renderShader,
	const glm::mat4& projection, const glm::mat4& view,
	const glm::vec3& cameraPos, const GraphicsSettings* gs,
	bool isShadowPass, GLuint hizTexture,
	int screenWidth, int screenHeight)
{
	if (totalCount == 0 || !sharedMesh) return;

	glm::mat4 viewProj = projection * view;

	// ================================================================
	// PHASE 1: GPU Compute Shader Culling
	// ================================================================
	glUseProgram(cullShaderID);

	// Reset ALL LOD indirect draw buffers
	for (int lod = 0; lod < lodCount; lod++) {
		Mesh* lodMesh = lodLevels[lod].mesh ? lodLevels[lod].mesh : sharedMesh;
		DrawElementsIndirectCommand resetCmd = {};
		resetCmd.count = lodMesh->GetIndexCount();
		resetCmd.instanceCount = 0;

		glBindBuffer(GL_DRAW_INDIRECT_BUFFER, lodLevels[lod].indirectBuffer);
		glBufferSubData(GL_DRAW_INDIRECT_BUFFER, 0, sizeof(DrawElementsIndirectCommand), &resetCmd);
	}
	glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);

	// Set uniforms
	glUniformMatrix4fv(glGetUniformLocation(cullShaderID, "viewProj"), 1, GL_FALSE, glm::value_ptr(viewProj));
	glUniform3fv(glGetUniformLocation(cullShaderID, "cameraPos"), 1, glm::value_ptr(cameraPos));
	glUniform1f(glGetUniformLocation(cullShaderID, "instanceBoundRadius"), meshBoundRadius);
	// LOD distance uniforms
	static GLuint lastCullShader = 0;
	static GLint uLodCount = -1, uMaxDist = -1, uMeshCenter = -1;
	static GLint uLodDistances[3] = { -1, -1, -1 };

	if (cullShaderID != lastCullShader) {
		lastCullShader = cullShaderID;
		uLodCount = glGetUniformLocation(cullShaderID, "lodCount");
		uMaxDist = glGetUniformLocation(cullShaderID, "maxDrawDistance");
		uMeshCenter = glGetUniformLocation(cullShaderID, "meshBoundsCenter");
		for (int i = 0; i < 3; i++) {
			char buf[64];
			snprintf(buf, sizeof(buf), "lodDistances[%d]", i);
			uLodDistances[i] = glGetUniformLocation(cullShaderID, buf);
		}
	}

	if (uLodCount != -1) glUniform1i(uLodCount, lodCount);
	glUniform3fv(uMeshCenter, 1, glm::value_ptr(meshBoundsCenter));

	// Use global graphics settings for distances
	float finalMaxDist = gs ? gs->renderDistance : 2000.0f;
	float finalLOD0 = gs ? gs->lod0Distance : 50.0f;
	float finalLOD1 = gs ? gs->lod1Distance : 150.0f;
	float finalLOD2 = gs ? gs->lod2Distance : 400.0f;

	if (uMaxDist != -1) glUniform1f(uMaxDist, finalMaxDist);

	float dvals[3] = { finalLOD0, finalLOD1, finalLOD2 };
	for (int i = 0; i < 3; i++) {
		if (uLodDistances[i] != -1) glUniform1f(uLodDistances[i], dvals[i]);
	}

	// Occlusion Culling (Hi-Z) Uniforms
	if (hizTexture > 0 && gs && gs->enableOcclusionCulling) {
		glUniform1i(glGetUniformLocation(cullShaderID, "useHiZ"), 1);
		glUniform2f(glGetUniformLocation(cullShaderID, "screenSize"), (float)screenWidth, (float)screenHeight);
		
		// Extract near and far planes from the projection matrix
		// For a standard perspective projection:
		//   P[2][2] = -(far+near)/(far-near)
		//   P[3][2] = -(2*far*near)/(far-near)
		float A = projection[2][2];
		float B = projection[3][2];
		float nearP = B / (A - 1.0f);
		float farP  = B / (A + 1.0f);
		glUniform1f(glGetUniformLocation(cullShaderID, "nearPlane"), nearP);
		glUniform1f(glGetUniformLocation(cullShaderID, "farPlane"), farP);
		
		// Bind the Hi-Z map to texture unit 15 (or any safe unit)
		glActiveTexture(GL_TEXTURE15);
		glBindTexture(GL_TEXTURE_2D, hizTexture);
		glUniform1i(glGetUniformLocation(cullShaderID, "hizMap"), 15);

		// In camera pass, hizViewProj matches viewProj
		glm::mat4 vp = projection * view;
		glUniformMatrix4fv(glGetUniformLocation(cullShaderID, "hizViewProj"), 1, GL_FALSE, glm::value_ptr(vp));
	} else {
		glUniform1i(glGetUniformLocation(cullShaderID, "useHiZ"), 0);
	}

	if (useChunking) {
		CullAndDrawChunked(cullShaderID, renderShader, projection, view, cameraPos, finalMaxDist, gs, isShadowPass);
	} else {
		CullAndDrawFlat(cullShaderID, renderShader, projection, view, cameraPos, finalMaxDist, gs, isShadowPass);
	}
}

// =====================================================================
// Flat cull+draw (single SSBO, < 1M instances)
// =====================================================================
void InstancedGroup::CullAndDrawFlat(GLuint cullShaderID, Shader& renderShader,
	const glm::mat4& projection, const glm::mat4& view,
	const glm::vec3& cameraPos, float maxDrawDistance,
	const GraphicsSettings* gs, bool isShadowPass)
{
	// Bind input + output SSBOs
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, instanceSSBO);

	// Bind LOD visible SSBOs (binding points 1, 3, 4)
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, lodLevels[0].visibleSSBO);
	if (lodCount > 1) glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, lodLevels[1].visibleSSBO);
	if (lodCount > 2) glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, lodLevels[2].visibleSSBO);

	// Bind LOD indirect buffers (binding points 2, 5, 6)
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, lodLevels[0].indirectBuffer);
	if (lodCount > 1) glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, lodLevels[1].indirectBuffer);
	if (lodCount > 2) glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 6, lodLevels[2].indirectBuffer);

	GLint prog;
	glGetIntegerv(GL_CURRENT_PROGRAM, &prog);
	glUniform1ui(glGetUniformLocation(prog, "totalInstances"), totalCount);

	// Dispatch compute
	GLuint numGroups = (totalCount + 255) / 256;
	glDispatchCompute(numGroups, 1, 1);

	glMemoryBarrier(GL_COMMAND_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT);

	// ================================================================
	// PHASE 2: Render each LOD level
	// ================================================================
	RenderLODs(renderShader, projection, view, cameraPos, gs, isShadowPass);
}

// =====================================================================
// Chunked cull+draw (spatial grid, 1M+ instances)
// =====================================================================
void InstancedGroup::CullAndDrawChunked(GLuint cullShaderID, Shader& renderShader,
	const glm::mat4& projection, const glm::mat4& view,
	const glm::vec3& cameraPos, float maxDrawDistance,
	const GraphicsSettings* gs, bool isShadowPass)
{
	// Build camera frustum for chunk-level CPU pre-cull
	glm::mat4 viewProj = projection * view;
	Frustum frustum = Frustum::CreateFrustumFromMatrix(viewProj);

	// Reset LOD counters (already done in CullAndDraw)

	int chunksProcessed = 0;

	for (auto& chunk : chunks) {
		// CPU-side AABB vs frustum test (6 dot products — trivial)
		if (!frustum.IsBoxVisible(chunk.boundsMin, chunk.boundsMax)) continue;

		// CPU-side distance test: skip chunks entirely beyond draw distance
		glm::vec3 chunkCenter = (chunk.boundsMin + chunk.boundsMax) * 0.5f;
		float chunkRadius = glm::length(chunk.boundsMax - chunk.boundsMin) * 0.5f;
		float distToChunk = glm::length(chunkCenter - cameraPos) - chunkRadius;
		if (distToChunk > maxDrawDistance) continue;

		// This chunk is potentially visible: dispatch compute cull
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, chunk.ssbo);

		// Bind LOD visible SSBOs
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, lodLevels[0].visibleSSBO);
		if (lodCount > 1) glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, lodLevels[1].visibleSSBO);
		if (lodCount > 2) glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, lodLevels[2].visibleSSBO);

		// Bind LOD indirect buffers
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, lodLevels[0].indirectBuffer);
		if (lodCount > 1) glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, lodLevels[1].indirectBuffer);
		if (lodCount > 2) glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 6, lodLevels[2].indirectBuffer);

		GLint prog;
		glGetIntegerv(GL_CURRENT_PROGRAM, &prog);
		glUniform1ui(glGetUniformLocation(prog, "totalInstances"), chunk.instanceCount);

		GLuint numGroups = (chunk.instanceCount + 255) / 256;
		glDispatchCompute(numGroups, 1, 1);

		chunksProcessed++;
	}

	if (chunksProcessed > 0) {
		glMemoryBarrier(GL_COMMAND_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT);
	}

	// Render all LOD levels
	RenderLODs(renderShader, projection, view, cameraPos, gs, isShadowPass);
}

// =====================================================================
// RenderLODs — Issue indirect draw calls for each LOD level
// =====================================================================
void InstancedGroup::RenderLODs(Shader& renderShader, const glm::mat4& projection,
	const glm::mat4& view, const glm::vec3& cameraPos,
	const GraphicsSettings* gs, bool isShadowPass)
{
	renderShader.UseShader();
	GLuint shaderID = renderShader.GetShaderID();

	// Set standard uniforms
	glUniformMatrix4fv(renderShader.GetProjectionLocation(), 1, GL_FALSE, glm::value_ptr(projection));
	glUniformMatrix4fv(renderShader.GetViewLocation(), 1, GL_FALSE, glm::value_ptr(view));
	glUniform3fv(renderShader.GetEyePositionLocation(), 1, glm::value_ptr(cameraPos));

	// Bind all material properties (Standard + Custom Uniforms)
	if (!isShadowPass && material) {
		material->UseMaterial(
			renderShader.GetSpecularIntensityLocation(),
			renderShader.GetShininessLocation(),
			glGetUniformLocation(shaderID, "material.baseColor"),
			renderShader.GetTilingLocation(),
			renderShader.GetOffsetLocation()
		);
		material->Bind(renderShader.GetShaderID());
	}
	else if (!isShadowPass) {
		// DEFAULT MATERIAL: Bind white color and identity tiling so objects are visible & not weirdly textured
		glUniform1f(renderShader.GetSpecularIntensityLocation(), 0.1f);
		glUniform1f(renderShader.GetShininessLocation(), 32.0f);
		glUniform4f(glGetUniformLocation(shaderID, "material.baseColor"), 1.0f, 1.0f, 1.0f, 1.0f);
		glUniform2f(renderShader.GetTilingLocation(), 1.0f, 1.0f);
		glUniform2f(renderShader.GetOffsetLocation(), 0.0f, 0.0f);
	}

	// Bind textures
	GLint useDiffuseLoc = glGetUniformLocation(shaderID, "useDiffuseTexture");
	GLint useNormalLoc = glGetUniformLocation(shaderID, "useNormalMap");

	if (!isShadowPass && texture) {
		if (useDiffuseLoc != -1) glUniform1i(useDiffuseLoc, 1);
		glUniform1i(glGetUniformLocation(shaderID, "theTexture"), 0);
		texture->UseTexture();
		
		// Sync LOD distances for debug coloring in fragment shader
		float finalLOD0 = gs ? gs->lod0Distance : 50.0f;
		float finalLOD1 = gs ? gs->lod1Distance : 150.0f;
		float finalLOD2 = gs ? gs->lod2Distance : 400.0f;

		glUniform1f(glGetUniformLocation(shaderID, "lodDistances[0]"), finalLOD0);
		glUniform1f(glGetUniformLocation(shaderID, "lodDistances[1]"), finalLOD1);
		glUniform1f(glGetUniformLocation(shaderID, "lodDistances[2]"), finalLOD2);
	}
	else {
		if (useDiffuseLoc != -1) glUniform1i(useDiffuseLoc, 0);
	}

	if (!isShadowPass && normalMap) {
		if (useNormalLoc != -1) glUniform1i(useNormalLoc, 1);
		glUniform1i(glGetUniformLocation(shaderID, "normalMap"), 1);
		normalMap->UseNormalMap();
	}
	else {
		if (useNormalLoc != -1) glUniform1i(useNormalLoc, 0);
	}

	// Texture layers (inherit parent's appearance)
	int layerCount = (int)textureLayers.size();
	GLint layerCountLoc = glGetUniformLocation(shaderID, "textureLayerCount");
	if (layerCountLoc != -1) glUniform1i(layerCountLoc, layerCount);

	if (!isShadowPass && layerCount > 0)
	{
		for (int i = 0; i < layerCount && i < 4; i++)
		{
			// Units 10+ for layers to avoid conflicts
			int diffUnit = 10 + i;
			int normUnit = 14 + i;
			int dispUnit = 18 + i;

			// Bind textures
			char buf[64];
			sprintf_s(buf, "textureLayers[%d]", i);
			glUniform1i(glGetUniformLocation(shaderID, buf), diffUnit);
			if (textureLayers[i].texture) textureLayers[i].texture->UseTextureOnUnit(GL_TEXTURE0 + diffUnit);

			sprintf_s(buf, "layerNormalMaps[%d]", i);
			glUniform1i(glGetUniformLocation(shaderID, buf), normUnit);
			if (textureLayers[i].normalMap) textureLayers[i].normalMap->UseTextureOnUnit(GL_TEXTURE0 + normUnit);

			sprintf_s(buf, "layerDisplacementMaps[%d]", i);
			glUniform1i(glGetUniformLocation(shaderID, buf), dispUnit);
			if (textureLayers[i].displacementMap) textureLayers[i].displacementMap->UseTextureOnUnit(GL_TEXTURE0 + dispUnit);

			// Bind layer data
			sprintf_s(buf, "layerData[%d].blendMode", i);
			glUniform1i(glGetUniformLocation(shaderID, buf), (int)textureLayers[i].blendMode);
			sprintf_s(buf, "layerData[%d].opacity", i);
			glUniform1f(glGetUniformLocation(shaderID, buf), textureLayers[i].opacity);
			sprintf_s(buf, "layerData[%d].tiling", i);
			glUniform1f(glGetUniformLocation(shaderID, buf), textureLayers[i].tiling);
			sprintf_s(buf, "layerData[%d].heightMin", i);
			glUniform1f(glGetUniformLocation(shaderID, buf), textureLayers[i].heightMin);
			sprintf_s(buf, "layerData[%d].heightMax", i);
			glUniform1f(glGetUniformLocation(shaderID, buf), textureLayers[i].heightMax);
			sprintf_s(buf, "layerData[%d].slopeMin", i);
			glUniform1f(glGetUniformLocation(shaderID, buf), textureLayers[i].slopeMin);
			sprintf_s(buf, "layerData[%d].slopeMax", i);
			glUniform1f(glGetUniformLocation(shaderID, buf), textureLayers[i].slopeMax);
			sprintf_s(buf, "layerData[%d].invert", i);
			glUniform1i(glGetUniformLocation(shaderID, buf), textureLayers[i].invert ? 1 : 0);
			sprintf_s(buf, "layerData[%d].hasNormalMap", i);
			glUniform1i(glGetUniformLocation(shaderID, buf), textureLayers[i].normalMap ? 1 : 0);
			sprintf_s(buf, "layerData[%d].hasDisplacementMap", i);
			glUniform1i(glGetUniformLocation(shaderID, buf), textureLayers[i].displacementMap ? 1 : 0);
			sprintf_s(buf, "layerData[%d].displacementScale", i);
			glUniform1f(glGetUniformLocation(shaderID, buf), textureLayers[i].displacementScale);
		}
	}

	// Draw each LOD level
	for (int lod = 0; lod < lodCount; lod++) {
		Mesh* lodMesh = lodLevels[lod].mesh ? lodLevels[lod].mesh : sharedMesh;

		if (gs && gs->debugLODColoring) {
			GLint debugLoc = glGetUniformLocation(shaderID, "debugLODColoring");
			if (debugLoc != -1) glUniform1i(debugLoc, 1);
			
			GLint lodColorLoc = glGetUniformLocation(shaderID, "lodDebugColor");
			if (lodColorLoc != -1) {
				if (lod == 0) glUniform3f(lodColorLoc, 1.0f, 0.2f, 0.2f); // RED = LOD0
				else if (lod == 1) glUniform3f(lodColorLoc, 0.2f, 1.0f, 0.2f); // GREEN = LOD1
				else if (lod == 2) glUniform3f(lodColorLoc, 0.2f, 0.2f, 1.0f); // BLUE = LOD2
				else glUniform3f(lodColorLoc, 1.0f, 1.0f, 0.0f);
			}
		} else {
			GLint debugLoc = glGetUniformLocation(shaderID, "debugLODColoring");
			if (debugLoc != -1) glUniform1i(debugLoc, 0);
		}

		// Bind this LOD's visible SSBO for vertex shader to read
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, lodLevels[lod].visibleSSBO);

		// Issue indirect draw
		lodMesh->RenderIndirect(lodLevels[lod].indirectBuffer, renderShader.HasTessellation());
	}
}

// =====================================================================
// CullAndDrawShadow — Dedicated shadow map pass
// Uses separate cull with light's frustum and limited draw distance
// =====================================================================
void InstancedGroup::CullAndDrawShadow(GLuint cullShaderID, Shader& shadowShader,
	const glm::mat4& lightViewProj, const glm::vec3& cameraPos,
	const GraphicsSettings* gs, float time,
	GLuint hizTexture, int screenWidth, int screenHeight,
	const glm::mat4& cameraViewProj)
{
	if (totalCount == 0 || !sharedMesh) return;

	float finalShadowDist = gs ? gs->shadowDistance : 100.0f;

	// ================================================================
	// PHASE 1: Cull against light frustum with tight distance limit
	// ================================================================
	glUseProgram(cullShaderID);
	glUniform1f(glGetUniformLocation(cullShaderID, "maxDrawDistance"), finalShadowDist);

	// Reset shadow indirect buffer
	DrawElementsIndirectCommand resetCmd = {};
	resetCmd.count = sharedMesh->GetIndexCount();
	resetCmd.instanceCount = 0;

	glBindBuffer(GL_DRAW_INDIRECT_BUFFER, shadowIndirectBuffer);
	glBufferSubData(GL_DRAW_INDIRECT_BUFFER, 0, sizeof(DrawElementsIndirectCommand), &resetCmd);
	glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);

	// Set cull uniforms — use LIGHT's VP for frustum culling
	glUniformMatrix4fv(glGetUniformLocation(cullShaderID, "viewProj"), 1, GL_FALSE, glm::value_ptr(lightViewProj));
	glUniform3fv(glGetUniformLocation(cullShaderID, "cameraPos"), 1, glm::value_ptr(cameraPos));
	glUniform1f(glGetUniformLocation(cullShaderID, "maxDrawDistance"), finalShadowDist);
	glUniform1f(glGetUniformLocation(cullShaderID, "instanceBoundRadius"), meshBoundRadius);
	glUniform3fv(glGetUniformLocation(cullShaderID, "meshBoundsCenter"), 1, glm::value_ptr(meshBoundsCenter));

	// Force LOD count to 1 for shadow pass (no LOD in shadows)
	glUniform1i(glGetUniformLocation(cullShaderID, "lodCount"), 1);
	glUniform1f(glGetUniformLocation(cullShaderID, "lodDistances[0]"), finalShadowDist);

	// Hi-Z occlusion culling for shadow pass MUST BE DISABLED for directional lights!
	// "If the camera can't see an object, its shadow is also invisible" is FALSE. 
	// Objects behind the camera or off-screen can cast shadows into the view.
	glUniform1i(glGetUniformLocation(cullShaderID, "useHiZ"), 0);

	// Disable sphere culling (used only by omni shadow pass)
	glUniform1i(glGetUniformLocation(cullShaderID, "useSphereCull"), 0);

	// Zero out screenSize for sub-pixel culling only if Hi-Z is not active
	// (Hi-Z already set screenSize above when enabled)
	if (!(hizTexture > 0 && gs && gs->enableOcclusionCulling && screenWidth > 0 && screenHeight > 0)) {
		glUniform2f(glGetUniformLocation(cullShaderID, "screenSize"), 0.0f, 0.0f);
	}

	// Bind shadow output buffers
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, shadowVisibleSSBO);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, shadowIndirectBuffer);

	// Dispatch for each chunk (or flat)
	if (useChunking) {
		Frustum lightFrustum = Frustum::CreateFrustumFromMatrix(lightViewProj);

		for (auto& chunk : chunks) {
			// CPU pre-cull: chunk AABB vs light frustum
			if (!lightFrustum.IsBoxVisible(chunk.boundsMin, chunk.boundsMax)) continue;

			// Distance pre-cull: skip chunks far from camera (shadows only matter near player)
			glm::vec3 chunkCenter = (chunk.boundsMin + chunk.boundsMax) * 0.5f;
			float distToChunk = glm::length(chunkCenter - cameraPos);
			if (distToChunk > finalShadowDist + chunkSize) continue;

			glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, chunk.ssbo);
			glUniform1ui(glGetUniformLocation(cullShaderID, "totalInstances"), chunk.instanceCount);

			GLuint numGroups = (chunk.instanceCount + 255) / 256;
			glDispatchCompute(numGroups, 1, 1);
		}
	} else {
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, instanceSSBO);
		GLint prog;
		glGetIntegerv(GL_CURRENT_PROGRAM, &prog);
		glUniform1ui(glGetUniformLocation(prog, "totalInstances"), totalCount);

		GLuint numGroups = (totalCount + 255) / 256;
		glDispatchCompute(numGroups, 1, 1);
	}

	glMemoryBarrier(GL_COMMAND_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT);

	// ================================================================
	// PHASE 2: Render into shadow map using instanced shadow shader
	// ================================================================
	shadowShader.UseShader();
	GLuint sid = shadowShader.GetShaderID();

	// Set light transform
	shadowShader.SetDirectionalLightTransform(lightViewProj);

	// Set time and wind uniforms
	GLint timeLoc = glGetUniformLocation(sid, "time");
	if (timeLoc != -1) glUniform1f(timeLoc, time);
	glUniform1i(glGetUniformLocation(sid, "windEnabled"), 0);

	// Material tiling/offset (default identity so TexCoord isn't zeroed out)
	glUniform2f(glGetUniformLocation(sid, "material.tiling"), 1.0f, 1.0f);
	glUniform2f(glGetUniformLocation(sid, "material.offset"), 0.0f, 0.0f);

	// Set material alpha (for shadow color map)
	GLint alphaLoc = glGetUniformLocation(sid, "materialAlpha");
	if (alphaLoc != -1) {
		float alpha = material ? material->GetAlpha() : 1.0f;
		glUniform1f(alphaLoc, alpha);
	}

	// Alpha testing uniforms (for foliage with transparent textures)
	GLint useDiffuseLoc = glGetUniformLocation(sid, "useDiffuseTexture");
	if (useDiffuseLoc != -1) {
		int useTex = texture ? 1 : 0;
		glUniform1i(useDiffuseLoc, useTex);
	}
	if (texture) {
		glUniform1i(glGetUniformLocation(sid, "theTexture"), 0);
		texture->UseTexture();
	}

	// Disable face culling for thin double-sided foliage (grass blades, leaves)
	glDisable(GL_CULL_FACE);

	// Bind shadow visible SSBO for vertex shader
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, shadowVisibleSSBO);

	// Draw using LOD 0 mesh only (full detail for shadows)
	sharedMesh->RenderIndirect(shadowIndirectBuffer);

	// Restore face culling
	glEnable(GL_CULL_FACE);
}

// =====================================================================
// CullAndDrawShadowOmni — Omni (point/spot) light shadow map pass
// Uses sphere-based culling against light position and limited draw distance
// =====================================================================
void InstancedGroup::CullAndDrawShadowOmni(GLuint cullShaderID, Shader& shadowShader,
	const glm::vec3& lightPos, float farPlane,
	const glm::vec3& cameraPos, const GraphicsSettings* gs, float time)
{
	if (totalCount == 0 || !sharedMesh) return;

	float finalShadowDist = gs ? gs->shadowDistance : 100.0f;
	float cullDistance = std::min(farPlane, finalShadowDist);

	// ================================================================
	// PHASE 1: Cull against light sphere (distance-based)
	// ================================================================
	glUseProgram(cullShaderID);

	// Reset shadow indirect buffer
	DrawElementsIndirectCommand resetCmd = {};
	resetCmd.count = sharedMesh->GetIndexCount();
	resetCmd.instanceCount = 0;

	glBindBuffer(GL_DRAW_INDIRECT_BUFFER, shadowIndirectBuffer);
	glBufferSubData(GL_DRAW_INDIRECT_BUFFER, 0, sizeof(DrawElementsIndirectCommand), &resetCmd);
	glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);

	// Set cull uniforms for sphere-based culling
	glUniformMatrix4fv(glGetUniformLocation(cullShaderID, "viewProj"), 1, GL_FALSE, glm::value_ptr(glm::mat4(1.0f)));
	glUniform3fv(glGetUniformLocation(cullShaderID, "cameraPos"), 1, glm::value_ptr(cameraPos));
	glUniform1f(glGetUniformLocation(cullShaderID, "maxDrawDistance"), cullDistance);
	glUniform1f(glGetUniformLocation(cullShaderID, "instanceBoundRadius"), meshBoundRadius);
	glUniform3fv(glGetUniformLocation(cullShaderID, "meshBoundsCenter"), 1, glm::value_ptr(meshBoundsCenter));

	// Enable sphere culling mode (uses lightPos instead of frustum)
	glUniform3fv(glGetUniformLocation(cullShaderID, "lightPos"), 1, glm::value_ptr(lightPos));
	glUniform1i(glGetUniformLocation(cullShaderID, "useSphereCull"), 1);

	// Force LOD count to 1 for shadow pass (no LOD in shadows)
	glUniform1i(glGetUniformLocation(cullShaderID, "lodCount"), 1);
	glUniform1f(glGetUniformLocation(cullShaderID, "lodDistances[0]"), cullDistance);

	// Disable Hi-Z for shadow pass
	glUniform1i(glGetUniformLocation(cullShaderID, "useHiZ"), 0);

	// Bind shadow output buffers
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, shadowVisibleSSBO);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, shadowIndirectBuffer);

	// Dispatch for each chunk (or flat)
	if (useChunking) {
		// Distance-based chunk pre-cull (light position)
		for (auto& chunk : chunks) {
			glm::vec3 chunkCenter = (chunk.boundsMin + chunk.boundsMax) * 0.5f;
			float distToLight = glm::length(chunkCenter - lightPos) - glm::length(chunk.boundsMax - chunk.boundsMin) * 0.5f;
			if (distToLight > cullDistance) continue;

			glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, chunk.ssbo);
			glUniform1ui(glGetUniformLocation(cullShaderID, "totalInstances"), chunk.instanceCount);

			GLuint numGroups = (chunk.instanceCount + 255) / 256;
			glDispatchCompute(numGroups, 1, 1);
		}
	} else {
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, instanceSSBO);
		GLint prog;
		glGetIntegerv(GL_CURRENT_PROGRAM, &prog);
		glUniform1ui(glGetUniformLocation(prog, "totalInstances"), totalCount);

		GLuint numGroups = (totalCount + 255) / 256;
		glDispatchCompute(numGroups, 1, 1);
	}

	glMemoryBarrier(GL_COMMAND_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT);

	// ================================================================
	// PHASE 2: Render into omni shadow map using instanced omni shadow shader
	// ================================================================
	shadowShader.UseShader();
	GLuint sid = shadowShader.GetShaderID();

	// Set omni light uniforms
	glUniform3fv(glGetUniformLocation(sid, "omniLightPos"), 1, glm::value_ptr(lightPos));
	glUniform1f(glGetUniformLocation(sid, "farPlane"), farPlane);
	glUniform1f(glGetUniformLocation(sid, "time"), time);
	glUniform1i(glGetUniformLocation(sid, "windEnabled"), 0);

	// Material tiling/offset (default to identity for shadows)
	glUniform2f(glGetUniformLocation(sid, "material.tiling"), 1.0f, 1.0f);
	glUniform2f(glGetUniformLocation(sid, "material.offset"), 0.0f, 0.0f);

	// Alpha testing uniforms
	GLint useDiffuseLoc = glGetUniformLocation(sid, "useDiffuseTexture");
	if (useDiffuseLoc != -1) {
		int useTex = texture ? 1 : 0;
		glUniform1i(useDiffuseLoc, useTex);
	}
	if (texture) {
		glUniform1i(glGetUniformLocation(sid, "theTexture"), 0);
		texture->UseTexture();
	}

	// Bind shadow visible SSBO for vertex shader
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, shadowVisibleSSBO);

	// Draw using LOD 0 mesh only (full detail for shadows)
	sharedMesh->RenderIndirect(shadowIndirectBuffer);
}

// =====================================================================
// Cleanup
// =====================================================================
void InstancedGroup::Release()
{
	if (instanceSSBO) { glDeleteBuffers(1, &instanceSSBO); instanceSSBO = 0; }
	ReleaseLODBuffers();
	ReleaseShadowBuffers();
	ReleaseChunks();

	if (sharedMesh) {
		sharedMesh->Release();
		sharedMesh = nullptr;
	}

	totalCount = 0;
	lastVisibleCount = 0;
	lodCount = 1;
	useChunking = false;
}

void InstancedGroup::ReleaseLODBuffers()
{
	for (int lod = 0; lod < MAX_LOD_LEVELS; lod++) {
		if (lodLevels[lod].visibleSSBO) { glDeleteBuffers(1, &lodLevels[lod].visibleSSBO); lodLevels[lod].visibleSSBO = 0; }
		if (lodLevels[lod].indirectBuffer) { glDeleteBuffers(1, &lodLevels[lod].indirectBuffer); lodLevels[lod].indirectBuffer = 0; }
	}
}

void InstancedGroup::ReleaseShadowBuffers()
{
	if (shadowVisibleSSBO) { glDeleteBuffers(1, &shadowVisibleSSBO); shadowVisibleSSBO = 0; }
	if (shadowIndirectBuffer) { glDeleteBuffers(1, &shadowIndirectBuffer); shadowIndirectBuffer = 0; }
}

void InstancedGroup::ReleaseChunks()
{
	for (auto& chunk : chunks) {
		if (chunk.ssbo) { glDeleteBuffers(1, &chunk.ssbo); chunk.ssbo = 0; }
	}
	chunks.clear();
}

// =====================================================================
// CPU Raycast: Finds the closest instance hit within this group
// =====================================================================
bool InstancedGroup::Raycast(glm::vec3 rayOrigin, glm::vec3 rayDir, int& outIndex, float& outDist)
{
	if (cpuInstances.empty()) return false;

	int bestIndex = -1;
	float bestDist = FLT_MAX;

	// Simple CPU intersection over instances 
	// For 1M instances this takes ~1ms, completely fine for mouse clicks
	for (size_t i = 0; i < cpuInstances.size(); i++) {
		const auto& inst = cpuInstances[i];
		glm::vec3 center(inst.positionAndScale.x, inst.positionAndScale.y, inst.positionAndScale.z);
		
		// Tighten the radius to 35% because grass meshes are thin/tall.
		// A full sphere would capture clicks meant for the terrain far off to the side.
		float radius = meshBoundRadius * inst.positionAndScale.w * 0.35f;

		// Ray-sphere intersection
		glm::vec3 m = rayOrigin - center;
		float b = glm::dot(m, rayDir);
		float c = glm::dot(m, m) - radius * radius;

		// Stop if origin is outside sphere and ray points away
		if (c > 0.0f && b > 0.0f) continue;
		float discr = b * b - c;
		
		if (discr >= 0.0f) {
			float dist = -b - sqrt(discr);
			if (dist < 0.0f) dist = 0.0f; // ray originated inside sphere
			
			if (dist < bestDist) {
				bestDist = dist;
				bestIndex = (int)i;
			}
		}
	}

	if (bestIndex != -1) {
		outIndex = bestIndex;
		outDist = bestDist;
		return true;
	}
	return false;
}

// =====================================================================
// Instantiates a specific piece of scattered grass into a real 
// editable GameObject, and removes it from the GPU group.
// =====================================================================
void InstancedGroup::ExtractInstance(int index, SceneManager* scene, bool skipReuploadAndSelect)
{
	if (index < 0 || index >= cpuInstances.size() || !scene) return;

	// 1. Get the instance data
	PackedInstance inst = cpuInstances[index];
	
	// Extract the node ID from the name if it's procedurally generated
	ScatterNode* scatterNode = nullptr;
	if (scene && name.find("Scatter_") != std::string::npos) {
		int nodeID = -1;
		if (sscanf_s(name.c_str(), "Scatter_Instanced_%d_", &nodeID) == 1 ||
			sscanf_s(name.c_str(), "Scatter_Group_%d_", &nodeID) == 1) 
		{
			GraphNode* node = scene->GetNodeGraph().FindNode(nodeID);
			if (node && node->title == "Scatter") {
				scatterNode = static_cast<ScatterNode*>(node);
			}
		}
	}

	if (scatterNode) {
		glm::vec3 pos(inst.positionAndScale.x, inst.positionAndScale.y, inst.positionAndScale.z);
		float scale = inst.positionAndScale.w;
		scatterNode->AddDeletionVolume(pos, meshBoundRadius * scale * 1.0f);
	}
	
	// 2. Remove it from the CPU array (O(1) removal via swap-pop)
	cpuInstances[index] = cpuInstances.back();
	cpuInstances.pop_back();

	// 3. Spawn a real GameObject
	glm::vec3 pos(inst.positionAndScale.x, inst.positionAndScale.y, inst.positionAndScale.z);
	float scale = inst.positionAndScale.w;
	glm::vec3 euler(inst.rotationAndFlags.x, inst.rotationAndFlags.y, inst.rotationAndFlags.z);

	GameObject* obj = new GameObject("Extracted " + name);
	obj->GetTransform().SetPosition(pos);
	obj->GetTransform().SetRotation(euler);
	obj->GetTransform().SetScale(glm::vec3(scale));
	
	// Assign components
	obj->SetMesh(sharedMesh);
	obj->SetMaterial(material);
	obj->SetTexture(texture);
	obj->SetNormalMap(normalMap);
	for (const auto& layer : textureLayers) {
		obj->AddTextureLayer(layer);
	}
	
	scene->AddObject(obj);

	if (!skipReuploadAndSelect) {
		// 4. Force a fast re-upload to GPU so the instance disappears from the scatter instantly
		Setup(sharedMesh, cpuInstances, material, texture, normalMap, textureLayers);

		// 5. Select the newly spawned GameObject in Editor
		scene->SetSelectedIndex((int)scene->GetObjects().size() - 1);
	}
}

// =====================================================================
// Instantiates a batch of scattered grass instances into real 
// editable GameObjects, optimized with multithreading.
// =====================================================================
void InstancedGroup::ExtractInstances(const std::vector<int>& indices, SceneManager* scene, bool skipReuploadAndSelect)
{
	if (indices.empty() || !scene) return;

	// Sort indices in descending order, so that swap-pop doesn't invalidate subsequent indices
	std::vector<int> sortedIndices = indices;
	std::sort(sortedIndices.rbegin(), sortedIndices.rend());

	// Prepare an array of GameObjects to hold the results of parallel creation
	std::vector<GameObject*> newObjects(sortedIndices.size(), nullptr);

	std::vector<size_t> loopIndices(sortedIndices.size());
	std::iota(loopIndices.begin(), loopIndices.end(), 0);

	// Multi-threaded GameObject creation (Transforms + Allocations)
	std::for_each(std::execution::par, loopIndices.begin(), loopIndices.end(), [&](size_t arrIdx) {
		int index = sortedIndices[arrIdx];
		
		PackedInstance inst = cpuInstances[index];
		glm::vec3 pos(inst.positionAndScale.x, inst.positionAndScale.y, inst.positionAndScale.z);
		float scale = inst.positionAndScale.w;
		glm::vec3 euler(inst.rotationAndFlags.x, inst.rotationAndFlags.y, inst.rotationAndFlags.z);

		// Note: "new GameObject" and string allocations are thread-safe here
		GameObject* obj = new GameObject("Extracted " + name);
		obj->GetTransform().SetPosition(pos);
		obj->GetTransform().SetRotation(euler);
		obj->GetTransform().SetScale(glm::vec3(scale));
		
		newObjects[arrIdx] = obj;
	});

	// Extract the node ID from the name if it's procedurally generated
	ScatterNode* scatterNode = nullptr;
	if (scene && name.find("Scatter_") != std::string::npos) {
		int nodeID = -1;
		if (sscanf_s(name.c_str(), "Scatter_Instanced_%d_", &nodeID) == 1 ||
			sscanf_s(name.c_str(), "Scatter_Group_%d_", &nodeID) == 1) 
		{
			GraphNode* node = scene->GetNodeGraph().FindNode(nodeID);
			if (node && node->title == "Scatter") {
				scatterNode = static_cast<ScatterNode*>(node);
			}
		}
	}

	// Sequential removal from cpuInstances (O(1) swap-pop in descending order)
	for (int idx : sortedIndices) {
		if (scatterNode) {
			PackedInstance& inst = cpuInstances[idx];
			glm::vec3 pos(inst.positionAndScale.x, inst.positionAndScale.y, inst.positionAndScale.z);
			float scale = inst.positionAndScale.w;
			scatterNode->AddDeletionVolume(pos, meshBoundRadius * scale * 1.0f);
		}
		cpuInstances[idx] = cpuInstances.back();
		cpuInstances.pop_back();
	}

	// Sequential assignment of shared components and scene integration
	int baseItemIdx = (int)scene->GetObjects().size();
	for (size_t i = 0; i < newObjects.size(); ++i) {
		newObjects[i]->SetMesh(sharedMesh); // Thread-safe since it's sequential (modifies mesh refCount)
		newObjects[i]->SetMaterial(material);
		newObjects[i]->SetTexture(texture);
		newObjects[i]->SetNormalMap(normalMap);
		for (const auto& layer : textureLayers) {
			newObjects[i]->AddTextureLayer(layer);
		}
		scene->AddObject(newObjects[i]);
	}

	if (!skipReuploadAndSelect) {
		// Force a fast re-upload to GPU so the instance disappears from the scatter instantly
		Setup(sharedMesh, cpuInstances, material, texture, normalMap, textureLayers);

		// Multi-selection behavior
		for (size_t i = 0; i < newObjects.size(); ++i) {
			scene->SetSelectedIndex(baseItemIdx + (int)i, true, false);
		}
	}
}

// =====================================================================
// GPU Selection Pipeline
// =====================================================================
void InstancedGroup::SelectInstances(const std::vector<int>& indices, bool additive)
{
	if (!additive) {
		ClearSelection();
	}

	bool changed = false;
	for (int idx : indices) {
		if (idx >= 0 && idx < cpuInstances.size()) {
			if (cpuInstances[idx].rotationAndFlags.w < 0.5f) {
				cpuInstances[idx].rotationAndFlags.w = 1.0f;
				selectedInstanceIndices.push_back(idx);
				changed = true;
			}
		}
	}

	if (changed) {
		ReuploadGPU();
	}
}

void InstancedGroup::ClearSelection()
{
	if (selectedInstanceIndices.empty()) return;

	for (int idx : selectedInstanceIndices) {
		if (idx >= 0 && idx < cpuInstances.size()) {
			cpuInstances[idx].rotationAndFlags.w = 0.0f;
		}
	}
	selectedInstanceIndices.clear();
	ReuploadGPU();
}

void InstancedGroup::DeleteSelectedInstances(SceneManager* scene)
{
	if (selectedInstanceIndices.empty()) return;

	// Extract the node ID from the name if it's procedurally generated
	ScatterNode* scatterNode = nullptr;
	if (scene && name.find("Scatter_") != std::string::npos) {
		int nodeID = -1;
		if (sscanf_s(name.c_str(), "Scatter_Instanced_%d_", &nodeID) == 1 ||
			sscanf_s(name.c_str(), "Scatter_Group_%d_", &nodeID) == 1) 
		{
			GraphNode* node = scene->GetNodeGraph().FindNode(nodeID);
			if (node && node->title == "Scatter") {
				scatterNode = static_cast<ScatterNode*>(node);
			}
		}
	}

	// O(N) filter to delete instances physically
	std::vector<PackedInstance> newInstances;
	newInstances.reserve(cpuInstances.size() - selectedInstanceIndices.size());
	
	for (const auto& inst : cpuInstances) {
		if (inst.rotationAndFlags.w < 0.5f) {
			newInstances.push_back(inst);
		} else {
			// It's being deleted! Mask it permanently if procedurally generated.
			if (scatterNode) {
				glm::vec3 pos(inst.positionAndScale.x, inst.positionAndScale.y, inst.positionAndScale.z);
				float scale = inst.positionAndScale.w;
				scatterNode->AddDeletionVolume(pos, meshBoundRadius * scale * 1.0f); // 1.0f tolerance multiplier
			}
		}
	}

	cpuInstances = std::move(newInstances);
	selectedInstanceIndices.clear();
	
	ReuploadGPU();
}

void InstancedGroup::ReuploadGPU()
{
	if (cpuInstances.empty()) {
		// All instances were extracted — disable rendering by zeroing count
		// so CullAndDraw returns early. Don't call Release() to avoid freeing
		// the sharedMesh that extracted GameObjects still reference.
		totalCount = 0;
		lastVisibleCount = 0;
		
		// Release ONLY GPU buffers (SSBOs, indirect buffers) — NOT the mesh
		if (instanceSSBO) { glDeleteBuffers(1, &instanceSSBO); instanceSSBO = 0; }
		ReleaseLODBuffers();
		ReleaseShadowBuffers();
		ReleaseChunks();
		
		printf("[InstancedGroup] '%s': All instances extracted, group disabled\n", name.c_str());
		return;
	}

	if (sharedMesh) {
		// Cache pointers before Setup (which calls Release internally)
		Mesh* meshCopy = sharedMesh;
		Material* matCopy = material;
		Texture* texCopy = texture;
		Texture* normCopy = normalMap;
		std::vector<TextureLayer> layersCopy = textureLayers;
		
		Setup(meshCopy, cpuInstances, matCopy, texCopy, normCopy, layersCopy);
	}
}


