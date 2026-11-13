#ifdef RUN_UNIT_TESTS
#define _CRT_SECURE_NO_WARNINGS
// Temporary test (not part of the repo): two frames of a real scene a moment apart
#include "External Libs/doctest.h"
#include "External Libs/nlohmann/json.hpp"
#include "Scene/SceneManager.h"
#include "Scene/SceneSerializer.h"
#include "Scene/GameObject.h"
#include "Rendering/Renderer.h"
#include "Rendering/Texture.h"
#include "Rendering/Material.h"
#include "Rendering/TemporalAA.h"
#include "Rendering/GlobalIllumination.h"
#include "Rendering/CloudShadows.h"
#include "Rendering/VolumetricFog.h"
#include "Rendering/PostProcess.h"
#include "Rendering/AmbientOcclusion.h"
#include "Lighting/DirectionalLight.h"
#include "Lighting/PointLight.h"
#include "Lighting/SpotLight.h"
#include "Core/Camera.h"
#include "Rendering/Shader.h"
#include <glm/gtc/type_ptr.hpp>
#include "GraphicsSettings.h"
#include "CommonValues.h"
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <vector>
#include <algorithm>

static void SavePFM(const char* path, const std::vector<float>& rgb, int w, int h)
{
	FILE* fp = fopen(path, "wb"); fprintf(fp, "PF\n%d %d\n-1.0\n", w, h);
	fwrite(rgb.data(), 4, rgb.size(), fp); fclose(fp);
}

