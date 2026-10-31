#include "Rendering/InstancedGroup.h"
#include "Rendering/Mesh.h"
#include "Rendering/Material.h"
#include "Rendering/Texture.h"
#include "Rendering/Shader.h"
#include "Rendering/LODPolicy.h"
#include "GraphicsSettings.h"
#include "Application.h"
#include "Editor/DebugOverlay.h"
#include "Core/Frustum.h"
#include "Scene/GameObject.h"
#include "Scene/SceneManager.h"
#include "Nodes/NodeGraph.h"
#include "Procedural/ScatterNode.h"
#include <cstdio>
#include <cmath>
#include <map>
#include <algorithm>
#include <execution>
#include <numeric>

namespace {
	// Uniform locations of the cull program (compute_cull.glsl), looked up once
	struct CullUniforms {
		GLuint program = 0;
		GLint frustumPlanes, useSphereCull, lightPos, totalInstances, instanceOffset, phase;
		GLint instanceBoundRadius, meshBoundsCenter;
		GLint cameraPos, lodProjScale, lodScreenSize, minScreenSize, crossfadeWidth, lodCount, lodMin;
		GLint maxDrawDistance, minCasterSize;
		GLint detailFoliage, detailRange, detailDensity;
		GLint useHiZ, screenSize, nearPlane, farPlane, hizViewProj, hizMap;
	};

	const CullUniforms& GetCullUniforms(GLuint program)
	{
		static CullUniforms u;
		if (u.program == program) return u;
		u.program = program;
		auto L = [program](const char* n) { return glGetUniformLocation(program, n); };
		u.frustumPlanes = L("frustumPlanes");
		u.useSphereCull = L("useSphereCull");
		u.lightPos = L("lightPos");
		u.totalInstances = L("totalInstances");
		u.instanceOffset = L("instanceOffset");
		u.phase = L("phase");
		u.instanceBoundRadius = L("instanceBoundRadius");
		u.meshBoundsCenter = L("meshBoundsCenter");
		u.cameraPos = L("cameraPos");
		u.lodProjScale = L("lodProjScale");
		u.lodScreenSize = L("lodScreenSize");
		u.minScreenSize = L("minScreenSize");
		u.crossfadeWidth = L("crossfadeWidth");
		u.lodCount = L("lodCount");
		u.lodMin = L("lodMin");
		u.maxDrawDistance = L("maxDrawDistance");
		u.minCasterSize = L("minCasterSize");
		u.detailFoliage = L("detailFoliage");
		u.detailRange = L("detailRange");
		u.detailDensity = L("detailDensity");
		u.useHiZ = L("useHiZ");
		u.screenSize = L("screenSize");
		u.nearPlane = L("nearPlane");
		u.farPlane = L("farPlane");
		u.hizViewProj = L("hizViewProj");
		u.hizMap = L("hizMap");
		return u;
	}
}

InstancedGroup::InstancedGroup(const std::string& name)
	: name(name)
{
}

InstancedGroup::~InstancedGroup()
{
	Release();
}

