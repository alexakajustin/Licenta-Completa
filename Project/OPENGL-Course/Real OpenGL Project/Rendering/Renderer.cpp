#include "Rendering/Renderer.h"
#include "Scene/SceneManager.h"
#include "Core/Camera.h"
#include "Core/Window.h"
#include "Core/Frustum.h"
#include "Rendering/InstancedGroup.h"
#include <GLFW/glfw3.h>
#include <fstream>
#include <sstream>
#include "Rendering/CascadedShadowMap.h"
#include "GraphicsSettings.h"
#include "Core/AssetManager.h"
#include "Core/ServiceLocator.h"

Renderer::Renderer()
	: uniformModel(-1), uniformProjection(-1), uniformView(-1),
	  uniformEyePosition(-1), uniformSpecularIntensity(-1), uniformShininess(-1),
	  uniformTiling(-1), uniformOffset(-1),
	  uniformOmniLightPos(-1), uniformFarPlane(-1), 
	  uniformUseNormalMap(-1), uniformUseDiffuseTexture(-1), uniformUseInstancing(-1),
	  proceduralSkyTextureId(0), skyboxFBO(0), skyboxQuadVAO(0), skyboxQuadVBO(0)
{
}

Renderer::~Renderer()
{
	for (auto const& [path, shader] : instancedShaderCache) {
		delete shader;
	}
	instancedShaderCache.clear();

	if (proceduralSkyTextureId) glDeleteTextures(1, &proceduralSkyTextureId);
	if (skyboxFBO) glDeleteFramebuffers(1, &skyboxFBO);
	if (skyboxQuadVAO) glDeleteVertexArrays(1, &skyboxQuadVAO);
	if (skyboxQuadVBO) glDeleteBuffers(1, &skyboxQuadVBO);

	// Scene cubemap cleanup
	if (sceneCubemapId)  glDeleteTextures(1, &sceneCubemapId);
	if (sceneCubemapFBO) glDeleteFramebuffers(1, &sceneCubemapFBO);
	if (sceneCubemapDepthRBO) glDeleteRenderbuffers(1, &sceneCubemapDepthRBO);
}

