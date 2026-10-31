#include "Rendering/WaterFoamMap.h"

#include <glm/gtc/type_ptr.hpp>

#include "Core/AssetManager.h"
#include "Core/ServiceLocator.h"
#include "GraphicsSettings.h"
#include "Rendering/InstancedGroup.h"
#include "Rendering/Material.h"
#include "Rendering/Mesh.h"
#include "Rendering/Model.h"
#include "Scene/GameObject.h"

// Texture units used while building the map and when the water shaders sample it. Chosen
// above every unit the renderer binds for lighting, layers (up to 33) and clouds (40-41).
static const int UNIT_SURFACE = 36;
static const int UNIT_FOAM_MAP = 37;
static const int UNIT_PATTERN = 38;
static const int UNIT_PROXIMITY = 39;
static const int PATTERN_SIZE = 512;

// Top-down orthographic window: world x -> clip x, world z -> clip y, and the higher a point
// the nearer it is (the depth test keeps the top surface where two overlap). Heights within
// +-5000 m fit the depth range.
static glm::mat4 TopDownMatrix(const glm::vec2& centre, float halfSize)
{
	glm::mat4 m(0.0f);
	m[0][0] = 1.0f / halfSize;
	m[3][0] = -centre.x / halfSize;
	m[2][1] = 1.0f / halfSize;
	m[3][1] = -centre.y / halfSize;
	m[1][2] = -1.0f / 5000.0f;
	m[3][3] = 1.0f;
	return m;
}

static GLuint CreateTexture(GLenum internalFormat, GLenum filter, int size)
{
	GLuint tex = 0;
	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	glTexStorage2D(GL_TEXTURE_2D, 1, internalFormat, size, size);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	return tex;
}

static bool IsRiverMaterial(GameObject* obj)
{
	Material* m = obj->GetMaterial();
	if (!m || !m->GetShader()) return false;
	return m->GetShader()->GetVertexPath().find("river.vert") != std::string::npos;
}

static void DrawGeometry(GameObject* obj)
{
	if (obj->GetMesh()) obj->GetMesh()->RenderMesh();
	else if (obj->GetModel()) obj->GetModel()->RenderModelGeometryOnly();
}

WaterFoamMap::~WaterFoamMap()
{
	Release();
}

bool WaterFoamMap::Init()
{
	initialised = true;
	auto am = ServiceLocator::GetAssetManager();
	surfaceShader.CreateFromFiles(am->GetShaderPath("water_foam_surface.vert").c_str(), am->GetShaderPath("water_foam_surface.frag").c_str());
	obstacleShader.CreateFromFiles(am->GetShaderPath("water_foam_obstacle.vert").c_str(), am->GetShaderPath("water_foam_obstacle.frag").c_str());
	obstacleInstancedShader.CreateFromFiles(am->GetShaderPath("water_foam_obstacle_instanced.vert").c_str(), am->GetShaderPath("water_foam_obstacle.frag").c_str());
	maskShader.CreateComputeShader(am->GetShaderPath("water_foam_mask.glsl").c_str());
	patternShader.CreateComputeShader(am->GetShaderPath("water_foam_pattern.glsl").c_str());
	if (!surfaceShader.GetShaderID() || !obstacleShader.GetShaderID() || !obstacleInstancedShader.GetShaderID() ||
		!maskShader.GetShaderID() || !patternShader.GetShaderID()) {
		printf("[WaterFoamMap] Shader creation failed, obstacle foam disabled\n");
		failed = true;
		return false;
	}

	// Water surfaces: height + flow, with depth so the top surface wins where two overlap
	surfaceTex = CreateTexture(GL_RGBA32F, GL_NEAREST, RESOLUTION);
	glGenRenderbuffers(1, &surfaceDepth);
	glBindRenderbuffer(GL_RENDERBUFFER, surfaceDepth);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT32F, RESOLUTION, RESOLUTION);
	glGenFramebuffers(1, &surfaceFBO);
	glBindFramebuffer(GL_FRAMEBUFFER, surfaceFBO);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, surfaceTex, 0);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, surfaceDepth);

	obstacleTex = CreateTexture(GL_R8, GL_NEAREST, RESOLUTION);
	glGenFramebuffers(1, &obstacleFBO);
	glBindFramebuffer(GL_FRAMEBUFFER, obstacleFBO);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, obstacleTex, 0);

	proximityTex = CreateTexture(GL_R16F, GL_LINEAR, RESOLUTION);
	foamTex = CreateTexture(GL_R16F, GL_LINEAR, RESOLUTION);

	// Lacy foam pattern, generated once (tileable, mipmapped)
	glGenTextures(1, &patternTex);
	glBindTexture(GL_TEXTURE_2D, patternTex);
	glTexStorage2D(GL_TEXTURE_2D, 10, GL_RGBA8, PATTERN_SIZE, PATTERN_SIZE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	if (GLEW_EXT_texture_filter_anisotropic) glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, 8.0f);
	patternShader.UseShader();
	glUniform1i(glGetUniformLocation(patternShader.GetShaderID(), "size"), PATTERN_SIZE);
	glBindImageTexture(0, patternTex, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);
	glDispatchCompute(PATTERN_SIZE / 8, PATTERN_SIZE / 8, 1);
	glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
	glGenerateMipmap(GL_TEXTURE_2D);

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glBindTexture(GL_TEXTURE_2D, 0);
	glUseProgram(0);
	return true;
}

