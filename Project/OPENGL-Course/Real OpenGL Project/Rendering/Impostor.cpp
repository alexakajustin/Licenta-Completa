#include "Rendering/Impostor.h"
#include "Rendering/InstancedGroup.h"
#include "Rendering/Mesh.h"
#include "Rendering/Material.h"
#include "Rendering/Texture.h"
#include "GraphicsSettings.h"
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace {
	const int FRAMES = 8;               // 8 x 8 views over the upper hemisphere
	const float FRAME_MARGIN = 1.04f;   // Views slightly larger than the tree: nothing touches their edges
	const int SUPERSAMPLE = 2;          // Views rendered at 2x and filtered down: soft coverage at the edges
	const float MIN_TREE_RADIUS = 2.5f; // Smaller plants keep their meshes (cheap anyway)
	const int MATERIAL_UNIT = 30;       // Leaf/bark atlas: a unit the scene never binds

	/// Grid position in [-1, 1]^2 -> direction on the upper hemisphere (same as impostor.vert)
	glm::vec3 HemiOctDecode(glm::vec2 e)
	{
		glm::vec2 xz = glm::vec2(e.x + e.y, e.x - e.y) * 0.5f;
		return glm::normalize(glm::vec3(xz.x, 1.0f - std::abs(xz.x) - std::abs(xz.y), xz.y));
	}

	/// Axes of the view baked along d (same as impostor.vert)
	void FrameBasis(const glm::vec3& d, glm::vec3& right, glm::vec3& up)
	{
		glm::vec3 reference = std::abs(d.y) > 0.999f ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
		right = glm::normalize(glm::cross(reference, d));
		up = glm::cross(d, right);
	}

	Texture* SurfaceTexture(const InstancedGroup* g)
	{
		if (g->GetTexture()) return g->GetTexture();
		return g->GetTextureLayers().empty() ? nullptr : g->GetTextureLayers()[0].texture;
	}

	bool IsCutout(const InstancedGroup* g)
	{
		Texture* t = SurfaceTexture(g);
		return t && t->HasCutoutAlpha();
	}

	std::string ReadText(const char* path)
	{
		std::ifstream file(path);
		if (!file.is_open()) return "";
		std::stringstream ss;
		ss << file.rdbuf();
		return ss.str();
	}

	/// Pastes the shared atlas reader after the #version line (GLSL has no #include)
	std::string WithCommon(const std::string& source, const std::string& common)
	{
		size_t eol = source.find('\n');
		if (eol == std::string::npos) return source;
		return source.substr(0, eol + 1) + common + "\n#line 2\n" + source.substr(eol + 1);
	}

	bool Linked(Shader& s)
	{
		GLint ok = 0;
		if (s.GetShaderID()) glGetProgramiv(s.GetShaderID(), GL_LINK_STATUS, &ok);
		return ok != 0;
	}

	/// Immutable RGBA texture with a mip chain, sampled trilinearly inside the atlas
	GLuint MakeAtlasTexture(GLenum format, int size, int levels)
	{
		GLuint tex = 0;
		glGenTextures(1, &tex);
		glBindTexture(GL_TEXTURE_2D, tex);
		glTexStorage2D(GL_TEXTURE_2D, levels, format, size, size);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, levels > 1 ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, levels - 1);
		return tex;
	}

	void FreeAtlas(ImpostorAtlas* atlas)
	{
		if (!atlas) return;
		if (atlas->albedo) glDeleteTextures(1, &atlas->albedo);
		if (atlas->data) glDeleteTextures(1, &atlas->data);
		if (atlas->material) glDeleteTextures(1, &atlas->material);
		delete atlas;
	}
}