InstancedGroup::CullPass InstancedGroup::CameraPass(const glm::mat4& projection, const glm::mat4& view,
	const glm::vec3& cameraPos, const GraphicsSettings* gs, float lodBias, float detailScale)
{
	CullPass pass;
	pass.viewProj = projection * view;
	pass.cameraPos = cameraPos;
	pass.lodProjScale = projection[1][1] * (gs ? gs->lodBias : 1.0f) * lodBias;
	pass.maxDrawDistance = gs ? gs->renderDistance : 2000.0f;
	pass.crossfade = (!gs || gs->lodCrossfade) ? LODPolicy::CROSSFADE_WIDTH : 0.0f;
	pass.minScreenSize = LODPolicy::CULL_SCREEN_SIZE;
	float detailEnd = std::max((gs ? gs->detailDistance : 150.0f) * detailScale, 1.0f);
	pass.detailRange = glm::vec2(detailEnd * 0.3f, detailEnd);
	pass.detailDensity = gs ? gs->detailDensity : 1.0f;
	// Depth range for the Hi-Z comparison: P[2][2] = -(f+n)/(f-n), P[3][2] = -2fn/(f-n)
	float A = projection[2][2], B = projection[3][2];
	pass.nearPlane = B / (A - 1.0f);
	pass.farPlane = B / (A + 1.0f);
	return pass;
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
	if (mesh) mesh->AddRef(); // Retain the new mesh BEFORE releasing the old one to prevent use-after-free

	Release(); // Clean up any previous buffers
	if (instances.empty()) {
		if (mesh) mesh->Release();
		return;
	}

	sharedMesh = mesh;

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

	// Compute overall group AABB (for quick CPU-side early out culling)
	groupBoundsMin = glm::vec3(FLT_MAX);
	groupBoundsMax = glm::vec3(-FLT_MAX);
	for (const auto& inst : instances) {
		glm::vec3 pos(inst.positionAndScale.x, inst.positionAndScale.y, inst.positionAndScale.z);
		float r = meshBoundRadius * inst.positionAndScale.w;
		groupBoundsMin = glm::min(groupBoundsMin, pos - glm::vec3(r));
		groupBoundsMax = glm::max(groupBoundsMax, pos + glm::vec3(r));
	}

	// LOD0 is the shared mesh; LOD1/LOD2 from SetLODMesh survive re-uploads (selection, extraction)
	lodLevels[0].mesh = sharedMesh;
	lodCount = 1;
	for (int lod = 1; lod < MAX_LOD_LEVELS; lod++) if (lodLevels[lod].mesh) lodCount = lod + 1;

	// Decide whether to use chunking
	const uint32_t CHUNK_THRESHOLD = 1000000; // 1M instances → enable chunking
	if (totalCount >= CHUNK_THRESHOLD) {
		useChunking = true;
		SetupChunking(instances);
	} else {
		useChunking = false;
		SetupFlat(instances);
	}

	AllocateLODBuffers();

	// Visibility buffer for two-phase occlusion culling (persists from frame to frame)
	if (visibilitySSBO) { glDeleteBuffers(1, &visibilitySSBO); visibilitySSBO = 0; }
	glGenBuffers(1, &visibilitySSBO);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, visibilitySSBO);
	std::vector<uint32_t> zeros(totalCount, 0);
	glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(uint32_t) * totalCount, zeros.data(), GL_DYNAMIC_DRAW);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

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

	instanceSSBO = 0; // Not used in chunked mode

	printf("[InstancedGroup] Chunked into %d cells (%dx%d grid, %.0fm cell size)\n",
		(int)chunks.size(), gridX, gridZ, chunkSize);
}

// =====================================================================
// Allocate per-LOD output buffers (shared by every pass of the frame)
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
// LOD Configuration
// =====================================================================
void InstancedGroup::SetLODMesh(int level, Mesh* mesh)
{
	if (level <= 0 || level >= MAX_LOD_LEVELS) return;

	lodLevels[level].mesh = mesh;
	lodCount = 1;
	for (int lod = 1; lod < MAX_LOD_LEVELS; lod++) if (lodLevels[lod].mesh) lodCount = lod + 1;
}

// Tens of thousands of instances of a few-hundred-triangle mesh with no LODs (grass cards):
// their cost is the instance count, so they thin out with distance instead of switching LOD
bool InstancedGroup::IsDetailFoliage() const
{
	return lodCount == 1 && totalCount >= 10000 && sharedMesh && sharedMesh->GetIndexCount() <= 3 * 500;
}

uint32_t InstancedGroup::ReadDrawnCount(int lod) const
{
	if (lod < 0 || lod >= lodCount || !lodLevels[lod].indirectBuffer) return 0;
	DrawElementsIndirectCommand cmd = {};
	glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
	glBindBuffer(GL_DRAW_INDIRECT_BUFFER, lodLevels[lod].indirectBuffer);
	glGetBufferSubData(GL_DRAW_INDIRECT_BUFFER, 0, sizeof(cmd), &cmd);
	glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
	return cmd.instanceCount;
}

