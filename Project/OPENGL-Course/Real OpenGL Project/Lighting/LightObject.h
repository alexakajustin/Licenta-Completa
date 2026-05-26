#pragma once

#include <string>
#include <glm/glm.hpp>
#include "Lighting/DirectionalLight.h"
#include "Lighting/PointLight.h"
#include "Lighting/SpotLight.h"

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
	void SetName(const std::string& newName) { name = newName; }
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
	float* GetPitchPtr();         // For directional
	float* GetYawPtr();           // For directional
	void SetPosition(const glm::vec3& pos);

	// Attenuation (for point/spot lights)
	float* GetConstantPtr();
	float* GetLinearPtr();
	float* GetExponentPtr();
	float* GetSpotEdgePtr();

private:
	std::string name;
	LightType lightType;

	DirectionalLight* directionalLight;
	PointLight* pointLight;
	SpotLight* spotLight;
};
