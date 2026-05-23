#include "Renderer.h"
#include "SceneManager.h"
#include "Camera.h"
#include "Window.h"
#include "Frustum.h"
#include "InstancedGroup.h"
#include <GLFW/glfw3.h>
#include <fstream>
#include <sstream>
#include "CascadedShadowMap.h"
#include "GraphicsSettings.h"

Renderer::Renderer()
	: uniformModel(-1), uniformProjection(-1), uniformView(-1),
	  uniformEyePosition(-1), uniformSpecularIntensity(-1), uniformShininess(-1),
	  uniformTiling(-1), uniformOffset(-1),
	  uniformOmniLightPos(-1), uniformFarPlane(-1), 
	  uniformUseNormalMap(-1), uniformUseDiffuseTexture(-1), uniformUseInstancing(-1)
{
}

Renderer::~Renderer()
{
	for (auto const& [path, shader] : instancedShaderCache) {
		delete shader;
	}
	instancedShaderCache.clear();
}

void Renderer::Init()
{
	mainShader.CreateFromFiles("Assets/Shaders/shader.vert", "Assets/Shaders/shader.frag");
	directionalShadowShader.CreateFromFiles("Shaders/directional_shadow_map.vert", "Shaders/directional_shadow_map.frag");
	omniShadowShader.CreateFromFiles("Shaders/omni_shadow_map.vert", "Shaders/omni_shadow_map.geom", "Shaders/omni_shadow_map.frag");

	// GPU-Driven Instanced Rendering shaders (OpenGL 4.3+)
	instancedCullShader.CreateComputeShader("Assets/Shaders/compute_cull.glsl");
	instancedRenderShader.CreateFromFiles("Assets/Shaders/instanced_object.vert", "Assets/Shaders/shader.frag");
	instancedShadowShader.CreateFromFiles("Shaders/instanced_shadow.vert", "Shaders/instanced_shadow.frag");
	instancedOmniShadowShader.CreateFromFiles("Shaders/instanced_omni_shadow.vert", "Shaders/omni_shadow_map.geom", "Shaders/omni_shadow_map.frag");

	tessShader.CreateFromFiles(
		"Assets/Shaders/shader_tess.vert",
		"Assets/Shaders/terrain_tess.tcs",
		"Assets/Shaders/terrain_tess.tes",
		"Assets/Shaders/shader.frag");

	directionalShadowTessShader.CreateFromFiles(
		"Assets/Shaders/shader_tess.vert",
		"Assets/Shaders/directional_shadow_map_tess.tcs",
		"Assets/Shaders/directional_shadow_map_tess.tes",
		"Shaders/directional_shadow_map.frag"
	);
	
	CacheUniforms();
}

void Renderer::LoadSkybox(const std::vector<std::string>& faces)
{
	skybox = Skybox(faces);
}

void Renderer::CacheUniforms()
{
	uniformModel = mainShader.GetModelLocation();
	uniformProjection = mainShader.GetProjectionLocation();
	uniformView = mainShader.GetViewLocation();
	uniformEyePosition = mainShader.GetEyePositionLocation();
	uniformSpecularIntensity = mainShader.GetSpecularIntensityLocation();
	uniformShininess = mainShader.GetShininessLocation();
	uniformMaterialColor = glGetUniformLocation(mainShader.GetShaderID(), "material.baseColor");
	uniformTiling = mainShader.GetTilingLocation();
	uniformOffset = mainShader.GetOffsetLocation();
	uniformOmniLightPos = mainShader.getOmniLightPosLocation();
	uniformFarPlane = mainShader.getFarPlaneLocation();
	uniformUseNormalMap = glGetUniformLocation(mainShader.GetShaderID(), "useNormalMap");
	uniformUseDiffuseTexture = glGetUniformLocation(mainShader.GetShaderID(), "useDiffuseTexture");
	uniformUseInstancing = glGetUniformLocation(mainShader.GetShaderID(), "useInstancing");
}

