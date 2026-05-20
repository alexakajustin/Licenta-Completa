#pragma once
#include "Light.h"
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
	void CalculateCascadedLightMatrices(const glm::mat4& view, const glm::mat4& projection, float near, float far, int cascadeCount = 4);
	const std::vector<glm::mat4>& GetCascadedLightMatrices() const { return cascadedLightMatrices; }
	const std::vector<float>& GetCascadeSplitDistances() const { return cascadeSplitDistances; }

	// Setters/Getters
	void SetDirection(const glm::vec3& dir);
	glm::vec3* GetDirectionPtr() { return &direction; }
	float* GetPitchPtr() { return &pitch; }
	float* GetYawPtr() { return &yaw; }
	void UpdateDirectionFromEuler();

	float GetShadowFrustumSize() const { return shadowFrustumSize; }

	~DirectionalLight();
private:
	glm::vec3 direction;
	float pitch = 0.0f;
	float yaw = 0.0f;
	float shadowFrustumSize = 100.0f;

	// CSM Data
	std::vector<glm::mat4> cascadedLightMatrices;
	std::vector<float> cascadeSplitDistances;
};


