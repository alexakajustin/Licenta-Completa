#include "Model.h"
#include <algorithm>

Model::Model()
{
}

void Model::LoadModelCPU(const std::string& fileName)
{
	this->filePath = fileName;
	minBound = glm::vec3(1e10);
	maxBound = glm::vec3(-1e10);

	Assimp::Importer importer;
	const aiScene* scene = importer.ReadFile(fileName, aiProcess_Triangulate | aiProcess_FlipUVs | aiProcess_GenSmoothNormals | aiProcess_JoinIdenticalVertices | aiProcess_CalcTangentSpace);
	if (!scene)
	{
		printf("Model [%s] failed to load: %s!\n", fileName.c_str(), importer.GetErrorString());
		loadFailed = true;
		return;
	}

	// start from first node
	LoadNode(scene->mRootNode, scene);

	// LoadMaterials now only prepares the paths, loading happens in GPU phase or deferred
	LoadMaterials(scene);

	isCPUReady = true;
}

void Model::LoadModelGPU()
{
	if (isGPUReady) return;
	if (loadFailed || !isCPUReady) {
		isGPUReady = true; // Still mark as ready so waiters aren't blocked, but with no meshes
		return;
	}

	for (auto& im : intermediateMeshes)
	{
		Mesh* newMesh = new Mesh();
		newMesh->CreateMesh(&im.vertices[0], &im.indices[0], (unsigned int)im.vertices.size(), (unsigned int)im.indices.size());
		
		// Set bounds from CPU data
		glm::vec3 min(1e10f), max(-1e10f);
		for (size_t v = 0; v < im.vertices.size() / 14; v++) {
			glm::vec3 p(im.vertices[v * 14], im.vertices[v * 14 + 1], im.vertices[v * 14 + 2]);
			min = glm::min(min, p);
			max = glm::max(max, p);
		}
		newMesh->SetBounds(min, max);
		newMesh->AddRef(); // Model claims ownership reference

		meshList.push_back(newMesh);
		meshNames.push_back(im.name);
		meshToTex.push_back(im.materialIndex);

		MeshData md;
		md.vertices = im.vertices;
		md.indices = im.indices;
		meshDataList.push_back(md);
	}

	// Load textures (GPU side)
	for (auto* tex : textureList) {
		if (tex) tex->LoadTexture();
	}
	for (auto* tex : normalMapList) {
		if (tex) tex->LoadTexture();
	}

	intermediateMeshes.clear();
	isGPUReady = true;
}

void Model::LoadNode(aiNode* node, const aiScene* scene)
{
	for (size_t i = 0; i < node->mNumMeshes; i++)
	{
		LoadMesh(scene->mMeshes[node->mMeshes[i]], scene);
	}

	for (size_t i = 0; i < node->mNumChildren; i++)
	{
		LoadNode(node->mChildren[i], scene);
	}
}

void Model::LoadMesh(aiMesh* mesh, const aiScene* scene)
{
	std::vector<GLfloat> vertices;
	std::vector<unsigned int> indices;

	// add vertices
	for (size_t i = 0; i < mesh->mNumVertices; i++)
	{
		// x y z positions
		vertices.insert(vertices.end(), { mesh->mVertices[i].x, mesh->mVertices[i].y, mesh->mVertices[i].z });
		
		// Update bounds
		minBound.x = std::min(minBound.x, mesh->mVertices[i].x);
		minBound.y = std::min(minBound.y, mesh->mVertices[i].y);
		minBound.z = std::min(minBound.z, mesh->mVertices[i].z);
		maxBound.x = std::max(maxBound.x, mesh->mVertices[i].x);
		maxBound.y = std::max(maxBound.y, mesh->mVertices[i].y);
		maxBound.z = std::max(maxBound.z, mesh->mVertices[i].z);

		// u v texture coordinates 
		if (mesh->mTextureCoords[0])
		{
			vertices.insert(vertices.end(), { mesh->mTextureCoords[0][i].x, mesh->mTextureCoords[0][i].y });
		}
		else
		{
			vertices.insert(vertices.end(), { 0.0f, 0.0f });
		}

		// normals
		vertices.insert(vertices.end(), { mesh->mNormals[i].x, mesh->mNormals[i].y, mesh->mNormals[i].z });

		if (mesh->mTangents)
			vertices.insert(vertices.end(), { mesh->mTangents[i].x, mesh->mTangents[i].y, mesh->mTangents[i].z });
		else
			vertices.insert(vertices.end(), { 0.0f, 0.0f, 0.0f });

		if (mesh->mBitangents)
			vertices.insert(vertices.end(), { mesh->mBitangents[i].x, mesh->mBitangents[i].y, mesh->mBitangents[i].z });
		else
			vertices.insert(vertices.end(), { 0.0f, 0.0f, 0.0f });
	}

	for (size_t i = 0; i < mesh->mNumFaces; i++)
	{
		aiFace face = mesh->mFaces[i];
		for (size_t j = 0; j < face.mNumIndices; j++)
			indices.push_back(face.mIndices[j]);
	}

	intermediateMeshes.push_back({ vertices, indices, mesh->mMaterialIndex, std::string(mesh->mName.C_Str()) });
}

