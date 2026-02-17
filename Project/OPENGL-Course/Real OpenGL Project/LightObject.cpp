#include "LightObject.h"

LightObject::LightObject(const std::string& name, DirectionalLight* light)
	: name(name), lightType(LightType::Directional), 
	  directionalLight(light), pointLight(nullptr), spotLight(nullptr)
{
}

LightObject::LightObject(const std::string& name, PointLight* light)
	: name(name), lightType(LightType::Point),
	  directionalLight(nullptr), pointLight(light), spotLight(nullptr)
{
}

LightObject::LightObject(const std::string& name, SpotLight* light)
	: name(name), lightType(LightType::Spot),
	  directionalLight(nullptr), pointLight(nullptr), spotLight(light)
{
}

LightObject::~LightObject()
{
	// Don't delete the lights - they're managed elsewhere
}

glm::vec3* LightObject::GetColorPtr()
{
	switch (lightType)
	{
	case LightType::Directional:
		return directionalLight->GetColourPtr();
	case LightType::Point:
		return pointLight->GetColourPtr();
	case LightType::Spot:
		return spotLight->GetColourPtr();
	}
	return nullptr;
}

float* LightObject::GetAmbientIntensityPtr()
{
	switch (lightType)
	{
	case LightType::Directional:
		return directionalLight->GetAmbientIntensityPtr();
	case LightType::Point:
		return pointLight->GetAmbientIntensityPtr();
	case LightType::Spot:
		return spotLight->GetAmbientIntensityPtr();
	}
	return nullptr;
}

float* LightObject::GetDiffuseIntensityPtr()
{
	switch (lightType)
	{
	case LightType::Directional:
		return directionalLight->GetDiffuseIntensityPtr();
	case LightType::Point:
		return pointLight->GetDiffuseIntensityPtr();
	case LightType::Spot:
		return spotLight->GetDiffuseIntensityPtr();
	}
	return nullptr;
}

glm::vec3* LightObject::GetPositionPtr()
{
	switch (lightType)
	{
	case LightType::Point:
		return pointLight->GetPositionPtr();
	case LightType::Spot:
		return spotLight->GetPositionPtr();
	default:
		return nullptr; // Directional lights don't have position
	}
}

glm::vec3* LightObject::GetDirectionPtr()
{
	switch (lightType)
	{
	case LightType::Directional:
		return directionalLight->GetDirectionPtr();
	case LightType::Spot:
		return spotLight->GetDirectionPtr();
	default:
		return nullptr; // Point lights don't have direction
	}
}

void LightObject::SetPosition(const glm::vec3& pos)
{
	glm::vec3* p = GetPositionPtr();
	if (p) *p = pos;
}

float* LightObject::GetConstantPtr()
{
	switch (lightType)
	{
	case LightType::Point:
		return pointLight->GetConstantPtr();
	case LightType::Spot:
		return spotLight->GetConstantPtr();
	default:
		return nullptr;
	}
}

float* LightObject::GetLinearPtr()
{
	switch (lightType)
	{
	case LightType::Point:
		return pointLight->GetLinearPtr();
	case LightType::Spot:
		return spotLight->GetLinearPtr();
	default:
		return nullptr;
	}
}

float* LightObject::GetExponentPtr()
{
	switch (lightType)
	{
	case LightType::Point:
		return pointLight->GetExponentPtr();
	case LightType::Spot:
		return spotLight->GetExponentPtr();
	default:
		return nullptr;
	}
}