// =====================================================================
// ImpostorAtlas
// =====================================================================
void ImpostorAtlas::Bind(GLuint program, const glm::mat4& viewProj, const glm::vec3& viewPos,
	const glm::vec3& viewDir, bool orthographic, float pixelScale) const
{
	auto L = [program](const char* n) { return glGetUniformLocation(program, n); };
	glUniformMatrix4fv(L("impostorViewProj"), 1, GL_FALSE, glm::value_ptr(viewProj));
	glUniform3fv(L("impostorViewPos"), 1, glm::value_ptr(viewPos));
	glUniform3fv(L("impostorViewDir"), 1, glm::value_ptr(viewDir));
	glUniform1i(L("impostorOrthographic"), orthographic ? 1 : 0);
	glUniform3fv(L("impostorCenter"), 1, glm::value_ptr(center));
	glUniform1f(L("impostorRadius"), radius);
	glUniform1i(L("impostorFrames"), frames);
	glUniform1f(L("foliageRadius"), foliageRadius);
	glUniform3fv(L("impostorBoundsMin"), 1, glm::value_ptr(boundsMin));
	glUniform3fv(L("impostorBoundsMax"), 1, glm::value_ptr(boundsMax));
	glUniform1f(L("impostorPixelScale"), pixelScale);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, albedo);
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, data);
	glActiveTexture(GL_TEXTURE0 + MATERIAL_UNIT);
	glBindTexture(GL_TEXTURE_2D, material);
	glActiveTexture(GL_TEXTURE0);
}

// =====================================================================
// ImpostorSystem
// =====================================================================
ImpostorSystem::~ImpostorSystem()
{
	Release();
}

void ImpostorSystem::Release()
{
	for (auto& [key, atlas] : atlases) FreeAtlas(atlas);
	atlases.clear();
	failed.clear();
	if (quad) { quad->Release(); quad = nullptr; }
	shader.ClearShader();
	shadowShader.ClearShader();
	bakeShader.ClearShader();
	resolveShader.ClearShader();
	initialized = false;
}

bool ImpostorSystem::Init()
{
	std::string common = ReadText("Assets/Shaders/impostor_common.glsl");
	std::string vert = ReadText("Assets/Shaders/impostor.vert");
	std::string frag = ReadText("Assets/Shaders/impostor.frag");
	std::string shadowFrag = ReadText("Assets/Shaders/impostor_shadow.frag");
	if (common.empty() || vert.empty() || frag.empty() || shadowFrag.empty()) {
		printf("[Impostor] Shader sources missing, impostors disabled\n");
		initFailed = true;
		return false;
	}
	std::string fragFull = WithCommon(frag, common);
	std::string shadowFull = WithCommon(shadowFrag, common);
	shader.CreateFromString(vert.c_str(), fragFull.c_str());
	shadowShader.CreateFromString(vert.c_str(), shadowFull.c_str());
	bakeShader.CreateFromFiles("Assets/Shaders/impostor_bake.vert", "Assets/Shaders/impostor_bake.frag");
	resolveShader.CreateComputeShader("Assets/Shaders/impostor_resolve.glsl");
	if (!Linked(shader) || !Linked(shadowShader) || !Linked(bakeShader) || !Linked(resolveShader)) {
		printf("[Impostor] Shaders failed to build, impostors disabled\n");
		initFailed = true;
		return false;
	}

	// Fixed sampler units. The atlases use 0 and 1; the shadow arrays get the units the scene binds
	// them to (SceneManager), so samplers of different types never share a unit
	GLint prevProgram = 0;
	glGetIntegerv(GL_CURRENT_PROGRAM, &prevProgram);
	for (Shader* s : { &shader, &shadowShader }) {
		GLuint p = s->GetShaderID();
		glUseProgram(p);
		glUniform1i(glGetUniformLocation(p, "impostorAlbedo"), 0);
		glUniform1i(glGetUniformLocation(p, "impostorData"), 1);
		glUniform1i(glGetUniformLocation(p, "impostorMaterial"), MATERIAL_UNIT);
		glUniform1i(glGetUniformLocation(p, "directionalShadowMap"), 3);
		glUniform1i(glGetUniformLocation(p, "directionalShadowColorMap"), 20);
	}
	glUseProgram(bakeShader.GetShaderID());
	glUniform1i(glGetUniformLocation(bakeShader.GetShaderID(), "theTexture"), 0);
	glUseProgram((GLuint)prevProgram);

	// The card, corners in [-1, 1]^2 (Mesh layout: position, uv, normal, tangent, bitangent)
	GLfloat vertices[4 * 14] = {
		-1.0f, -1.0f, 0.0f,  0.0f, 0.0f,  0.0f, 0.0f, 1.0f,  1.0f, 0.0f, 0.0f,  0.0f, 1.0f, 0.0f,
		 1.0f, -1.0f, 0.0f,  1.0f, 0.0f,  0.0f, 0.0f, 1.0f,  1.0f, 0.0f, 0.0f,  0.0f, 1.0f, 0.0f,
		 1.0f,  1.0f, 0.0f,  1.0f, 1.0f,  0.0f, 0.0f, 1.0f,  1.0f, 0.0f, 0.0f,  0.0f, 1.0f, 0.0f,
		-1.0f,  1.0f, 0.0f,  0.0f, 1.0f,  0.0f, 0.0f, 1.0f,  1.0f, 0.0f, 0.0f,  0.0f, 1.0f, 0.0f,
	};
	unsigned int indices[6] = { 0, 1, 2, 2, 3, 0 };
	quad = new Mesh();
	quad->CreateMesh(vertices, indices, 4 * 14, 6);
	quad->SetBounds(glm::vec3(-1.0f, -1.0f, 0.0f), glm::vec3(1.0f, 1.0f, 0.0f));
	quad->AddRef();

	initialized = true;
	return true;
}

