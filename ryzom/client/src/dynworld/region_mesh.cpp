// DynWorld
// Copyright (C) 2014 Kishan Grimout <kishan.grimout@gmail.com>
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Affero General Public License as
// published by the Free Software Foundation, either version 3 of the
// License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

#include "region_mesh.h"

#include "nel/3d/tile_bank.h"

#include "game_share/dynworld/map_sector.h"
#include "game_share/dynworld/material_library.h"


using namespace NL3D;
using namespace NLMISC;

void CRegionSubMesh::getMappedIndices(uint* sourceIndex, uint* destIndices) const
{
	destIndices[0] = InvalidIndex;
	destIndices[1] = InvalidIndex;
	destIndices[2] = InvalidIndex;

	for (uint i = 0; i < m_indicesMapping.size(); ++i)
	{
		if (m_indicesMapping[i] == sourceIndex[0])
			destIndices[0] = i;
		else if (m_indicesMapping[i] == sourceIndex[1])
			destIndices[1] = i;
		else if (m_indicesMapping[i] == sourceIndex[2])
			destIndices[2] = i;
	}
}

void CRegionSubMesh::reserve(uint verticesCount, uint indicesCount, Voxel::BiomeType biome, Voxel::MaterialType materialType)
{
	m_vertices.clear();
	m_vertices.reserve(verticesCount);

	m_indices.clear();
	m_indices.reserve(indicesCount);

	m_indicesMapping.clear();
	m_indicesMapping.reserve(indicesCount);

	m_materialBiome = biome;
	m_materialType = materialType;
}

void CRegionSubMesh::addTriangle(uint* sourceIndices, const TDecodedVertex** vertices)
{
	uint destIndices[3];
	getMappedIndices(sourceIndices, destIndices);

	// build the submesh triangle
	for (uint i = 0; i < 3; ++i)
	{
		// check if mapping already exists
		if (destIndices[i] == CRegionSubMesh::InvalidIndex)
		{
			destIndices[i] = m_indicesMapping.size();
			m_indicesMapping.push_back(sourceIndices[i]);
			m_vertices.push_back(vertices[i]);
		}

		// add submesh new index
		m_indices.push_back(destIndices[i]);
	}
}

void CRegionSubMesh::bindBuffers()
{
	if (m_vertices.empty())
		return;

	m_vb.addValueEx(NL3D::CVertexBuffer::Position, NL3D::CVertexBuffer::Float3);
	m_vb.addValueEx(NL3D::CVertexBuffer::Normal, NL3D::CVertexBuffer::Float3);
	m_vb.addValueEx(NL3D::CVertexBuffer::Empty, NL3D::CVertexBuffer::Float1); // alpha
	m_vb.initEx();

	m_vb.setNumVertices(m_vertices.size());

	{
		NL3D::CVertexBufferReadWrite vba;
		m_vb.lock(vba);
		for (uint i = 0; i < m_vertices.size(); ++i)
		{
			vba.setValueFloat3Ex(NL3D::CVertexBuffer::Position, i, *(const NLMISC::CVector*)&(m_vertices[i]->position));
			vba.setValueFloat3Ex(NL3D::CVertexBuffer::Normal, i, *(const NLMISC::CVector*)&(m_vertices[i]->normal));

			float alpha;
			if (m_vertices[i]->data.getBiome() == m_materialBiome)
				alpha = (m_vertices[i]->data.getMaterial() >= m_materialType) ? 1.f : 0.f;
			else
				alpha = (m_vertices[i]->data.getBiome() >= m_materialBiome) ? 1.f : 0.f;

			vba.setValueFloat1Ex(NL3D::CVertexBuffer::Empty, i, alpha);
		}
	}

	//Now we can set to active our vertex buffer
	m_ib.setNumIndexes(m_indices.size());
	{
		CIndexBufferReadWrite iba1;
		m_ib.lock(iba1);

		//With vertex we can now make some triangles for cube
		for (uint i = 0; i < m_indices.size(); i += 3)
		{
			iba1.setTri(i, m_indices[i], m_indices[i + 1], m_indices[i + 2]);
		}
	}

	m_vertices.clear();
	m_indices.clear();
	m_indicesMapping.clear();
}