// =====================================================================
// Cull — run compute_cull.glsl for one pass into the per-LOD lists
// =====================================================================
bool InstancedGroup::Cull(GLuint cullShaderID, const CullPass& pass)
{
	if (totalCount == 0 || !sharedMesh || cullShaderID == 0) return false;

	const bool detail = IsDetailFoliage();
	float maxDist = pass.maxDrawDistance * ((pass.sphereCull || !pass.sizeScaledDistance) ? 1.0f : DrawDistanceScale());
	if (detail) maxDist = std::min(maxDist, pass.detailMaxDistance);
	Frustum frustum = Frustum::CreateFrustumFromMatrix(pass.viewProj);

	// Bounds (whole group or one chunk) that cannot contribute to this pass
	auto rejected = [&](const glm::vec3& bmin, const glm::vec3& bmax) {
		glm::vec3 center = (bmin + bmax) * 0.5f;
		float radius = glm::length(bmax - bmin) * 0.5f;
		if (pass.sphereCull) return glm::length(center - pass.lightPos) - radius > maxDist;
		return glm::length(center - pass.cameraPos) - radius > maxDist || !frustum.IsBoxVisible(bmin, bmax);
	};
	if (!useChunking && rejected(groupBoundsMin, groupBoundsMax)) return false;

	glUseProgram(cullShaderID);

	// Reset the indirect command of every LOD
	for (int lod = 0; lod < lodCount; lod++) {
		DrawElementsIndirectCommand cmd = {};
		cmd.count = GetLODMesh(lod)->GetIndexCount();
		glBindBuffer(GL_DRAW_INDIRECT_BUFFER, lodLevels[lod].indirectBuffer);
		glBufferSubData(GL_DRAW_INDIRECT_BUFFER, 0, sizeof(DrawElementsIndirectCommand), &cmd);
	}
	glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);

	const CullUniforms& u = GetCullUniforms(cullShaderID);
	glm::vec4 planes[6];
	for (int i = 0; i < 6; i++) planes[i] = glm::vec4(frustum.planes[i].normal, frustum.planes[i].d);
	glUniform4fv(u.frustumPlanes, 6, glm::value_ptr(planes[0]));
	glUniform1i(u.useSphereCull, pass.sphereCull ? 1 : 0);
	glUniform3fv(u.lightPos, 1, glm::value_ptr(pass.lightPos));
	glUniform1i(u.phase, pass.phase);
	glUniform1f(u.instanceBoundRadius, meshBoundRadius);
	glUniform3fv(u.meshBoundsCenter, 1, glm::value_ptr(meshBoundsCenter));
	glUniform3fv(u.cameraPos, 1, glm::value_ptr(pass.cameraPos));
	glUniform1f(u.lodProjScale, pass.lodProjScale);
	glUniform1fv(u.lodScreenSize, 2, LODPolicy::LOD_SCREEN_SIZE);
	glUniform1f(u.minScreenSize, pass.minScreenSize);
	glUniform1f(u.crossfadeWidth, pass.crossfade);
	glUniform1i(u.lodCount, lodCount);
	glUniform1i(u.lodMin, std::min(pass.lodMin, lodCount - 1));
	glUniform1f(u.maxDrawDistance, maxDist);
	glUniform1f(u.minCasterSize, pass.minCasterSize);
	glUniform1i(u.detailFoliage, detail ? 1 : 0);
	glUniform2f(u.detailRange, std::min(pass.detailRange.x, pass.detailRange.y - 0.01f), pass.detailRange.y);
	glUniform1f(u.detailDensity, pass.detailDensity);
	glUniform1i(u.useHiZ, pass.hizTexture != 0 ? 1 : 0);
	if (pass.hizTexture != 0) {
		glUniform2fv(u.screenSize, 1, glm::value_ptr(pass.screenSize));
		glUniform1f(u.nearPlane, pass.nearPlane);
		glUniform1f(u.farPlane, pass.farPlane);
		glUniformMatrix4fv(u.hizViewProj, 1, GL_FALSE, glm::value_ptr(pass.viewProj));
		glActiveTexture(GL_TEXTURE15);
		glBindTexture(GL_TEXTURE_2D, pass.hizTexture);
		glUniform1i(u.hizMap, 15);
	}

	// Output lists of the three LOD slots (unused slots are bound but never written)
	static const GLuint visibleBindings[MAX_LOD_LEVELS] = { 1, 3, 4 };
	static const GLuint indirectBindings[MAX_LOD_LEVELS] = { 2, 5, 6 };
	for (int lod = 0; lod < MAX_LOD_LEVELS; lod++) {
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, visibleBindings[lod], lodLevels[lod].visibleSSBO);
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, indirectBindings[lod], lodLevels[lod].indirectBuffer);
	}
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 7, visibilitySSBO);

	bool dispatched = false;
	if (useChunking) {
		uint32_t offset = 0;
		for (auto& chunk : chunks) {
			if (!rejected(chunk.boundsMin, chunk.boundsMax)) {
				glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, chunk.ssbo);
				glUniform1ui(u.totalInstances, chunk.instanceCount);
				glUniform1ui(u.instanceOffset, offset);
				glDispatchCompute((chunk.instanceCount + 255) / 256, 1, 1);
				dispatched = true;
			}
			offset += chunk.instanceCount;
		}
	} else {
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, instanceSSBO);
		glUniform1ui(u.totalInstances, totalCount);
		glUniform1ui(u.instanceOffset, 0);
		glDispatchCompute((totalCount + 255) / 256, 1, 1);
		dispatched = true;
	}

	if (dispatched) glMemoryBarrier(GL_COMMAND_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT);
	return dispatched;
}