void Renderer::Init()
{
	mainShader.CreateFromFiles("Assets/Shaders/shader.vert", "Assets/Shaders/shader.frag");
	directionalShadowShader.CreateFromFiles("Shaders/directional_shadow_map.vert", "Shaders/directional_shadow_map.frag");
	omniShadowShader.CreateFromFiles("Shaders/omni_shadow_map.vert", "Shaders/omni_shadow_map.frag");

	// GPU-Driven Instanced Rendering shaders (OpenGL 4.3+)
	instancedCullShader.CreateComputeShader("Assets/Shaders/compute_cull.glsl");
	instancedRenderShader.CreateFromFiles("Assets/Shaders/instanced_object.vert", "Assets/Shaders/shader.frag");
	instancedShadowShader.CreateFromFiles("Shaders/instanced_shadow.vert", "Shaders/instanced_shadow.frag");
	instancedOmniShadowShader.CreateFromFiles("Shaders/instanced_omni_shadow.vert", "Shaders/omni_shadow_map.frag");

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

	// Initialize procedural sky cubemap texture (256x256 per face)
	glGenTextures(1, &proceduralSkyTextureId);
	glBindTexture(GL_TEXTURE_CUBE_MAP, proceduralSkyTextureId);
	for (unsigned int i = 0; i < 6; ++i) {
		glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB, 256, 256, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
	}
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
	glBindTexture(GL_TEXTURE_CUBE_MAP, 0);

	volumetricSkyShader.CreateFromFiles("Assets/Shaders/volumetric_sky.vert", "Assets/Shaders/volumetric_sky.frag");
	universeSkyShader.CreateFromFiles("Assets/Shaders/volumetric_sky.vert", "Assets/Shaders/universe_sky.frag");
	
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
	glViewport(0, 0, light->GetShadowMap()->GetShadowWidth(), light->GetShadowMap()->GetShadowHeight());

	float sw = (float)light->GetShadowMap()->GetShadowWidth();
	float sh = (float)light->GetShadowMap()->GetShadowHeight();
	float time = (float)glfwGetTime();

	// Calculate all 6 face view-projection matrices
	std::vector<glm::mat4> lightMatrices = light->CalculateLightTransform();

	// Bind the FBO
	glBindFramebuffer(GL_FRAMEBUFFER, light->GetShadowMap()->GetFBO());

	for (int face = 0; face < 6; face++) {
		// Attach the specific face of the depth and color cubemaps to FBO
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, light->GetShadowMap()->GetTextureID(), 0);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, light->GetShadowMap()->GetColorTextureID(), 0);

		// Clear depth and color for this face
		glClearColor(1.0f, 1.0f, 1.0f, 1.0f); // Default alpha 1.0
		glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);

		// Render standard scene objects
		omniShadowShader.UseShader();
		GLint shadowModelLoc = omniShadowShader.GetModelLocation();
		GLint omniLightPosLoc = omniShadowShader.getOmniLightPosLocation();
		GLint farPlaneLoc = omniShadowShader.getFarPlaneLocation();
		GLint lightMatLoc = glGetUniformLocation(omniShadowShader.GetShaderID(), "lightMatrix");

		glUniform3f(omniLightPosLoc, light->GetPosition().x, light->GetPosition().y, light->GetPosition().z);
		glUniform1f(farPlaneLoc, light->GetFarPlane());
		if (lightMatLoc != -1) {
			glUniformMatrix4fv(lightMatLoc, 1, GL_FALSE, glm::value_ptr(lightMatrices[face]));
		}
		omniShadowShader.Validate();

		scene.RenderAll(glm::mat4(1.0f), glm::mat4(1.0f), light->GetPosition(), nullptr, nullptr, 0, nullptr, 0, 0.0f, nullptr, &omniShadowShader, sw, sh, this, 0, 0, 0, glm::vec4(0.0f), lightMatrices[face], gs, light->GetFarPlane());

		// Render GPU-Driven Instanced Groups
		auto& groups = scene.GetInstancedGroups();
		if (!groups.empty() && instancedCullShader.GetShaderID()) {
			instancedOmniShadowShader.UseShader();
			GLint instLightMatLoc = glGetUniformLocation(instancedOmniShadowShader.GetShaderID(), "lightMatrix");
			if (instLightMatLoc != -1) {
				glUniformMatrix4fv(instLightMatLoc, 1, GL_FALSE, glm::value_ptr(lightMatrices[face]));
			}
			
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
	UpdateProceduralSkybox(mainLight, gs, (float)glfwGetTime());

	// Render dynamic cubemaps for each visible, reflective object from its own position.
	// This ensures perspective and parallax are mathematically correct.
	for (auto* obj : scene.GetObjects()) {
		if (obj && obj->GetVisible() && obj->GetMaterial() && obj->GetMaterial()->GetReflectivity() > 0.0f) {
			RenderSceneCubemapPass(obj, cameraPos, scene, mainLight,
			                       pointLights, pointLightCount,
			                       spotLights, spotLightCount, gs);
		}
	}

	if (gs && gs->showWireframe) {
		glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
	} else {
		glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	}

	glViewport(0, 0, fbw, fbh);
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_DEPTH_BUFFER_BIT);

	// Skybox (Render if Volumetric Sky is disabled)
	if (!gs || !gs->volumetricSkyEnabled) {
		glDisable(GL_BLEND);
		glDisable(GL_CULL_FACE);
		skybox.DrawSkybox(view, projection);
		glEnable(GL_CULL_FACE);
	}

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
		for (size_t i = 0; i < 4; ++i) {
			char buf[64];
			snprintf(buf, sizeof(buf), "cascadeSplits[%zu]", i);
			GLint sLoc = glGetUniformLocation(mainShader.GetShaderID(), buf);
			if (sLoc != -1) {
				float val = (i < cascadedSplits.size()) ? cascadedSplits[i] : 1000000.0f;
				glUniform1f(sLoc, val);
			}
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
	// NOTE: Gizmo/icon rendering is handled in Application::Run()
}