uint CRegionMesh::s_getSubMeshIndex(Voxel voxel)
{
	uint maxMatCount = CMaterialLibrary::instance().getMaxMaterialId() + 1;
	return voxel.getBiome() * maxMatCount + voxel.getMaterial();
}

CRegionMesh::CRegionMesh()
	: m_displayFactor(1.f)
{
	
}

CRegionMesh::~CRegionMesh()
{
	uint submeshesCount = m_submeshes.size();
	for (uint i = 0; i < submeshesCount; ++i)
	{
		delete m_submeshes[i];
	}
}

void CRegionMesh::initMesh(const TDecodedMesh& decodedMesh)
{
	uint maxEnvCount = CMaterialLibrary::instance().getMaxEnvironmentId() + 1;
	uint maxMatCount = CMaterialLibrary::instance().getMaxMaterialId() + 1;
	m_submeshes.resize(maxEnvCount * maxMatCount);

	const sint vecIndicesCount = decodedMesh.getNoOfIndices();
	const sint vecVerticesCount = decodedMesh.getNoOfVertices();

	if (vecVerticesCount != 0)
	{
		m_bv.setCenter(ToNeLVectorF(decodedMesh.getVertex(0).position));
	}
	
	// reference vertices to their corresponding mapping
	uint sourceIndices[3];
	uint destIndices[3];
	Voxel matUsed[3];
	uint submeshIndices[3];
	const TDecodedVertex* triVertices[3];
	for (uint idx = 0; idx < vecIndicesCount; idx += 3)
	{
		// get the 3 indices of the triangle
		for (uint i = 0; i < 3; ++i)
			sourceIndices[i] = decodedMesh.getIndex(idx + i);

		// get the 3 indices of the triangle
		for (uint i = 0; i < 3; ++i)
		{
			triVertices[i] = &decodedMesh.getVertex(sourceIndices[i]);
			m_bv.extend(ToNeLVectorF(triVertices[i]->position));
		}

		// get the 3 materials of the triangle
		for (uint i = 0; i < 3; ++i)
			matUsed[i] = decodedMesh.getVertex(sourceIndices[i]).data;

		for (uint i = 0; i < 3; ++i)
		{
			submeshIndices[i] = s_getSubMeshIndex(matUsed[i]);
		}

		// add triangle for the first material
		uint submeshIndex = submeshIndices[0];
		CRegionSubMesh* submesh = m_submeshes[submeshIndex];
		if (submesh == NULL)
		{
			submesh = m_submeshes[submeshIndex] = new CRegionSubMesh();
			submesh->reserve(vecVerticesCount, vecIndicesCount, matUsed[0].getBiome(), matUsed[0].getMaterial());
		}

		submesh->addTriangle(sourceIndices, triVertices);

		// do the same for second material if different
		if (matUsed[0].getMaterialAndBiome() != matUsed[1].getMaterialAndBiome())
		{
			submeshIndex = submeshIndices[1];
			submesh = m_submeshes[submeshIndex];
			if (submesh == NULL)
			{
				submesh = m_submeshes[submeshIndex] = new CRegionSubMesh();
				submesh->reserve(vecVerticesCount, vecIndicesCount, matUsed[1].getBiome(), matUsed[1].getMaterial());
			}

			submesh->addTriangle(sourceIndices, triVertices);
		}

		// do the same for third material if different
		if (matUsed[2].getMaterialAndBiome() != matUsed[0].getMaterialAndBiome() && matUsed[2].getMaterialAndBiome() != matUsed[1].getMaterialAndBiome())
		{
			submeshIndex = submeshIndices[2];
			submesh = m_submeshes[submeshIndex];
			if (submesh == NULL)
			{
				submesh = m_submeshes[submeshIndex] = new CRegionSubMesh();
				submesh->reserve(vecVerticesCount, vecIndicesCount, matUsed[2].getBiome(), matUsed[2].getMaterial());
			}

			submesh->addTriangle(sourceIndices, triVertices);
		}
	}

	// initialize vertex and index buffers
	const uint submeshesCount = m_submeshes.size();
	for (uint i = 0; i < submeshesCount; ++i)
	{
		CRegionSubMesh* submesh = m_submeshes[i];
		if (submesh != NULL)
		{
			submesh->bindBuffers();
		}
	}
}