// =====================================================================
// CullAndDraw — camera passes (main view phases, reflection)
// =====================================================================
void InstancedGroup::CullAndDraw(GLuint cullShaderID, Shader& renderShader, const CullPass& pass,
	const glm::mat4& projection, const glm::mat4& view, const GraphicsSettings* gs)
{
	if (Cull(cullShaderID, pass))
		DrawLODs(renderShader, projection, view, pass.cameraPos, gs);
}

// =====================================================================
// DrawLODs — Issue indirect draw calls for each LOD level (material shader)
// =====================================================================
void InstancedGroup::DrawLODs(Shader& renderShader, const glm::mat4& projection,
	const glm::mat4& view, const glm::vec3& cameraPos, const GraphicsSettings* gs)
{
	renderShader.UseShader();
	GLuint shaderID = renderShader.GetShaderID();

	// Set standard uniforms
	glUniformMatrix4fv(renderShader.GetProjectionLocation(), 1, GL_FALSE, glm::value_ptr(projection));
	glUniformMatrix4fv(renderShader.GetViewLocation(), 1, GL_FALSE, glm::value_ptr(view));
	glUniform3fv(renderShader.GetEyePositionLocation(), 1, glm::value_ptr(cameraPos));

	// Bind all material properties (Standard + Custom Uniforms)
	if (material) {
		material->UseMaterial(
			renderShader.GetSpecularIntensityLocation(),
			renderShader.GetShininessLocation(),
			glGetUniformLocation(shaderID, "material.baseColor"),
			renderShader.GetTilingLocation(),
			renderShader.GetOffsetLocation()
		);
		material->Bind(renderShader.GetShaderID());
	}
	else {
		// DEFAULT MATERIAL: Bind white color and identity tiling so objects are visible & not weirdly textured
		glUniform1f(renderShader.GetSpecularIntensityLocation(), 0.1f);
		glUniform1f(renderShader.GetShininessLocation(), 32.0f);
		GLint baseColorLoc = glGetUniformLocation(shaderID, "material.baseColor");
		if (baseColorLoc != -1) glUniform4f(baseColorLoc, 1.0f, 1.0f, 1.0f, 1.0f);
		glUniform2f(renderShader.GetTilingLocation(), 1.0f, 1.0f);
		glUniform2f(renderShader.GetOffsetLocation(), 0.0f, 0.0f);
	}

	// Bind textures
	GLint useDiffuseLoc = glGetUniformLocation(shaderID, "useDiffuseTexture");
	GLint useNormalLoc = glGetUniformLocation(shaderID, "useNormalMap");

	if (texture) {
		if (useDiffuseLoc != -1) glUniform1i(useDiffuseLoc, 1);
		glUniform1i(glGetUniformLocation(shaderID, "theTexture"), 0);
		texture->UseTexture();
	}
	else {
		if (useDiffuseLoc != -1) glUniform1i(useDiffuseLoc, 0);
	}

	if (normalMap) {
		if (useNormalLoc != -1) glUniform1i(useNormalLoc, 1);
		glUniform1i(glGetUniformLocation(shaderID, "normalMap"), 1);
		normalMap->UseNormalMap();
	}
	else {
		if (useNormalLoc != -1) glUniform1i(useNormalLoc, 0);
	}

	// Texture layers (inherit parent's appearance). All MAX_TEXTURE_LAYERS are bound (this used
	// to stop at 4 while telling the shader there were more), on the same conflict-free units
	// as GameObject — the old 18+i displacement units collided with the shadow colour maps.
	int layerCount = std::min((int)textureLayers.size(), MAX_TEXTURE_LAYERS);
	GLint layerCountLoc = glGetUniformLocation(shaderID, "textureLayerCount");
	if (layerCountLoc != -1) glUniform1i(layerCountLoc, layerCount);

	for (int i = 0; i < layerCount; i++)
	{
		int diffUnit = LAYER_DIFFUSE_UNITS[i];
		int normUnit = LAYER_NORMAL_UNITS[i];
		int dispUnit = LAYER_DISPLACEMENT_UNITS[i];

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

	// Alpha-tested cards (grass, leaves) are single quads: draw both sides
	Texture* cutoutTex = texture ? texture : (!textureLayers.empty() ? textureLayers[0].texture : nullptr);
	bool doubleSided = cutoutTex && cutoutTex->HasCutoutAlpha();
	GLboolean cullWasEnabled = doubleSided ? glIsEnabled(GL_CULL_FACE) : GL_FALSE;
	if (doubleSided) glDisable(GL_CULL_FACE);
	GLint foliageLoc = glGetUniformLocation(shaderID, "isFoliage");
	if (foliageLoc != -1) glUniform1i(foliageLoc, doubleSided ? 1 : 0); // Thin translucent leaves (shader.frag)
	GLint foliageRadiusLoc = glGetUniformLocation(shaderID, "foliageRadius");
	if (foliageRadiusLoc != -1) glUniform1f(foliageRadiusLoc, doubleSided ? meshBoundRadius : 0.0f); // Crown occlusion scale

	GLint debugLoc = glGetUniformLocation(shaderID, "debugLODColoring");
	GLint lodColorLoc = glGetUniformLocation(shaderID, "lodDebugColor");
	if (debugLoc != -1) glUniform1i(debugLoc, (gs && gs->debugLODColoring) ? 1 : 0);
	static const glm::vec3 lodColors[MAX_LOD_LEVELS] = {
		glm::vec3(1.0f, 0.2f, 0.2f),  // RED = LOD0
		glm::vec3(0.2f, 1.0f, 0.2f),  // GREEN = LOD1
		glm::vec3(0.2f, 0.2f, 1.0f)   // BLUE = LOD2
	};

	for (int lod = 0; lod < lodCount; lod++) {
		if (gs && gs->debugLODColoring && lodColorLoc != -1) glUniform3fv(lodColorLoc, 1, glm::value_ptr(lodColors[lod]));

		// Bind this LOD's visible SSBO for vertex shader to read
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, lodLevels[lod].visibleSSBO);
		GetLODMesh(lod)->RenderIndirect(lodLevels[lod].indirectBuffer, renderShader.HasTessellation());
	}

	if (foliageLoc != -1 && doubleSided) glUniform1i(foliageLoc, 0);
	if (doubleSided && cullWasEnabled) glEnable(GL_CULL_FACE);
}

