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

#ifndef MAP_CLIENT_HEADER
#define MAP_CLIENT_HEADER

#include "game_share/dynworld/map.h"
#include "nel/misc/vector.h"
#include "nel/3d/vertex_buffer.h"
#include "nel/3d/index_buffer.h"

class CSlavePager;
class IRegionMesh;
class MarchingCubesController;

class CMapClient : public CMapBase
{
	CSlavePager* m_pager;

	TVoxelPos m_voxelUnderCursor;
	bool m_voxelSelected;
	bool m_voxelSelectionActive;

	TVoxelPos m_emptyVoxelUnderCursor;
	bool m_emptyVoxelSelected;

public:
	CMapClient();
	~CMapClient();

	using CMapBase::selectSectors;
	void selectSectors(const NLMISC::CAABBox &bbox, TSectorsList& sectorsOut, uint lod) const;
	void selectSectors(const NLMISC::CVector& center, uint radius, TSectorsList& sectorsOut, uint lod, uint startRadius = 0) const;

	using CMapBase::diffSectors;
	template <typename LoadedContainer>
	void diffSectors(const LoadedContainer& loaded, const TSectorsList& input, TSectorsList& added, TSectorsList& removed, uint lod);

	// return false if not needed to generate
	bool prepareExtraction(const TRegion& regionToEmerge);
	void extractMesh(TGenericSectorId sectorId, IRegionMesh& mesh, MarchingCubesController& controller, uint sectorLod, uint meshLod);

	virtual void regionPagedIn(const TRegion& region, TVolume::Chunk* pChunk, const SectorMetadata& infos) override;
	virtual void regionPagedOut(const TRegion& region, TVolume::Chunk* pChunk, SectorMetadata& outInfos) override;

	bool isVoxelSelectionActive() const
	{
		return m_voxelSelectionActive;
	}
	void setVoxelSelectionActivation(bool active = true)
	{
		m_voxelSelectionActive = active;
	}
	void selectVoxelUnderPos(float cursX, float cursY);
	void unselectVoxel();
	bool isVoxelSelected() const
	{
		return m_voxelSelected;
	}
	const TVoxelPos& voxelUnderCursor() const
	{
		return m_voxelUnderCursor;
	}
	bool isEmptyVoxelSelected() const
	{
		return m_emptyVoxelSelected;
	}
	const TVoxelPos& emptyVoxelUnderCursor() const
	{
		return m_emptyVoxelUnderCursor;
	}
};

template <typename LoadedContainer>
void CMapClient::diffSectors(const LoadedContainer& loaded, const TSectorsList& input, TSectorsList& added, TSectorsList& removed, uint lod)
{
	auto convertIt2Id = [](typename LoadedContainer::const_iterator it) { return *it; };
	auto findId = [](const LoadedContainer& list, TGenericSectorId id) { return list.find(id); };
	switch (lod)
	{
	case 0:
		CMapBase::diffSectors<TMeshSectorLod<0>, LoadedContainer>(loaded, input, added, removed, convertIt2Id, findId);
		break;
	case 1:
		CMapBase::diffSectors<TMeshSectorLod<1>, LoadedContainer>(loaded, input, added, removed, convertIt2Id, findId);
		break;
	case 2:
		CMapBase::diffSectors<TMeshSectorLod<2>, LoadedContainer>(loaded, input, added, removed, convertIt2Id, findId);
		break;
	}
}

#endif
