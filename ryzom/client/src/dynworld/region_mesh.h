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

#ifndef REGION_MESH_HEADER
#define REGION_MESH_HEADER

#include "game_share/dynworld/region_mesh_interface.h"

#include "nel/misc/aabbox.h"
#include "nel/3d/vertex_buffer.h"
#include "nel/3d/index_buffer.h"
#include "nel/3d/texture.h"

class CMaterialDefinition;

class CRegionSubMesh
{
	void getMappedIndices(uint* sourceIndex, uint* destIndices) const;

public:
	static const uint InvalidIndex = 0xFFFFFFFF;

	void reserve(uint verticesCount, uint indicesCount, Voxel::BiomeType biome, Voxel::MaterialType materialType);
	void addTriangle(uint* sourceIndices, const TDecodedVertex** vertices);
	void bindBuffers();

	std::vector<const TDecodedVertex*> m_vertices;
	std::vector<uint> m_indices;
	std::vector<uint> m_indicesMapping;

	NL3D::CVertexBuffer m_vb;
	NL3D::CIndexBuffer m_ib;
	Voxel::BiomeType m_materialBiome;
	Voxel::MaterialType m_materialType;

	NLMISC::CSmartPtr<NL3D::ITexture> DiffuseTop;
	NLMISC::CSmartPtr<NL3D::ITexture> DiffuseSide;
};

class CRegionMesh : public IRegionMesh
{
	CRegionMesh(const CRegionMesh&) {}
	CRegionMesh& operator=(const CRegionMesh&) { return *this; }

public:
	static uint s_getSubMeshIndex(Voxel voxel);

	CRegionMesh();
	virtual ~CRegionMesh();
	virtual void initMesh(const TDecodedMesh& mesh) override;

	std::vector<CRegionSubMesh*> m_submeshes;
	NLMISC::CAABBox m_bv;
	float m_displayFactor;
};

class CSkirtRegionMesh : public IRegionMesh
{

};

#endif
