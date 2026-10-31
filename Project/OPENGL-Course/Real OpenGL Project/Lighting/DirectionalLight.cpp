#include "Lighting/DirectionalLight.h"
#include <algorithm>
#include <cmath>
#include "Rendering/CascadedShadowMap.h"

DirectionalLight::DirectionalLight() : Light()
{
	// We don't initialize CascadedShadowMap here because GLEW might not be ready yet.
	// It will be properly initialized in LoadResources() or when a scene is loaded.
	direction = glm::vec3(-20.0f, -5.0f, 7.0f);
	lightProj = glm::ortho(-50.0f, 50.0f, -50.0f, 50.0f, 0.1f, 200.0f);
	
	// Initial pitch/yaw from default direction
	direction = glm::normalize(direction);
	pitch = glm::degrees(asin(glm::clamp(-direction.y, -1.0f, 1.0f)));
	yaw = glm::degrees(atan2(direction.z, direction.x));
}

DirectionalLight::DirectionalLight(GLfloat shadowWidth, GLfloat shadowHeight,
	GLfloat red, GLfloat green, GLfloat blue, GLfloat ambientIntensity,
	GLfloat diffuseIntensity, GLfloat xDirection, GLfloat yDirection, GLfloat zDirection) : Light(shadowWidth, shadowHeight, red, green, blue, ambientIntensity, diffuseIntensity)
{
	// Replace default ShadowMap with CascadedShadowMap
	shadowMap = std::make_unique<CascadedShadowMap>();
	static_cast<CascadedShadowMap*>(shadowMap.get())->Init((GLuint)shadowWidth, (GLuint)shadowHeight, 4);

	direction = glm::normalize(glm::vec3(xDirection, yDirection, zDirection));
	lightProj = glm::ortho(-20.0f, 20.0f, -20.0f, 20.0f, 0.1f, 100.0f);

	pitch = glm::degrees(asin(glm::clamp(-direction.y, -1.0f, 1.0f)));
	yaw = glm::degrees(atan2(direction.z, direction.x));
}

void DirectionalLight::UseLight(GLuint ambientIntensityLocation, GLuint ambientColourLocation, GLuint diffuseIntensityLocation, GLuint directionLocation)
{
	if (ambientColourLocation != (GLuint)-1) glUniform3f(ambientColourLocation, colour.x, colour.y, colour.z);
	if (ambientIntensityLocation != (GLuint)-1) glUniform1f(ambientIntensityLocation, ambientIntensity);
	
	glm::vec3 normalizedDir = glm::normalize(direction);
	if (directionLocation != (GLuint)-1) glUniform3f(directionLocation, normalizedDir.x, normalizedDir.y, normalizedDir.z);
	if (diffuseIntensityLocation != (GLuint)-1) glUniform1f(diffuseIntensityLocation, diffuseIntensity);
}

void DirectionalLight::SetDirection(const glm::vec3& dir)
{
	direction = glm::normalize(dir);
	pitch = glm::degrees(asin(glm::clamp(-direction.y, -1.0f, 1.0f)));
	yaw = glm::degrees(atan2(direction.z, direction.x));
}

void DirectionalLight::SetShadowFrustum(float size, float near, float far)
{
	shadowFrustumSize = size;
	lightProj = glm::ortho(-size, size, -size, size, near, far);
}

void DirectionalLight::UpdateDirectionFromEuler()
{
	float p = glm::radians(pitch);
	float y = glm::radians(yaw);
	direction.x = cos(p) * cos(y);
	direction.y = -sin(p); // pitch 90 = zenith (traveling down)
	direction.z = cos(p) * sin(y);
	direction = glm::normalize(direction);
}

glm::mat4 DirectionalLight::CalculateLightTransform(glm::vec3 target)
{
	// Center the shadow frustum on the target (e.g. camera) to provide high-quality shadows around the viewer.
	// We move the shadow "eye" back from the target along the light ray.
	return lightProj * glm::lookAt(target + glm::normalize(-direction) * 250.0f, target, glm::vec3(0.0f, 1.0f, 0.0f));
}

