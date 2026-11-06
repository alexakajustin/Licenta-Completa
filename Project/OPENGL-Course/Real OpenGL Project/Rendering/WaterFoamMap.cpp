#include "Rendering/WaterFoamMap.h"

#include <glm/gtc/type_ptr.hpp>
#include <algorithm>

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
static const int UNIT_SIM_VELOCITY = 42;
static const int UNIT_SIM_WAVE = 43;
static const int UNIT_SIM_FOAM = 44;
static const int UNIT_RIPPLE = 45;
static const int RIPPLE_SIZE = 512;
static const int PRESSURE_ITERATIONS = 24;
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
	simShader.CreateComputeShader(am->GetShaderPath("water_sim.glsl").c_str());
	rippleShader.CreateComputeShader(am->GetShaderPath("water_ripple_normal.glsl").c_str());
	if (!surfaceShader.GetShaderID() || !obstacleShader.GetShaderID() || !obstacleInstancedShader.GetShaderID() ||
		!maskShader.GetShaderID() || !patternShader.GetShaderID() || !simShader.GetShaderID() || !rippleShader.GetShaderID()) {
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

	// Water simulation: the ground's top height (highest wins: depth test), then the fields
	groundTex = CreateTexture(GL_R32F, GL_NEAREST, RESOLUTION);
	glGenRenderbuffers(1, &groundDepth);
	glBindRenderbuffer(GL_RENDERBUFFER, groundDepth);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT32F, RESOLUTION, RESOLUTION);
	glGenFramebuffers(1, &groundFBO);
	glBindFramebuffer(GL_FRAMEBUFFER, groundFBO);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, groundTex, 0);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, groundDepth);
	domainTex = CreateTexture(GL_RGBA16F, GL_NEAREST, RESOLUTION);
	velTmpTex = CreateTexture(GL_RG16F, GL_NEAREST, RESOLUTION);
	curlTex = CreateTexture(GL_R16F, GL_LINEAR, RESOLUTION);
	divTex = CreateTexture(GL_R16F, GL_NEAREST, RESOLUTION);
	const float zero[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	for (int i = 0; i < 2; i++) {
		velTex[i] = CreateTexture(GL_RG16F, GL_LINEAR, RESOLUTION);
		pressureTex[i] = CreateTexture(GL_R16F, GL_NEAREST, RESOLUTION);
		waveTex[i] = CreateTexture(GL_RG16F, GL_LINEAR, RESOLUTION);
		simFoamTex[i] = CreateTexture(GL_R16F, GL_LINEAR, RESOLUTION);
		GLuint fields[] = { velTex[i], pressureTex[i], waveTex[i], simFoamTex[i] };
		for (GLuint t : fields) glClearTexImage(t, 0, GL_RGBA, GL_FLOAT, zero);
	}

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

	// Ripple slopes, generated once (tileable, mipmapped: far away they average out)
	glGenTextures(1, &rippleTex);
	glBindTexture(GL_TEXTURE_2D, rippleTex);
	glTexStorage2D(GL_TEXTURE_2D, 10, GL_RG16F, RIPPLE_SIZE, RIPPLE_SIZE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	if (GLEW_EXT_texture_filter_anisotropic) glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, 8.0f);
	rippleShader.UseShader();
	glUniform1i(glGetUniformLocation(rippleShader.GetShaderID(), "size"), RIPPLE_SIZE);
	glBindImageTexture(0, rippleTex, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RG16F);
	glDispatchCompute(RIPPLE_SIZE / 8, RIPPLE_SIZE / 8, 1);
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
	GLuint textures[] = { surfaceTex, obstacleTex, proximityTex, foamTex, patternTex, rippleTex };
	for (GLuint t : textures) if (t) glDeleteTextures(1, &t);
	surfaceFBO = obstacleFBO = surfaceDepth = 0;
	surfaceTex = obstacleTex = proximityTex = foamTex = patternTex = rippleTex = 0;
	if (groundFBO) glDeleteFramebuffers(1, &groundFBO);
	if (groundDepth) glDeleteRenderbuffers(1, &groundDepth);
	GLuint sim[] = { groundTex, domainTex, velTmpTex, curlTex, divTex, velTex[0], velTex[1], pressureTex[0], pressureTex[1],
		waveTex[0], waveTex[1], simFoamTex[0], simFoamTex[1] };
	for (GLuint t : sim) if (t) glDeleteTextures(1, &t);
	groundFBO = groundDepth = groundTex = domainTex = velTmpTex = curlTex = divTex = 0;
	for (int i = 0; i < 2; i++) velTex[i] = pressureTex[i] = waveTex[i] = simFoamTex[i] = 0;
}

