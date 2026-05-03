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

	// 1. Calculate the cascade matrices
	light->CalculateCascadedLightMatrices(view, projection, near, far);
	const auto& matrices = light->GetCascadedLightMatrices();

	directionalShadowShader.UseShader();
	glViewport(0, 0, light->GetShadowMap()->GetShadowWidth(), light->GetShadowMap()->GetShadowHeight());

	// 2. Loop through each cascade and render
	for (GLuint i = 0; i < csm->GetCascadeCount(); i++)
	{
		csm->WriteLayer(i);
		glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
		glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);

		glm::mat4 lightProjView = matrices[i];
		directionalShadowShader.SetDirectionalLightTransform(lightProjView);

		directionalShadowShader.Validate();

		Frustum dirFrustum = Frustum::CreateFrustumFromMatrix(lightProjView);

		// Render regular objects
		float sw = (float)light->GetShadowMap()->GetShadowWidth();
		float sh = (float)light->GetShadowMap()->GetShadowHeight();
		scene.RenderAll(glm::mat4(1.0f), glm::mat4(1.0f), cameraPos, light, nullptr, 0, nullptr, 0, 0.0f, &dirFrustum, &directionalShadowShader, sw, sh, this, 0, 0, glm::vec4(0.0f), lightProjView);

		// GPU-Driven Instanced Groups
		float time = (float)glfwGetTime();
		auto& groups = scene.GetInstancedGroups();
		if (!groups.empty() && instancedCullShader.GetShaderID()) {
			for (auto* group : groups) {
				if (!group) continue;
				group->CullAndDrawShadow(
					instancedCullShader.GetShaderID(),
					instancedShadowShader,
					lightProjView,
					cameraPos,
					gs,
					time
				);
			}
		}
	}

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Renderer::OmniShadowMapPass(PointLight* light, SceneManager& scene)
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

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Renderer::RenderPass(const glm::mat4& projection, const glm::mat4& view,
						  const glm::vec3& cameraPos, SceneManager& scene,
						  DirectionalLight& mainLight,
						  PointLight* pointLights, unsigned int pointLightCount,
						  SpotLight* spotLights, unsigned int spotLightCount,
						  int fbw, int fbh, GLuint sceneDepthTexture, GLuint reflectionTexture,
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
	if (clipPlaneLoc != -1) glUniform4f(clipPlaneLoc, 0.0f, 0.0f, 0.0f, 1.0f);

	mainShader.SetDirectionalLight(&mainLight);
	mainShader.SetPointLights(pointLights, pointLightCount, 4, 0);
	mainShader.SetSpotLights(spotLights, spotLightCount, 4 + pointLightCount, pointLightCount);
	
	// Pass cascade matrices and split distances
	const auto& cascadedMatrices = mainLight.GetCascadedLightMatrices();
	const auto& cascadedSplits = mainLight.GetCascadeSplitDistances();
	if (!cascadedMatrices.empty()) {
		glUniformMatrix4fv(glGetUniformLocation(mainShader.GetShaderID(), "dirLightMatrices"), (GLsizei)cascadedMatrices.size(), GL_FALSE, glm::value_ptr(cascadedMatrices[0]));
		glUniform1fv(glGetUniformLocation(mainShader.GetShaderID(), "cascadeSplits"), (GLsizei)cascadedSplits.size(), &cascadedSplits[0]);
	}
	glUniformMatrix4fv(glGetUniformLocation(mainShader.GetShaderID(), "viewMatrix"), 1, GL_FALSE, glm::value_ptr(view));

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

	scene.RenderAll(projection, view, cameraPos, &mainLight, pointLights, pointLightCount, spotLights, spotLightCount, time, activeFrustum, nullptr, (float)fbw, (float)fbh, this, sceneDepthTexture, reflectionTexture, glm::vec4(0, 0, 0, 1), glm::mat4(1.0f), gs);

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
	if (clipPlaneLoc != -1) glUniform4f(clipPlaneLoc, 0.0f, 1.0f, 0.0f, -waterHeight + 0.01f);

	mainShader.SetDirectionalLight(&mainLight);
	mainShader.SetPointLights(pointLights, pointLightCount, 4, 0);
	mainShader.SetSpotLights(spotLights, spotLightCount, 4 + pointLightCount, pointLightCount);

	const auto& cascadedMatrices = mainLight.GetCascadedLightMatrices();
	const auto& cascadedSplits = mainLight.GetCascadeSplitDistances();
	if (!cascadedMatrices.empty()) {
		glUniformMatrix4fv(glGetUniformLocation(mainShader.GetShaderID(), "dirLightMatrices"), (GLsizei)cascadedMatrices.size(), GL_FALSE, glm::value_ptr(cascadedMatrices[0]));
		glUniform1fv(glGetUniformLocation(mainShader.GetShaderID(), "cascadeSplits"), (GLsizei)cascadedSplits.size(), &cascadedSplits[0]);
	}
	glUniformMatrix4fv(glGetUniformLocation(mainShader.GetShaderID(), "viewMatrix"), 1, GL_FALSE, glm::value_ptr(reflectedView));

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
	scene.RenderAll(projection, reflectedView, reflectedCamPos, &mainLight, pointLights, pointLightCount, spotLights, spotLightCount, time, &frustum, nullptr, (float)fbw, (float)fbh, this, 0, 0, reflectionClipPlane, glm::mat4(1.0f), gs);

	// Restore state
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	glDisable(GL_CLIP_DISTANCE0);
	glFrontFace(GL_CCW);
	glDisable(GL_BLEND);

	// Reset clip plane to neutral on the main shader
	if (clipPlaneLoc != -1) glUniform4f(clipPlaneLoc, 0.0f, 0.0f, 0.0f, 1.0f);
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

	// 3. Patch the version and prepend header
	size_t vPos = vSource.find("#version");
	if (vPos != std::string::npos) {
		size_t eol = vSource.find("\n", vPos);
		vSource.replace(vPos, eol - vPos, "#version 430 core");
		
		// Recalculate eol because replace() changed string length!
		eol = vSource.find("\n", vPos);
		vSource.insert(eol + 1, "\n" + hSource + "\n");
	}

	// 4. Robust Neutralization of conflicting globals
	auto neutralize = [&](const std::string& target, const std::string& replacement) {
		size_t pos = 0;
		while ((pos = vSource.find(target, pos)) != std::string::npos) {
			vSource.replace(pos, target.length(), replacement);
			pos += replacement.length();
		}
	};
	neutralize("uniform mat4 model", "uniform mat4 _unused_model");
	neutralize("in mat4 instanceMatrix", "in mat4 _unused_inst");
	neutralize("attribute mat4 instanceMatrix", "attribute mat4 _unused_inst2");
	// Header sets vFadeFactor from SSBO — neutralize the original 0.0 assignment so it doesn't overwrite
	neutralize("vFadeFactor = 0.0;", "// vFadeFactor set by ResolveInstancedModelMatrix()");

	// 5. Inject the shadow variables at the start of main()
	size_t mainPos = vSource.find("void main()");
	if (mainPos != std::string::npos) {
		mainPos = vSource.find("{", mainPos);
		if (mainPos != std::string::npos) {
			std::string shadowInjection = 
				"\n    mat4 model; model = ResolveInstancedModelMatrix();"
				"\n    mat4 instanceMatrix; instanceMatrix = model;"
				"\n    vFadeFactor = _instanceFadeFactor;\n";
			vSource.insert(mainPos + 1, shadowInjection);
		}
	}

	// 6. Create the hybrid shader
	Shader* hybrid = new Shader();
	std::ifstream fFile(fragPath);
	if (!fFile.is_open()) { delete hybrid; return &instancedRenderShader; }
	std::stringstream fStream;
	fStream << fFile.rdbuf();
	fFile.close();
	
	hybrid->CreateFromString(vSource.c_str(), fStream.str().c_str());
	printf("[Renderer] Created 'Instancified' hybrid: %s\n", cacheKey.c_str());
	
	// DEBUG DUMP
	std::ofstream dbg("debug_patched_shader.vert");
	dbg << vSource;
	dbg.close();
	
	instancedShaderCache[cacheKey] = hybrid;
	return hybrid;
}
