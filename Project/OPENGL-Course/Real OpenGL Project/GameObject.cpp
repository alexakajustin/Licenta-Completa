#include "GameObject.h"
#include "Frustum.h"

GameObject::GameObject()
	: name("GameObject"), model(nullptr), mesh(nullptr), texture(nullptr), normalMap(nullptr), material(nullptr)
{
}

GameObject::GameObject(const std::string& name)
	: name(name), model(nullptr), mesh(nullptr), texture(nullptr), normalMap(nullptr), material(nullptr)
{
}

GameObject::~GameObject()
{
	if (parent) {
		parent->RemoveChild(this);
	}
	
	if (mesh) {
		mesh->Release();
		mesh = nullptr;
	}

	// Note: We don't delete children here because SceneManager owns them in the 'objects' list.
	// We just need to make sure they are orphaned if the parent is deleted alone, 
	// OR better, SceneManager should delete them recursively.
	for (auto* child : children) {
		child->parent = nullptr;
	}
}

void GameObject::SetParent(GameObject* newParent)
{
	if (parent == newParent) return;

	// Calculate current world matrix to maintain world position
	glm::mat4 worldMat = GetWorldMatrix();

	// Remove from old parent
	if (parent) {
		parent->RemoveChild(this);
	}

	parent = newParent;

	// Add to new parent
	if (parent) {
		parent->AddChild(this);
	}

	// Update local transform to maintain world position, rotation, and scale
	if (parent) {
		glm::mat4 pMat = parent->GetWorldMatrix();
		if (!inheritScale) {
			pMat[0] = glm::normalize(pMat[0]);
			pMat[1] = glm::normalize(pMat[1]);
			pMat[2] = glm::normalize(pMat[2]);
		}
		glm::mat4 invParent = glm::inverse(pMat);
		glm::mat4 localMat = invParent * worldMat;
		transform.SetFromMatrix(localMat);
	} else {
		transform.SetFromMatrix(worldMat);
	}

	SetDirty();
}

glm::mat4 GameObject::GetWorldMatrix()
{
	if (!worldDirty) return cachedWorldMatrix;

	glm::mat4 localModel = transform.GetModelMatrix();
	if (parent) {
		glm::mat4 pWorld = parent->GetWorldMatrix();
		if (!inheritScale) {
			// 1. Calculate world position with NORMALIZED parent basis (for translation only)
			glm::mat4 pBasis = pWorld;
			pBasis[0] = glm::normalize(pBasis[0]);
			pBasis[1] = glm::normalize(pBasis[1]);
			pBasis[2] = glm::normalize(pBasis[2]);
			
			glm::vec3 worldPos = glm::vec3(pBasis * localModel[3]);

			// 2. Calculate world orientation/scale without parent scale
			pBasis[3] = glm::vec4(0, 0, 0, 1);
			cachedWorldMatrix = pBasis * localModel;
			cachedWorldMatrix[3] = glm::vec4(worldPos, 1.0f);
		} else {
			cachedWorldMatrix = pWorld * localModel;
		}
	} else {
		cachedWorldMatrix = localModel;
	}

	worldDirty = false;
	return cachedWorldMatrix;
}

void GameObject::SetDirty()
{
	transform.MarkDirty();
	if (worldDirty && boundsDirty) return; // Already dirty

	worldDirty = true;
	boundsDirty = true;

	for (auto* child : children) {
		child->SetDirty();
	}
}

void GameObject::AddChild(GameObject* child)
{
	if (!child) return;
	
	// Ensure not already a child
	for (auto* c : children) if (c == child) return;

	children.push_back(child);
	child->parent = this;
	child->SetDirty();
}

void GameObject::RemoveChild(GameObject* child)
{
	for (auto it = children.begin(); it != children.end(); ++it) {
		if (*it == child) {
			child->parent = nullptr;
			child->SetDirty();
			children.erase(it);
			return;
		}
	}
}

void GameObject::Render(GLint uniformModel, GLint uniformSpecularIntensity, GLint uniformShininess, GLint uniformMaterialColor, 
	GLint uniformTiling, GLint uniformOffset,
	GLint uniformUseNormalMap, GLint uniformUseDiffuseTexture, GLint uniformDiffuseTexture, GLint uniformNormalMap,
	const glm::mat4& parentMatrix, const Frustum* frustum)
{
	// FRUSTUM CULLING CHECK
	if (frustum && (model || mesh))
	{
		glm::vec3 min, max;
		GetWorldBounds(min, max);
		if (!frustum->IsBoxVisible(min, max)) return;
	}

	glm::mat4 modelMatrix = GetWorldMatrix();
	RenderSingle(uniformModel, uniformSpecularIntensity, uniformShininess, uniformMaterialColor, uniformTiling, uniformOffset, uniformUseNormalMap, uniformUseDiffuseTexture, uniformDiffuseTexture, uniformNormalMap);

	// Recursive render for children
	for (auto* child : children)
	{
		child->Render(uniformModel, uniformSpecularIntensity, uniformShininess, uniformMaterialColor, 
			uniformTiling, uniformOffset,
			uniformUseNormalMap, uniformUseDiffuseTexture, uniformDiffuseTexture, uniformNormalMap, modelMatrix, frustum);
	}
}

