#include "DirectionalLight.h"
#include <algorithm>
#include "CascadedShadowMap.h"

DirectionalLight::DirectionalLight() : Light()
{
	// We don't initialize CascadedShadowMap here because GLEW might not be ready yet.
	// It will be properly initialized in LoadResources() or when a scene is loaded.
	direction = glm::vec3(-20.0f, -5.0f, 7.0f);
	lightProj = glm::ortho(-50.0f, 50.0f, -50.0f, 50.0f, 0.1f, 200.0f);
}

DirectionalLight::DirectionalLight(GLfloat shadowWidth, GLfloat shadowHeight,
	GLfloat red, GLfloat green, GLfloat blue, GLfloat ambientIntensity,
	GLfloat diffuseIntensity, GLfloat xDirection, GLfloat yDirection, GLfloat zDirection) : Light(shadowWidth, shadowHeight, red, green, blue, ambientIntensity, diffuseIntensity)
{
	// Replace default ShadowMap with CascadedShadowMap
	if (shadowMap) delete shadowMap;
	shadowMap = new CascadedShadowMap();
	((CascadedShadowMap*)shadowMap)->Init((GLuint)shadowWidth, (GLuint)shadowHeight, 3);

	direction = glm::vec3(xDirection, yDirection, zDirection);
	lightProj = glm::ortho(-20.0f, 20.0f, -20.0f, 20.0f, 0.1f, 100.0f);
}

void DirectionalLight::UseLight(GLuint ambientIntensityLocation, GLuint ambientColourLocation, GLuint diffuseIntensityLocation, GLuint directionLocation)
{
	glUniform3f(ambientColourLocation, colour.x, colour.y, colour.z);
	glUniform1f(ambientIntensityLocation, ambientIntensity);
	
	glUniform3f(directionLocation, direction.x, direction.y, direction.z);
	glUniform1f(diffuseIntensityLocation, diffuseIntensity);
}

void DirectionalLight::SetShadowFrustum(float size, float near, float far)
{
	shadowFrustumSize = size;
	lightProj = glm::ortho(-size, size, -size, size, near, far);
}

glm::mat4 DirectionalLight::CalculateLightTransform(glm::vec3 target)
{
	// Center the shadow frustum on the target (e.g. camera) to provide high-quality shadows around the viewer.
	// We move the shadow "eye" back from the target along the light ray.
	return lightProj * glm::lookAt(target + glm::normalize(-direction) * 250.0f, target, glm::vec3(0.0f, 1.0f, 0.0f));
}

void DirectionalLight::CalculateCascadedLightMatrices(const glm::mat4& view, const glm::mat4& projection, float near, float far)
{
	const int cascadeCount = 3; // Fixed for now to match CascadedShadowMap
	cascadedLightMatrices.clear();
	cascadeSplitDistances.clear();

	float cascadeSplits[cascadeCount + 1];
	
	// Split distances (Logarithmic/Linear mix)
	// lambda = 1.0 is purely logarithmic (good for close-up detail, poor for distant coverage)
	// lambda = 0.0 is purely linear (uniform resolution, bad for close-up)
	// 0.8 - 0.9 is better for balancing sharpness at close range with distant coverage
	float lambda = 0.85f; 
	float ratio = far / near;

	cascadeSplits[0] = near;
	for (int i = 1; i < cascadeCount; i++)
	{
		float si = i / (float)cascadeCount;
		cascadeSplits[i] = lambda * (near * powf(ratio, si)) + (1 - lambda) * (near + si * (far - near));
	}
	cascadeSplits[cascadeCount] = far;

	glm::mat4 invVP = glm::inverse(projection * view);

	for (int i = 0; i < cascadeCount; i++)
	{
		float prevSplit = cascadeSplits[i];
		float nextSplit = cascadeSplits[i + 1];
		cascadeSplitDistances.push_back(nextSplit);

		// 1. Find the bounding sphere of this frustum segment
		// The sphere is invariant to camera rotation, which is key for stabilization.
		auto getNDCFromViewDepth = [&](float depth) {
			glm::vec4 clip = projection * glm::vec4(0, 0, -depth, 1.0);
			return clip.z / clip.w;
		};

		float ndcNear = getNDCFromViewDepth(prevSplit);
		float ndcFar = getNDCFromViewDepth(nextSplit);

		glm::vec4 corners[8] = {
			glm::vec4(-1, -1, ndcNear, 1.0), glm::vec4(1, -1, ndcNear, 1.0),
			glm::vec4(-1,  1, ndcNear, 1.0), glm::vec4(1,  1, ndcNear, 1.0),
			glm::vec4(-1, -1, ndcFar, 1.0),  glm::vec4(1, -1, ndcFar, 1.0),
			glm::vec4(-1,  1, ndcFar, 1.0),  glm::vec4(1,  1, ndcFar, 1.0)
		};

		glm::vec3 center(0, 0, 0);
		for (int j = 0; j < 8; j++) {
			corners[j] = invVP * corners[j];
			corners[j] /= corners[j].w;
			center += glm::vec3(corners[j]);
		}
		center /= 8.0f;

		float radius = 0.0f;
		for (int j = 0; j < 8; j++) {
			float distance = glm::length(glm::vec3(corners[j]) - center);
			radius = std::max(radius, distance);
		}
		radius = std::ceil(radius * 16.0f) / 16.0f; // Round up slightly to avoid precision flickering

		// 2. Create stable light view/proj matrices
		glm::vec3 lightDir = glm::normalize(direction);
		
		// The view matrix should be centered on the sphere center
		glm::mat4 lightView = glm::lookAt(center - lightDir * radius * 2.0f, center, glm::vec3(0.0f, 1.0f, 0.0f));
		
		// Projection is a fixed-size square based on the sphere diameter
		glm::mat4 lightProjection = glm::ortho(-radius, radius, -radius, radius, 0.0f, radius * 4.0f);

		// 3. SNAPPING: Snap the light's "eye" to texel increments in light-space
		glm::mat4 shadowMatrix = lightProjection * lightView;
		glm::vec4 shadowOrigin(0.0f, 0.0f, 0.0f, 1.0f);
		shadowOrigin = shadowMatrix * shadowOrigin;
		shadowOrigin *= (float)shadowMap->GetShadowWidth() / 2.0f;

		glm::vec4 roundedOrigin = glm::round(shadowOrigin);
		glm::vec4 roundOffset = roundedOrigin - shadowOrigin;
		roundOffset = roundOffset * 2.0f / (float)shadowMap->GetShadowWidth();
		roundOffset.z = 0.0f;
		roundOffset.w = 0.0f;

		lightProjection[3] += roundOffset;

		cascadedLightMatrices.push_back(lightProjection * lightView);
	}
}

DirectionalLight::~DirectionalLight()
{
}