void Renderer::DirectionalShadowMapPass(DirectionalLight* light, SceneManager& scene, const glm::vec3& cameraPos, const glm::mat4& projection, const glm::mat4& view, float near, float far, const GraphicsSettings* gs)
{
	CascadedShadowMap* csm = (CascadedShadowMap*)light->GetShadowMap();
	if (!csm) return;

	// 1. Calculate the cascade matrices (use configurable count)
	int numCascades = (gs && gs->shadowCascades >= 1 && gs->shadowCascades <= 4) ? gs->shadowCascades : 4;
	light->CalculateCascadedLightMatrices(view, projection, near, far, numCascades);
	const auto& matrices = light->GetCascadedLightMatrices();

	directionalShadowShader.UseShader();
	glViewport(0, 0, light->GetShadowMap()->GetShadowWidth(), light->GetShadowMap()->GetShadowHeight());

	// 2. Loop through each cascade and render
	glDisable(GL_CULL_FACE);   // Disable culling for single-sided objects (chairs, table tops)
	glEnable(GL_DEPTH_TEST);
	glDepthMask(GL_TRUE);      // Ensure depth writing is ON
	glDepthFunc(GL_LESS);
	glDisable(GL_BLEND);       // No blending for shadow map generation

	// Only iterate over computed cascades (may be fewer than csm->GetCascadeCount())
	GLuint actualCascades = std::min((GLuint)matrices.size(), csm->GetCascadeCount());
	for (GLuint i = 0; i < actualCascades; i++)
	{
		csm->WriteLayer(i);
		glClearColor(0.0f, 0.0f, 0.0f, 0.0f); // Clear to 0.0 (no occlusion)
		glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);

		glm::mat4 lightProjView = matrices[i];
		directionalShadowShader.SetDirectionalLightTransform(lightProjView);

		directionalShadowShader.Validate();

		Frustum dirFrustum = Frustum::CreateFrustumFromMatrix(lightProjView);

		// Render regular objects
		float sw = (float)light->GetShadowMap()->GetShadowWidth();
		float sh = (float)light->GetShadowMap()->GetShadowHeight();
		scene.RenderAll(glm::mat4(1.0f), glm::mat4(1.0f), cameraPos, light, nullptr, 0, nullptr, 0, 0.0f, nullptr, &directionalShadowShader, sw, sh, this, 0, 0, 0, glm::vec4(0.0f), lightProjView, gs);

		// GPU-Driven Instanced Groups
		float time = (float)glfwGetTime();
		auto& groups = scene.GetInstancedGroups();
		GLuint hiz = scene.GetHiZTexture();
		// Use camera's view-projection for Hi-Z occlusion test in shadow pass
		glm::mat4 camVP = projection * view;
		// Hi-Z dimensions match the camera viewport, NOT the shadow map
		int hizW = scene.GetHiZWidth();
		int hizH = scene.GetHiZHeight();
		if (!groups.empty() && instancedCullShader.GetShaderID()) {
			for (auto* group : groups) {
				if (!group) continue;
				group->CullAndDrawShadow(
					instancedCullShader.GetShaderID(),
					instancedShadowShader,
					lightProjView,
					cameraPos,
					gs,
					time,
					hiz,
					hizW, hizH,
					camVP
				);
			}
		}
	}

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glEnable(GL_CULL_FACE); // Re-enable culling
}

