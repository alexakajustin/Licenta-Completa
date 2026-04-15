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
	void DirectionalShadowMapPass(DirectionalLight* light, SceneManager& scene, const glm::vec3& cameraPos);
	void OmniShadowMapPass(PointLight* light, SceneManager& scene);
	void RenderPass(const glm::mat4& projection, const glm::mat4& view, 
					const glm::vec3& cameraPos, SceneManager& scene,
					DirectionalLight& mainLight,
					PointLight* pointLights, unsigned int pointLightCount,
					SpotLight* spotLights, unsigned int spotLightCount,
					int fbw, int fbh);

	Shader& GetMainShader() { return mainShader; }
	Shader* GetInstancedShader(Shader* original);

private:
	Shader mainShader;
	Shader directionalShadowShader;
	Shader omniShadowShader;
	Shader instancedCullShader;    // GPU compute shader for frustum culling
	Shader instancedRenderShader;  // Vertex/Fragment shader for instanced objects
	Shader instancedShadowShader;  // Vertex/Fragment shader for instanced shadow pass
	Skybox skybox;

	// Cached uniform locations (fetched once at init)
	GLint uniformModel, uniformProjection, uniformView, uniformEyePosition,
		uniformSpecularIntensity, uniformShininess, uniformMaterialColor,
		uniformTiling, uniformOffset,
		uniformOmniLightPos, uniformFarPlane,
		uniformUseNormalMap, uniformUseDiffuseTexture, uniformUseInstancing;

	void CacheUniforms();
	
	std::map<std::string, Shader*> instancedShaderCache; // Key: vertexPath + fragmentPath
};
