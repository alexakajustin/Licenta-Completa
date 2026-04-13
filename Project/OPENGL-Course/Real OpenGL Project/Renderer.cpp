#include "Renderer.h"
#include "SceneManager.h"
#include "Camera.h"
#include "Window.h"
#include "Frustum.h"
#include <GLFW/glfw3.h>

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
}

void Renderer::Init()
{
	mainShader.CreateFromFiles("Assets/Shaders/shader.vert", "Assets/Shaders/shader.frag");
	directionalShadowShader.CreateFromFiles("Shaders/directional_shadow_map.vert", "Shaders/directional_shadow_map.frag");
	omniShadowShader.CreateFromFiles("Shaders/omni_shadow_map.vert", "Shaders/omni_shadow_map.geom", "Shaders/omni_shadow_map.frag");

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

void Renderer::DirectionalShadowMapPass(DirectionalLight* light, SceneManager& scene, const glm::vec3& cameraPos)
{
	directionalShadowShader.UseShader();

	glViewport(0, 0, light->GetShadowMap()->GetShadowWidth(), light->GetShadowMap()->GetShadowHeight());

	light->GetShadowMap()->Write();
	glClearColor(1.0f, 1.0f, 1.0f, 1.0f); // default alpha 1.0
	glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);

	GLint shadowModelLoc = directionalShadowShader.GetModelLocation();
	glm::mat4 lightProjView = light->CalculateLightTransform(cameraPos);
	directionalShadowShader.SetDirectionalLightTransform(lightProjView);

	directionalShadowShader.Validate();

	Frustum dirFrustum = Frustum::CreateFrustumFromMatrix(lightProjView);
	scene.RenderAll(glm::mat4(1.0f), glm::mat4(1.0f), cameraPos, light, nullptr, 0, nullptr, 0, 0.0f, &dirFrustum, &directionalShadowShader);

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

	scene.RenderAll(glm::mat4(1.0f), glm::mat4(1.0f), light->GetPosition(), nullptr, nullptr, 0, nullptr, 0, 0.0f, nullptr, &omniShadowShader);

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Renderer::RenderPass(const glm::mat4& projection, const glm::mat4& view,
						  const glm::vec3& cameraPos, SceneManager& scene,
						  DirectionalLight& mainLight,
						  PointLight* pointLights, unsigned int pointLightCount,
						  SpotLight* spotLights, unsigned int spotLightCount,
						  int fbw, int fbh)
{
	glViewport(0, 0, fbw, fbh);
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	// Skybox
	glDisable(GL_BLEND);
	glDisable(GL_CULL_FACE);
	skybox.DrawSkybox(view, projection);
	glEnable(GL_CULL_FACE);

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

	mainShader.SetDirectionalLight(&mainLight);
	mainShader.SetPointLights(pointLights, pointLightCount, 4, 0);
	mainShader.SetSpotLights(spotLights, spotLightCount, 4 + pointLightCount, pointLightCount);
	mainShader.SetDirectionalLightTransform(mainLight.CalculateLightTransform(cameraPos));

	mainLight.GetShadowMap()->Read(GL_TEXTURE3);
	mainLight.GetShadowMap()->ReadColor(GL_TEXTURE20);
	mainShader.SetTexture(0);
	mainShader.SetNormalMap(1);
	mainShader.SetDirectionalShadowMap(3);
	mainShader.SetDirectionalShadowColorMap(20);

	// mainShader.Validate(); 
	
	// Scene objects with Frustum Culling
	Frustum frustum = Frustum::CreateFrustumFromMatrix(projection * view);
	float time = (float)glfwGetTime();
	scene.RenderAll(projection, view, cameraPos, &mainLight, pointLights, pointLightCount, spotLights, spotLightCount, time, &frustum, nullptr, (float)fbh);

	// Disable blending for icons/gizmos overlay
	glDisable(GL_BLEND);

	// Clear depth only so icons/gizmos draw over scene but inter-occlude
	glClear(GL_DEPTH_BUFFER_BIT);

	// Light icons + gizmos
	scene.RenderIcons(projection, view);
	scene.RenderGizmo(projection, view, cameraPos);
}
