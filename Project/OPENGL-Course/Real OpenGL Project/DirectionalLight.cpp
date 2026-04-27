#include "DirectionalLight.h"
#include <algorithm>
#include "CascadedShadowMap.h"

DirectionalLight::DirectionalLight() : Light()
{
	// We don't initialize CascadedShadowMap here because GLEW might not be ready yet.
	// It will be properly initialized in LoadResources() or when a scene is loaded.
	direction = glm::vec3(-20.0f, -5.0f, 7.0f);
	lightProj = glm::ortho(-50.0f, 50.0f, -50.0f, 50.0f, 0.1f, 200.0f);
	
	// Initial pitch/yaw from default direction
	pitch = glm::degrees(asin(glm::clamp(direction.y, -1.0f, 1.0f)));
	yaw = glm::degrees(atan2(direction.z, direction.x));
}

DirectionalLight::DirectionalLight(GLfloat shadowWidth, GLfloat shadowHeight,
	GLfloat red, GLfloat green, GLfloat blue, GLfloat ambientIntensity,
	GLfloat diffuseIntensity, GLfloat xDirection, GLfloat yDirection, GLfloat zDirection) : Light(shadowWidth, shadowHeight, red, green, blue, ambientIntensity, diffuseIntensity)
{
	// Replace default ShadowMap with CascadedShadowMap
	if (shadowMap) delete shadowMap;
	shadowMap = new CascadedShadowMap();
	((CascadedShadowMap*)shadowMap)->Init((GLuint)shadowWidth, (GLuint)shadowHeight, 4);

	direction = glm::vec3(xDirection, yDirection, zDirection);
	lightProj = glm::ortho(-20.0f, 20.0f, -20.0f, 20.0f, 0.1f, 100.0f);

	pitch = glm::degrees(asin(glm::clamp(direction.y, -1.0f, 1.0f)));
	yaw = glm::degrees(atan2(direction.z, direction.x));
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

void DirectionalLight::UpdateDirectionFromEuler()
{
	float p = glm::radians(pitch);
	float y = glm::radians(yaw);
	direction.x = cos(p) * cos(y);
	direction.y = sin(p);
	direction.z = cos(p) * sin(y);
	direction = glm::normalize(direction);
}

glm::mat4 DirectionalLight::CalculateLightTransform(glm::vec3 target)
{
	// Center the shadow frustum on the target (e.g. camera) to provide high-quality shadows around the viewer.
	// We move the shadow "eye" back from the target along the light ray.
	return lightProj * glm::lookAt(target + glm::normalize(-direction) * 250.0f, target, glm::vec3(0.0f, 1.0f, 0.0f));
}

void DirectionalLight::CalculateCascadedLightMatrices(const glm::mat4& view, const glm::mat4& projection, float near, float far)
{
	const int cascadeCount = 4; // Fixed for now to match CascadedShadowMap
	cascadedLightMatrices.clear();
	cascadeSplitDistances.clear();

	float cascadeSplits[cascadeCount + 1];
	
	// Split distances (Logarithmic/Linear mix)
	// lambda = 0.8 - 0.9 is better for balancing sharpness at close range with distant coverage
	// Using 0.95 to keep the first cascade very tight even at large shadow distances (preventing blur)
	float lambda = 0.95f; 
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
		// Snap center to world grid to prevent sub-texel camera movement from affecting light view
		center = glm::floor(center * 2.0f) / 2.0f; 


		// 1. Find a rotation-invariant bounding sphere for this frustum segment
		// Instead of calculating from corners (which can fluctuate), we use a stable radius
		// based on the split distance and the FOV.
		float tanHalfFOV = 1.0f / projection[1][1];
		float aspect = projection[1][1] / projection[0][0];
		
		float h = nextSplit * tanHalfFOV;
		float w = h * aspect;
		float radius = std::sqrt(nextSplit * nextSplit + h * h + w * w);
		
		// Round to a coarse step to be extra safe
		radius = std::ceil(radius / 8.0f) * 8.0f;

		// 2. Create stable light view/proj matrices
		glm::vec3 lightDir = glm::normalize(direction);
		
		// Use a fixed UP vector unless light is parallel to it
		glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);
		if (std::abs(glm::dot(lightDir, up)) > 0.99f) {
			up = glm::vec3(0.0f, 0.0f, 1.0f);
		}

		// Fixed radius quantization to prevent flickering as the frustum segment shifts
		// We use a coarser step (1.0 units) to ensure the orthographic box size is extremely stable
		radius = std::ceil(radius); 

		// Build a view matrix that only translates with the center and rotates with the light
		glm::mat4 lightView = glm::lookAt(center - lightDir * radius, center, up);
		
		// Projection is a fixed-size square based on the stable radius
		glm::mat4 lightProjection = glm::ortho(-radius, radius, -radius, radius, -radius * 6.0f, radius * 6.0f);

		// 3. PIXEL-PERFECT SNAPPING
		// We transform the origin to light-space, snap it to the texel grid, and apply the offset.
		glm::mat4 shadowMatrix = lightProjection * lightView;
		glm::vec4 shadowOrigin = shadowMatrix * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
		float texelsPerUnit = (float)shadowMap->GetShadowWidth() / (radius * 2.0f);

		shadowOrigin *= (float)shadowMap->GetShadowWidth() / 2.0f;

		glm::vec2 roundedOrigin = glm::round(glm::vec2(shadowOrigin.x, shadowOrigin.y));
		glm::vec2 roundOffset = roundedOrigin - glm::vec2(shadowOrigin.x, shadowOrigin.y);
		roundOffset = roundOffset * 2.0f / (float)shadowMap->GetShadowWidth();

		lightProjection[3][0] += roundOffset.x;
		lightProjection[3][1] += roundOffset.y;

		cascadedLightMatrices.push_back(lightProjection * lightView);
	}
}

DirectionalLight::~DirectionalLight()
{
}


