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

	// --- movement collision queries (GitHub issue #2, phase B1 of
	// docs/PLAYER_WALL_CLIFF_COLLISION_PLAN.md) -------------------------------

	// Voxels block movement when they are neither Air (empty) nor Liquid
	// (wading); Object voxels block like Raw (blanket rule, see the plan §6).
	static bool isBlockingVoxel(Voxel voxel)
	{
		Voxel::Type type = voxel.getType();
		return type != Voxel::Air && type != Voxel::Liquid;
	}

	// result of a segment sweep: the first blocking voxel, if any
	struct TSegmentHit
	{
		bool m_hit;
		float m_t;                    // param along the swept segment (0..1); 1.f when no hit
		TVoxelPos m_voxelPos;         // voxel position of the blocking voxel
		NLMISC::CVector m_faceNormal; // outward normal of the blocking face

		TSegmentHit() : m_hit(false), m_t(1.f), m_voxelPos(0, 0, 0), m_faceNormal(0.f, 0.f, 0.f) {}
	};

	// true when the position holds a blocking voxel
	bool isBlockingPosition(TLoadingSector::TId localSector, const NLMISC::CVector &estimated);

	// sweeps the segment (local-sector coordinates) from start to end and fills
	// outHit with the first blocking voxel; returns outHit.m_hit
	bool traceSegment(TLoadingSector::TId localSector, const NLMISC::CVector &start, const NLMISC::CVector &end, TSegmentHit& outHit);

	// locates the ground voxel at/below the estimated position (climbs out of
	// a non-Air voxel like CDynGlobalRetriever::getMeanHeight, else descends to
	// the first non-Air voxel) and returns its outward surface normal in
	// outNormal; returns false when no ground is found within searchMaxVoxels
	bool getGroundNormal(TLoadingSector::TId localSector, const NLMISC::CVector &estimated, NLMISC::CVector& outNormal, Voxel* voxelOut = nullptr, sint searchMaxVoxels = 4 * TLoadingSector::SideSizeInVoxels);

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
