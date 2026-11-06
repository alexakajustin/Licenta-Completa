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
#include "Lighting/DirectionalLight.h"
#include "Lighting/PointLight.h"
#include "Lighting/SpotLight.h"
#include "Core/Camera.h"
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
	{
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
	if (const char* ex = getenv("GI_EXEC")) {
		scene.ExecutePipeline(&plain, &mat);
		for (auto& tab : scene.GetGraphTabs())
			for (GraphNode* n : tab.graph->GetNodes())
				if (n->status.info.find("rivers:") != std::string::npos) printf("[GIS] river node: %s\n", n->status.info.c_str());
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
		return;
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

	TemporalAA taa; taa.Init();
	GlobalIllumination gi; gi.Init();
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
		glBeginQuery(GL_TIME_ELAPSED, q);
		renderer.RenderPass(proj, view, camPos, scene, light, pointLights, pc, spotLights, sc, W, H, dep, 0, refraction, nullptr, &gs);
		glEndQuery(GL_TIME_ELAPSED);
		GLuint64 ns = 0; glGetQueryObjectui64v(q, GL_QUERY_RESULT, &ns);
		best = std::min(best, ns / 1e6);
	};
	auto Save = [&](const char* name) {
		std::vector<float> px((size_t)W * H * 4), rgb((size_t)W * H * 3);
		glBindTexture(GL_TEXTURE_2D, col);
		glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT, px.data());
		for (size_t i = 0; i < (size_t)W * H; i++) for (int c = 0; c < 3; c++) rgb[i * 3 + c] = px[i * 4 + c];
		char path[512]; snprintf(path, sizeof(path), "%s/%s_%s.pfm", outDir, tag, name);
		SavePFM(path, rgb, W, H);
	};
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