void Renderer::OmniShadowMapPass(PointLight* light, SceneManager& scene, const GraphicsSettings* gs)
{
	omniShadowShader.UseShader();

	glViewport(0, 0, light->GetShadowMap()->GetShadowWidth(), light->GetShadowMap()->GetShadowHeight());

	light->GetShadowMap()->Write();
	glClearColor(1.0f, 1.0f, 1.0f, 1.0f); // default alpha 1.0
	glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);

	GLint shadowModelLoc = omniShadowShader.GetModelLocation();
	GLint omniLightPosLoc = omniShadowShader.getOmniLightPosLocation();
	GLint farPlaneLoc = omniShadowShader.getFarPlaneLocation();

	glUniform3f(omniLightPosLoc, light->GetPosition().x, light->GetPosition().y, light->GetPosition().z);
	glUniform1f(farPlaneLoc, light->GetFarPlane());
	omniShadowShader.SetLightMatrices(light->CalculateLightTransform());

	omniShadowShader.Validate();

	float sw = (float)light->GetShadowMap()->GetShadowWidth();
	float sh = (float)light->GetShadowMap()->GetShadowHeight();
	scene.RenderAll(glm::mat4(1.0f), glm::mat4(1.0f), light->GetPosition(), nullptr, nullptr, 0, nullptr, 0, 0.0f, nullptr, &omniShadowShader, sw, sh, this);

	// GPU-Driven Instanced Groups — omni shadow pass
	float time = (float)glfwGetTime();
	auto& groups = scene.GetInstancedGroups();
	if (!groups.empty() && instancedCullShader.GetShaderID()) {
		instancedOmniShadowShader.UseShader();
		instancedOmniShadowShader.SetLightMatrices(light->CalculateLightTransform());
		for (auto* group : groups) {
			if (!group) continue;
			group->CullAndDrawShadowOmni(
				instancedCullShader.GetShaderID(),
				instancedOmniShadowShader,
				light->GetPosition(),
				light->GetFarPlane(),
				light->GetPosition(),
				gs,
				time
			);
		}
	}

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glEnable(GL_CULL_FACE);
	glEnable(GL_BLEND);
}

