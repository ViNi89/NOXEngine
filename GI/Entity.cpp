#include "Entity.h"

#include "JobFactory.h"
#include "Engine.h"
#include "Renderer.h"

#include "GLUtils.h"

#include <iostream>

#include <assimp/Importer.hpp>      // C++ importer interface
#include <assimp/scene.h>           // Output data structure
#include <assimp/postprocess.h> 

nxEntity::nxEntity() {
	m_IBO = -1;
	m_VBO = -1;
	m_VAO = -1;
	m_MaterialIndex = -1;
	m_SceneIndex = -1;
	m_NumMeshes = -1;

	m_MaxX = m_MaxY = m_MaxZ = -100000.0f;
	m_MinX = m_MinY = m_MinZ = 100000.0f;

	m_DataCurrentSize = 0;
	m_VertexDataSize = 0;

}

nxEntity::nxEntity(const std::string filename) {
	m_IBO = -1;
	m_VBO = -1;
	m_VAO = -1;
	m_MaterialIndex = -1;
	m_SceneIndex = -1;
	m_NumMeshes = -1;

	m_MaxX = m_MaxY = m_MaxZ = -100000.0f;
	m_MinX = m_MinY = m_MinZ = 100000.0f;

	m_DataCurrentSize = 0;
	m_VertexDataSize = 0;

	std::string model_name;
	std::string::size_type posEnd = filename.find('.');
	std::string::size_type posStart = filename.find_last_of('/');
	if ( (posStart != std::string::npos) && (posEnd != std::string::npos))
	{
		m_ModelName = filename.substr(posStart, posEnd);
	}
	else
	{
		m_ModelName = filename;
	}

	InitFromFile(filename);
}

void nxEntity::InitFromFile(const std::string& path) {
	Assimp::Importer importer;
	const aiScene* scene = importer.ReadFile(path,
		aiProcess_CalcTangentSpace |
		aiProcess_Triangulate |
		aiProcess_JoinIdenticalVertices |
		aiProcess_SortByPType);

	if (!scene)
	{
		printf("Problem Loading\n");
	}

	const aiVector3D Zero3D(0.0f, 0.0f, 0.0f);
	const int vertex_size = sizeof(aiVector3D) * 2 + sizeof(aiVector2D);

	m_NumMeshes = scene->mNumMeshes;

	std::cout << "Num of Meshes " << scene->mNumMeshes << std::endl;
	for (size_t i = 0; i < scene->mNumMeshes; i++) {
		aiMesh* mesh = scene->mMeshes[i];
		//std::cout << "Num of Faces " << mesh->mNumFaces << std::endl;

		m_NumVertices += mesh->mNumVertices;
		m_NumFaces += mesh->mNumFaces;
		int before_size = m_DataCurrentSize;
		m_MaterialIndices.push_back(mesh->mMaterialIndex);
		m_MeshStartIndices.push_back(before_size / vertex_size);
		
		for (size_t j = 0; j < mesh->mNumFaces; j++) {
			const aiFace& face = mesh->mFaces[j];
			for (int k = 0; k < 3; k++)
			{
				aiVector3D pos = mesh->mVertices[face.mIndices[k]];

				if (pos.x < m_MinX) m_MinX = pos.x;
				if (pos.y < m_MinY) m_MinY = pos.y;
				if (pos.z < m_MinZ) m_MinZ = pos.z;
				if (pos.x > m_MaxX) m_MaxX = pos.x;
				if (pos.y > m_MaxY) m_MaxY = pos.y;
				if (pos.z > m_MaxZ) m_MaxZ = pos.z;

				aiVector3D uv = mesh->mTextureCoords[0][face.mIndices[k]];
				aiVector3D normal = mesh->mNormals[face.mIndices[k]];
				m_EntityData.insert(m_EntityData.end(), (unsigned char*)&pos, (unsigned char*)&pos + sizeof(aiVector3D));
				m_DataCurrentSize += sizeof(aiVector3D);
				m_EntityData.insert(m_EntityData.end(), (unsigned char*)&uv, (unsigned char*)&uv + sizeof(aiVector2D));
				m_DataCurrentSize += sizeof(aiVector2D);
				m_EntityData.insert(m_EntityData.end(), (unsigned char*)&normal, (unsigned char*)&normal + sizeof(aiVector3D));
				m_DataCurrentSize += sizeof(aiVector3D);
			}
		}
		m_MeshSizes.push_back((m_DataCurrentSize - before_size) / vertex_size);
	}
	std::cout << "Entity Data Length " << m_EntityData.size() << std::endl;

	//m_NumMaterials = scene->mNumMaterials;
}

void nxEntity::UploadData() {
	glGenBuffers(1, &m_VBO);

	glGenVertexArrays(1, &m_VAO);
	glBindVertexArray(m_VAO);

	glBindBuffer(GL_ARRAY_BUFFER, m_VBO);
	glBufferData(GL_ARRAY_BUFFER, m_EntityData.size(), &m_EntityData[0], GL_STATIC_DRAW);

	// Vertex positions
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 2 * sizeof(aiVector3D) + sizeof(aiVector2D), 0);
	// Texture coordinates
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(aiVector3D) + sizeof(aiVector2D), (void*)sizeof(aiVector3D));
	// Normal vectors
	glEnableVertexAttribArray(2);
	glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 2 * sizeof(aiVector3D) + sizeof(aiVector2D), (void*)(sizeof(aiVector3D) + sizeof(aiVector2D)));

	Utils::GL::CheckGLState("VAOs");

}

void nxEntity::Draw() {

}

bool nxAssetLoader::operator()(void* data) {
	nxAssetLoaderBlob* blob = (nxAssetLoaderBlob*)data;
	
	//std::cout << "Asset Loading " << blob->m_ResourcePath << std::endl;
	//std::cout << "Asset Loading " << blob->m_ResourceType << std::endl;
	//std::cout << "Asset Loading " << blob->m_Center.z << std::endl;

	nxEntity* ent = new nxEntity(blob->m_ResourcePath + blob->m_ResourceType);

	nxGLAssetLoaderBlob* newData = new nxGLAssetLoaderBlob(blob->m_Engine, ent);
	blob->m_Engine->Renderer()->ScheduleGLJob((nxGLJob*)nxJobFactory::CreateJob(NX_GL_JOB_LOAD_ASSET, newData));

	return true;
}

bool nxGLAssetLoader::operator()(void* data) {
	nxGLAssetLoaderBlob* blob = (nxGLAssetLoaderBlob*)data;

	blob->m_Entity->UploadData();

	//std::cout << "Model name " << blob->m_Entity->ModelName() << std::endl;
	//std::cout << "Asset Loading " << blob->m_ResourceType << std::endl;
	//std::cout << "Asset Loading " << blob->m_Center.z << std::endl;

	return true;
}