void GameObject::RenderSingle(GLint uniformModel, GLint uniformSpecularIntensity, GLint uniformShininess, GLint uniformMaterialColor, 
	GLint uniformTiling, GLint uniformOffset, GLint uniformUseNormalMap, GLint uniformUseDiffuseTexture, GLint uniformDiffuseTexture, GLint uniformNormalMap)
{
	if (!mesh && !model) return;

	glm::mat4 modelMatrix = GetWorldMatrix();
	glUniformMatrix4fv(uniformModel, 1, GL_FALSE, glm::value_ptr(modelMatrix));

	// Apply material if available, otherwise reset to defaults
	if (material)
	{
		material->UseMaterial(uniformSpecularIntensity, uniformShininess, uniformMaterialColor, uniformTiling, uniformOffset);
	}
	else
	{
		glUniform1f(uniformSpecularIntensity, 0.0f);
		glUniform1f(uniformShininess, 1.0f);
		glUniform3f(uniformMaterialColor, 1.0f, 1.0f, 1.0f);
		glUniform2f(uniformTiling, 1.0f, 1.0f);
		glUniform2f(uniformOffset, 0.0f, 0.0f);
	}

	// Render the visual component
	if (model && !hasCustomMesh)
	{
		bool hasOverrideTex = (texture != nullptr);
		bool hasOverrideNorm = (normalMap != nullptr);

		if (hasOverrideTex || hasOverrideNorm) {
			// Inspector overrides: bind our textures, skip model's own
			if (hasOverrideTex) {
				glUniform1i(uniformUseDiffuseTexture, 1);
				glUniform1i(uniformDiffuseTexture, 0); // Unit 0
				texture->UseTexture();
			} else {
				glUniform1i(uniformUseDiffuseTexture, 0);
			}

			if (hasOverrideNorm) {
				glUniform1i(uniformUseNormalMap, 1);
				glUniform1i(uniformNormalMap, 1); // Unit 1
				normalMap->UseNormalMap();
			} else {
				glUniform1i(uniformUseNormalMap, 0);
			}
			model->RenderModelGeometryOnly();
		} else {
			// No overrides — model uses its own per-mesh textures
			model->RenderModel(uniformUseNormalMap, uniformUseDiffuseTexture, uniformNormalMap, uniformDiffuseTexture);
		}
	}
	else if (mesh)
	{
		if (texture) {
			glUniform1i(uniformUseDiffuseTexture, 1);
			glUniform1i(uniformDiffuseTexture, 0); // Unit 0
			texture->UseTexture(); // Internal glActiveTexture(GL_TEXTURE0)
		} else {
			glUniform1i(uniformUseDiffuseTexture, 0);
		}

		if (normalMap) {
			glUniform1i(uniformUseNormalMap, 1);
			glUniform1i(uniformNormalMap, 1); // Unit 1
			normalMap->UseNormalMap(); // Internal glActiveTexture(GL_TEXTURE1)
		} else {
			glUniform1i(uniformUseNormalMap, 0);
		}
		mesh->RenderMesh();
	}
}

void GameObject::SetMesh(Mesh* newMesh)
{
	if (mesh == newMesh) return;

	if (mesh)
	{
		mesh->Release();
	}

	mesh = newMesh;

	if (mesh)
	{
		mesh->AddRef();
	}
	SetDirty();
}

void GameObject::SetCPUMeshData(const MeshData& data)
{
	// Store a unique copy (standard behavior)
	cpuMeshData = std::make_shared<MeshData>(data);
	hasCustomMesh = true;
	SetDirty();
}

void GameObject::SetCPUMeshData(std::shared_ptr<MeshData> data)
{
	// Store a shared reference (memory efficient)
	cpuMeshData = data;
	hasCustomMesh = (cpuMeshData != nullptr);
	SetDirty();
}

const MeshData& GameObject::GetCPUMeshData() const
{
	static MeshData empty;
	return cpuMeshData ? *cpuMeshData : empty;
}

void GameObject::GetWorldBounds(glm::vec3& min, glm::vec3& max)
{
	if (!boundsDirty) {
		min = cachedWorldMin;
		max = cachedWorldMax;
		return;
	}

	glm::vec3 localMin(0.0f), localMax(0.0f);
	if (model && !hasCustomMesh) {
		localMin = model->GetMinBound();
		localMax = model->GetMaxBound();
	}
	else if (mesh) {
		mesh->GetBounds(localMin, localMax);
	}
	else {
		min = transform.GetPosition();
		max = transform.GetPosition();
		cachedWorldMin = min;
		cachedWorldMax = max;
		boundsDirty = false;
		return;
	}

	glm::mat4 world = GetWorldMatrix();
	glm::vec3 corners[8] = {
		{localMin.x, localMin.y, localMin.z},
		{localMax.x, localMin.y, localMin.z},
		{localMin.x, localMax.y, localMin.z},
		{localMin.x, localMin.y, localMax.z},
		{localMax.x, localMax.y, localMin.z},
		{localMax.x, localMin.y, localMax.z},
		{localMin.x, localMax.y, localMax.z},
		{localMax.x, localMax.y, localMax.z},
	};

	min = glm::vec3(1e10f);
	max = glm::vec3(-1e10f);
	for (int i = 0; i < 8; i++) {
		glm::vec3 worldCorner = glm::vec3(world * glm::vec4(corners[i], 1.0f));
		min = glm::min(min, worldCorner);
		max = glm::max(max, worldCorner);
	}

	cachedWorldMin = min;
	cachedWorldMax = max;
	boundsDirty = false;
}