void Renderer::RenderPass(const glm::mat4& projection, const glm::mat4& view,
						  const glm::vec3& cameraPos, SceneManager& scene,
						  DirectionalLight& mainLight,
						  PointLight* pointLights, unsigned int pointLightCount,
						  SpotLight* spotLights, unsigned int spotLightCount,
						  int fbw, int fbh, GLuint sceneDepthTexture, GLuint reflectionTexture, GLuint refractionTexture,
						  const Frustum* debugFrustum, const GraphicsSettings* gs)
{
	if (gs && gs->showWireframe) {
		glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
	} else {
		glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	}

	glViewport(0, 0, fbw, fbh);
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_DEPTH_BUFFER_BIT);

	// Skybox (Disabled in favor of Volumetric Sky pass)
	/*glDisable(GL_BLEND);
	glDisable(GL_CULL_FACE);
	skybox.DrawSkybox(view, projection);
	glEnable(GL_CULL_FACE);*/

	// Enable alpha blending for transparent materials (Unity-style Fade)
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	// Main shader
	mainShader.UseShader();

	// Safe defaults: ensure textureLayerCount=0 at frame start so the vertex shader
	// never reads stale state before any object has overridden it.
	GLint layerCountLoc = glGetUniformLocation(mainShader.GetShaderID(), "textureLayerCount");
	if (layerCountLoc != -1) glUniform1i(layerCountLoc, 0);

	glUniformMatrix4fv(uniformProjection, 1, GL_FALSE, glm::value_ptr(projection));
	glUniformMatrix4fv(uniformView, 1, GL_FALSE, glm::value_ptr(view));
	glUniform3f(uniformEyePosition, cameraPos.x, cameraPos.y, cameraPos.z);
	
	GLint clipPlaneLoc = glGetUniformLocation(mainShader.GetShaderID(), "clipPlane");
	if (clipPlaneLoc != -1) {
		while(glGetError() != GL_NO_ERROR);
		glUniform4f(clipPlaneLoc, 0.0f, 0.0f, 0.0f, 1.0f);
		if (glGetError() != GL_NO_ERROR) std::cout << "[ERROR] glUniform4f failed for clipPlane at location: " << clipPlaneLoc << " in Renderer line 230\n";
	}

	mainShader.SetDirectionalLight(&mainLight);
	mainShader.SetPointLights(pointLights, pointLightCount, 4, 0);
	mainShader.SetSpotLights(spotLights, spotLightCount, 4 + pointLightCount, pointLightCount);
	
	// Pass cascade matrices and split distances
	const auto& cascadedMatrices = mainLight.GetCascadedLightMatrices();
	const auto& cascadedSplits = mainLight.GetCascadeSplitDistances();
	if (!cascadedMatrices.empty()) {
		for (size_t i = 0; i < cascadedMatrices.size(); ++i) {
			char buf[64];
			snprintf(buf, sizeof(buf), "directionalLightTransform[%zu]", i);
			GLint mLoc = glGetUniformLocation(mainShader.GetShaderID(), buf);
			if (mLoc != -1) {
				while(glGetError() != GL_NO_ERROR);
				glUniformMatrix4fv(mLoc, 1, GL_FALSE, glm::value_ptr(cascadedMatrices[i]));
				if (glGetError() != GL_NO_ERROR) std::cout << "[ERROR] glUniformMatrix4fv failed for " << buf << " at location: " << mLoc << "\n";
			}
		}
		for (size_t i = 0; i < cascadedSplits.size(); ++i) {
			char buf[64];
			snprintf(buf, sizeof(buf), "cascadeSplits[%zu]", i);
			GLint sLoc = glGetUniformLocation(mainShader.GetShaderID(), buf);
			if (sLoc != -1) glUniform1f(sLoc, cascadedSplits[i]);
		}
	}
	
	GLint vLoc = glGetUniformLocation(mainShader.GetShaderID(), "viewMatrix");
	if (vLoc != -1) {
		while(glGetError() != GL_NO_ERROR);
		glUniformMatrix4fv(vLoc, 1, GL_FALSE, glm::value_ptr(view));
		if (glGetError() != GL_NO_ERROR) std::cout << "[ERROR] glUniformMatrix4fv failed for viewMatrix at location: " << vLoc << "\n";
	}

	mainLight.GetShadowMap()->Read(GL_TEXTURE3);
	mainLight.GetShadowMap()->ReadColor(GL_TEXTURE20);
	mainShader.SetTexture(0);
	mainShader.SetNormalMap(1);
	mainShader.SetDirectionalShadowMap(3);
	mainShader.SetDirectionalShadowColorMap(20);

	// Pass shadow distance to shader for percentage-based fade
	// Use the actual cascade far distance, not the old pre-cascade frustum size
	GLint sdLoc = glGetUniformLocation(mainShader.GetShaderID(), "shadowDistance");
	if (sdLoc != -1) {
		const auto& splits = mainLight.GetCascadeSplitDistances();
		float shadowFar = splits.empty() ? mainLight.GetShadowFrustumSize() : splits.back();
		glUniform1f(sdLoc, shadowFar);
	}

	// mainShader.Validate(); 
	
	// Scene objects with Frustum Culling
	Frustum liveFrustum = Frustum::CreateFrustumFromMatrix(projection * view);
	const Frustum* activeFrustum = debugFrustum ? debugFrustum : &liveFrustum;
	float time = (float)glfwGetTime();

	// Pass instanced shaders to scene manager for GPU-driven rendering
	scene.SetCullShader(&instancedCullShader);
	scene.SetInstancedRenderShader(&instancedRenderShader);

	scene.RenderAll(projection, view, cameraPos, &mainLight, pointLights, pointLightCount, spotLights, spotLightCount, time, activeFrustum, nullptr, (float)fbw, (float)fbh, this, sceneDepthTexture, reflectionTexture, refractionTexture, glm::vec4(0, 0, 0, 1), glm::mat4(1.0f), gs);

	// Reset polygon mode after main render pass
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

	// Disable blending for overlays
	glDisable(GL_BLEND);
	// NOTE: Gizmo/icon rendering moved to Application::Run() AFTER the SSAO pass,
	// so that the depth buffer retains valid object data for SSAO sampling.
}