void ImpostorSystem::Update(std::vector<InstancedGroup*>& groups, const GraphicsSettings* gs)
{
	bool enabled = !gs || gs->impostors;
	if (enabled && !initialized && !initFailed) Init();
	if (!enabled || !initialized) {
		for (auto* g : groups) if (g) g->ClearImpostor();
		// Switched off: give the video memory back (re-enabling bakes them again)
		for (auto& [key, atlas] : atlases) FreeAtlas(atlas);
		atlases.clear();
		return;
	}
	int resolution = gs ? std::clamp(gs->impostorResolution, 512, 4096) : 2048;

	// A tree is every group of one scatter: same source object, the very same instances
	std::map<std::string, std::vector<InstancedGroup*>> trees;
	for (auto* g : groups) {
		if (!g) continue;
		if (g->GetTotalCount() == 0 || !g->GetMesh() || g->cpuInstances.empty() || g->IsDetailFoliage()) {
			g->ClearImpostor();
			continue;
		}
		const glm::vec4& a = g->cpuInstances.front().positionAndScale;
		const glm::vec4& b = g->cpuInstances.back().positionAndScale;
		char key[160];
		snprintf(key, sizeof(key), "|%u|%.3f %.3f %.3f %.3f|%.3f %.3f %.3f %.3f", g->GetTotalCount(),
			a.x, a.y, a.z, a.w, b.x, b.y, b.z, b.w);
		trees[g->GetSourceObjectName() + key].push_back(g);
	}

	std::set<const ImpostorAtlas*> used;
	bool bakedThisFrame = false;
	for (auto& [treeKey, members] : trees) {
		std::sort(members.begin(), members.end(),
			[](const InstancedGroup* x, const InstancedGroup* y) { return x->GetName() < y->GetName(); });

		// Bounds of the whole tree; its leaves (the biggest cutout group) draw the card
		glm::vec3 bmin(FLT_MAX), bmax(-FLT_MAX);
		InstancedGroup* leaves = nullptr;
		for (auto* g : members) {
			glm::vec3 mn, mx;
			g->GetMesh()->GetBounds(mn, mx);
			bmin = glm::min(bmin, mn);
			bmax = glm::max(bmax, mx);
			if (IsCutout(g) && (!leaves || g->GetMesh()->GetIndexCount() > leaves->GetMesh()->GetIndexCount())) leaves = g;
		}
		glm::vec3 center = (bmin + bmax) * 0.5f;
		float boundsRadius = glm::length(bmax - bmin) * 0.5f;
		auto Skip = [&]() { for (auto* g : members) g->ClearImpostor(); };
		if (!leaves || boundsRadius < MIN_TREE_RADIUS) { Skip(); continue; }

		// What the bake depends on: the LOD0 meshes, their textures and colours, the atlas size
		std::string atlasKey = std::to_string(resolution);
		for (auto* g : members) {
			Material* m = g->GetMaterial();
			Texture* t = SurfaceTexture(g);
			glm::vec3 c = m ? m->GetColorRGB() : glm::vec3(1.0f);
			glm::vec3 mn, mx;
			g->GetMesh()->GetBounds(mn, mx);
			char part[256];
			snprintf(part, sizeof(part), "|%d %u %.3f %.3f %.3f %.3f %.3f %.3f %.3f %.3f %.3f",
				(int)g->GetMesh()->GetIndexCount(), t ? (unsigned)t->GetTextureID() : 0u,
				mn.x, mn.y, mn.z, mx.x, mx.y, mx.z, c.r, c.g, c.b);
			atlasKey += part;
		}
		if (failed.count(atlasKey)) { Skip(); continue; }

		ImpostorAtlas* atlas = nullptr;
		auto it = atlases.find(atlasKey);
		if (it != atlases.end()) {
			atlas = it->second;
		} else {
			// One bake per frame: a scene full of trees loads without one long stall
			if (bakedThisFrame) { Skip(); continue; }
			bakedThisFrame = true;
			atlas = Bake(members, bmin, bmax, resolution);
			if (!atlas) { failed.insert(atlasKey); Skip(); continue; }
			atlas->foliageRadius = leaves->GetMeshBoundRadius();
			atlases[atlasKey] = atlas;
		}
		used.insert(atlas);
		for (auto* g : members) g->SetImpostor(atlas, g == leaves, center, boundsRadius);
	}

	// Trees that are gone (no group refers to these any more: every group was assigned above)
	for (auto it = atlases.begin(); it != atlases.end();) {
		if (used.count(it->second)) { ++it; continue; }
		FreeAtlas(it->second);
		it = atlases.erase(it);
	}
}

