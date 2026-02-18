#include "Model.h"
#include <algorithm>

Model::Model()
{
}

void Model::LoadModel(const std::string& fileName)
{
	minBound = glm::vec3(1e10);
	maxBound = glm::vec3(-1e10);

	Assimp::Importer importer;
	const aiScene* scene = importer.ReadFile(fileName, aiProcess_Triangulate | aiProcess_FlipUVs | aiProcess_GenSmoothNormals | aiProcess_JoinIdenticalVertices | aiProcess_CalcTangentSpace);
	if (!scene)
	{
		printf("Model [%s] failed to load: %s!\n", fileName, importer.GetErrorString());
		return;
	}

	// start from first node
	LoadNode(scene->mRootNode, scene);

	LoadMaterials(scene);

}

void Model::LoadNode(aiNode* node, const aiScene* scene)
{
	for (size_t i = 0; i < node->mNumMeshes; i++)
	{
		// nodemmeshes[i] holds an id and the scene references it
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

		// tangents
		if (mesh->mTangents)
		{
			vertices.insert(vertices.end(), { mesh->mTangents[i].x, mesh->mTangents[i].y, mesh->mTangents[i].z });
		}
		else
		{
			vertices.insert(vertices.end(), { 0.0f, 0.0f, 0.0f });
		}

		// bitangents
		if (mesh->mBitangents)
		{
			vertices.insert(vertices.end(), { mesh->mBitangents[i].x, mesh->mBitangents[i].y, mesh->mBitangents[i].z });
		}
		else
		{
			vertices.insert(vertices.end(), { 0.0f, 0.0f, 0.0f });
		}
	}

	// go thru the faces so we can add the indices
	// each face has 3 values
	for (size_t i = 0; i < mesh->mNumFaces; i++)
	{
		aiFace face = mesh->mFaces[i];
		for (size_t j = 0; j < face.mNumIndices; j++)
		{
			indices.push_back(face.mIndices[j]);
		}
	}

	// create mesh and add to meshlist
	Mesh* newMesh = new Mesh();
	newMesh->CreateMesh(&vertices[0], &indices[0], vertices.size(), indices.size());
	meshList.push_back(newMesh);
	meshToTex.push_back(mesh->mMaterialIndex);
}

void Model::LoadMaterials(const aiScene* scene)
{
	textureList.resize(scene->mNumMaterials);
	normalMapList.resize(scene->mNumMaterials);

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
				int idx = std::string(path.data).rfind("\\");
				std::string filename = std::string(path.data).substr(idx + 1);

				std::string texPath = std::string("Assets/Textures/") + filename;

				textureList[i] = new Texture(texPath.c_str());

				if (!textureList[i]->LoadTexture())
				{
					printf("Failed to load texture at: %s!\n", texPath.c_str());
					delete textureList[i];
					textureList[i] = nullptr;
				}
				else
				{
					// Auto-detect normal map using _normal naming convention
					// e.g. "Textures/brick.png" -> "Textures/brick_normal.png"
					size_t dotPos = texPath.rfind('.');
					if (dotPos != std::string::npos)
					{
						std::string normalPath = texPath.substr(0, dotPos) + "_normal" + texPath.substr(dotPos);

						// Check if file exists using fopen
						FILE* testFile = nullptr;
						fopen_s(&testFile, normalPath.c_str(), "r");
						if (testFile)
						{
							fclose(testFile);
							normalMapList[i] = new Texture(normalPath.c_str());
							if (!normalMapList[i]->LoadTexture())
							{
								printf("Failed to load normal map at: %s!\n", normalPath.c_str());
								delete normalMapList[i];
								normalMapList[i] = nullptr;
							}
							else
							{
								printf("Normal map loaded: %s\n", normalPath.c_str());
							}
						}
					}
				}
			}
		}
		if (!textureList[i])
		{
			textureList[i] = new Texture("Assets/Textures/plain.png");
			textureList[i]->LoadTextureA();
		}
	}
}


void Model::ClearModel()
{
	for (size_t i = 0; i < meshList.size(); i++)
	{
		if (meshList[i])
		{
			delete meshList[i];
			meshList[i] = nullptr;
		}
	}

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
}

void Model::RenderModel(GLuint uniformUseNormalMap)
{
	for (size_t i = 0; i < meshList.size(); i++)
	{
		unsigned int materialIndex = meshToTex[i];

		if (materialIndex < textureList.size() && textureList[materialIndex])
		{
			textureList[materialIndex]->UseTexture();
		}

		if (materialIndex < normalMapList.size() && normalMapList[materialIndex])
		{
			glUniform1i(uniformUseNormalMap, 1);
			normalMapList[materialIndex]->UseNormalMap();
		}
		else
		{
			glUniform1i(uniformUseNormalMap, 0);
		}

		meshList[i]->RenderMesh();
	}
}


Model::~Model()
{
}


