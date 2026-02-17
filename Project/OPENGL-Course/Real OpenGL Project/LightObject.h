#pragma once

#include <string>
#include <glm/glm.hpp>
#include "DirectionalLight.h"
#include "PointLight.h"
#include "SpotLight.h"

enum class LightType
{
	Directional,
	Point,
	Spot
};

// Wrapper class to make lights appear in the scene hierarchy
class LightObject
{
public:
	LightObject(const std::string& name, DirectionalLight* light);
	LightObject(const std::string& name, PointLight* light);
	LightObject(const std::string& name, SpotLight* light);
	~LightObject();

	const std::string& GetName() const { return name; }
	LightType GetLightType() const { return lightType; }

	// Get underlying light
	DirectionalLight* GetDirectionalLight() { return directionalLight; }
	PointLight* GetPointLight() { return pointLight; }
	SpotLight* GetSpotLight() { return spotLight; }
	void SetPointLight(PointLight* p) { pointLight = p; }
	void SetSpotLight(SpotLight* s) { spotLight = s; }

	// Get editable properties (pointers for ImGui)
	glm::vec3* GetColorPtr();
	float* GetAmbientIntensityPtr();
	float* GetDiffuseIntensityPtr();
	glm::vec3* GetPositionPtr();  // For point/spot
	glm::vec3* GetDirectionPtr(); // For directional/spot

	// Attenuation (for point/spot lights)
	float* GetConstantPtr();
	float* GetLinearPtr();
	float* GetExponentPtr();

private:
	std::string name;
	LightType lightType;

	DirectionalLight* directionalLight;
	PointLight* pointLight;
	SpotLight* spotLight;
};
