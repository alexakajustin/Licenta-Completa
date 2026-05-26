#pragma once

#include <vector>
#include <GL/glew.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "CommonValues.h"
#include "Rendering/Shader.h"
#include "Lighting/DirectionalLight.h"
#include "Lighting/PointLight.h"
#include "Lighting/SpotLight.h"
#include "Rendering/Skybox.h"

class SceneManager;
class Camera;
class Window;
struct GraphicsSettings;

class Renderer
{
public:
	Renderer();
	~Renderer();

	void Init();
	void LoadSkybox(const std::vector<std::string>& faces);

	// Render passes
	void DirectionalShadowMapPass(DirectionalLight* light, SceneManager& scene, const glm::vec3& cameraPos, const glm::mat4& projection, const glm::mat4& view, float near, float far, const GraphicsSettings* gs);
	void OmniShadowMapPass(PointLight* light, SceneManager& scene, const GraphicsSettings* gs);
	void RenderPass(const glm::mat4& projection, const glm::mat4& view, const glm::vec3& cameraPos, SceneManager& scene, 
		DirectionalLight& mainLight, PointLight* pointLights, unsigned int pointLightCount,
		SpotLight* spotLights, unsigned int spotLightCount, int fbw, int fbh, GLuint sceneDepthTexture = 0, GLuint reflectionTexture = 0, GLuint refractionTexture = 0,
		const struct Frustum* debugFrustum = nullptr, const GraphicsSettings* gs = nullptr);

	void ReflectionPass(const glm::mat4& projection, const glm::mat4& view,
						const glm::vec3& cameraPos, SceneManager& scene,
						DirectionalLight& mainLight,
						PointLight* pointLights, unsigned int pointLightCount,
						SpotLight* spotLights, unsigned int spotLightCount,
						int fbw, int fbh, float waterHeight, const GraphicsSettings* gs = nullptr);

	Shader& GetMainShader() { return mainShader; }
	Shader& GetTessShader() { return tessShader; }
	Shader& GetTessShadowShader() { return directionalShadowTessShader; }
	Shader& GetDirectionalShadowShader() { return directionalShadowShader; }
	Shader* GetInstancedShader(Shader* original);

private:
	Shader mainShader;
	Shader directionalShadowShader;
	Shader omniShadowShader;
	Shader instancedCullShader;    // GPU compute shader for frustum culling
	Shader instancedRenderShader;  // Vertex/Fragment shader for instanced objects
	Shader instancedShadowShader;  // Vertex/Fragment shader for instanced shadow pass (directional)
	Shader instancedOmniShadowShader; // Vertex/Geom/Fragment shader for instanced omni shadow pass (point/spot)
	Shader tessShader;             // Tessellation shader (vert+tcs+tes+frag) for GPU displacement
	Shader directionalShadowTessShader; // Tessellation shader for shadow pass
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
