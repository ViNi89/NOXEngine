#pragma once

#include <gl/glew.h>

#include <glm/vec3.hpp>

#include <string>
#include <vector>

#include "CustomTypes.h"

#include "Texture.h"

class nxVoxelizer;

class nxEntity {
public:

							nxEntity();
							nxEntity(const std::string filename);
	void					InitFromFile(const std::string& filename);
    void					InitFromBuffer(glm::vec3* buffer, nxInt32 size, GLuint adHocTexture = -1);
    void					InitFromVoxelizer(nxVoxelizer* voxel);
    void					UpdateFromVoxelizer(nxVoxelizer* voxel);
    void                    InitPreviewFromBuffer(glm::vec3* buffer, nxInt32 size, GLuint adHocTexture = -1);

	void					ClearData();

	void					UploadData();

	float					MaxX(){ return m_MaxX; }
	float					MaxY(){ return m_MaxY; }
	float					MaxZ(){ return m_MaxZ; }
	float					MinX(){ return m_MinX; }
	float					MinY(){ return m_MinY; }
	float					MinZ(){ return m_MinZ; }
	float					Scale(){ return m_Scale; }

	std::string				ModelName() { return m_ModelName; }
	glm::vec3&				ModelTransform() { return m_ModelTransform; }
	void					SetModelTransform(glm::vec3 trans) { m_ModelTransform = trans; }

	void					SetScale(float factor) { m_Scale = factor; }
	void					BindVAO(){ glBindVertexArray(m_VAO); };
	static void				BindVAO(GLuint id){ glBindVertexArray(id); };

	void					Scale(nxFloat32 factor);
	void					Draw();
    void					LineDraw();
    void					PointDraw();

    std::vector<nxTexture*>& Cache() { return m_TextureCache; }
    nxTexture* CachedAt(int p_Index) { return m_TextureCache[p_Index]; }

private:

	std::vector<nxByte>		m_EntityData;

	GLuint					m_VBO;
	GLuint					m_VAO;
	GLuint					m_IBO;

	unsigned int			m_Indices;
	unsigned int			m_MaterialIndex;

	unsigned int			m_SceneIndex;
	unsigned int			m_NumMeshes;

	unsigned int			m_NumFaces = 0;
	unsigned int			m_NumVertices = 0;
	unsigned int			m_NumNormals = 0;
	unsigned int			m_NumTexCoords = 0;

	float					m_MaxX, m_MaxY, m_MaxZ;
	float					m_MinX, m_MinY, m_MinZ;
	float					m_Scale;

	unsigned int			m_DataCurrentSize;
	unsigned int			m_VertexDataSize;

	std::vector<int>		m_MaterialIndices;
	std::vector<int>		m_MeshStartIndices;
	std::vector<int>		m_MeshSizes;

    std::vector<nxTexture*> m_TextureCache;

	GLuint					m_AdHocTextureObject;

    std::string				m_ModelName;

	glm::vec3				m_ModelTransform;

};