void Renderer::ReflectionPass(const glm::mat4& projection, const glm::mat4& view,
							  const glm::vec3& cameraPos, SceneManager& scene,
							  DirectionalLight& mainLight,
							  PointLight* pointLights, unsigned int pointLightCount,
							  SpotLight* spotLights, unsigned int spotLightCount,
							  int fbw, int fbh, float waterHeight, const GraphicsSettings* gs)
{
	if (gs && gs->showWireframe) {
		glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
	} else {
		glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	}

	glViewport(0, 0, fbw, fbh);
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_DEPTH_BUFFER_BIT);

	// Reflect camera across water plane (y = waterHeight)
	glm::mat4 reflectionMatrix(1.0f);
	reflectionMatrix[1][1] = -1.0f;
	reflectionMatrix[3][1] = 2.0f * waterHeight;
	glm::mat4 reflectedView = view * reflectionMatrix;

	glm::vec3 reflectedCamPos = cameraPos;
	reflectedCamPos.y = 2.0f * waterHeight - reflectedCamPos.y;

	// Flip face winding since reflection inverts the coordinate system
	glFrontFace(GL_CW);

	// Enable clip plane to only render above water
	glEnable(GL_CLIP_DISTANCE0);

	// Skybox (reflected) - Disabled in favor of Volumetric Sky
	/*glDisable(GL_BLEND);
	glDisable(GL_CULL_FACE);
	skybox.DrawSkybox(reflectedView, projection);
	glEnable(GL_CULL_FACE);*/

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	// Main shader
	mainShader.UseShader();

	GLint layerCountLoc = glGetUniformLocation(mainShader.GetShaderID(), "textureLayerCount");
	if (layerCountLoc != -1) glUniform1i(layerCountLoc, 0);

	glUniformMatrix4fv(uniformProjection, 1, GL_FALSE, glm::value_ptr(projection));
	glUniformMatrix4fv(uniformView, 1, GL_FALSE, glm::value_ptr(reflectedView));
	glUniform3f(uniformEyePosition, reflectedCamPos.x, reflectedCamPos.y, reflectedCamPos.z);

	// Set clip plane: render only stuff ABOVE water (y > waterHeight)
	// Clip plane equation: 0*x + 1*y + 0*z + (-waterHeight) > 0
	GLint clipPlaneLoc = glGetUniformLocation(mainShader.GetShaderID(), "clipPlane");
	if (clipPlaneLoc != -1) {
		while(glGetError() != GL_NO_ERROR);
		glUniform4f(clipPlaneLoc, 0.0f, 1.0f, 0.0f, -waterHeight + 0.01f);
		if (glGetError() != GL_NO_ERROR) std::cout << "[ERROR] glUniform4f failed for clipPlane at location: " << clipPlaneLoc << " in Renderer line 337\n";
	}

	mainShader.SetDirectionalLight(&mainLight);
	mainShader.SetPointLights(pointLights, pointLightCount, 4, 0);
	mainShader.SetSpotLights(spotLights, spotLightCount, 4 + pointLightCount, pointLightCount);

	const auto& cascadedMatrices = mainLight.GetCascadedLightMatrices();
	const auto& cascadedSplits = mainLight.GetCascadeSplitDistances();
	if (!cascadedMatrices.empty()) {
		for (size_t i = 0; i < cascadedMatrices.size(); ++i) {
			char buf[64];
			snprintf(buf, sizeof(buf), "directionalLightTransform[%zu]", i);
			GLint mLoc = glGetUniformLocation(mainShader.GetShaderID(), buf);
			if (mLoc != -1) {
				while(glGetError() != GL_NO_ERROR);
				glUniformMatrix4fv(mLoc, 1, GL_FALSE, glm::value_ptr(cascadedMatrices[i]));
				if (glGetError() != GL_NO_ERROR) std::cout << "[ERROR] glUniformMatrix4fv failed for " << buf << " at location: " << mLoc << "\n";
			}
		}
		for (size_t i = 0; i < cascadedSplits.size(); ++i) {
			char buf[64];
			snprintf(buf, sizeof(buf), "cascadeSplits[%zu]", i);
			GLint sLoc = glGetUniformLocation(mainShader.GetShaderID(), buf);
			if (sLoc != -1) glUniform1f(sLoc, cascadedSplits[i]);
		}
	}
	
	GLint vLoc = glGetUniformLocation(mainShader.GetShaderID(), "viewMatrix");
	if (vLoc != -1) {
		while(glGetError() != GL_NO_ERROR);
		glUniformMatrix4fv(vLoc, 1, GL_FALSE, glm::value_ptr(reflectedView));
		if (glGetError() != GL_NO_ERROR) std::cout << "[ERROR] glUniformMatrix4fv failed for viewMatrix at location: " << vLoc << "\n";
	}

	mainLight.GetShadowMap()->Read(GL_TEXTURE3);
	mainLight.GetShadowMap()->ReadColor(GL_TEXTURE20);
	mainShader.SetTexture(0);
	mainShader.SetNormalMap(1);
	mainShader.SetDirectionalShadowMap(3);
	mainShader.SetDirectionalShadowColorMap(20);

	Frustum frustum = Frustum::CreateFrustumFromMatrix(projection * reflectedView);
	float time = (float)glfwGetTime();

	// Render opaque scene objects only (no transparent/water) with clip plane active
	glm::vec4 reflectionClipPlane = glm::vec4(0.0f, 1.0f, 0.0f, -waterHeight + 0.01f);
	scene.RenderAll(projection, reflectedView, reflectedCamPos, &mainLight, pointLights, pointLightCount, spotLights, spotLightCount, time, nullptr, nullptr, (float)fbw, (float)fbh, this, 0, 0, 0, reflectionClipPlane, glm::mat4(1.0f), gs);

	// Restore state
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	glDisable(GL_CLIP_DISTANCE0);
	glFrontFace(GL_CCW);
	glDisable(GL_BLEND);

	// Reset clip plane to neutral on the main shader
	if (clipPlaneLoc != -1) {
		mainShader.UseShader(); // MUST BIND BEFORE SETTING UNIFORM
		while(glGetError() != GL_NO_ERROR);
		glUniform4f(clipPlaneLoc, 0.0f, 0.0f, 0.0f, 1.0f);
		if (glGetError() != GL_NO_ERROR) std::cout << "[ERROR] glUniform4f failed for clipPlane at location: " << clipPlaneLoc << " in Renderer line 372\n";
	}
}