void DirectionalLight::CalculateCascadedLightMatrices(const glm::mat4& view, const glm::mat4& projection, float near, float far, int cascadeCountIn, float splitLambda)
{
	const int cascadeCount = std::max(1, std::min(cascadeCountIn, 4));
	cascadedLightMatrices.clear();
	cascadeSplitDistances.clear();
	cascadeTexelSizes.clear();
	cascadeDepthRanges.clear();
	if (!shadowMap) return;

	// Practical split scheme (Zhang et al., "Parallel-Split Shadow Maps"): a blend of the
	// logarithmic and the uniform distribution. The logarithmic part starts at 1 m, not at the
	// camera's 0.1 m near plane, which would spend whole cascades on the first few metres.
	float splitNear = std::max(near, 1.0f);
	far = std::max(far, splitNear + 1.0f);
	float lambda = glm::clamp(splitLambda, 0.0f, 1.0f);
	float splits[5];
	splits[0] = near;
	for (int i = 1; i < cascadeCount; i++)
	{
		float si = i / (float)cascadeCount;
		float logSplit = splitNear * std::pow(far / splitNear, si);
		float uniformSplit = splitNear + si * (far - splitNear);
		splits[i] = lambda * logSplit + (1.0f - lambda) * uniformSplit;
	}
	splits[cascadeCount] = far;

	glm::mat4 invView = glm::inverse(view);
	glm::vec3 cameraPos = glm::vec3(invView[3]);
	glm::vec3 cameraForward = -glm::normalize(glm::vec3(invView[2]));

	// Half-diagonal of a view slice per metre of depth
	float tanHalfFovY = 1.0f / projection[1][1];
	float aspect = projection[1][1] / projection[0][0];
	float k = tanHalfFovY * std::sqrt(1.0f + aspect * aspect);

	float resolution = (float)shadowMap->GetShadowWidth();
	glm::vec3 lightDir = glm::normalize(direction);
	glm::vec3 up = std::abs(lightDir.y) > 0.99f ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);

	for (int i = 0; i < cascadeCount; i++)
	{
		float n = splits[i], f = splits[i + 1];
		cascadeSplitDistances.push_back(f);

		// Smallest sphere around the slice [n, f] of the view frustum. Its size depends only on
		// the split distances and the field of view, never on where the camera looks, so the
		// shadow map keeps its scale and only slides (in whole texels, see below) — stable,
		// shimmer-free shadows (Valient, "Stable Cascaded Shadow Maps", ShaderX6).
		float centerDepth = 0.5f * (n + f) * (1.0f + k * k);
		float radius;
		if (centerDepth >= f) {
			centerDepth = f;
			radius = f * k;
		} else {
			radius = std::sqrt((f - centerDepth) * (f - centerDepth) + f * f * k * k);
		}
		radius = std::ceil(radius * 4.0f) / 4.0f;
		glm::vec3 center = cameraPos + cameraForward * centerDepth;

		// The depth range reaches far toward the light: casters outside the slice (a hill, a tall
		// tree behind the camera) still have to land in the map
		float zMult = std::max(2000.0f, radius * 4.0f);
		glm::mat4 lightView = glm::lookAt(center - lightDir * radius, center, up);
		glm::mat4 lightProjection = glm::ortho(-radius, radius, -radius, radius, -zMult, zMult);

		// Snap the projection to the texel grid: moving the camera shifts the map by whole texels
		glm::mat4 shadowMatrix = lightProjection * lightView;
		glm::vec4 shadowOrigin = shadowMatrix * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
		shadowOrigin *= resolution / 2.0f;
		glm::vec2 roundedOrigin = glm::round(glm::vec2(shadowOrigin.x, shadowOrigin.y));
		glm::vec2 roundOffset = (roundedOrigin - glm::vec2(shadowOrigin.x, shadowOrigin.y)) * 2.0f / resolution;
		lightProjection[3][0] += roundOffset.x;
		lightProjection[3][1] += roundOffset.y;

		cascadedLightMatrices.push_back(lightProjection * lightView);
		cascadeTexelSizes.push_back(2.0f * radius / resolution);
		cascadeDepthRanges.push_back(2.0f * zMult);
	}
}

void DirectionalLight::EnsureCascadeMap(int resolution, int cascadeCount)
{
	resolution = std::max(256, std::min(resolution, 8192));
	cascadeCount = std::max(1, std::min(cascadeCount, 4));
	CascadedShadowMap* csm = dynamic_cast<CascadedShadowMap*>(shadowMap.get());
	if (csm && csm->GetShadowWidth() == (GLuint)resolution && csm->GetCascadeCount() == (GLuint)cascadeCount) return;
	if (!csm) {
		shadowMap = std::make_unique<CascadedShadowMap>();
		csm = static_cast<CascadedShadowMap*>(shadowMap.get());
	}
	csm->Init((GLuint)resolution, (GLuint)resolution, (GLuint)cascadeCount);
}

DirectionalLight::~DirectionalLight()
{
}


