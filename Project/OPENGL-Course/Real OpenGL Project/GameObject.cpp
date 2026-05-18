#include "GameObject.h"
#include "CommonValues.h"
#include "Application.h"
#include <glm/gtx/norm.hpp>
#include "GraphicsSettings.h"

GameObject::GameObject()
	: name("GameObject"), model(nullptr), mesh(nullptr), texture(nullptr), normalMap(nullptr), material(nullptr)
{
	for (int i = 0; i < 3; i++) lodMeshes[i] = nullptr;
}

GameObject::GameObject(const std::string& name)
	: name(name), model(nullptr), mesh(nullptr), texture(nullptr), normalMap(nullptr), material(nullptr)
{
	for (int i = 0; i < 3; i++) lodMeshes[i] = nullptr;
}

GameObject* GameObject::Clone(const std::string& newName)
{
	GameObject* clone = new GameObject(newName);

	// Copy local transform
	clone->transform = this->transform;
	clone->inheritScale = this->inheritScale;

	// Copy components
	clone->model = this->model;
	if (this->mesh) {
		clone->SetMesh(this->mesh);
	}
	clone->texture = this->texture;
	clone->normalMap = this->normalMap;
	clone->material = this->material;
	clone->textureLayers = this->textureLayers;
	clone->primitiveType = this->primitiveType;
	clone->modelSourcePath = this->modelSourcePath;

	// Copy tessellation settings
	clone->useTessellation = this->useTessellation;
	clone->tessLevel = this->tessLevel;
	clone->tessDistance = this->tessDistance;
	clone->tessDisplacementScale = this->tessDisplacementScale;
	clone->tessDisplacementBias = this->tessDisplacementBias;

	if (this->hasCustomMesh && this->cpuMeshData) {
		clone->SetCPUMeshData(this->cpuMeshData); // Shared ref
	}

	// Recursively clone children
	for (auto* child : this->children) {
		GameObject* childClone = child->Clone(child->GetName());
		clone->AddChild(childClone); // Safe: preserves local transforms exactly as they were in the source
	}

	return clone;
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
	for (int i = 0; i < 3; i++) {
		if (lodMeshes[i]) {
			lodMeshes[i]->Release();
			lodMeshes[i] = nullptr;
		}
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

void GameObject::Orphan()
{
	parent = nullptr;
	children.clear();
}

void GameObject::SetModel(Model* mdl)
{
	model = mdl;
	if (!model) return;

	// Automatically detect LOD meshes from the model if they follow naming conventions
	// (LOD0, LOD1, LOD2)
	size_t meshCount = model->GetMeshCount();
	const auto& meshNames = model->GetMeshNames();

	if (meshCount > 1) {
		Mesh* foundLODs[3] = { nullptr, nullptr, nullptr };
		int highestLOD = -1;

		for (size_t i = 0; i < meshCount; i++) {
			Mesh* msh = model->GetMesh(i);
			if (!msh || i >= meshNames.size()) continue;

			std::string mName = meshNames[i];
			for (auto& c : mName) c = toupper(c);

			if (mName.find("LOD0") != std::string::npos || mName.find("LOD_0") != std::string::npos) {
				SetMesh(msh);
			}
			else if (mName.find("LOD1") != std::string::npos || mName.find("LOD_1") != std::string::npos) {
				foundLODs[0] = msh;
				if (highestLOD < 1) highestLOD = 1;
			}
			else if (mName.find("LOD2") != std::string::npos || mName.find("LOD_2") != std::string::npos) {
				foundLODs[1] = msh;
				if (highestLOD < 2) highestLOD = 2;
			}
			else if (mName.find("LOD3") != std::string::npos || mName.find("LOD_3") != std::string::npos) {
				foundLODs[2] = msh;
				if (highestLOD < 3) highestLOD = 3;
			}
		}

		if (highestLOD >= 1) {
			for (int i = 0; i < 3; i++) {
				if (foundLODs[i]) SetLODMesh(i, foundLODs[i]);
			}
		}
	}
	SetDirty();
}

void GameObject::Render(GLint uniformModel, GLint uniformSpecularIntensity, GLint uniformShininess, GLint uniformMaterialColor, 
	GLint uniformTiling, GLint uniformOffset,
	GLint uniformUseNormalMap, GLint uniformUseDiffuseTexture, GLint uniformDiffuseTexture, GLint uniformNormalMap,
	const glm::vec3& cameraPos, const GraphicsSettings* gs,
	const glm::mat4& parentMatrix, const Frustum* frustum)
{
	glm::mat4 modelMatrix = parentMatrix * GetWorldMatrix();

	// Frustum culling
	if (frustum) {
		glm::vec3 min, max;
		GetWorldBounds(min, max);
		if (!frustum->IsBoxVisible(min, max)) {
			return; 
		}
	}

	RenderSingle(uniformModel, uniformSpecularIntensity, uniformShininess, uniformMaterialColor, uniformTiling, uniformOffset, uniformUseNormalMap, uniformUseDiffuseTexture, uniformDiffuseTexture, uniformNormalMap, cameraPos, gs, 0, false);

	// Recursive render for children
	for (auto* child : children)
	{
		child->Render(uniformModel, uniformSpecularIntensity, uniformShininess, uniformMaterialColor, 
			uniformTiling, uniformOffset,
			uniformUseNormalMap, uniformUseDiffuseTexture, uniformDiffuseTexture, uniformNormalMap, cameraPos, gs, modelMatrix, frustum);
	}
}

void GameObject::RenderSingle(GLint uniformModel, GLint uniformSpecularIntensity, GLint uniformShininess, GLint uniformMaterialColor,
	GLint uniformTiling, GLint uniformOffset,
	GLint uniformUseNormalMap, GLint uniformUseDiffuseTexture, GLint uniformDiffuseTexture, GLint uniformNormalMap,
	const glm::vec3& cameraPos,
	const GraphicsSettings* gs,
	GLuint shaderID,
	bool shaderSupportsTessellation)
{
	glm::mat4 modelMatrix = GetWorldMatrix();
	glUniformMatrix4fv(uniformModel, 1, GL_FALSE, glm::value_ptr(modelMatrix));

	// Calculate distance to camera for LOD
	float dist = glm::distance(glm::vec3(modelMatrix[3]), cameraPos);

	// Use global graphics settings for distances
	float finalMaxDist = gs ? gs->renderDistance : 2000.0f;
	float finalLOD0 = gs ? gs->lod0Distance : 50.0f;
	float finalLOD1 = gs ? gs->lod1Distance : 150.0f;
	float finalLOD2 = gs ? gs->lod2Distance : 400.0f;


	// Reset texture flags to prevent stale state from previous object
	if (uniformUseDiffuseTexture != -1) glUniform1i(uniformUseDiffuseTexture, 0);
	if (uniformUseNormalMap != -1) glUniform1i(uniformUseNormalMap, 0);

	// Apply material if available, otherwise reset to defaults
	if (material)
	{
		material->UseMaterial(uniformSpecularIntensity, uniformShininess, uniformMaterialColor, uniformTiling, uniformOffset);
		material->Bind(shaderID); // IMPORTANT: This uploads all shader-specific properties (baseColor, windSpeed, etc.) using the correct shader program
		
		// Shadow shaders use materialAlpha for transparency color mapping
		GLint alphaLoc = glGetUniformLocation(shaderID, "materialAlpha");
		if (alphaLoc != -1) glUniform1f(alphaLoc, material->GetAlpha());
	}
	else
	{
		glUniform1f(uniformSpecularIntensity, 0.0f);
		glUniform1f(uniformShininess, 1.0f);
		if (uniformMaterialColor != -1) {
			while(glGetError() != GL_NO_ERROR);
			glUniform4f(uniformMaterialColor, 1.0f, 1.0f, 1.0f, 1.0f);
			if (glGetError() != GL_NO_ERROR) std::cout << "[ERROR] glUniform4f failed for materialColor in GameObject at location: " << uniformMaterialColor << "\n";
		}
		glUniform2f(uniformTiling, 1.0f, 1.0f);
		glUniform2f(uniformOffset, 0.0f, 0.0f);

		GLint alphaLoc = glGetUniformLocation(shaderID, "materialAlpha");
		if (alphaLoc != -1) glUniform1f(alphaLoc, 1.0f);
	}

	// ========== Texture Layers Configuration (Must happen BEFORE draw calls) ==========
	if (shaderID != 0)
	{
		if (!textureLayers.empty())
		{
			// Cache uniform locations (done once per shader program)
			static GLuint cachedShaderID = 0;
			static GLint uLayerCount = -1;
			static GLint uLayerSamplers[MAX_TEXTURE_LAYERS];
			static GLint uLayerNormalSamplers[MAX_TEXTURE_LAYERS];
			static GLint uLayerDisplacementSamplers[MAX_TEXTURE_LAYERS];
			static struct {
				GLint blendMode, opacity, tiling, heightMin, heightMax, slopeMin, slopeMax, invert, hasNormalMap, hasDisplacementMap, displacementScale;
			} uLayer[MAX_TEXTURE_LAYERS];

			if (cachedShaderID != shaderID)
			{
				cachedShaderID = shaderID;
				uLayerCount = glGetUniformLocation(shaderID, "textureLayerCount");
				for (int i = 0; i < MAX_TEXTURE_LAYERS; i++)
				{
					char buf[128];
					snprintf(buf, sizeof(buf), "textureLayers[%d]", i);
					uLayerSamplers[i] = glGetUniformLocation(shaderID, buf);
					snprintf(buf, sizeof(buf), "layerNormalMaps[%d]", i);
					uLayerNormalSamplers[i] = glGetUniformLocation(shaderID, buf);
					snprintf(buf, sizeof(buf), "layerDisplacementMaps[%d]", i);
					uLayerDisplacementSamplers[i] = glGetUniformLocation(shaderID, buf);
					snprintf(buf, sizeof(buf), "layerData[%d].blendMode", i);
					uLayer[i].blendMode = glGetUniformLocation(shaderID, buf);
					snprintf(buf, sizeof(buf), "layerData[%d].opacity", i);
					uLayer[i].opacity = glGetUniformLocation(shaderID, buf);
					snprintf(buf, sizeof(buf), "layerData[%d].tiling", i);
					uLayer[i].tiling = glGetUniformLocation(shaderID, buf);
					snprintf(buf, sizeof(buf), "layerData[%d].heightMin", i);
					uLayer[i].heightMin = glGetUniformLocation(shaderID, buf);
					snprintf(buf, sizeof(buf), "layerData[%d].heightMax", i);
					uLayer[i].heightMax = glGetUniformLocation(shaderID, buf);
					snprintf(buf, sizeof(buf), "layerData[%d].slopeMin", i);
					uLayer[i].slopeMin = glGetUniformLocation(shaderID, buf);
					snprintf(buf, sizeof(buf), "layerData[%d].slopeMax", i);
					uLayer[i].slopeMax = glGetUniformLocation(shaderID, buf);
					snprintf(buf, sizeof(buf), "layerData[%d].invert", i);
					uLayer[i].invert = glGetUniformLocation(shaderID, buf);
					snprintf(buf, sizeof(buf), "layerData[%d].hasNormalMap", i);
					uLayer[i].hasNormalMap = glGetUniformLocation(shaderID, buf);
					snprintf(buf, sizeof(buf), "layerData[%d].hasDisplacementMap", i);
					uLayer[i].hasDisplacementMap = glGetUniformLocation(shaderID, buf);
					snprintf(buf, sizeof(buf), "layerData[%d].displacementScale", i);
					uLayer[i].displacementScale = glGetUniformLocation(shaderID, buf);
				}
			}

			int count = (int)textureLayers.size();
			if (count > MAX_TEXTURE_LAYERS) count = MAX_TEXTURE_LAYERS;
			glUniform1i(uLayerCount, count);

			// Texture unit scheme: diffuse, normal, displacement per layer
			static const int diffuseUnits[MAX_TEXTURE_LAYERS]      = { 10, 12, 24, 29, 31 };
			static const int normalUnits[MAX_TEXTURE_LAYERS]       = { 11, 13, 28, 30, 32 };
			static const int displacementUnits[MAX_TEXTURE_LAYERS] = { 16, 17, 18, 19, 33 };

			for (int i = 0; i < count; i++)
			{
				const TextureLayer& layer = textureLayers[i];
				if (layer.texture)
				{
					layer.texture->UseTextureOnUnit(GL_TEXTURE0 + diffuseUnits[i]);
					glUniform1i(uLayerSamplers[i], diffuseUnits[i]);
				}
				else
				{
					glActiveTexture(GL_TEXTURE0 + diffuseUnits[i]);
					glBindTexture(GL_TEXTURE_2D, 0);
					glUniform1i(uLayerSamplers[i], diffuseUnits[i]);
				}

				bool hasNorm = (layer.normalMap != nullptr);
				if (hasNorm)
				{
					layer.normalMap->UseTextureOnUnit(GL_TEXTURE0 + normalUnits[i]);
					glUniform1i(uLayerNormalSamplers[i], normalUnits[i]);
				}
				else
				{
					glActiveTexture(GL_TEXTURE0 + normalUnits[i]);
					glBindTexture(GL_TEXTURE_2D, 0);
					glUniform1i(uLayerNormalSamplers[i], normalUnits[i]);
				}

				bool hasDisp = (layer.displacementMap != nullptr);
				if (hasDisp)
				{
					layer.displacementMap->UseTextureOnUnit(GL_TEXTURE0 + displacementUnits[i]);
					glUniform1i(uLayerDisplacementSamplers[i], displacementUnits[i]);
				}
				else
				{
					glActiveTexture(GL_TEXTURE0 + displacementUnits[i]);
					glBindTexture(GL_TEXTURE_2D, 0);
					glUniform1i(uLayerDisplacementSamplers[i], displacementUnits[i]);
				}

				glUniform1i(uLayer[i].blendMode, (int)layer.blendMode);
				glUniform1f(uLayer[i].opacity, layer.opacity);
				glUniform1f(uLayer[i].tiling, layer.tiling);
				glUniform1f(uLayer[i].heightMin, layer.heightMin);
				glUniform1f(uLayer[i].heightMax, layer.heightMax);
				glUniform1f(uLayer[i].slopeMin, layer.slopeMin);
				glUniform1f(uLayer[i].slopeMax, layer.slopeMax);
				glUniform1i(uLayer[i].invert, layer.invert ? 1 : 0);
				glUniform1i(uLayer[i].hasNormalMap, hasNorm ? 1 : 0);
				glUniform1i(uLayer[i].hasDisplacementMap, hasDisp ? 1 : 0);
				glUniform1f(uLayer[i].displacementScale, layer.displacementScale);
			}
		}
		else
		{
			// Explicitly set count to 0 if no layers, to prevent leakage
			static GLint uLayerCountLoc = -1;
			static GLuint lastShader = 0;
			if (lastShader != shaderID) {
				uLayerCountLoc = glGetUniformLocation(shaderID, "textureLayerCount");
				lastShader = shaderID;
			}
			if (uLayerCountLoc != -1) glUniform1i(uLayerCountLoc, 0);
		}
	}

	// Determine which mesh to render based on distance and available LODs
	Mesh* meshToRender = mesh;

	if (lodCount > 0) {
		if (dist < finalLOD0 || lodCount == 1) {
			meshToRender = mesh;
		}
		else if (dist < finalLOD1 || lodCount == 2) {
			meshToRender = (lodMeshes[0] ? lodMeshes[0] : mesh);
		}
		else if (dist < finalLOD2 || lodCount == 3) {
			meshToRender = (lodMeshes[1] ? lodMeshes[1] : (lodMeshes[0] ? lodMeshes[0] : mesh));
		}
		else {
			meshToRender = (lodMeshes[2] ? lodMeshes[2] : (lodMeshes[1] ? lodMeshes[1] : (lodMeshes[0] ? lodMeshes[0] : mesh)));
		}
	}

	// LOD Debug Coloring
	if (gs && gs->debugLODColoring && meshToRender) {
		GLint debugLoc = glGetUniformLocation(shaderID, "debugLODColoring");
		if (debugLoc != -1) glUniform1i(debugLoc, 1);
		
		GLint lodColorLoc = glGetUniformLocation(shaderID, "lodDebugColor");
		if (lodColorLoc != -1) {
			if (meshToRender == mesh) glUniform3f(lodColorLoc, 1.0f, 0.2f, 0.2f); // RED = LOD0
			else if (lodCount >= 1 && meshToRender == lodMeshes[0]) glUniform3f(lodColorLoc, 0.2f, 1.0f, 0.2f); // GREEN = LOD1
			else if (lodCount >= 2 && meshToRender == lodMeshes[1]) glUniform3f(lodColorLoc, 0.2f, 0.2f, 1.0f); // BLUE = LOD2
			else glUniform3f(lodColorLoc, 1.0f, 1.0f, 0.0f); // YELLOW = Other
		}
	} else {
		GLint debugLoc = glGetUniformLocation(shaderID, "debugLODColoring");
		if (debugLoc != -1) glUniform1i(debugLoc, 0);
	}

	if (meshToRender) {
		Texture* activeTexture = texture;
		if (!activeTexture && !textureLayers.empty()) {
			activeTexture = textureLayers[0].texture;
		}

		if (activeTexture) {
			glUniform1i(uniformUseDiffuseTexture, 1);
			glUniform1i(uniformDiffuseTexture, 0);
			activeTexture->UseTexture();
		} else {
			glUniform1i(uniformUseDiffuseTexture, 0);
		}

		Texture* activeNormal = normalMap;
		if (!activeNormal && !textureLayers.empty()) {
			activeNormal = textureLayers[0].normalMap;
		}

		if (activeNormal) {
			glUniform1i(uniformUseNormalMap, 1);
			glUniform1i(uniformNormalMap, 1);
			activeNormal->UseNormalMap();
		} else {
			glUniform1i(uniformUseNormalMap, 0);
		}

		// Render
		if (useTessellation) {
			meshToRender->RenderMeshTessellated(shaderSupportsTessellation);
		} else {
			meshToRender->RenderMesh();
		}
	}
	else if (model) {
		// Fallback for models without detected LODs
		if (texture) {
			glUniform1i(uniformUseDiffuseTexture, 1);
			glUniform1i(uniformDiffuseTexture, 0);
			texture->UseTexture();
		} else {
			glUniform1i(uniformUseDiffuseTexture, 0);
		}

		if (normalMap) {
			glUniform1i(uniformUseNormalMap, 1);
			glUniform1i(uniformNormalMap, 1);
			normalMap->UseNormalMap();
		} else {
			glUniform1i(uniformUseNormalMap, 0);
		}

		model->RenderModel(uniformUseNormalMap, uniformUseDiffuseTexture, uniformNormalMap, uniformDiffuseTexture);
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

void GameObject::SetLODMesh(int level, Mesh* newMesh)
{
	if (level < 0 || level >= 3) return;
	if (lodMeshes[level] == newMesh) return;

	if (lodMeshes[level]) lodMeshes[level]->Release();
	lodMeshes[level] = newMesh;
	if (lodMeshes[level]) lodMeshes[level]->AddRef();

	// Update lodCount
	int count = 1;
	for (int i = 0; i < 3; i++) if (lodMeshes[i]) count = i + 2;
	lodCount = count;
}

Mesh* GameObject::GetLODMesh(int level) const
{
	if (level < 0 || level >= 3) return nullptr;
	return lodMeshes[level];
}

void GameObject::SetCPUMeshData(const MeshData& data)
{
	// Store a unique copy (standard behavior)
	cpuMeshData = std::make_shared<MeshData>(data);
	hasCustomMesh = true;
	// Compute bounds once and cache them
	cpuMeshData->GetBounds(customMeshMin, customMeshMax);
	customBoundsDirty = false;
	SetDirty();
}

void GameObject::SetCPUMeshData(std::shared_ptr<MeshData> data)
{
	// Store a shared reference (memory efficient)
	cpuMeshData = data;
	hasCustomMesh = (cpuMeshData != nullptr);
	if (hasCustomMesh) {
		cpuMeshData->GetBounds(customMeshMin, customMeshMax);
		customBoundsDirty = false;
	} else {
		customBoundsDirty = true;
	}
	SetDirty();
}

void GameObject::AddTextureLayer(const TextureLayer& layer)
{
	if ((int)textureLayers.size() < MAX_TEXTURE_LAYERS)
		textureLayers.push_back(layer);
}

void GameObject::RemoveTextureLayer(int index)
{
	if (index >= 0 && index < (int)textureLayers.size())
		textureLayers.erase(textureLayers.begin() + index);
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

	glm::vec3 worldMin(1e10f), worldMax(-1e10f);
	bool hasAnyBounds = false;

	// 1. Include own mesh bounds
	if (model || mesh || hasCustomMesh) {
		glm::vec3 localMin(0.0f), localMax(0.0f);
		if (model && !hasCustomMesh) {
			localMin = model->GetMinBound();
			localMax = model->GetMaxBound();
		}
		else if (hasCustomMesh) {
			if (customBoundsDirty) {
				if (cpuMeshData) cpuMeshData->GetBounds(customMeshMin, customMeshMax);
				customBoundsDirty = false;
			}
			localMin = customMeshMin;
			localMax = customMeshMax;
		}
		else if (mesh) {
			mesh->GetBounds(localMin, localMax);
		}

		glm::mat4 world = GetWorldMatrix();
		glm::vec3 corners[8] = {
			{localMin.x, localMin.y, localMin.z}, {localMax.x, localMin.y, localMin.z},
			{localMin.x, localMax.y, localMin.z}, {localMin.x, localMin.y, localMax.z},
			{localMax.x, localMax.y, localMin.z}, {localMax.x, localMin.y, localMax.z},
			{localMin.x, localMax.y, localMax.z}, {localMax.x, localMax.y, localMax.z},
		};

		for (int i = 0; i < 8; i++) {
			glm::vec3 worldCorner = glm::vec3(world * glm::vec4(corners[i], 1.0f));
			worldMin = glm::min(worldMin, worldCorner);
			worldMax = glm::max(worldMax, worldCorner);
		}
		hasAnyBounds = true;
	}

	// 2. Include children bounds recursively
	for (auto* child : children) {
		glm::vec3 cMin, cMax;
		child->GetWorldBounds(cMin, cMax);
		
		// Only expand if child actually has bounds (avoiding expanding by infinite/empty bounds)
		if (cMin.x < 1e9f) {
			worldMin = glm::min(worldMin, cMin);
			worldMax = glm::max(worldMax, cMax);
			hasAnyBounds = true;
		}
	}

	// 3. Fallback: if no mesh and no children with bounds, use position
	if (!hasAnyBounds) {
		worldMin = worldMax = transform.GetPosition();
	}

	cachedWorldMin = worldMin;
	cachedWorldMax = worldMax;
	min = cachedWorldMin;
	max = cachedWorldMax;

	// Compute bounding sphere from AABB
	cachedSphereCenter = (min + max) * 0.5f;
	cachedSphereRadius = glm::length(max - cachedSphereCenter);

	boundsDirty = false;
}

void GameObject::GetWorldBoundingSphere(glm::vec3& center, float& radius)
{
	if (boundsDirty) {
		glm::vec3 dummyMin, dummyMax;
		GetWorldBounds(dummyMin, dummyMax); // This will recompute and cache everything
	}
	center = cachedSphereCenter;
	radius = cachedSphereRadius;
}
