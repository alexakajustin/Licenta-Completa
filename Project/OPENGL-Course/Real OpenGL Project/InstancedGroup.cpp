#include "InstancedGroup.h"
#include "Mesh.h"
#include "Material.h"
#include "Texture.h"
#include "Shader.h"
#include "DebugOverlay.h"
#include "Frustum.h"
#include "GameObject.h"
#include "SceneManager.h"
#include <cstdio>
#include <cmath>
#include <map>
#include <algorithm>

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

	// Initialize LOD 0 with the shared mesh
	lodLevels[0].mesh = sharedMesh;
	lodLevels[0].maxDistance = defaultMaxDrawDistance;
	lodCount = 1;

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
	const glm::vec3& cameraPos, float maxDrawDistance,
	bool isShadowPass)
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
	glUniform1f(glGetUniformLocation(cullShaderID, "maxDrawDistance"), maxDrawDistance);
	glUniform1f(glGetUniformLocation(cullShaderID, "instanceBoundRadius"), meshBoundRadius);
	glUniform3fv(glGetUniformLocation(cullShaderID, "meshBoundsCenter"), 1, glm::value_ptr(meshBoundsCenter));

	// LOD distance uniforms
	glUniform1i(glGetUniformLocation(cullShaderID, "lodCount"), lodCount);
	for (int lod = 0; lod < lodCount; lod++) {
		char buf[64];
		snprintf(buf, sizeof(buf), "lodDistances[%d]", lod);
		float dist = lodLevels[lod].maxDistance > 0.0f ? lodLevels[lod].maxDistance : maxDrawDistance;
		glUniform1f(glGetUniformLocation(cullShaderID, buf), dist);
	}

	if (useChunking) {
		CullAndDrawChunked(cullShaderID, renderShader, projection, view, cameraPos, maxDrawDistance, isShadowPass);
	} else {
		CullAndDrawFlat(cullShaderID, renderShader, projection, view, cameraPos, maxDrawDistance, isShadowPass);
	}
}

// =====================================================================
// Flat cull+draw (single SSBO, < 1M instances)
// =====================================================================
void InstancedGroup::CullAndDrawFlat(GLuint cullShaderID, Shader& renderShader,
	const glm::mat4& projection, const glm::mat4& view,
	const glm::vec3& cameraPos, float maxDrawDistance,
	bool isShadowPass)
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
	RenderLODs(renderShader, projection, view, cameraPos, isShadowPass);
}

// =====================================================================
// Chunked cull+draw (spatial grid, 1M+ instances)
// =====================================================================
void InstancedGroup::CullAndDrawChunked(GLuint cullShaderID, Shader& renderShader,
	const glm::mat4& projection, const glm::mat4& view,
	const glm::vec3& cameraPos, float maxDrawDistance,
	bool isShadowPass)
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
	RenderLODs(renderShader, projection, view, cameraPos, isShadowPass);
}

// =====================================================================
// RenderLODs — Issue indirect draw calls for each LOD level
// =====================================================================
void InstancedGroup::RenderLODs(Shader& renderShader, const glm::mat4& projection,
	const glm::mat4& view, const glm::vec3& cameraPos,
	bool isShadowPass)
{
	renderShader.UseShader();
	GLuint shaderID = renderShader.GetShaderID();

	// Set standard uniforms
	glUniformMatrix4fv(renderShader.GetProjectionLocation(), 1, GL_FALSE, glm::value_ptr(projection));
	glUniformMatrix4fv(renderShader.GetViewLocation(), 1, GL_FALSE, glm::value_ptr(view));
	glUniform3fv(renderShader.GetEyePositionLocation(), 1, glm::value_ptr(cameraPos));

	// Bind all material properties (Standard + Custom Uniforms)
	if (!isShadowPass && material) {
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

		// Bind this LOD's visible SSBO for vertex shader to read
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, lodLevels[lod].visibleSSBO);

		// Issue indirect draw
		lodMesh->RenderIndirect(lodLevels[lod].indirectBuffer);
	}
}

// =====================================================================
// CullAndDrawShadow — Dedicated shadow map pass
// Uses separate cull with light's frustum and limited draw distance
// =====================================================================
void InstancedGroup::CullAndDrawShadow(GLuint cullShaderID, Shader& shadowShader,
	const glm::mat4& lightViewProj, const glm::vec3& cameraPos,
	float shadowDist, float time)
{
	if (totalCount == 0 || !sharedMesh) return;

	// ================================================================
	// PHASE 1: Cull against light frustum with tight distance limit
	// ================================================================
	glUseProgram(cullShaderID);

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
	glUniform1f(glGetUniformLocation(cullShaderID, "maxDrawDistance"), shadowDist);
	glUniform1f(glGetUniformLocation(cullShaderID, "instanceBoundRadius"), meshBoundRadius);
	glUniform3fv(glGetUniformLocation(cullShaderID, "meshBoundsCenter"), 1, glm::value_ptr(meshBoundsCenter));

	// Force LOD count to 1 for shadow pass (no LOD in shadows)
	glUniform1i(glGetUniformLocation(cullShaderID, "lodCount"), 1);
	glUniform1f(glGetUniformLocation(cullShaderID, "lodDistances[0]"), shadowDist);

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
			if (distToChunk > shadowDist + chunkSize) continue;

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

	// Set material alpha (for shadow color map)
	GLint alphaLoc = glGetUniformLocation(sid, "materialAlpha");
	if (alphaLoc != -1) {
		float alpha = material ? material->GetAlpha() : 1.0f;
		glUniform1f(alphaLoc, alpha);
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
	
	scene->AddObject(obj);

	if (!skipReuploadAndSelect) {
		// 4. Force a fast re-upload to GPU so the instance disappears from the scatter instantly
		Setup(sharedMesh, cpuInstances, material, texture, normalMap, textureLayers);

		// 5. Select the newly spawned GameObject in Editor
		scene->SetSelectedIndex((int)scene->GetObjects().size() - 1);
	}
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