TEST_CASE("GI scenes")
{
	setvbuf(stdout, nullptr, _IONBF, 0);
	const char* scenePath = getenv("GI_SCENE");
	const char* outDir = getenv("GI_OUT");
	if (!scenePath || !outDir) return;
	const char* tag = getenv("GI_TAG") ? getenv("GI_TAG") : "scene";
	const int W = 1280, H = 720;

	GraphicsSettings gs;
	const bool full = getenv("GI_FULL") != nullptr;	// The engine's whole frame, with every setting of its own
	if (full) {
		std::ifstream f(getenv("GI_SETTINGS") ? getenv("GI_SETTINGS") : "graphics_settings.json");
		nlohmann::json j;
		REQUIRE(f.is_open());
		f >> j;
#include "GISettings_tmp.inl"
		if (const char* o = getenv("GI_ATMO")) sscanf(o, "%f %f %f", &gs.atmosphereVisibility, &gs.aerialPerspectiveScale, &gs.skyIntensity);
		if (getenv("GI_NOATMO")) gs.physicalAtmosphere = false;
		if (const char* c = getenv("GI_CLOUDS")) sscanf(c, "%f %f", &gs.cloudsDensity, &gs.cloudsLayer);
		if (getenv("GI_NOGODRAYS")) gs.godraysEnabled = false;
		if (getenv("GI_NOFOG")) gs.volumetricFogEnabled = false;
	} else {
		std::ifstream f("graphics_settings.json");
		nlohmann::json j;
		if (f.is_open()) f >> j;
		auto B = [&](const char* k, bool& v) { if (j.contains(k)) v = j[k]; };
		auto F = [&](const char* k, float& v) { if (j.contains(k)) v = j[k]; };
		B("volumetricSkyEnabled", gs.volumetricSkyEnabled);
		B("impostors", gs.impostors);
		F("shadowDistance", gs.shadowDistance);
		F("renderDistance", gs.renderDistance);
		F("detailDistance", gs.detailDistance);
		F("detailDensity", gs.detailDensity);
		F("waterDetail", gs.waterDetail);
		B("physicalAtmosphere", gs.physicalAtmosphere); F("atmosphereVisibility", gs.atmosphereVisibility);
		F("aerialPerspectiveScale", gs.aerialPerspectiveScale); F("skyIntensity", gs.skyIntensity);
		if (const char* o = getenv("GI_ATMO")) sscanf(o, "%f %f %f", &gs.atmosphereVisibility, &gs.aerialPerspectiveScale, &gs.skyIntensity);
		if (getenv("GI_NOATMO")) gs.physicalAtmosphere = false;
		F("cloudsDensity", gs.cloudsDensity); F("cloudsLayer", gs.cloudsLayer);
		if (const char* c = getenv("GI_CLOUDS")) sscanf(c, "%f %f", &gs.cloudsDensity, &gs.cloudsLayer); F("cloudsSharpness", gs.cloudsSharpness); F("cloudsSpeed", gs.cloudsSpeed);
		B("cloudsEnabled", gs.cloudsEnabled);
		auto V = [&](const char* k, glm::vec3& v) { if (j.contains(k)) v = glm::vec3(j[k][0], j[k][1], j[k][2]); };
		V("zenithDay", gs.zenithDay); V("horizonDay", gs.horizonDay); V("zenithSunset", gs.zenithSunset);
		V("horizonSunset", gs.horizonSunset); V("zenithNight", gs.zenithNight); V("horizonNight", gs.horizonNight);
	}
	gs.enableOcclusionCulling = false;

	Renderer renderer;
	renderer.Init();
	renderer.LoadSkybox({ "Assets/Textures/Skybox_Cute/px.png", "Assets/Textures/Skybox_Cute/nx.png", "Assets/Textures/Skybox_Cute/py.png",
		"Assets/Textures/Skybox_Cute/ny.png", "Assets/Textures/Skybox_Cute/pz.png", "Assets/Textures/Skybox_Cute/nz.png" });
	SceneManager scene;
	scene.SetMainShader(&renderer.GetMainShader());
	scene.SetGraphicsSettings(&gs);
	Texture plain("Assets/Textures/plain.png");
	plain.LoadTexture();
	Material mat(0.1f, 32.0f);
	scene.SetDefaultResources(&plain, &mat);
	DirectionalLight light(2048, 2048, 1.0f, 1.0f, 1.0f, 0.4f, 0.6f, -20.0f, -5.0f, 7.0f);
	PointLight pointLights[MAX_POINT_LIGHTS];
	SpotLight spotLights[MAX_SPOT_LIGHTS];
	unsigned int pc = 0, sc = 0;
	Camera camera(glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f), -90.0f, 0.0f, 5.0f, 0.5f);
	REQUIRE(SceneSerializer::LoadScene(scenePath, scene, light, pointLights, pc, spotLights, sc, &plain, &mat, &camera));
	if (const char* sun = getenv("GI_SUN")) {	// "elevation azimuth" in degrees: the direction toward the sun
		float el, az; sscanf(sun, "%f %f", &el, &az);
		glm::vec3 to(std::cos(glm::radians(el)) * std::cos(glm::radians(az)), std::sin(glm::radians(el)), std::cos(glm::radians(el)) * std::sin(glm::radians(az)));
		light.SetDirection(-to);
	}
	if (const char* sc = getenv("GI_SUNCOLOR")) { glm::vec3& c = *light.GetColourPtr(); sscanf(sc, "%f %f %f", &c.x, &c.y, &c.z); }
	if (const char* li = getenv("GI_LIGHT")) {	// "ambient diffuse r g b": the sun's intensities and colour
		glm::vec3& c = *light.GetColourPtr();
		sscanf(li, "%f %f %f %f %f", light.GetAmbientIntensityPtr(), light.GetDiffuseIntensityPtr(), &c.x, &c.y, &c.z);
	}
	if (const char* ex = getenv("GI_EXEC")) {
		double tp = glfwGetTime();
		scene.ExecutePipeline(&plain, &mat);
		printf("[GIS] pipeline %.1f s\n", glfwGetTime() - tp);
		for (auto& tab : scene.GetGraphTabs())
			for (GraphNode* n : tab.graph->GetNodes()) {
				if (n->status.ms > 200.0) printf("[GIS]   %s / %s: %.2f s\n", tab.name.c_str(), n->title.c_str(), n->status.ms / 1000.0);
				if (n->status.info.find("rivers:") != std::string::npos) printf("[GIS] river node: %s\n", n->status.info.c_str());
			}
		if (GameObject* t = scene.FindObject("Plane"))
			printf("[GIS] terrain: %zu vertices, %zu triangles\n", t->GetCPUMeshData().vertices.size() / 14, t->GetCPUMeshData().indices.size() / 3);
		if (std::string(ex) == "only") return;
	}
	if (getenv("GI_LISTWATER")) {
		for (auto& o : scene.GetObjects()) {
			Material* mm = o->GetMaterial();
			if (!mm || !mm->GetShader()) continue;
			const std::string& vp = mm->GetShader()->GetVertexPath();
			if (vp.find("river.vert") == std::string::npos && vp.find("water.vert") == std::string::npos) continue;
			glm::vec3 b0, b1; o->GetWorldBounds(b0, b1);
			printf("[GIS] water %s: (%.0f %.0f %.0f) - (%.0f %.0f %.0f)\n", o->GetName().c_str(), b0.x, b0.y, b0.z, b1.x, b1.y, b1.z);
			const auto& v = o->GetCPUMeshData().vertices;
			size_t n = v.size() / 14;
			glm::mat4 m = o->GetWorldMatrix();
			for (int s = 0; s < 16 && n; s++) {
				size_t i = (n * s / 16) * 14;
				glm::vec4 w = m * glm::vec4(v[i], v[i + 1], v[i + 2], 1.0f);
				printf("[GIS]   %.0f %.0f %.0f\n", w.x, w.y, w.z);
			}
		}
		return;
	}
	if (const char* name = getenv("GI_DUMPTERRAIN")) {	// The generated terrain: x, y, z per vertex
		GameObject* t = scene.FindObject(name);
		REQUIRE(t != nullptr);
		const auto& v = t->GetCPUMeshData().vertices;
		glm::mat4 m = t->GetWorldMatrix();
		std::vector<float> xyz;
		for (size_t i = 0; i + 13 < v.size(); i += 14) {
			glm::vec4 w = m * glm::vec4(v[i], v[i + 1], v[i + 2], 1.0f);
			xyz.push_back(w.x); xyz.push_back(w.y); xyz.push_back(w.z);
		}
		char path[512]; snprintf(path, sizeof(path), "%s/%s_terrain.bin", outDir, tag);
		FILE* fp = fopen(path, "wb"); fwrite(xyz.data(), 4, xyz.size(), fp); fclose(fp);
		printf("[GIS] %s: terrain dumped (%zu vertices)\n", tag, xyz.size() / 3);
		// The river surface: x, z, u (across the channel), y, v per vertex
		if (GameObject* r = scene.FindObject("River_Water_467")) {
			const auto& rv = r->GetCPUMeshData().vertices;
			glm::mat4 rm = r->GetWorldMatrix();
			std::vector<float> xzu;
			for (size_t i = 0; i + 13 < rv.size(); i += 14) {
				glm::vec4 w = rm * glm::vec4(rv[i], rv[i + 1], rv[i + 2], 1.0f);
				xzu.push_back(w.x); xzu.push_back(w.z); xzu.push_back(rv[i + 3]); xzu.push_back(w.y); xzu.push_back(rv[i + 4]);
			}
			snprintf(path, sizeof(path), "%s/%s_river.bin", outDir, tag);
			fp = fopen(path, "wb"); fwrite(xzu.data(), 4, xzu.size(), fp); fclose(fp);
		}
		if (!getenv("GI_CAMS")) return;
	}
	if (const char* layerStr = getenv("GI_MASKTEST")) {	// Layer N hidden west of x = 20 (mask check)
		GameObject* t = scene.FindObject("Plane");
		REQUIRE(t != nullptr);
		glm::vec3 bmin, bmax; t->GetWorldBounds(bmin, bmax);
		float size = std::max(bmax.x - bmin.x, bmax.z - bmin.z);
		const int res = 256;
		std::vector<float> m((size_t)res * res);
		for (int k = 0; k < res; k++) for (int i = 0; i < res; i++) m[(size_t)k * res + i] = (bmin.x + i * size / (res - 1)) > 20.0f ? 1.0f : 0.0f;
		t->SetLayerMask(atoi(layerStr), m, res, bmin, size);
	}
	glm::vec3 camPos = camera.getCameraPosition();
	glm::mat4 view = camera.calculateViewMatrix();
	if (const char* s = getenv("GI_CAM")) {
		float x, y, z, yaw, pitch; sscanf(s, "%f %f %f %f %f", &x, &y, &z, &yaw, &pitch);
		Camera c(glm::vec3(x, y, z), glm::vec3(0.0f, 1.0f, 0.0f), yaw, pitch, 5.0f, 0.5f);
		camPos = c.getCameraPosition();
		view = c.calculateViewMatrix();
	}
	glm::mat4 proj = glm::perspective(glm::radians(60.0f), (float)W / H, 0.1f, 20000.0f);

	GLuint fbo, col, dep, refraction;
	glGenFramebuffers(1, &fbo); glGenTextures(1, &col); glGenTextures(1, &dep); glGenTextures(1, &refraction);
	glBindTexture(GL_TEXTURE_2D, col); glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, W, H, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glBindTexture(GL_TEXTURE_2D, dep); glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, W, H, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glBindTexture(GL_TEXTURE_2D, refraction); glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, W, H, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, col, 0);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, dep, 0);
	REQUIRE(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);

	GLuint cloudUbo; glGenBuffers(1, &cloudUbo);
	std::vector<char> zeros(20000, 0);
	glBindBuffer(GL_UNIFORM_BUFFER, cloudUbo); glBufferData(GL_UNIFORM_BUFFER, zeros.size(), zeros.data(), GL_STATIC_DRAW);
	glBindBufferBase(GL_UNIFORM_BUFFER, CLOUD_SHADOW_UBO_BINDING, cloudUbo);

	// The sky behind the scene (Application's full-screen sky pass, at full resolution, no history)
	Shader noiseCS; noiseCS.CreateComputeShader("Assets/Shaders/cloud_noise.glsl");
	GLuint noiseTex[2] = { 0, 0 };
	for (int m = 0; m < 2; m++) {
		int size = m == 0 ? 128 : 64;
		glGenTextures(1, &noiseTex[m]); glBindTexture(GL_TEXTURE_3D, noiseTex[m]);
		glTexStorage3D(GL_TEXTURE_3D, 1 + (int)std::floor(std::log2((float)size)), GL_RGBA8, size, size, size);
		glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
		glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_REPEAT); glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_REPEAT); glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_REPEAT);
		noiseCS.UseShader();
		glUniform1i(glGetUniformLocation(noiseCS.GetShaderID(), "mode"), m);
		glUniform1i(glGetUniformLocation(noiseCS.GetShaderID(), "size"), size);
		glBindImageTexture(0, noiseTex[m], 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA8);
		glDispatchCompute((size + 3) / 4, (size + 3) / 4, (size + 3) / 4);
		glMemoryBarrier(GL_ALL_BARRIER_BITS);
		glGenerateMipmap(GL_TEXTURE_3D);
	}
	renderer.SetCloudNoise(noiseTex[0], noiseTex[1]);
	Shader skyShader; skyShader.CreateFromFiles("Assets/Shaders/volumetric_sky.vert", "Assets/Shaders/volumetric_sky.frag");
	GLuint quadVAO, quadVBO;
	{
		float qv[] = { -1, 1, 0, 0, 1, -1, -1, 0, 0, 0, 1, 1, 0, 1, 1, 1, -1, 0, 1, 0 };
		glGenVertexArrays(1, &quadVAO); glGenBuffers(1, &quadVBO); glBindVertexArray(quadVAO); glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
		glBufferData(GL_ARRAY_BUFFER, sizeof(qv), qv, GL_STATIC_DRAW);
		glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
		glEnableVertexAttribArray(1); glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
		glBindVertexArray(0);
	}
	int skyFrame = 0;
	float simTime = 100.0f;
	auto SkyUniforms = [&]() {
		skyShader.UseShader();
		GLuint id = skyShader.GetShaderID();
		glm::mat4 invProj = glm::inverse(proj), invView = glm::inverse(view);
		glUniformMatrix4fv(glGetUniformLocation(id, "invProjection"), 1, GL_FALSE, glm::value_ptr(invProj));
		glUniformMatrix4fv(glGetUniformLocation(id, "invView"), 1, GL_FALSE, glm::value_ptr(invView));
		glm::vec3 toSun = -glm::normalize(*light.GetDirectionPtr());
		glUniform3fv(glGetUniformLocation(id, "sunDir"), 1, glm::value_ptr(toSun));
		glUniform3fv(glGetUniformLocation(id, "sunColor"), 1, glm::value_ptr(*light.GetColourPtr()));
		glUniform1f(glGetUniformLocation(id, "time"), simTime);
		glUniform1i(glGetUniformLocation(id, "jitterFrame"), skyFrame++);
		glUniform1i(glGetUniformLocation(id, "cloudsEnabled"), gs.cloudsEnabled ? 1 : 0);
		glUniform1f(glGetUniformLocation(id, "cloudsDensity"), gs.cloudsDensity);
		glUniform1f(glGetUniformLocation(id, "cloudsLayer"), gs.cloudsLayer);
		glUniform1f(glGetUniformLocation(id, "cloudsSpeed"), gs.cloudsSpeed);
		glUniform1f(glGetUniformLocation(id, "cloudsSharpness"), gs.cloudsSharpness);
		glUniform3fv(glGetUniformLocation(id, "zenithDay"), 1, glm::value_ptr(gs.zenithDay));
		glUniform3fv(glGetUniformLocation(id, "horizonDay"), 1, glm::value_ptr(gs.horizonDay));
		glUniform3fv(glGetUniformLocation(id, "zenithSunset"), 1, glm::value_ptr(gs.zenithSunset));
		glUniform3fv(glGetUniformLocation(id, "horizonSunset"), 1, glm::value_ptr(gs.horizonSunset));
		glUniform3fv(glGetUniformLocation(id, "zenithNight"), 1, glm::value_ptr(gs.zenithNight));
		glUniform3fv(glGetUniformLocation(id, "horizonNight"), 1, glm::value_ptr(gs.horizonNight));
		glUniform1i(glGetUniformLocation(id, "physicalAtmosphere"), gs.physicalAtmosphere ? 1 : 0);
		glUniform3fv(glGetUniformLocation(id, "cameraWorldPos"), 1, glm::value_ptr(camPos));
		renderer.GetAtmosphere().Bind(id);
		glActiveTexture(GL_TEXTURE0 + 40); glBindTexture(GL_TEXTURE_3D, noiseTex[0]); glUniform1i(glGetUniformLocation(id, "cloudShapeNoise"), 40);
		glActiveTexture(GL_TEXTURE0 + 41); glBindTexture(GL_TEXTURE_3D, noiseTex[1]); glUniform1i(glGetUniformLocation(id, "cloudDetailNoise"), 41);
		glActiveTexture(GL_TEXTURE0);
	};
	auto Quad = [&]() { glBindVertexArray(quadVAO); glDrawArrays(GL_TRIANGLE_STRIP, 0, 4); glBindVertexArray(0); };
	auto DrawSky = [&]() {
		SkyUniforms();
		glDisable(GL_DEPTH_TEST); glDisable(GL_BLEND); glDepthMask(GL_FALSE);
		Quad();
		glDepthMask(GL_TRUE); glEnable(GL_DEPTH_TEST);
	};
	TemporalAA taa; taa.Init();
	GlobalIllumination gi; gi.Init();

	// ---- The engine's whole frame (GI_FULL): Application's passes in its order, down to the
	// tone-mapped viewport. Fixed time step, so two runs go through the same clouds and fog.
	CloudShadowMap cloudShadows; AmbientOcclusion ao; VolumetricFog fog; PostProcess post;
	Shader skyTemporal, skyUpsample, godray;
	GLuint skyHalfFBO = 0, skyHalfTex = 0, skyHistFBO[2] = { 0, 0 }, skyHistTex[2] = { 0, 0 };
	int skyHistIndex = 0; bool skyHistValid = false; glm::mat4 skyPrevVPR(1.0f);
	GLuint lastHdr = 0;
	if (full) {
		cloudShadows.Init(); ao.Init(); post.Init();
		skyTemporal.CreateFromFiles("Assets/Shaders/volumetric_sky.vert", "Assets/Shaders/sky_temporal.frag");
		skyUpsample.CreateFromFiles("Assets/Shaders/volumetric_sky.vert", "Assets/Shaders/sky_upsample.frag");
		godray.CreateFromFiles("Assets/Shaders/godrays.vert", "Assets/Shaders/godrays.frag");
		auto alloc = [&](GLuint& f, GLuint& t) {
			glGenFramebuffers(1, &f); glGenTextures(1, &t);
			glBindTexture(GL_TEXTURE_2D, t);
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, W / 2, H / 2, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
			glBindFramebuffer(GL_FRAMEBUFFER, f);
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t, 0);
		};
		alloc(skyHalfFBO, skyHalfTex); alloc(skyHistFBO[0], skyHistTex[0]); alloc(skyHistFBO[1], skyHistTex[1]);
		glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	}
	// Application's renderSky: the half-resolution march, its temporal accumulation, the upscale
	auto FullSky = [&]() {
		glDisable(GL_DEPTH_TEST); glDisable(GL_BLEND);
		glm::mat4 invProj = glm::inverse(proj), invView = glm::inverse(view);
		SkyUniforms();
		glBindFramebuffer(GL_FRAMEBUFFER, skyHalfFBO); glViewport(0, 0, W / 2, H / 2);
		Quad();
		int read = skyHistIndex; skyHistIndex = 1 - skyHistIndex;
		glBindFramebuffer(GL_FRAMEBUFFER, skyHistFBO[skyHistIndex]);
		skyTemporal.UseShader();
		GLuint tid = skyTemporal.GetShaderID();
		glUniformMatrix4fv(glGetUniformLocation(tid, "invProjection"), 1, GL_FALSE, glm::value_ptr(invProj));
		glUniformMatrix4fv(glGetUniformLocation(tid, "invView"), 1, GL_FALSE, glm::value_ptr(invView));
		glUniformMatrix4fv(glGetUniformLocation(tid, "prevViewProjRot"), 1, GL_FALSE, glm::value_ptr(skyPrevVPR));
		glUniform1f(glGetUniformLocation(tid, "historyWeight"), skyHistValid ? 0.9f : 0.0f);
		glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, skyHalfTex); glUniform1i(glGetUniformLocation(tid, "currentSky"), 0);
		glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, skyHistTex[read]); glUniform1i(glGetUniformLocation(tid, "historySky"), 1);
		Quad();
		skyPrevVPR = proj * glm::mat4(glm::mat3(view)); skyHistValid = true;
		glBindFramebuffer(GL_FRAMEBUFFER, fbo); glViewport(0, 0, W, H);
		skyUpsample.UseShader();
		GLuint uid = skyUpsample.GetShaderID();
		glUniformMatrix4fv(glGetUniformLocation(uid, "invProjection"), 1, GL_FALSE, glm::value_ptr(invProj));
		glUniformMatrix4fv(glGetUniformLocation(uid, "invView"), 1, GL_FALSE, glm::value_ptr(invView));
		glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, skyHistTex[skyHistIndex]); glUniform1i(glGetUniformLocation(uid, "skyTexture"), 0);
		Quad();
		glEnable(GL_DEPTH_TEST);
	};
	// The app's planar reflection at the lake's height (Application: InitReflectionFBO + its reflection pass)
	GLuint reflFBO = 0, reflTex = 0, reflDep = 0;
	const int RW = 960, RH = 540;
	const float lakeHeight = getenv("GI_LAKE") ? (float)atof(getenv("GI_LAKE")) : 261.0f;
	if (full) {
		glGenFramebuffers(1, &reflFBO); glBindFramebuffer(GL_FRAMEBUFFER, reflFBO);
		glGenTextures(1, &reflTex); glBindTexture(GL_TEXTURE_2D, reflTex);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, RW, RH, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, reflTex, 0);
		glGenTextures(1, &reflDep); glBindTexture(GL_TEXTURE_2D, reflDep);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, RW, RH, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, reflDep, 0);
		glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	}
	auto FullFrame = [&]() {
		const float dt = 1.0f / 60.0f;
		simTime += dt;
		glfwSetTime(simTime);	// The renderer's clock (water, wind) runs at the fixed step too
		scene.UpdateImpostors(&gs);
		renderer.UpdateWind(&gs, dt);
		glm::mat4 sceneProj = taa.BeginFrame(proj, view, W, H, gs.taaEnabled, fbo);
		renderer.DirectionalShadowMapPass(&light, scene, camPos, proj, view, 0.1f, gs.shadowDistance, &gs);
		glm::vec3 toSun = -glm::normalize(*light.GetDirectionPtr());
		cloudShadows.Update(camPos, toSun, gs, noiseTex[0], noiseTex[1], simTime, Quad);
		renderer.UpdateAtmosphere(&gs, light, camPos);
		gi.Update(scene, camPos, view, light, pointLights, pc, spotLights, sc, gs, renderer.GetCullShaderID(),
			gs.volumetricSkyEnabled ? renderer.GetProceduralSkyTextureID() : renderer.GetSkybox().GetTextureID());
		if (!getenv("GI_NOPLANAR")) {
			glBindFramebuffer(GL_FRAMEBUFFER, reflFBO); glViewport(0, 0, RW, RH);
			glClearColor(0.1f, 0.1f, 0.1f, 1.0f); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
			if (gs.volumetricSkyEnabled) {
				glm::mat4 rm(1.0f); rm[1][1] = -1.0f; rm[3][1] = 2.0f * lakeHeight;
				glm::mat4 keep = view; view = view * rm;
				SkyUniforms();
				glUniform1i(glGetUniformLocation(skyShader.GetShaderID(), "jitterFrame"), 0);
				view = keep;
				glDisable(GL_DEPTH_TEST); glDisable(GL_BLEND); Quad(); glEnable(GL_DEPTH_TEST);
			}
			renderer.ReflectionPass(proj, view, camPos, scene, light, pointLights, pc, spotLights, sc, RW, RH, lakeHeight, &gs);
			glBindTexture(GL_TEXTURE_2D, reflTex); glGenerateMipmap(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D, 0);
			scene.SetReflectionDepthTexture(reflDep); scene.SetReflectionPlaneHeight(lakeHeight);
		}
		glBindFramebuffer(GL_FRAMEBUFFER, fbo);
		glViewport(0, 0, W, H);
		glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		if (gs.volumetricSkyEnabled) FullSky();
		renderer.RenderPass(sceneProj, view, camPos, scene, light, pointLights, pc, spotLights, sc, W, H, dep, getenv("GI_NOPLANAR") ? 0 : reflTex, refraction, nullptr, &gs);
		if (gs.aoEnabled) ao.Render(fbo, dep, W, H, proj, gs, Quad);
		if (Renderer::AtmosphereLit(&gs)) {
			glBindFramebuffer(GL_FRAMEBUFFER, fbo); glViewport(0, 0, W, H);
			renderer.GetAtmosphere().ApplyAerialPerspective(dep, proj, view, camPos, gs);
		}
		if (gs.volumetricFogEnabled) {
			glBindFramebuffer(GL_FRAMEBUFFER, fbo); glViewport(0, 0, W, H);
			fog.Render(proj, view, camPos, light, dep, W, H, gs, simTime);
		}
		if (gs.godraysEnabled) {	// Application's god ray pass, as it is there
			glBindFramebuffer(GL_FRAMEBUFFER, fbo); glViewport(0, 0, W, H);
			glDisable(GL_DEPTH_TEST); glEnable(GL_BLEND); glBlendFunc(GL_ONE, GL_ONE);
			godray.UseShader();
			GLuint g = godray.GetShaderID();
			glm::mat4 invProj = glm::inverse(proj), invView = glm::inverse(view);
			glUniformMatrix4fv(glGetUniformLocation(g, "projection"), 1, GL_FALSE, glm::value_ptr(proj));
			glUniformMatrix4fv(glGetUniformLocation(g, "view"), 1, GL_FALSE, glm::value_ptr(view));
			glUniformMatrix4fv(glGetUniformLocation(g, "invProjection"), 1, GL_FALSE, glm::value_ptr(invProj));
			glUniformMatrix4fv(glGetUniformLocation(g, "invView"), 1, GL_FALSE, glm::value_ptr(invView));
			glUniform3fv(glGetUniformLocation(g, "sunDir"), 1, glm::value_ptr(toSun));
			glm::vec3 godraySun = light.GetRenderColour();
			glUniform3fv(glGetUniformLocation(g, "sunColor"), 1, glm::value_ptr(godraySun));
			float sunIntensity = *light.GetAmbientIntensityPtr() + *light.GetDiffuseIntensityPtr();
			glUniform1f(glGetUniformLocation(g, "exposure"), sunIntensity * 0.036f);
			glUniform1f(glGetUniformLocation(g, "decay"), gs.godraysDecay);
			glUniform1f(glGetUniformLocation(g, "density"), gs.godraysDensity);
			glUniform1f(glGetUniformLocation(g, "weight"), sunIntensity * 0.014f);
			glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, dep); glUniform1i(glGetUniformLocation(g, "depthMap"), 0);
			bool cloudMask = gs.volumetricSkyEnabled && gs.cloudsEnabled && gs.skyboxType == SkyboxType::Atmospheric && skyHistValid;
			glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, cloudMask ? skyHistTex[skyHistIndex] : 0);
			glUniform1i(glGetUniformLocation(g, "cloudMask"), 1);
			glUniform1i(glGetUniformLocation(g, "useCloudMask"), cloudMask ? 1 : 0);
			glActiveTexture(GL_TEXTURE0);
			Quad();
			glDisable(GL_BLEND); glEnable(GL_DEPTH_TEST);
		}
		GLuint hdr = 0;
		if (gs.taaEnabled) hdr = taa.Resolve(col, dep, W, H, Quad);
		post.SetExtraExposure(renderer.GetAtmosphereExposure());
		post.Apply(hdr, col, fbo, W, H, gs, dt, hdr ? gs.taaSharpness : 0.0f, Quad);
		lastHdr = hdr ? hdr : post.GetScratch();
	};
	GLuint q; glGenQueries(1, &q);
	double best = 1e9;
	auto Frame = [&]() {
		gs.giEnabled = true;
		scene.UpdateImpostors(&gs);
		renderer.UpdateWind(&gs, 0.016f);
		taa.BeginFrame(proj, view, W, H, true, 0);
		renderer.DirectionalShadowMapPass(&light, scene, camPos, proj, view, 0.1f, gs.shadowDistance, &gs);
		gi.Update(scene, camPos, view, light, pointLights, pc, spotLights, sc, gs, renderer.GetCullShaderID(),
			gs.volumetricSkyEnabled ? renderer.GetProceduralSkyTextureID() : renderer.GetSkybox().GetTextureID());
		glBindFramebuffer(GL_FRAMEBUFFER, fbo);
		glViewport(0, 0, W, H);
		glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		renderer.UpdateAtmosphere(&gs, light, camPos);
		glBindFramebuffer(GL_FRAMEBUFFER, fbo);
		glViewport(0, 0, W, H);
		if (gs.volumetricSkyEnabled) DrawSky();
		glBeginQuery(GL_TIME_ELAPSED, q);
		renderer.RenderPass(proj, view, camPos, scene, light, pointLights, pc, spotLights, sc, W, H, dep, 0, refraction, nullptr, &gs);
		glEndQuery(GL_TIME_ELAPSED);
		if (gs.volumetricSkyEnabled && gs.physicalAtmosphere) {
			glBindFramebuffer(GL_FRAMEBUFFER, fbo);
			glViewport(0, 0, W, H);
			renderer.GetAtmosphere().ApplyAerialPerspective(dep, proj, view, camPos, gs);
		}
		GLuint64 ns = 0; glGetQueryObjectui64v(q, GL_QUERY_RESULT, &ns);
		best = std::min(best, ns / 1e6);
	};
	auto SaveTex = [&](GLuint tex, const char* name, int w, int h, bool alpha) {
		std::vector<float> px((size_t)w * h * 4), rgb((size_t)w * h * 3);
		glBindTexture(GL_TEXTURE_2D, tex);
		glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT, px.data());
		for (size_t i = 0; i < (size_t)w * h; i++) for (int c = 0; c < 3; c++) rgb[i * 3 + c] = alpha ? px[i * 4 + 3] : px[i * 4 + c];
		char path[512]; snprintf(path, sizeof(path), "%s/%s_%s.pfm", outDir, tag, name);
		SavePFM(path, rgb, w, h);
	};
	auto Save = [&](const char* name) { SaveTex(col, name, W, H, false); };
	// The whole frame: NAME (HDR, before exposure), NAME_ldr (the viewport as shown), NAME_mask (cloud transmittance)
	auto SaveFull = [&](const char* name) {
		std::string n = name;
		SaveTex(lastHdr, n.c_str(), W, H, false);
		SaveTex(col, (n + "_ldr").c_str(), W, H, false);
		SaveTex(skyHistTex[skyHistIndex], (n + "_mask").c_str(), W / 2, H / 2, true);
	};
	if (full && !getenv("GI_CAMS")) {	// The scene's own camera
		for (int f = 0; f < 240; f++) FullFrame();
		SaveFull("c0");
		glm::vec3 tint = light.GetRenderColour(), amb = light.GetAmbientRenderColour();
		printf("[GIS] %s: full frame, exposure +%.2f EV, sun render colour (%.3f %.3f %.3f), ambient (%.3f %.3f %.3f)\n", tag,
			renderer.GetAtmosphereExposure(), tint.x, tint.y, tint.z, amb.x, amb.y, amb.z);
		gi.Shutdown(); taa.Shutdown();
		return;
	}
	if (const char* step = getenv("GI_STEP")) {	// "ambient|sun value": settle, change it, then the screen's mean per frame
		char what[16]; float v; sscanf(step, "%15s %f", what, &v);
		if (const char* s = getenv("GI_CAM")) { float x, y, z, yaw, pitch; sscanf(s, "%f %f %f %f %f", &x, &y, &z, &yaw, &pitch);
			Camera c(glm::vec3(x, y, z), glm::vec3(0.0f, 1.0f, 0.0f), yaw, pitch, 5.0f, 0.5f); camPos = c.getCameraPosition(); view = c.calculateViewMatrix(); }
		std::vector<float> px((size_t)W * H * 4);
		auto Mean = [&]() {
			glBindTexture(GL_TEXTURE_2D, col); glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT, px.data());
			double s2 = 0; size_t n = 0;
			for (int y = 0; y < H * 2 / 5; y++) for (int x = 0; x < W; x++) { size_t i = ((size_t)y * W + x) * 4; s2 += 0.2126 * px[i] + 0.7152 * px[i + 1] + 0.0722 * px[i + 2]; n++; }
			return s2 / n; // Bottom 40 % of the image: the ground
		};
		for (int f = 0; f < 200; f++) Frame();
		printf("[GIS] before %.4f\n", Mean());
		if (std::string(what) == "ambient") *light.GetAmbientIntensityPtr() = v;
		else { glm::vec3 d = *light.GetDirectionPtr(); float el = glm::radians(v); glm::vec2 h = glm::normalize(glm::vec2(d.x, d.z));
			light.SetDirection(-glm::vec3(-h.x * std::cos(el), std::sin(el), -h.y * std::cos(el))); }
		for (int f = 0; f < 240; f++) { Frame(); if (f < 10 || f % 10 == 0) printf("[GIS] frame %d %.4f\n", f, Mean()); }
		return;
	}
	if (const char* cams = getenv("GI_CAMS")) {	// "x y z yaw pitch;x y z yaw pitch;...": one frame each, TAG_cN
		std::string all = cams;
		int k = 0;
		for (size_t s = 0; s < all.size();) {
			size_t e = all.find(';', s); if (e == std::string::npos) e = all.size();
			float x, y, z, yaw, pitch, sel, saz;
			int got = sscanf(all.substr(s, e - s).c_str(), "%f %f %f %f %f %f %f", &x, &y, &z, &yaw, &pitch, &sel, &saz);
			if (got == 7) {	// This camera's sun: elevation, azimuth (degrees)
				glm::vec3 to(std::cos(glm::radians(sel)) * std::cos(glm::radians(saz)), std::sin(glm::radians(sel)), std::cos(glm::radians(sel)) * std::sin(glm::radians(saz)));
				light.SetDirection(-to);
			}
			if (got >= 5) {
				Camera c(glm::vec3(x, y, z), glm::vec3(0.0f, 1.0f, 0.0f), yaw, pitch, 5.0f, 0.5f);
				camPos = c.getCameraPosition();
				view = c.calculateViewMatrix();
				best = 1e9;
				char nm[16]; snprintf(nm, sizeof(nm), "c%d", k);
				if (full) {
					skyHistValid = false; taa.Invalidate(); fog.InvalidateHistory(); post.Invalidate();
					for (int f = 0; f < 240; f++) FullFrame();
					SaveFull(nm);
					if (const char* seq = getenv("GI_SEQ")) {	// N consecutive frames after it: NAME_fJ_ldr
						for (int j = 0; j < atoi(seq); j++) {
							FullFrame();
							char sn[32]; snprintf(sn, sizeof(sn), "%s_f%d_ldr", nm, j);
							SaveTex(col, sn, W, H, false);
						}
					}
				} else {
					for (int f = 0; f < 160; f++) { if (f == 110) best = 1e9; Frame(); }
					Save(nm);
				}
				printf("[GIS] %s c%d: scene pass %.2f ms, exposure +%.2f EV\n", tag, k, best, renderer.GetAtmosphereExposure());
			}
			k++; s = e + 1;
		}
		gi.Shutdown();
		taa.Shutdown();
		return;
	}
	for (int f = 0; f < 200; f++) { if (f == 150) best = 1e9; Frame(); }
	printf("[GIS] %s: scene pass %.2f ms (best of 50)\n", tag, best);
	Save("a");
	double t0 = glfwGetTime();
	while (glfwGetTime() - t0 < 1.5) Frame();
	Save("b");
	printf("[GIS] %s: camera (%.1f %.1f %.1f), frames %.2f s apart\n", tag, camPos.x, camPos.y, camPos.z, glfwGetTime() - t0);
	gi.Shutdown();
	taa.Shutdown();
}
#endif