// =====================================================================
// Bake — every view of the tree's LOD0 into the atlas
// =====================================================================
ImpostorAtlas* ImpostorSystem::Bake(const std::vector<InstancedGroup*>& members, const glm::vec3& boundsMin,
	const glm::vec3& boundsMax, int resolution)
{
	auto start = std::chrono::high_resolution_clock::now();
	const glm::vec3 center = (boundsMin + boundsMax) * 0.5f;

	// Tight bounding sphere around the centre: the farthest LOD0 vertex (a cone-shaped crown
	// leaves the corners of its box empty, a box-diagonal sphere would waste texels)
	float radius = 0.0f;
	for (auto* g : members) {
		Mesh* mesh = g->GetMesh();
		if (!mesh || !mesh->GetVBO()) continue;
		GLint bytes = 0;
		glBindBuffer(GL_ARRAY_BUFFER, mesh->GetVBO());
		glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &bytes);
		std::vector<float> v(bytes > 0 ? (size_t)bytes / sizeof(float) : 0);
		if (!v.empty()) glGetBufferSubData(GL_ARRAY_BUFFER, 0, v.size() * sizeof(float), v.data());
		for (size_t i = 0; i + 2 < v.size(); i += 14)
			radius = std::max(radius, glm::length(glm::vec3(v[i], v[i + 1], v[i + 2]) - center));
	}
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	if (radius <= 0.0f) return nullptr;
	radius *= FRAME_MARGIN;

	const int view = std::max(resolution / FRAMES, 16);
	const int size = view * FRAMES;
	const int ssView = view * SUPERSAMPLE;
	int levels = 1;
	while ((view >> levels) >= 2) levels++; // Down to views of 2 texels

	// --- GL state of the caller ---
	GLint prevDraw = 0, prevRead = 0, prevProgram = 0, prevVAO = 0, prevActive = 0, prevDepthFunc = 0;
	GLint prevViewport[4] = { 0, 0, 0, 0 };
	GLfloat prevClear[4] = { 0, 0, 0, 0 };
	GLboolean prevDepthMask = GL_TRUE;
	glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevDraw);
	glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevRead);
	glGetIntegerv(GL_CURRENT_PROGRAM, &prevProgram);
	glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVAO);
	glGetIntegerv(GL_ACTIVE_TEXTURE, &prevActive);
	glGetIntegerv(GL_DEPTH_FUNC, &prevDepthFunc);
	glGetIntegerv(GL_VIEWPORT, prevViewport);
	glGetFloatv(GL_COLOR_CLEAR_VALUE, prevClear);
	glGetBooleanv(GL_DEPTH_WRITEMASK, &prevDepthMask);
	GLboolean prevDepthTest = glIsEnabled(GL_DEPTH_TEST), prevCull = glIsEnabled(GL_CULL_FACE);
	GLboolean prevBlend = glIsEnabled(GL_BLEND), prevScissor = glIsEnabled(GL_SCISSOR_TEST);
	GLboolean prevClip = glIsEnabled(GL_CLIP_DISTANCE0);

	// --- Targets: one supersampled row of views, and the atlas it is filtered into ---
	auto MakeTarget = [](int w, int h, GLenum format) {
		GLuint t = 0;
		glGenTextures(1, &t);
		glBindTexture(GL_TEXTURE_2D, t);
		glTexStorage2D(GL_TEXTURE_2D, 1, format, w, h);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		return t;
	};
	const int rowW = FRAMES * ssView, rowH = ssView;
	GLuint rowTex[3] = { MakeTarget(rowW, rowH, GL_RGBA8), MakeTarget(rowW, rowH, GL_RGBA8), MakeTarget(rowW, rowH, GL_R8) };
	GLuint rowDepth = 0, rowFBO = 0, atlasFBO = 0;
	glGenRenderbuffers(1, &rowDepth);
	glBindRenderbuffer(GL_RENDERBUFFER, rowDepth);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, rowW, rowH);
	glBindRenderbuffer(GL_RENDERBUFFER, 0);

	glGenFramebuffers(1, &rowFBO);
	glBindFramebuffer(GL_FRAMEBUFFER, rowFBO);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, rowTex[0], 0);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, rowTex[1], 0);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2, GL_TEXTURE_2D, rowTex[2], 0);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, rowDepth);
	bool complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;

	// The views go into premultiplied atlases (RGBA16F: the deep mips divide small coverages)
	GLuint premult[3] = { MakeAtlasTexture(GL_RGBA16F, size, levels), MakeAtlasTexture(GL_RGBA16F, size, levels),
		MakeAtlasTexture(GL_R16F, size, levels) };
	glGenFramebuffers(1, &atlasFBO);
	glBindFramebuffer(GL_FRAMEBUFFER, atlasFBO);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, premult[0], 0);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, premult[1], 0);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2, GL_TEXTURE_2D, premult[2], 0);
	complete = complete && glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;

	ImpostorAtlas* atlas = nullptr;
	if (complete) {
		GLuint p = bakeShader.GetShaderID();
		glUseProgram(p);
		GLint locRight = glGetUniformLocation(p, "frameRight"), locUp = glGetUniformLocation(p, "frameUp");
		GLint locDir = glGetUniformLocation(p, "frameDir");
		GLint locTiling = glGetUniformLocation(p, "uvTiling"), locOffset = glGetUniformLocation(p, "uvOffset");
		GLint locUseTex = glGetUniformLocation(p, "useTexture"), locColor = glGetUniformLocation(p, "baseColor");
		GLint locFoliage = glGetUniformLocation(p, "foliage");
		glUniform3fv(glGetUniformLocation(p, "frameCenter"), 1, glm::value_ptr(center));
		glUniform1f(glGetUniformLocation(p, "frameRadius"), radius);

		glEnable(GL_DEPTH_TEST);
		glDepthFunc(GL_LESS);
		glDepthMask(GL_TRUE);
		glDisable(GL_CULL_FACE);   // Leaf cards are double-sided
		glDisable(GL_BLEND);
		glDisable(GL_SCISSOR_TEST);
		glDisable(GL_CLIP_DISTANCE0);
		glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
		glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
		const GLenum allTargets[3] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2 };

		for (int j = 0; j < FRAMES; j++) {
			glBindFramebuffer(GL_FRAMEBUFFER, rowFBO);
			glDrawBuffers(3, allTargets);
			glViewport(0, 0, rowW, rowH);
			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

			for (int i = 0; i < FRAMES; i++) {
				glViewport(i * ssView, 0, ssView, ssView);
				glm::vec3 dir = HemiOctDecode(glm::vec2((float)i, (float)j) / float(FRAMES - 1) * 2.0f - 1.0f);
				glm::vec3 right, up;
				FrameBasis(dir, right, up);
				glUniform3fv(locRight, 1, glm::value_ptr(right));
				glUniform3fv(locUp, 1, glm::value_ptr(up));
				glUniform3fv(locDir, 1, glm::value_ptr(dir));

				for (auto* g : members) {
					Mesh* mesh = g->GetMesh();
					if (!mesh) continue;
					Material* mat = g->GetMaterial();
					Texture* tex = SurfaceTexture(g);
					glm::vec3 color = mat ? mat->GetColorRGB() : glm::vec3(1.0f);
					glm::vec2 tiling = mat ? mat->GetTiling() : glm::vec2(1.0f);
					glm::vec2 offset = mat ? mat->GetOffset() : glm::vec2(0.0f);
					if (tiling == glm::vec2(0.0f)) tiling = glm::vec2(1.0f);
					if (!g->GetTextureLayers().empty()) { tiling = glm::vec2(1.0f); offset = glm::vec2(0.0f); }
					glUniform2fv(locTiling, 1, glm::value_ptr(tiling));
					glUniform2fv(locOffset, 1, glm::value_ptr(offset));
					glUniform3fv(locColor, 1, glm::value_ptr(color));
					glUniform1i(locUseTex, tex ? 1 : 0);
					glUniform1f(locFoliage, IsCutout(g) ? 1.0f : 0.0f); // Leaves vs bark: their own lighting
					if (tex) {
						glActiveTexture(GL_TEXTURE0);
						glBindTexture(GL_TEXTURE_2D, tex->GetTextureID());
					}
					glBindVertexArray(mesh->GetVAO());
					glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh->GetIBO());
					glDrawElements(GL_TRIANGLES, mesh->GetIndexCount(), GL_UNSIGNED_INT, nullptr);
				}
			}

			// 2x2 box filter of the row into its place in the atlas (premultiplied by coverage)
			glBindFramebuffer(GL_READ_FRAMEBUFFER, rowFBO);
			glBindFramebuffer(GL_DRAW_FRAMEBUFFER, atlasFBO);
			for (int a = 0; a < 3; a++) {
				glReadBuffer(GL_COLOR_ATTACHMENT0 + a);
				glDrawBuffer(GL_COLOR_ATTACHMENT0 + a);
				glBlitFramebuffer(0, 0, rowW, rowH, 0, j * view, size, (j + 1) * view, GL_COLOR_BUFFER_BIT, GL_LINEAR);
			}
		}
		glBindVertexArray(0);
		glBindFramebuffer(GL_FRAMEBUFFER, 0);

		// Premultiplied pyramid, then each level divided out from the coarsest up (push-pull)
		for (GLuint t : premult) {
			glBindTexture(GL_TEXTURE_2D, t);
			glGenerateMipmap(GL_TEXTURE_2D);
		}
		atlas = new ImpostorAtlas();
		atlas->albedo = MakeAtlasTexture(GL_RGBA8, size, levels);
		atlas->data = MakeAtlasTexture(GL_RGBA8, size, levels);
		atlas->material = MakeAtlasTexture(GL_R8, size, levels);

		GLuint rp = resolveShader.GetShaderID();
		glUseProgram(rp);
		GLint locLevel = glGetUniformLocation(rp, "level");
		GLint locCoarser = glGetUniformLocation(rp, "hasCoarser");
		GLint locSize = glGetUniformLocation(rp, "levelSize");
		const GLuint samplers[6] = { premult[0], premult[1], atlas->albedo, atlas->data, premult[2], atlas->material };
		for (int u = 0; u < 6; u++) {
			glActiveTexture(GL_TEXTURE0 + u);
			glBindTexture(GL_TEXTURE_2D, samplers[u]);
		}
		for (int l = levels - 1; l >= 0; l--) {
			int s = size >> l;
			glUniform1i(locLevel, l);
			glUniform1i(locCoarser, l + 1 < levels ? 1 : 0);
			glUniform2i(locSize, s, s);
			glBindImageTexture(0, atlas->albedo, l, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);
			glBindImageTexture(1, atlas->data, l, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);
			glBindImageTexture(2, atlas->material, l, GL_FALSE, 0, GL_WRITE_ONLY, GL_R8);
			glDispatchCompute((s + 7) / 8, (s + 7) / 8, 1);
			glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
		}
		glBindImageTexture(0, 0, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);
		glBindImageTexture(1, 0, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);
		glBindImageTexture(2, 0, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_R8);
		for (int u = 5; u >= 0; u--) {
			glActiveTexture(GL_TEXTURE0 + u);
			glBindTexture(GL_TEXTURE_2D, 0);
		}
		if (GLEW_EXT_texture_filter_anisotropic) {
			for (GLuint t : { atlas->albedo, atlas->data, atlas->material }) {
				glBindTexture(GL_TEXTURE_2D, t);
				glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, 4.0f);
			}
			glBindTexture(GL_TEXTURE_2D, 0);
		}

		// Did anything land in the views? (the coarsest level is 16 x 16 texels)
		int top = size >> (levels - 1);
		std::vector<float> coarsest((size_t)top * top * 4, 0.0f);
		glBindTexture(GL_TEXTURE_2D, premult[0]);
		glGetTexImage(GL_TEXTURE_2D, levels - 1, GL_RGBA, GL_FLOAT, coarsest.data());
		glBindTexture(GL_TEXTURE_2D, 0);
		float coverage = 0.0f;
		for (size_t i = 3; i < coarsest.size(); i += 4) coverage += coarsest[i];
		if (coverage <= 0.0f) {
			printf("[Impostor] Bake of '%s' produced an empty atlas\n", members.front()->GetSourceObjectName().c_str());
			FreeAtlas(atlas);
			atlas = nullptr;
		}
	}

	glDeleteFramebuffers(1, &rowFBO);
	glDeleteFramebuffers(1, &atlasFBO);
	glDeleteRenderbuffers(1, &rowDepth);
	glDeleteTextures(3, rowTex);
	glDeleteTextures(3, premult);

	// --- Restore ---
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)prevDraw);
	glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)prevRead);
	glUseProgram((GLuint)prevProgram);
	glBindVertexArray((GLuint)prevVAO);
	glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
	glClearColor(prevClear[0], prevClear[1], prevClear[2], prevClear[3]);
	glDepthMask(prevDepthMask);
	glDepthFunc((GLenum)prevDepthFunc);
	if (prevDepthTest) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
	if (prevCull) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
	if (prevBlend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
	if (prevScissor) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
	if (prevClip) glEnable(GL_CLIP_DISTANCE0); else glDisable(GL_CLIP_DISTANCE0);
	glActiveTexture((GLenum)prevActive);

	if (!complete) {
		printf("[Impostor] Bake framebuffer incomplete\n");
		return nullptr;
	}
	if (!atlas) return nullptr;

	atlas->frames = FRAMES;
	atlas->resolution = size;
	atlas->center = center;
	atlas->radius = radius;
	atlas->boundsMin = boundsMin;
	atlas->boundsMax = boundsMax;
	atlas->quad = quad;
	atlas->shader = &shader;
	atlas->shadowShader = &shadowShader;

	double ms = std::chrono::duration<double, std::milli>(std::chrono::high_resolution_clock::now() - start).count();
	printf("[Impostor] Baked '%s': %d x %d views of %d px, radius %.2f m, %.0f ms\n",
		members.front()->GetSourceObjectName().c_str(), FRAMES, FRAMES, view, radius, ms);
	return atlas;
}