void Renderer::ReflectionPass(const glm::mat4& projection, const glm::mat4& view,
							  const glm::vec3& cameraPos, SceneManager& scene,
							  DirectionalLight& mainLight,
							  PointLight* pointLights, unsigned int pointLightCount,
							  SpotLight* spotLights, unsigned int spotLightCount,
							  int fbw, int fbh, float waterHeight, const GraphicsSettings* gs)
{
	UpdateProceduralSkybox(mainLight, gs, (float)glfwGetTime());

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

	// Skybox (reflected) - Render if Volumetric Sky is disabled
	if (!gs || !gs->volumetricSkyEnabled) {
		glDisable(GL_BLEND);
		glDisable(GL_CULL_FACE);
		skybox.DrawSkybox(reflectedView, projection);
		glEnable(GL_CULL_FACE);
	}

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
		for (size_t i = 0; i < 4; ++i) {
			char buf[64];
			snprintf(buf, sizeof(buf), "cascadeSplits[%zu]", i);
			GLint sLoc = glGetUniformLocation(mainShader.GetShaderID(), buf);
			if (sLoc != -1) {
				float val = (i < cascadedSplits.size()) ? cascadedSplits[i] : 1000000.0f;
				glUniform1f(sLoc, val);
			}
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

	ServiceLocator::GetAssetManager()->RegisterShader(hybrid->GetShaderID(), "Instanced Hybrid: " + cacheKey);

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

void Renderer::RenderSceneCubemapPass(
	GameObject* obj, const glm::vec3& cameraPos, SceneManager& scene,
	DirectionalLight& mainLight,
	PointLight* pointLights, unsigned int pointLightCount,
	SpotLight* spotLights, unsigned int spotLightCount,
	const GraphicsSettings* gs)
{
	if (!obj) return;
	isRenderingCubemap = true;
	const int CUBEMAP_SIZE = 512;

	GLuint cubemapId = obj->GetCustomCubemapID();
	GLuint cubemapFBO = obj->GetCustomCubemapFBO();
	GLuint cubemapDepthRBO = obj->GetCustomCubemapDepthRBO();

	// ---- Lazy init: create cubemap + FBO once per object ----
	if (cubemapId == 0) {
		glGenTextures(1, &cubemapId);
		glBindTexture(GL_TEXTURE_CUBE_MAP, cubemapId);
		for (int i = 0; i < 6; ++i)
			glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB16F,
			             CUBEMAP_SIZE, CUBEMAP_SIZE, 0, GL_RGB, GL_FLOAT, nullptr);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
		glBindTexture(GL_TEXTURE_CUBE_MAP, 0);

		glGenFramebuffers(1, &cubemapFBO);
		glGenRenderbuffers(1, &cubemapDepthRBO);
		glBindFramebuffer(GL_FRAMEBUFFER, cubemapFBO);
		glBindRenderbuffer(GL_RENDERBUFFER, cubemapDepthRBO);
		glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, CUBEMAP_SIZE, CUBEMAP_SIZE);
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, cubemapDepthRBO);
		glBindFramebuffer(GL_FRAMEBUFFER, 0);

		obj->SetCustomCubemapID(cubemapId);
		obj->SetCustomCubemapFBO(cubemapFBO);
		obj->SetCustomCubemapDepthRBO(cubemapDepthRBO);
	}

	// ---- Save GL state ----
	GLint oldFBO = 0;
	glGetIntegerv(GL_FRAMEBUFFER_BINDING, &oldFBO);
	GLint oldViewport[4];
	glGetIntegerv(GL_VIEWPORT, oldViewport);

	// ---- World Position of the Object / Optimal capture position for flat planes ----
	glm::vec3 capturePos = glm::vec3(obj->GetWorldMatrix()[3]);

	// Clean OOP/Geometry Check: detect if the object is flat along any axis in local space.
	// If it is, project the player camera onto the plane's surface to minimize parallax distortion.
	glm::vec3 localMin(0.0f), localMax(0.0f);
	bool hasBounds = false;
	if (obj->GetMesh()) {
		obj->GetMesh()->GetBounds(localMin, localMax);
		hasBounds = true;
	} else if (obj->GetModel()) {
		localMin = obj->GetModel()->GetMinBound();
		localMax = obj->GetModel()->GetMaxBound();
		hasBounds = true;
	}

	if (hasBounds) {
		glm::vec3 localSize = localMax - localMin;
		int flatAxis = 0;
		float minDim = localSize[0];
		float maxDim = localSize[0];
		
		for (int i = 1; i < 3; ++i) {
			if (localSize[i] < minDim) {
				minDim = localSize[i];
				flatAxis = i;
			}
			if (localSize[i] > maxDim) {
				maxDim = localSize[i];
			}
		}

		// A surface is near-flat if its thickness (smallest dimension) is less than 15% of its width/length
		// and it is relatively thin overall in local space (e.g. less than 1.0 units).
		bool isNearFlat = (minDim < maxDim * 0.15f) && (minDim < 1.0f);
		if (minDim < 0.1f) {
			isNearFlat = true; // Always count ultra-thin surfaces
		}

		if (isNearFlat) {
			glm::mat4 invWorld = glm::inverse(obj->GetWorldMatrix());
			glm::vec3 localCam = glm::vec3(invWorld * glm::vec4(cameraPos, 1.0f));

			glm::vec3 localCapture = localCam;
			float planeLocalPos = (localMin[flatAxis] + localMax[flatAxis]) * 0.5f;
			localCapture[flatAxis] = 2.0f * planeLocalPos - localCam[flatAxis];

			// Clamp local coordinates to the bounding limits of the flat surface
			for (int i = 0; i < 3; ++i) {
				if (i != flatAxis) {
					localCapture[i] = glm::clamp(localCapture[i], localMin[i], localMax[i]);
				}
			}

			capturePos = glm::vec3(obj->GetWorldMatrix() * glm::vec4(localCapture, 1.0f));
		}
	}

	// ---- Six face view matrices ----
	const glm::mat4 captureProj = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 500.0f);
	const glm::mat4 captureViews[6] = {
		glm::lookAt(capturePos, capturePos + glm::vec3( 1, 0, 0), glm::vec3(0,-1, 0)),
		glm::lookAt(capturePos, capturePos + glm::vec3(-1, 0, 0), glm::vec3(0,-1, 0)),
		glm::lookAt(capturePos, capturePos + glm::vec3( 0, 1, 0), glm::vec3(0, 0, 1)),
		glm::lookAt(capturePos, capturePos + glm::vec3( 0,-1, 0), glm::vec3(0, 0,-1)),
		glm::lookAt(capturePos, capturePos + glm::vec3( 0, 0, 1), glm::vec3(0,-1, 0)),
		glm::lookAt(capturePos, capturePos + glm::vec3( 0, 0,-1), glm::vec3(0,-1, 0)),
	};

	glBindFramebuffer(GL_FRAMEBUFFER, cubemapFBO);
	glViewport(0, 0, CUBEMAP_SIZE, CUBEMAP_SIZE);
	glEnable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);
	glEnable(GL_CULL_FACE);

	// Exclude the object itself so it doesn't render inside its own cubemap reflection!
	scene.SetExcludeObject(obj);

	for (int face = 0; face < 6; ++face) {
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
		                       GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, cubemapId, 0);
		glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		// Draw skybox face first so background is visible in reflections
		if (!gs || !gs->volumetricSkyEnabled) {
			glDisable(GL_CULL_FACE);
			skybox.DrawSkybox(captureViews[face], captureProj);
			glEnable(GL_CULL_FACE);
		} else {
			// Use the procedural sky cubemap as background
			glDisable(GL_CULL_FACE);
			skybox.DrawSkyboxFromCubemap(proceduralSkyTextureId, captureViews[face], captureProj);
			glEnable(GL_CULL_FACE);
		}

		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

		// Render scene — no override shader, no shadow re-render, no water
		scene.RenderAll(
			captureProj, captureViews[face], capturePos,
			&mainLight, pointLights, pointLightCount, spotLights, spotLightCount,
			(float)glfwGetTime(), nullptr, nullptr,
			(float)CUBEMAP_SIZE, (float)CUBEMAP_SIZE,
			this, 0, 0, 0,
			glm::vec4(0, 0, 0, 1), glm::mat4(1.0f), gs);

		glDisable(GL_BLEND);
	}

	scene.SetExcludeObject(nullptr);

	// ---- Restore GL state ----
	glBindFramebuffer(GL_FRAMEBUFFER, oldFBO);
	glViewport(oldViewport[0], oldViewport[1], oldViewport[2], oldViewport[3]);
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	isRenderingCubemap = false;
}

