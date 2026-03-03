#include "Renderer.h"
#include "SceneManager.h"
#include "Camera.h"
#include "Window.h"

Renderer::Renderer()
	: uniformModel(-1), uniformProjection(-1), uniformView(-1),
	  uniformEyePosition(-1), uniformSpecularIntensity(-1), uniformShininess(-1),
	  uniformOmniLightPos(-1), uniformFarPlane(-1), uniformUseNormalMap(-1)
{
}

Renderer::~Renderer()
{
}

void Renderer::Init()
{
	mainShader.CreateFromFiles("Shaders/shader.vert", "Shaders/shader.frag");
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
	uniformUseNormalMap = glGetUniformLocation(mainShader.GetShaderID(), "useNormalMap");
}

void Renderer::DirectionalShadowMapPass(DirectionalLight* light, SceneManager& scene)
{
	directionalShadowShader.UseShader();

	glViewport(0, 0, light->GetShadowMap()->GetShadowWidth(), light->GetShadowMap()->GetShadowHeight());

	light->GetShadowMap()->Write();
	glClear(GL_DEPTH_BUFFER_BIT);

	GLint shadowModelLoc = directionalShadowShader.GetModelLocation();
	directionalShadowShader.SetDirectionalLightTransform(light->CalculateLightTransform());

	directionalShadowShader.Validate();

	scene.RenderAll(shadowModelLoc, -1, -1, -1, -1);

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Renderer::OmniShadowMapPass(PointLight* light, SceneManager& scene)
{
	omniShadowShader.UseShader();

	glViewport(0, 0, light->GetShadowMap()->GetShadowWidth(), light->GetShadowMap()->GetShadowHeight());

	light->GetShadowMap()->Write();
	glClear(GL_DEPTH_BUFFER_BIT);

	GLint shadowModelLoc = omniShadowShader.GetModelLocation();
	GLint omniLightPosLoc = omniShadowShader.getOmniLightPosLocation();
	GLint farPlaneLoc = omniShadowShader.getFarPlaneLocation();

	glUniform3f(omniLightPosLoc, light->GetPosition().x, light->GetPosition().y, light->GetPosition().z);
	glUniform1f(farPlaneLoc, light->GetFarPlane());
	omniShadowShader.SetLightMatrices(light->CalculateLightTransform());

	omniShadowShader.Validate();

	scene.RenderAll(shadowModelLoc, -1, -1, -1, -1);

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Renderer::RenderPass(const glm::mat4& projection, const glm::mat4& view,
						  const glm::vec3& cameraPos, SceneManager& scene,
						  DirectionalLight& mainLight,
						  PointLight* pointLights, unsigned int pointLightCount,
						  SpotLight* spotLights, unsigned int spotLightCount,
						  Window& window)
{
	glViewport(0, 0, (GLint)window.getBufferWidth(), (GLint)window.getBufferHeight());
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	// Skybox
	glDisable(GL_CULL_FACE);
	skybox.DrawSkybox(view, projection);
	glEnable(GL_CULL_FACE);

	// Main shader
	mainShader.UseShader();

	glUniformMatrix4fv(uniformProjection, 1, GL_FALSE, glm::value_ptr(projection));
	glUniformMatrix4fv(uniformView, 1, GL_FALSE, glm::value_ptr(view));
	glUniform3f(uniformEyePosition, cameraPos.x, cameraPos.y, cameraPos.z);

	mainShader.SetDirectionalLight(&mainLight);
	mainShader.SetPointLights(pointLights, pointLightCount, 4, 0);
	mainShader.SetSpotLights(spotLights, spotLightCount, 4 + pointLightCount, pointLightCount);
	mainShader.SetDirectionalLightTransform(mainLight.CalculateLightTransform());

	mainLight.GetShadowMap()->Read(GL_TEXTURE3);
	mainShader.SetTexture(1);
	mainShader.SetNormalMap(2);
	mainShader.SetDirectionalShadowMap(3);

	mainShader.Validate();

	// Scene objects
	scene.RenderAll(uniformModel, uniformSpecularIntensity, uniformShininess, uniformMaterialColor, uniformUseNormalMap);

	// Clear depth only so icons/gizmos draw over scene but inter-occlude
	glClear(GL_DEPTH_BUFFER_BIT);

	// Light icons + gizmos
	scene.RenderIcons(projection, view);
	scene.RenderGizmo(projection, view, cameraPos);
}