void WaterFoamMap::Update(const std::vector<GameObject*>& waterObjects, const std::vector<GameObject*>& obstacleObjects,
	const std::vector<GameObject*>& groundObjects,
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
	// Snapped to 8 m steps (whole texels): the map does not shimmer, and the top-down passes are
	// redrawn only when the window steps (the simulation carries its fields across the step)
	const float WINDOW_STEP = 8.0f;
	glm::vec2 centre = glm::floor(glm::vec2(cameraPos.x, cameraPos.z) / WINDOW_STEP) * WINDOW_STEP;
	origin = centre - glm::vec2(WINDOW * 0.5f);
	glm::mat4 viewProj = TopDownMatrix(centre, WINDOW * 0.5f);

	glViewport(0, 0, RESOLUTION, RESOLUTION);
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	glDisable(GL_BLEND);
	glDisable(GL_CULL_FACE);
	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);

	// The top-down passes only when something can have changed: the window moved, the scene's
	// object lists changed, or every REBUILD_INTERVAL seconds (objects moved in the editor)
	size_t signature = waterObjects.size() * 73856093u ^ obstacleObjects.size() * 19349663u ^ groundObjects.size() * 83492791u ^ groups.size() * 2654435761u;
	for (InstancedGroup* g : groups) if (g) signature = signature * 31u + g->GetTotalCount();
	bool rebuild = !passesValid || origin != passesOrigin || signature != passesSignature
		|| time < passesTime || time - passesTime > REBUILD_INTERVAL;
	if (rebuild) {
	passesValid = true;
	passesOrigin = origin;
	passesSignature = signature;
	passesTime = time;

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
	GLint speedLoc = glGetUniformLocation(sid, "speedScale");
	for (GameObject* obj : waterObjects) {
		glm::mat4 model = obj->GetWorldMatrix();
		glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(model));
		glUniform1i(riverLoc, IsRiverMaterial(obj) ? 1 : 0);
		float speedScale = 1.0f;
		if (Material* m = obj->GetMaterial()) {
			auto it = m->GetFloats().find("material_waveSpeed");
			if (it != m->GetFloats().end() && it->second > 0.0f) speedScale = it->second;
		}
		glUniform1f(speedLoc, speedScale);
		DrawGeometry(obj);
	}

	// 1b. The ground under the water (terrains), highest point per texel: banks and dry land
	// are walls of the simulation, the depth below the surface stills its waves
	glBindFramebuffer(GL_FRAMEBUFFER, groundFBO);
	glClearColor(-1.0e6f, 0.0f, 0.0f, 0.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	glUniform1i(riverLoc, 0);
	for (GameObject* obj : groundObjects) {
		glm::mat4 model = obj->GetWorldMatrix();
		glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(model));
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
		pass.settings = gs;
		pass.projScale = 1.732f; // 60 degree camera
		pass.maxDrawDistance = WINDOW * 0.75f;
		pass.lodMin = MAX_LOD_LEVELS - 1;
		pass.minCasterSize = texel * 2.0f; // Twigs thinner than half a metre do not part a river
		for (InstancedGroup* group : groups) {
			if (!group) continue;
			// Grass is filtered out of the water by the scatter filters and is by far the
			// largest group: not worth a top-down pass every frame
			Material* m = group->GetMaterial();
			if (m && m->GetShader() && m->GetShader()->GetVertexPath().find("grass") != std::string::npos) continue;
			// Leaves, ferns, reeds (alpha-tested cards) do not stop the current: only solid objects
			Texture* gt = group->GetTexture();
			if (gt && gt->HasCutoutAlpha()) continue;
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

	} // rebuild

	// 4. The water simulation, one step of the frame's time
	float dt = lastTime < 0.0f ? 0.0f : std::clamp(time - lastTime, 0.0f, 1.0f / 20.0f);
	if (lastTime >= 0.0f && (time - lastTime > 0.5f || time < lastTime)) simHistory = false; // A pause / a jump
	lastTime = time;
	Simulate(dt);

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

void WaterFoamMap::Simulate(float dt)
{
	const float texel = WINDOW / RESOLUTION;
	// Last frame's window moved by more than half its size: nothing carries over
	if (glm::length(origin - prevOrigin) > WINDOW * 0.5f) simHistory = false;
	int prev = cur, next = 1 - cur;

	simShader.UseShader();
	GLuint id = simShader.GetShaderID();
	auto U = [&](const char* n) { return glGetUniformLocation(id, n); };
	glUniform1i(U("size"), RESOLUTION);
	glUniform1f(U("dx"), texel);
	glUniform1f(U("dt"), dt);
	glUniform1f(U("time"), lastTime);
	glUniform2fv(U("origin"), 1, glm::value_ptr(origin));
	glUniform2fv(U("prevOrigin"), 1, glm::value_ptr(simHistory ? prevOrigin : origin));
	glUniform1i(U("reset"), simHistory ? 0 : 1);
	GLint modeLoc = U("mode");
	GLuint groups = (RESOLUTION + 7) / 8;
	auto Bind = [&](int unit, GLuint tex) { glActiveTexture(GL_TEXTURE0 + unit); glBindTexture(GL_TEXTURE_2D, tex); };
	auto Run = [&](int mode, GLuint out, GLenum format, int imageUnit) {
		glUniform1i(modeLoc, mode);
		glBindImageTexture(imageUnit, out, 0, GL_FALSE, 0, GL_WRITE_ONLY, format);
		glDispatchCompute(groups, groups, 1);
		glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
	};

	// 0. Domain: water / walls, the river's current, the depth
	Bind(0, surfaceTex); Bind(1, groundTex); Bind(2, obstacleTex);
	Run(0, domainTex, GL_RGBA16F, 0);
	// 1. Curl of last frame's current
	Bind(0, velTex[prev]);
	Run(1, curlTex, GL_R16F, 2);
	// 2. The current carried along itself, driven by the river, eddies kept
	Bind(0, velTex[prev]); Bind(1, domainTex); Bind(2, curlTex);
	Run(2, velTmpTex, GL_RG16F, 1);
	// 3-5. Made divergence-free around the walls (pressure: Jacobi, warm-started)
	Bind(0, velTmpTex);
	Run(3, divTex, GL_R16F, 2);
	Bind(2, divTex);
	int p = 0;
	for (int i = 0; i < PRESSURE_ITERATIONS; i++) {
		Bind(0, pressureTex[p]);
		Run(4, pressureTex[1 - p], GL_R16F, 2);
		p = 1 - p;
	}
	if (p != 0) std::swap(pressureTex[0], pressureTex[1]); // The solution stays in [0] for next frame
	Bind(0, velTmpTex); Bind(2, pressureTex[0]);
	Run(5, velTex[next], GL_RG16F, 1);
	// 6. The surface, in two half steps (the second carries the first's result, from this
	//    frame's window)
	Bind(0, waveTex[prev]); Bind(1, domainTex); Bind(2, velTex[next]); Bind(4, curlTex);
	glUniform1f(U("dt"), dt * 0.5f);
	Run(6, waveTex[next], GL_RG16F, 1);
	glUniform2fv(U("prevOrigin"), 1, glm::value_ptr(origin));
	glUniform1i(U("reset"), 0);
	Bind(0, waveTex[next]);
	Run(6, waveTex[prev], GL_RG16F, 1);
	std::swap(waveTex[prev], waveTex[next]); // waveTex[next] holds the result
	glUniform1f(U("dt"), dt);
	glUniform2fv(U("prevOrigin"), 1, glm::value_ptr(simHistory ? prevOrigin : origin));
	glUniform1i(U("reset"), simHistory ? 0 : 1);
	// 7. Foam carried by the current
	Bind(0, simFoamTex[prev]); Bind(2, velTex[next]); Bind(3, waveTex[next]); Bind(4, curlTex);
	Run(7, simFoamTex[next], GL_R16F, 2);

	for (int u = 0; u < 5; u++) Bind(u, 0);
	glActiveTexture(GL_TEXTURE0);
	cur = next;
	prevOrigin = origin;
	simHistory = true;
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
	GLint rippleLoc = glGetUniformLocation(shaderID, "waterRipples");
	if (rippleLoc != -1 && rippleTex) {
		glActiveTexture(GL_TEXTURE0 + UNIT_RIPPLE);
		glBindTexture(GL_TEXTURE_2D, rippleTex);
		glUniform1i(rippleLoc, UNIT_RIPPLE);
	}
	// The simulated water: current, surface, foam
	GLint simLoc = glGetUniformLocation(shaderID, "waterSimVelocity");
	if (simLoc != -1) {
		glActiveTexture(GL_TEXTURE0 + UNIT_SIM_VELOCITY);
		glBindTexture(GL_TEXTURE_2D, velTex[cur]);
		glUniform1i(simLoc, UNIT_SIM_VELOCITY);
	}
	GLint waveLoc = glGetUniformLocation(shaderID, "waterSimWave");
	if (waveLoc != -1) {
		glActiveTexture(GL_TEXTURE0 + UNIT_SIM_WAVE);
		glBindTexture(GL_TEXTURE_2D, waveTex[cur]);
		glUniform1i(waveLoc, UNIT_SIM_WAVE);
	}
	GLint simFoamLoc = glGetUniformLocation(shaderID, "waterSimFoam");
	if (simFoamLoc != -1) {
		glActiveTexture(GL_TEXTURE0 + UNIT_SIM_FOAM);
		glBindTexture(GL_TEXTURE_2D, simFoamTex[cur]);
		glUniform1i(simFoamLoc, UNIT_SIM_FOAM);
	}
	glUniform2fv(glGetUniformLocation(shaderID, "waterFoamOrigin"), 1, glm::value_ptr(origin));
	glUniform1f(glGetUniformLocation(shaderID, "waterFoamSize"), valid ? WINDOW : 0.0f);
	glActiveTexture(GL_TEXTURE0);
}