void WaterFoamMap::Release()
{
	if (surfaceFBO) glDeleteFramebuffers(1, &surfaceFBO);
	if (obstacleFBO) glDeleteFramebuffers(1, &obstacleFBO);
	if (surfaceDepth) glDeleteRenderbuffers(1, &surfaceDepth);
	GLuint textures[] = { surfaceTex, obstacleTex, proximityTex, foamTex, patternTex };
	for (GLuint t : textures) if (t) glDeleteTextures(1, &t);
	surfaceFBO = obstacleFBO = surfaceDepth = 0;
	surfaceTex = obstacleTex = proximityTex = foamTex = patternTex = 0;
}

void WaterFoamMap::Update(const std::vector<GameObject*>& waterObjects, const std::vector<GameObject*>& obstacleObjects,
	const std::vector<InstancedGroup*>& groups, GLuint cullShaderID,
	const glm::vec3& cameraPos, const GraphicsSettings* gs, float time)
{
	valid = false;
	if (waterObjects.empty() || failed) return;
	if (!initialised && !Init()) return;

	// Save the state this pass changes
	GLint prevDrawFBO = 0, prevReadFBO = 0, prevProgram = 0, prevViewport[4] = { 0, 0, 0, 0 };
	glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevDrawFBO);
	glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevReadFBO);
	glGetIntegerv(GL_CURRENT_PROGRAM, &prevProgram);
	glGetIntegerv(GL_VIEWPORT, prevViewport);
	GLboolean blendWas = glIsEnabled(GL_BLEND), depthTestWas = glIsEnabled(GL_DEPTH_TEST), cullWas = glIsEnabled(GL_CULL_FACE);
	GLboolean depthMaskWas = GL_TRUE;
	glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMaskWas);
	GLfloat clearWas[4];
	glGetFloatv(GL_COLOR_CLEAR_VALUE, clearWas);
	GLint depthFuncWas = GL_LESS, polygonModeWas[2] = { GL_FILL, GL_FILL };
	glGetIntegerv(GL_DEPTH_FUNC, &depthFuncWas);
	glGetIntegerv(GL_POLYGON_MODE, polygonModeWas);

	const float texel = WINDOW / RESOLUTION;
	// Snap to whole texels so the map does not shimmer as the camera moves
	glm::vec2 centre = glm::floor(glm::vec2(cameraPos.x, cameraPos.z) / texel) * texel;
	origin = centre - glm::vec2(WINDOW * 0.5f);
	glm::mat4 viewProj = TopDownMatrix(centre, WINDOW * 0.5f);

	glViewport(0, 0, RESOLUTION, RESOLUTION);
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	glDisable(GL_BLEND);
	glDisable(GL_CULL_FACE);
	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);

	// 1. Water surfaces: height + flow direction
	glBindFramebuffer(GL_FRAMEBUFFER, surfaceFBO);
	glEnable(GL_DEPTH_TEST);
	glDepthMask(GL_TRUE);
	glDepthFunc(GL_LESS);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	surfaceShader.UseShader();
	GLuint sid = surfaceShader.GetShaderID();
	glUniformMatrix4fv(glGetUniformLocation(sid, "foamViewProj"), 1, GL_FALSE, glm::value_ptr(viewProj));
	GLint modelLoc = glGetUniformLocation(sid, "model");
	GLint riverLoc = glGetUniformLocation(sid, "isRiver");
	for (GameObject* obj : waterObjects) {
		glm::mat4 model = obj->GetWorldMatrix();
		glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(model));
		glUniform1i(riverLoc, IsRiverMaterial(obj) ? 1 : 0);
		DrawGeometry(obj);
	}

	// 2. Obstacles: everything crossing the water surface (no depth test: all fragments count)
	glBindFramebuffer(GL_FRAMEBUFFER, obstacleFBO);
	glDisable(GL_DEPTH_TEST);
	glDepthMask(GL_FALSE);
	glClear(GL_COLOR_BUFFER_BIT);
	glActiveTexture(GL_TEXTURE0 + UNIT_SURFACE);
	glBindTexture(GL_TEXTURE_2D, surfaceTex);

	auto bindWaterUniforms = [&](Shader& shader) {
		shader.UseShader();
		GLuint id = shader.GetShaderID();
		glUniform1i(glGetUniformLocation(id, "waterSurface"), UNIT_SURFACE);
		glUniform2fv(glGetUniformLocation(id, "foamOrigin"), 1, glm::value_ptr(origin));
		glUniform1f(glGetUniformLocation(id, "foamSize"), WINDOW);
		glUniform1i(glGetUniformLocation(id, "useDiffuseTexture"), 0);
		return id;
	};

	GLuint oid = bindWaterUniforms(obstacleShader);
	glUniformMatrix4fv(glGetUniformLocation(oid, "directionalLightTransform"), 1, GL_FALSE, glm::value_ptr(viewProj));
	modelLoc = glGetUniformLocation(oid, "model");
	for (GameObject* obj : obstacleObjects) {
		glm::mat4 model = obj->GetWorldMatrix();
		glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(model));
		DrawGeometry(obj);
	}

	if (cullShaderID != 0 && !groups.empty()) {
		bindWaterUniforms(obstacleInstancedShader);
		// Top-down pass over the window (corners included). The mask has 25 cm texels and only
		// needs the outline where an obstacle crosses the water surface: the cheapest LOD does,
		// and anything thinner than a texel is skipped
		InstancedGroup::CullPass pass;
		pass.viewProj = viewProj;
		pass.cameraPos = cameraPos;
		pass.lodProjScale = 1.732f * (gs ? gs->lodBias : 1.0f); // 60 degree camera
		pass.maxDrawDistance = WINDOW * 0.75f;
		pass.lodMin = MAX_LOD_LEVELS - 1;
		pass.minCasterSize = texel;
		for (InstancedGroup* group : groups) {
			if (!group) continue;
			// Grass is filtered out of the water by the scatter filters and is by far the
			// largest group: not worth a top-down pass every frame
			Material* m = group->GetMaterial();
			if (m && m->GetShader() && m->GetShader()->GetVertexPath().find("grass") != std::string::npos) continue;
			group->CullAndDrawShadow(cullShaderID, obstacleInstancedShader, pass, viewProj, time);
		}
		glDisable(GL_CULL_FACE); // CullAndDrawShadow re-enables it
	}

	// 3. Foam mask: soft ring around obstacles, then smeared downstream
	maskShader.UseShader();
	GLuint mid = maskShader.GetShaderID();
	glUniform1i(glGetUniformLocation(mid, "size"), RESOLUTION);
	glUniform1f(glGetUniformLocation(mid, "texelWorld"), texel);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, obstacleTex);
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, surfaceTex);
	GLuint groupsXY = (RESOLUTION + 7) / 8;

	glUniform1i(glGetUniformLocation(mid, "mode"), 0);
	glBindImageTexture(0, proximityTex, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_R16F);
	glDispatchCompute(groupsXY, groupsXY, 1);
	glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

	glActiveTexture(GL_TEXTURE2);
	glBindTexture(GL_TEXTURE_2D, proximityTex);
	glUniform1i(glGetUniformLocation(mid, "mode"), 1);
	glBindImageTexture(0, foamTex, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_R16F);
	glDispatchCompute(groupsXY, groupsXY, 1);
	glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
	glActiveTexture(GL_TEXTURE0);

	// Restore
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prevDrawFBO);
	glBindFramebuffer(GL_READ_FRAMEBUFFER, prevReadFBO);
	glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
	if (blendWas) glEnable(GL_BLEND); else glDisable(GL_BLEND);
	if (depthTestWas) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
	if (cullWas) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
	glDepthMask(depthMaskWas);
	glDepthFunc(depthFuncWas);
	glPolygonMode(GL_FRONT_AND_BACK, polygonModeWas[0]);
	glClearColor(clearWas[0], clearWas[1], clearWas[2], clearWas[3]);
	glUseProgram(prevProgram);
	valid = true;
}

