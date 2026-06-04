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

/**
 * @class Renderer
 * @brief Handles rendering operations, drawing stages, shadow map generation, and viewport/reflection passes.
 */
class Renderer
{
public:
	/**
	 * @brief Constructor initializing default shader locations.
	 */
	Renderer();

	/**
	 * @brief Destructor releasing instanced hybrid shaders.
	 */
	~Renderer();

	/**
	 * @brief Compiles primary render shaders and setup default state.
	 */
	void Init();

	/**
	 * @brief Configures the standard cubemap skybox texture set.
	 * @param faces Vector of 6 filepaths for standard skybox faces.
	 */
	void LoadSkybox(const std::vector<std::string>& faces);

	// Render passes
	
	/**
	 * @brief Shadow mapping generation pass for directional lights.
	 * @param light Pointer to target directional light.
	 * @param scene Scene containing geometry.
	 * @param cameraPos Position of current camera.
	 * @param projection Active projection matrix.
	 * @param view Active view matrix.
	 * @param near Near clipping plane distance.
	 * @param far Far clipping plane distance.
	 * @param gs Active configuration settings.
	 */
	void DirectionalShadowMapPass(DirectionalLight* light, SceneManager& scene, const glm::vec3& cameraPos, const glm::mat4& projection, const glm::mat4& view, float near, float far, const GraphicsSettings* gs);
	
	/**
	 * @brief Shadow mapping generation pass for omnidirectional point lights.
	 * @param light Pointer to target point light.
	 * @param scene Scene containing geometry.
	 * @param gs Active configuration settings.
	 */
	void OmniShadowMapPass(PointLight* light, SceneManager& scene, const GraphicsSettings* gs);
	
	/**
	 * @brief Core scene rendering pass.
	 * @param projection Projection transformation matrix.
	 * @param view View transformation matrix.
	 * @param cameraPos Camera position vector.
	 * @param scene Target scene manager.
	 * @param mainLight Directional light.
	 * @param pointLights Array of point lights.
	 * @param pointLightCount Active point light count.
	 * @param spotLights Array of spot lights.
	 * @param spotLightCount Active spot light count.
	 * @param fbw Viewport width.
	 * @param fbh Viewport height.
	 * @param sceneDepthTexture Optional reference to depth texture.
	 * @param reflectionTexture Optional reference to reflection texture.
	 * @param refractionTexture Optional reference to refraction texture.
	 * @param debugFrustum Optional custom culling frustum.
	 * @param gs Global configuration settings.
	 */
	void RenderPass(const glm::mat4& projection, const glm::mat4& view, const glm::vec3& cameraPos, SceneManager& scene, 
		DirectionalLight& mainLight, PointLight* pointLights, unsigned int pointLightCount,
		SpotLight* spotLights, unsigned int spotLightCount, int fbw, int fbh, GLuint sceneDepthTexture = 0, GLuint reflectionTexture = 0, GLuint refractionTexture = 0,
		const struct Frustum* debugFrustum = nullptr, const GraphicsSettings* gs = nullptr);

	/**
	 * @brief Reflection water rendering pass.
	 * @param projection Projection transformation matrix.
	 * @param view View transformation matrix.
	 * @param cameraPos Camera position vector.
	 * @param scene Target scene manager.
	 * @param mainLight Directional light.
	 * @param pointLights Array of point lights.
	 * @param pointLightCount Active point light count.
	 * @param spotLights Array of spot lights.
	 * @param spotLightCount Active spot light count.
	 * @param fbw Viewport width.
	 * @param fbh Viewport height.
	 * @param waterHeight Clipping boundary y coordinate.
	 * @param gs Global configuration settings.
	 */
	void ReflectionPass(const glm::mat4& projection, const glm::mat4& view,
						const glm::vec3& cameraPos, SceneManager& scene,
						DirectionalLight& mainLight,
						PointLight* pointLights, unsigned int pointLightCount,
						SpotLight* spotLights, unsigned int spotLightCount,
						int fbw, int fbh, float waterHeight, const GraphicsSettings* gs = nullptr);

	/**
	 * @brief Gets reference to main shader object.
	 */
	Shader& GetMainShader() { return mainShader; }

	const Skybox& GetSkybox() const { return skybox; }

	/**
	 * @brief Gets reference to terrain displacement tessellation shader.
	 */
	Shader& GetTessShader() { return tessShader; }

	/**
	 * @brief Gets reference to shadow tessellation shader.
	 */
	Shader& GetTessShadowShader() { return directionalShadowTessShader; }

	/**
	 * @brief Gets reference to directional shadow shader.
	 */
	Shader& GetDirectionalShadowShader() { return directionalShadowShader; }

	/**
	 * @brief Automates shader instancification hybrid caching for GPU draw calls.
	 */
	Shader* GetInstancedShader(Shader* original);

	GLuint GetProceduralSkyTextureID() const { return proceduralSkyTextureId; }


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

	GLuint proceduralSkyTextureId = 0;
	GLuint skyboxFBO = 0;
	GLuint skyboxQuadVAO = 0;
	GLuint skyboxQuadVBO = 0;
	Shader volumetricSkyShader;
	Shader universeSkyShader;

	void UpdateProceduralSkybox(DirectionalLight& mainLight, const GraphicsSettings* gs, float time);

	// Cached uniform locations (fetched once at init)
	GLint uniformModel, uniformProjection, uniformView, uniformEyePosition,
		uniformSpecularIntensity, uniformShininess, uniformMaterialColor,
		uniformTiling, uniformOffset,
		uniformOmniLightPos, uniformFarPlane,
		uniformUseNormalMap, uniformUseDiffuseTexture, uniformUseInstancing;

	void CacheUniforms();
	
	std::map<std::string, Shader*> instancedShaderCache; // Key: vertexPath + fragmentPath
};