// =====================================================================
// DrawDepthLODs — every LOD list with a depth-only shader (uniforms already set)
// =====================================================================
void InstancedGroup::DrawDepthLODs()
{
	for (int lod = 0; lod < lodCount; lod++) {
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, lodLevels[lod].visibleSSBO);
		GetLODMesh(lod)->RenderIndirect(lodLevels[lod].indirectBuffer);
	}
}

// =====================================================================
// ApplyShadowWind — the instanced shadow shaders apply the same sway as
// grass.vert. Groups whose material shader has no wind (trees, rocks) get
// strength 0; the value must be set for every group because the shadow
// program is shared and keeps the previous group's uniforms otherwise.
// =====================================================================
void InstancedGroup::ApplyShadowWind(GLuint shadowShaderID, float time) const
{
	float strength = 0.0f;
	float speed = 1.0f;

	Shader* matShader = material ? material->GetShader() : nullptr;
	if (matShader && matShader->GetShaderID() != 0) {
		GLuint msid = matShader->GetShaderID();
		GLint strengthLoc = glGetUniformLocation(msid, "windStrength");
		if (strengthLoc != -1) {
			// Start from the shader's own defaults (grass.vert initializers), then the material overrides
			glGetUniformfv(msid, strengthLoc, &strength);
			GLint speedLoc = glGetUniformLocation(msid, "windSpeed");
			if (speedLoc != -1) glGetUniformfv(msid, speedLoc, &speed);

			const auto& floats = material->GetFloats();
			auto it = floats.find("windStrength");
			if (it != floats.end()) strength = it->second;
			it = floats.find("windSpeed");
			if (it != floats.end()) speed = it->second;
		}
	}

	GLint loc = glGetUniformLocation(shadowShaderID, "windStrength");
	if (loc != -1) glUniform1f(loc, strength);
	loc = glGetUniformLocation(shadowShaderID, "windSpeed");
	if (loc != -1) glUniform1f(loc, speed);
	loc = glGetUniformLocation(shadowShaderID, "time");
	if (loc != -1) glUniform1f(loc, time);
}