void WaterFoamMap::Bind(GLuint shaderID) const
{
	GLint patternLoc = glGetUniformLocation(shaderID, "waterFoamPattern");
	if (patternLoc == -1 || !patternTex) return;
	glActiveTexture(GL_TEXTURE0 + UNIT_PATTERN);
	glBindTexture(GL_TEXTURE_2D, patternTex);
	glUniform1i(patternLoc, UNIT_PATTERN);

	glActiveTexture(GL_TEXTURE0 + UNIT_FOAM_MAP);
	glBindTexture(GL_TEXTURE_2D, foamTex);
	glUniform1i(glGetUniformLocation(shaderID, "waterFoamMap"), UNIT_FOAM_MAP);
	// Rivers steer their current around obstacles along the gradient of the proximity field
	GLint proximityLoc = glGetUniformLocation(shaderID, "waterFoamProximity");
	if (proximityLoc != -1) {
		glActiveTexture(GL_TEXTURE0 + UNIT_PROXIMITY);
		glBindTexture(GL_TEXTURE_2D, proximityTex);
		glUniform1i(proximityLoc, UNIT_PROXIMITY);
	}
	glUniform2fv(glGetUniformLocation(shaderID, "waterFoamOrigin"), 1, glm::value_ptr(origin));
	glUniform1f(glGetUniformLocation(shaderID, "waterFoamSize"), valid ? WINDOW : 0.0f);
	glActiveTexture(GL_TEXTURE0);
}