void Model::LoadMaterials(const aiScene* scene)
{
	textureList.resize(scene->mNumMaterials);
	normalMapList.resize(scene->mNumMaterials);
	materialList.resize(scene->mNumMaterials);

	std::string directory = "";
	size_t slashPos = filePath.find_last_of("/\\");
	if (slashPos != std::string::npos) {
		directory = filePath.substr(0, slashPos + 1);
	}

	for (size_t i = 0; i < scene->mNumMaterials; i++)
	{
		aiMaterial* material = scene->mMaterials[i];

		textureList[i] = nullptr;
		normalMapList[i] = nullptr;

		if (material->GetTextureCount(aiTextureType_DIFFUSE))
		{
			aiString path;
			if (material->GetTexture(aiTextureType_DIFFUSE, 0, &path) == aiReturn_SUCCESS)
			{
				size_t idx = std::string(path.data).find_last_of("/\\");
				std::string filename = std::string(path.data).substr(idx == std::string::npos ? 0 : idx + 1);
				
				std::string texPath = directory + filename;
				FILE* testFile = nullptr;
				fopen_s(&testFile, texPath.c_str(), "r");
				if (testFile) {
					fclose(testFile);
				} else {
					texPath = std::string("Assets/Textures/") + filename;
				}

				textureList[i] = new Texture(texPath.c_str());
				// LoadTexture() will be called in LoadModelGPU
				
				// Predetermine normal map path
				size_t dotPos = texPath.rfind('.');
				if (dotPos != std::string::npos)
				{
					std::string normalPath = texPath.substr(0, dotPos) + "_normal" + texPath.substr(dotPos);
					FILE* testFile = nullptr;
					fopen_s(&testFile, normalPath.c_str(), "r");
					if (testFile)
					{
						fclose(testFile);
						normalMapList[i] = new Texture(normalPath.c_str());
					}
				}
			}
		}
		if (!textureList[i])
		{
			textureList[i] = new Texture("Assets/Textures/plain.png");
		}

		Material* mat = new Material();
		aiColor3D color(1.0f, 1.0f, 1.0f);
		material->Get(AI_MATKEY_COLOR_DIFFUSE, color);
		mat->SetColor(glm::vec3(color.r, color.g, color.b));
		
		float shininess = 32.0f;
		material->Get(AI_MATKEY_SHININESS, shininess);
		mat->SetShininess(shininess);
		
		float specular = 0.5f;
		material->Get(AI_MATKEY_SHININESS_STRENGTH, specular);
		mat->SetSpecularIntensity(specular);
		
		materialList[i] = mat;
	}
}


void Model::ClearModel()
{
	for (size_t i = 0; i < meshList.size(); i++)
	{
		if (meshList[i])
		{
			meshList[i]->Release(); // Use reference counting instead of direct delete
			meshList[i] = nullptr;
		}
	}
	meshList.clear();
	meshNames.clear();

	for (size_t i = 0; i < textureList.size(); i++)
	{
		if (textureList[i])
		{
			delete textureList[i];
			textureList[i] = nullptr;
		}
	}

	for (size_t i = 0; i < normalMapList.size(); i++)
	{
		if (normalMapList[i])
		{
			delete normalMapList[i];
			normalMapList[i] = nullptr;
		}
	}

	for (size_t i = 0; i < materialList.size(); i++)
	{
		if (materialList[i])
		{
			delete materialList[i];
			materialList[i] = nullptr;
		}
	}
}

void Model::RenderModel(GLuint uniformUseNormalMap, GLuint uniformUseDiffuseTexture, GLuint uniformNormalMapSampler, GLuint uniformDiffuseTextureSampler)
{
	if (!isGPUReady) return;

	for (size_t i = 0; i < meshList.size(); i++)
	{
		unsigned int materialIndex = meshToTex[i];

		if (materialIndex < textureList.size() && textureList[materialIndex])
		{
			glUniform1i(uniformUseDiffuseTexture, 1);
			glUniform1i(uniformDiffuseTextureSampler, 0); // Diffuse to Unit 0
			textureList[materialIndex]->UseTexture();
		}
		else
		{
			glUniform1i(uniformUseDiffuseTexture, 0);
		}

		if (materialIndex < normalMapList.size() && normalMapList[materialIndex])
		{
			glUniform1i(uniformUseNormalMap, 1);
			glUniform1i(uniformNormalMapSampler, 1); // Normal to Unit 1
			normalMapList[materialIndex]->UseNormalMap();
		}
		else
		{
			glUniform1i(uniformUseNormalMap, 0);
		}

		meshList[i]->RenderMesh();
	}
}

void Model::RenderModelGeometryOnly()
{
	if (!isGPUReady) return;

	for (size_t i = 0; i < meshList.size(); i++)
	{
		meshList[i]->RenderMesh();
	}
}


Model::~Model()
{
}


