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

#ifndef MAP_HEADER
#define MAP_HEADER

#include "nel/misc/vectord.h"
#include "nel/misc/aabbox.h"
#include "nel/misc/mutex.h"

#include "game_share/dynworld/polyvox_defs.h"
#include "game_share/dynworld/map_sector.h"
#include "game_share/dynworld/map_deferred_command.h"

class IVoxelListener
{
public:
	virtual void voxelTouched(const TVoxelPos& voxelPos, Voxel voxelBefore, Voxel voxelAfter) {}
	virtual void regionPagedIn(const TRegion& region, TVolume::Chunk* pChunk, const SectorMetadata& infos) {}
	virtual void regionPagedOut(const TRegion& region, TVolume::Chunk* pChunk) {}
};

class ICastCollector;

using SectorStatus = PolyVox::RegionStatus;

class CMapBase
{
public:
	CMapBase();
	virtual ~CMapBase();

	void initMap(TPager* pager, uint64 memoryLimit);

	SectorStatus isSectorLoaded(TLoadingSector::TId sectorIndex) const;

	SectorMetadata* getSectorInfo(TLoadingSector::TId sectorIndex);

	// returns true if all selected sectors are loaded
	// if orderByDist is true, closer sectors will be placed first in sectorsOut
	template <typename SectorType>
	void selectSectors(const NLMISC::CAABBox &bbox, TSectorsList& sectorsOut, uint startRadius = 0) const;

	template <typename SectorType>
	void selectSectors(const NLMISC::CVector& center, uint radius, TSectorsList& sectorsOut, uint startRadius = 0) const;

	template <typename SectorType>
	void selectSectorsNoOrder(const NLMISC::CAABBox &bbox, TSectorsList& sectorsOut) const;

	// generic version
	template <typename SectorType, typename LoadedContainer, typename It2IdFunc, typename FindFunc>
	void diffSectors(const LoadedContainer& loaded, const TSectorsList& input, TSectorsList& added, TSectorsList& removed, It2IdFunc getIdFunc, FindFunc findFunc);

	// version upon loaded sectors (thus SectorType is TLoadingSector and 'loaded' list is m_loadedSectors)
	void diffSectors(const TSectorsList& input, TSectorsList& added, TSectorsList& removed);

	// returns true if it has been loaded, false if it was already loaded
	bool emergeSector(TLoadingSector::TId sectorIndex);

	void flushSectors(const TSectorsList &list);

	NLMISC::CVector getLocalPosition(TLoadingSector::TId, const NLMISC::CVectorD &estimated);
	NLMISC::CVector getGlobalPosition(TLoadingSector::TId, const NLMISC::CVector &estimated);
	NLMISC::CVectorD getDoubleGlobalPosition(TLoadingSector::TId, const NLMISC::CVector &estimated);

	bool isValidPosition(TLoadingSector::TId, const NLMISC::CVector &estimated);
	Voxel getVoxelAt(TLoadingSector::TId, const NLMISC::CVector &estimated);

	Voxel getVoxelAt(const TVoxelPos& voxelPos);
	void setVoxelAt(const TVoxelPos& voxelPos, Voxel voxel);

	void getLoadedSectors(TSectorsList& outList) const;

	float castRay(const NLMISC::CVector& start, const NLMISC::CVector& end, ICastCollector* hitCollector = nullptr, ICastCollector* emptyVoxelsCollector = nullptr);

	void registerVoxelListener(IVoxelListener* listener);
	void unregisterVoxelListener(IVoxelListener* listener);

	virtual void regionPagedIn(const TRegion& region, TVolume::Chunk* pChunk, const SectorMetadata& infos);
	virtual void regionPagedOut(const TRegion& region, TVolume::Chunk* pChunk, SectorMetadata& outInfos);

protected:
	mutable NLMISC::CMutex m_volumeLock; // TODO: find a better way to avoid concurrent accesses to volume
	TVolume* m_volume;

	typedef std::map<TLoadingSector::TId, SectorMetadata> TSectorsContainer;
	TSectorsContainer m_loadedSectors;
	TSectorsContainer m_unloadedSectors; // keep cached metadata for unloaded sectors

	typedef std::vector<IVoxelListener*> TVoxelsListenersContainer;
	TVoxelsListenersContainer m_voxelsListerners;
	void voxelTouched(const TVoxelPos& voxelPos, Voxel voxelBefore, Voxel voxelAfter);
};

class CPagerBase : public TPager
{
protected:
	CMapBase& m_map;

public:
	CPagerBase(CMapBase& map) : m_map(map) {}
	virtual ~CPagerBase() {}
};

// Some basic commands
class CPrefetchSectorCommand : public IMapDeferredCommand
{
	void prefetchSector();

	virtual void getName(std::string& name) const override
	{
		name = "Prefetch Sector";
	}

	virtual float getPriority() const override
	{
		return -2.f;
	}
public:
	CPrefetchSectorCommand(CMapBase* map, TLoadingSector::TId sectorId, const IMapDeferredCommand* parent = nullptr);

	virtual bool execute() override;

	CMapBase* m_map;
	TLoadingSector::TId m_sectorId;
};

#include "map.hpp"

#endif
