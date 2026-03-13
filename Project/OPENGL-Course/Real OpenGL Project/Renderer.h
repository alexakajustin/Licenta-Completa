#pragma once

#include <vector>
#include <GL/glew.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "CommonValues.h"
#include "Shader.h"
#include "DirectionalLight.h"
#include "PointLight.h"
#include "SpotLight.h"
#include "Skybox.h"

class SceneManager;
class Camera;
class Window;

class Renderer
{
public:
	Renderer();
	~Renderer();

	void Init();
	void LoadSkybox(const std::vector<std::string>& faces);

	// Render passes
	void DirectionalShadowMapPass(DirectionalLight* light, SceneManager& scene);
	void OmniShadowMapPass(PointLight* light, SceneManager& scene);
	void RenderPass(const glm::mat4& projection, const glm::mat4& view, 
					const glm::vec3& cameraPos, SceneManager& scene,
					DirectionalLight& mainLight,
					PointLight* pointLights, unsigned int pointLightCount,
					SpotLight* spotLights, unsigned int spotLightCount,
					int fbw, int fbh);

	Shader& GetMainShader() { return mainShader; }

private:
	Shader mainShader;
	Shader directionalShadowShader;
	Shader omniShadowShader;
	Skybox skybox;

	// Cached uniform locations (fetched once at init)
	GLint uniformModel, uniformProjection, uniformView;
	GLint uniformEyePosition, uniformSpecularIntensity, uniformShininess, uniformMaterialColor;
	GLint uniformOmniLightPos, uniformFarPlane, uniformUseNormalMap, uniformUseDiffuseTexture;

	void CacheUniforms();
};