// =====================================================================
// CullAndDrawShadow — depth-only passes: directional cascades, water foam
// obstacles. Opaque casters keep back-face culling and skip the texture
// fetch (early depth test); alpha-tested cards are drawn double-sided.
// =====================================================================
void InstancedGroup::CullAndDrawShadow(GLuint cullShaderID, Shader& shadowShader, const CullPass& pass,
	const glm::mat4& lightViewProj, float time)
{
	if (!Cull(cullShaderID, pass)) return;

	shadowShader.UseShader();
	GLuint sid = shadowShader.GetShaderID();
	shadowShader.SetDirectionalLightTransform(lightViewProj);
	ApplyShadowWind(sid, time);

	// Material tiling/offset (default identity so TexCoord isn't zeroed out)
	glUniform2f(glGetUniformLocation(sid, "material.tiling"), 1.0f, 1.0f);
	glUniform2f(glGetUniformLocation(sid, "material.offset"), 0.0f, 0.0f);

	// Material alpha for the shadow colour map (semi-transparent casters)
	GLint alphaLoc = glGetUniformLocation(sid, "materialAlpha");
	if (alphaLoc != -1) glUniform1f(alphaLoc, material ? material->GetAlpha() : 1.0f);

	Texture* cutoutTex = texture ? texture : (!textureLayers.empty() ? textureLayers[0].texture : nullptr);
	bool cutout = cutoutTex && cutoutTex->HasCutoutAlpha();
	GLint useDiffuseLoc = glGetUniformLocation(sid, "useDiffuseTexture");
	if (useDiffuseLoc != -1) glUniform1i(useDiffuseLoc, cutout ? 1 : 0);
	if (cutout) {
		glUniform1i(glGetUniformLocation(sid, "theTexture"), 0);
		cutoutTex->UseTexture();
		glDisable(GL_CULL_FACE);
	} else {
		glEnable(GL_CULL_FACE);
	}

	DrawDepthLODs();
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
	CullPass pass;
	pass.sphereCull = true;
	pass.lightPos = lightPos;
	pass.cameraPos = cameraPos;
	pass.lodProjScale = 1.0f; // 90 degree cube faces
	pass.maxDrawDistance = std::min(farPlane, gs ? gs->shadowDistance : 100.0f);
	float detailEnd = std::max((gs ? gs->detailDistance : 150.0f), 1.0f);
	pass.detailRange = glm::vec2(detailEnd * 0.3f, detailEnd);
	pass.detailDensity = gs ? gs->detailDensity : 1.0f;
	pass.detailMaxDistance = gs ? gs->detailShadowDistance : 25.0f;
	if (!Cull(cullShaderID, pass)) return;

	shadowShader.UseShader();
	GLuint sid = shadowShader.GetShaderID();

	// Set omni light uniforms
	GLint lightPosLoc = glGetUniformLocation(sid, "lightPos");
	if (lightPosLoc != -1) glUniform3fv(lightPosLoc, 1, glm::value_ptr(lightPos));
	GLint omniLightPosLoc = glGetUniformLocation(sid, "omniLightPos");
	if (omniLightPosLoc != -1) glUniform3fv(omniLightPosLoc, 1, glm::value_ptr(lightPos));
	glUniform1f(glGetUniformLocation(sid, "farPlane"), farPlane);
	ApplyShadowWind(sid, time);

	// Material tiling/offset (default to identity for shadows)
	glUniform2f(glGetUniformLocation(sid, "material.tiling"), 1.0f, 1.0f);
	glUniform2f(glGetUniformLocation(sid, "material.offset"), 0.0f, 0.0f);

	// Set material alpha (for shadow color map)
	GLint alphaLoc = glGetUniformLocation(sid, "materialAlpha");
	if (alphaLoc != -1) glUniform1f(alphaLoc, material ? material->GetAlpha() : 1.0f);

	// Alpha testing uniforms
	GLint useDiffuseLoc = glGetUniformLocation(sid, "useDiffuseTexture");
	if (useDiffuseLoc != -1) glUniform1i(useDiffuseLoc, texture ? 1 : 0);
	if (texture) {
		glUniform1i(glGetUniformLocation(sid, "theTexture"), 0);
		texture->UseTexture();
	}

	// Disable face culling for thin double-sided foliage (grass blades, leaves)
	glDisable(GL_CULL_FACE);
	DrawDepthLODs();
	glEnable(GL_CULL_FACE);
}

// =====================================================================
// Cleanup
// =====================================================================
void InstancedGroup::Release()
{
	if (instanceSSBO) { glDeleteBuffers(1, &instanceSSBO); instanceSSBO = 0; }
	if (visibilitySSBO) { glDeleteBuffers(1, &visibilitySSBO); visibilitySSBO = 0; }
	ReleaseLODBuffers();
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
			GraphNode* node = scene->FindNodeInAllGraphs(nodeID);
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
void InstancedGroup::ExtractInstances(const std::vector<int>& indices, SceneManager* scene, GameObject* parentGroup, bool skipReuploadAndSelect)
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
			GraphNode* node = scene->FindNodeInAllGraphs(nodeID);
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
		if (parentGroup) {
			parentGroup->AddChild(newObjects[i]);
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
			GraphNode* node = scene->FindNodeInAllGraphs(nodeID);
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
		if (visibilitySSBO) { glDeleteBuffers(1, &visibilitySSBO); visibilitySSBO = 0; }
		ReleaseLODBuffers();
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