Shader* Renderer::GetInstancedShader(Shader* original)
{
	if (!original) return &instancedRenderShader;

	std::string vertPath = original->GetVertexPath();
	std::string fragPath = original->GetFragmentPath();
	if (vertPath.empty() || fragPath.empty()) return &instancedRenderShader;

	// Use combined path as key
	std::string cacheKey = vertPath + "||" + fragPath;
	if (instancedShaderCache.count(cacheKey)) {
		return instancedShaderCache[cacheKey];
	}

	// 1. Read the header template
	std::ifstream hFile("Assets/Shaders/instanced_header.glsl");
	std::string hSource = "";
	if (hFile.is_open()) {
		std::stringstream hStream;
		hStream << hFile.rdbuf();
		hFile.close();
		hSource = hStream.str();
	}

	// 2. Read the original vertex shader
	std::ifstream vFile(vertPath);
	if (!vFile.is_open()) return &instancedRenderShader;
	std::stringstream vStream;
	vStream << vFile.rdbuf();
	vFile.close();
	std::string vSource = vStream.str();
	// 3. Robust Patching Logic for multi-stage pipelines
	auto patchSource = [&](std::string& src, int stageType, bool hasTessPipe = false) {
		// stageType: 0=VS, 1=TCS, 2=TES, 3=FS
		
		// Neutralize conflicting globals
		auto neutralize = [&](const std::string& target, const std::string& replacement) {
			size_t pos = 0;
			while ((pos = src.find(target, pos)) != std::string::npos) {
				src.replace(pos, target.length(), replacement);
				pos += replacement.length();
			}
		};

		neutralize("uniform mat4 model", "uniform mat4 _unused_model");
		neutralize("in mat4 instanceMatrix", "in mat4 _unused_inst");
		neutralize("attribute mat4 instanceMatrix", "attribute mat4 _unused_inst2");
		neutralize("modelMatrix = instanceMatrix;", "modelMatrix = model; // GPU-driven override");
		neutralize("out float vFadeFactor;", "// Redefined by header");
		neutralize("in float vFadeFactor;", "// Redefined by header");
		neutralize("varying float vFadeFactor;", "// Redefined by header");
		neutralize("vFadeFactor = 0.0;", "// vFadeFactor set by ResolveInstancedModelMatrix()");
		neutralize("vIsSelected = 0.0;", "// vIsSelected set by ResolveInstancedModelMatrix()");

		// Update version and prepend header
		size_t vPos = src.find("#version");
		if (vPos != std::string::npos) {
			size_t eol = src.find("\n", vPos);
			src.replace(vPos, eol - vPos, "#version 430 core");
			
			eol = src.find("\n", vPos);
			std::string injection = "\n";
			if (stageType == 0) injection += "#define IS_VERTEX_SHADER\n";
			else if (stageType == 1) injection += "#define IS_TCS\n";
			else if (stageType == 2) injection += "#define IS_TES\n";
			else if (stageType == 3) injection += "#define IS_FRAG\n";
			
			if (hasTessPipe) injection += "#define HAS_TESSELLATION\n";
			injection += hSource + "\n";
			src.insert(eol + 1, injection);
		}

		// Inject main() logic
		size_t mainPos = src.find("void main()");
		if (mainPos != std::string::npos) {
			mainPos = src.find("{", mainPos);
			if (mainPos != std::string::npos) {
				std::string logic = "\n";
				if (stageType == 0) { // VS
					logic += "    mat4 model; model = ResolveInstancedModelMatrix();\n";
					logic += "    vData.vFadeFactor = _instanceFadeFactor;\n";
					logic += "    vData.iInstanceID = gl_InstanceID;\n";
					logic += "    vIsSelected = _instanceIsSelected;\n";
					if (hasTessPipe) logic += "    vFadeFactor = _instanceFadeFactor;\n"; 
				} else if (stageType == 1) { // TCS
					logic += "    vDataOut[gl_InvocationID].iInstanceID = vDataIn[gl_InvocationID].iInstanceID;\n";
					logic += "    vDataOut[gl_InvocationID].vFadeFactor = vDataIn[gl_InvocationID].vFadeFactor;\n";
				} else if (stageType == 2) { // TES
					logic += "    mat4 model; model = ResolveInstancedModelMatrix();\n";
					logic += "    vData.vFadeFactor = _instanceFadeFactor;\n";
					logic += "    vData.iInstanceID = vDataIn[0].iInstanceID;\n";
				}
				src.insert(mainPos + 1, logic);
			}
		}
	};

	// 5. Handle Tessellation if the original shader has it
	bool hasTess = original && original->HasTessellation();

	// 4. Patch Vertex Shader
	patchSource(vSource, 0, hasTess);

	std::string tcsSource = "";
	std::string tesSource = "";
	if (hasTess) {
		tcsSource = original->ReadFile(original->GetTCSPath().c_str());
		tesSource = original->ReadFile(original->GetTESPath().c_str());

		patchSource(tcsSource, 1, true);
		patchSource(tesSource, 2, true);
	}

	// 7. Create the hybrid shader
	Shader* hybrid = new Shader();
	std::ifstream fFile(fragPath);
	if (!fFile.is_open()) { delete hybrid; return &instancedRenderShader; }
	std::stringstream fStream;
	fStream << fFile.rdbuf();
	fFile.close();

	// 6. Patch Fragment Shader
	std::string fSource = fStream.str();
	patchSource(fSource, 3, hasTess);

	if (hasTess) {
		hybrid->CreateFromString(vSource.c_str(), tcsSource.c_str(), tesSource.c_str(), fSource.c_str());
	} else {
		hybrid->CreateFromString(vSource.c_str(), fSource.c_str());
	}

	extern std::unordered_map<GLuint, std::string> g_ShaderNames;
	g_ShaderNames[hybrid->GetShaderID()] = "Instanced Hybrid: " + cacheKey;

	printf("[Renderer] Created 'Instancified' hybrid: %s%s\n", cacheKey.c_str(), hasTess ? " (Tessellation)" : "");
	
	// DEBUG DUMP
	std::ofstream dbg("debug_patched_shader.vert");
	dbg << vSource;
	dbg.close();
	if (hasTess) {
		std::ofstream dbgT("debug_patched_shader.tes");
		dbgT << tesSource;
		dbgT.close();
	}
	
	instancedShaderCache[cacheKey] = hybrid;
	return hybrid;
}