void Renderer::UpdateProceduralSkybox(DirectionalLight& mainLight, const GraphicsSettings* gs, float time)
{
	if (!gs || !gs->volumetricSkyEnabled) return;

	// 1. Create FBO and Quad VAO/VBO if not initialized
	if (skyboxFBO == 0) {
		glGenFramebuffers(1, &skyboxFBO);
	}
	if (skyboxQuadVAO == 0) {
		float quadVertices[] = {
			-1.0f,  1.0f, 0.0f, 0.0f, 1.0f,
			-1.0f, -1.0f, 0.0f, 0.0f, 0.0f,
			 1.0f,  1.0f, 0.0f, 1.0f, 1.0f,
			 1.0f, -1.0f, 0.0f, 1.0f, 0.0f,
		};
		glGenVertexArrays(1, &skyboxQuadVAO);
		glGenBuffers(1, &skyboxQuadVBO);
		glBindVertexArray(skyboxQuadVAO);
		glBindBuffer(GL_ARRAY_BUFFER, skyboxQuadVBO);
		glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), &quadVertices, GL_STATIC_DRAW);
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
		glEnableVertexAttribArray(1);
		glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
		glBindVertexArray(0);
	}

	// 2. Set viewport to match the cubemap size
	GLint oldViewport[4];
	glGetIntegerv(GL_VIEWPORT, oldViewport);
	glViewport(0, 0, 256, 256);

	GLint oldFBO = 0;
	glGetIntegerv(GL_FRAMEBUFFER_BINDING, &oldFBO);

	// 3. Bind FBO and disable testing/blending for screen-space draw
	glBindFramebuffer(GL_FRAMEBUFFER, skyboxFBO);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);

	// 4. Select shader
	Shader* skyShader = nullptr;
	if (gs->skyboxType == SkyboxType::Atmospheric) {
		skyShader = &volumetricSkyShader;
	} else if (gs->skyboxType == SkyboxType::Universe) {
		skyShader = &universeSkyShader;
	}
	if (!skyShader) {
		glEnable(GL_DEPTH_TEST);
		glEnable(GL_BLEND);
		glBindFramebuffer(GL_FRAMEBUFFER, oldFBO);
		glViewport(oldViewport[0], oldViewport[1], oldViewport[2], oldViewport[3]);
		return;
	}

	skyShader->UseShader();

	// 5. Setup Projection and View matrices for the 6 faces
	glm::mat4 proj = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 10.0f);
	glm::mat4 invProj = glm::inverse(proj);

	glm::mat4 views[] = {
		glm::lookAt(glm::vec3(0.0f), glm::vec3( 1.0f,  0.0f,  0.0f), glm::vec3(0.0f, -1.0f,  0.0f)), // POSITIVE_X
		glm::lookAt(glm::vec3(0.0f), glm::vec3(-1.0f,  0.0f,  0.0f), glm::vec3(0.0f, -1.0f,  0.0f)), // NEGATIVE_X
		glm::lookAt(glm::vec3(0.0f), glm::vec3( 0.0f,  1.0f,  0.0f), glm::vec3(0.0f,  0.0f,  1.0f)), // POSITIVE_Y
		glm::lookAt(glm::vec3(0.0f), glm::vec3( 0.0f, -1.0f,  0.0f), glm::vec3(0.0f,  0.0f, -1.0f)), // NEGATIVE_Y
		glm::lookAt(glm::vec3(0.0f), glm::vec3( 0.0f,  0.0f,  1.0f), glm::vec3(0.0f, -1.0f,  0.0f)), // POSITIVE_Z
		glm::lookAt(glm::vec3(0.0f), glm::vec3( 0.0f,  0.0f, -1.0f), glm::vec3(0.0f, -1.0f,  0.0f))  // NEGATIVE_Z
	};

	// 6. Upload common uniforms
	glUniform1f(glGetUniformLocation(skyShader->GetShaderID(), "time"), time);
	if (gs->skyboxType == SkyboxType::Atmospheric) {
		glm::vec3 sunDir = *mainLight.GetDirectionPtr();
		glm::vec3 dirToSun = -glm::normalize(sunDir);
		glUniform3fv(glGetUniformLocation(skyShader->GetShaderID(), "sunDir"), 1, glm::value_ptr(dirToSun));
		glUniform3fv(glGetUniformLocation(skyShader->GetShaderID(), "sunColor"), 1, glm::value_ptr(*mainLight.GetColourPtr()));
		glUniform1i(glGetUniformLocation(skyShader->GetShaderID(), "cloudsEnabled"), gs->cloudsEnabled ? 1 : 0);
		glUniform1f(glGetUniformLocation(skyShader->GetShaderID(), "cloudsDensity"), gs->cloudsDensity);
		glUniform1f(glGetUniformLocation(skyShader->GetShaderID(), "cloudsSpeed"), gs->cloudsSpeed);
		glUniform1f(glGetUniformLocation(skyShader->GetShaderID(), "cloudsSharpness"), gs->cloudsSharpness);
		glUniform3fv(glGetUniformLocation(skyShader->GetShaderID(), "zenithDay"), 1, glm::value_ptr(gs->zenithDay));
		glUniform3fv(glGetUniformLocation(skyShader->GetShaderID(), "horizonDay"), 1, glm::value_ptr(gs->horizonDay));
		glUniform3fv(glGetUniformLocation(skyShader->GetShaderID(), "zenithSunset"), 1, glm::value_ptr(gs->zenithSunset));
		glUniform3fv(glGetUniformLocation(skyShader->GetShaderID(), "horizonSunset"), 1, glm::value_ptr(gs->horizonSunset));
		glUniform3fv(glGetUniformLocation(skyShader->GetShaderID(), "zenithNight"), 1, glm::value_ptr(gs->zenithNight));
		glUniform3fv(glGetUniformLocation(skyShader->GetShaderID(), "horizonNight"), 1, glm::value_ptr(gs->horizonNight));
	} else {
		glUniform1f(glGetUniformLocation(skyShader->GetShaderID(), "starDensity"), gs->universeStarDensity);
		glUniform1f(glGetUniformLocation(skyShader->GetShaderID(), "starBrightness"), gs->universeStarBrightness);
		glUniform1f(glGetUniformLocation(skyShader->GetShaderID(), "nebulaIntensity"), gs->universeNebulaIntensity);
		glUniform1f(glGetUniformLocation(skyShader->GetShaderID(), "universeSpeed"), gs->universeSpeed);
		glUniform3fv(glGetUniformLocation(skyShader->GetShaderID(), "nebulaColor1"), 1, glm::value_ptr(gs->universeNebulaColor1));
		glUniform3fv(glGetUniformLocation(skyShader->GetShaderID(), "nebulaColor2"), 1, glm::value_ptr(gs->universeNebulaColor2));
	}

	glBindVertexArray(skyboxQuadVAO);

	for (int i = 0; i < 6; i++) {
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, proceduralSkyTextureId, 0);
		glClear(GL_COLOR_BUFFER_BIT);

		glm::mat4 invView = glm::inverse(views[i]);
		glUniformMatrix4fv(glGetUniformLocation(skyShader->GetShaderID(), "invProjection"), 1, GL_FALSE, glm::value_ptr(invProj));
		glUniformMatrix4fv(glGetUniformLocation(skyShader->GetShaderID(), "invView"), 1, GL_FALSE, glm::value_ptr(invView));

		glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	}

	glBindVertexArray(0);
	glEnable(GL_DEPTH_TEST);
	glEnable(GL_BLEND);
	glBindFramebuffer(GL_FRAMEBUFFER, oldFBO);
	glViewport(oldViewport[0], oldViewport[1], oldViewport[2], oldViewport[3]);
}
