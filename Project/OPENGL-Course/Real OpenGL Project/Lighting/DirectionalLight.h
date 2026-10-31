#pragma once
#include "Lighting/Light.h"
#include <vector>

class DirectionalLight : public Light
{
public:
	DirectionalLight();

	DirectionalLight(GLfloat shadowWidth, GLfloat shadowHeight,
		GLfloat red, GLfloat green, GLfloat blue, GLfloat ambientIntensity, GLfloat diffuseIntensity,
		GLfloat xDirection, GLfloat yDirection, GLfloat zDirection);

	void UseLight(GLuint ambientIntensityLocation, GLuint ambientColourLocation,
		GLuint diffuseIntensityLocation, GLuint directionLocation);

	void SetShadowFrustum(float size, float near, float far);

	glm::mat4 CalculateLightTransform(glm::vec3 target);

	// Cascaded Shadow Map methods
	// splitLambda blends the split distances: 0 = even, 1 = logarithmic (sharper near the camera)
	void CalculateCascadedLightMatrices(const glm::mat4& view, const glm::mat4& projection, float near, float far, int cascadeCount = 4, float splitLambda = 0.75f);
	const std::vector<glm::mat4>& GetCascadedLightMatrices() const { return cascadedLightMatrices; }
	const std::vector<float>& GetCascadeSplitDistances() const { return cascadeSplitDistances; }
	const std::vector<float>& GetCascadeTexelSizes() const { return cascadeTexelSizes; }   // World size of one shadow texel
	const std::vector<float>& GetCascadeDepthRanges() const { return cascadeDepthRanges; } // Depth span of each cascade's projection

	// (Re)allocates the cascade texture array when the resolution or the cascade count changes
	void EnsureCascadeMap(int resolution, int cascadeCount);

	// Setters/Getters
	void SetDirection(const glm::vec3& dir);
	glm::vec3* GetDirectionPtr() { return &direction; }
	float* GetPitchPtr() { return &pitch; }
	float* GetYawPtr() { return &yaw; }
	void UpdateDirectionFromEuler();

	float GetShadowFrustumSize() const { return shadowFrustumSize; }

	~DirectionalLight();
	DirectionalLight(DirectionalLight&&) = default;
	DirectionalLight& operator=(DirectionalLight&&) = default;

private:
	glm::vec3 direction;
	float pitch = 0.0f;
	float yaw = 0.0f;
	float shadowFrustumSize = 100.0f;

	// CSM Data
	std::vector<glm::mat4> cascadedLightMatrices;
	std::vector<float> cascadeSplitDistances;
	std::vector<float> cascadeTexelSizes;
	std::vector<float> cascadeDepthRanges;
};


