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

#include "stdpch.h"

#include "map.h"

#include "nel/misc/aabbox.h"
#include "game_share/dynworld/cast_collector.h"
#include "game_share/dynworld/marching_controller.h"

#include <PolyVox/Raycast.h>


using namespace NLMISC;

CMapBase::CMapBase()
	: m_volume(NULL)
{
	
}

CMapBase::~CMapBase()
{
	nlassert(m_loadedSectors.empty());
	delete m_volume;
	m_volume = NULL;
}

void CMapBase::initMap(TPager* pager, uint64 memoryLimit)
{
	m_volume = new TVolume(pager, memoryLimit, 64, new MultiThreadChunkCache);
}

SectorStatus CMapBase::isSectorLoaded(TLoadingSector::TId sectorIndex) const
{
	TRegion region;
	TLoadingSector::getRegion(sectorIndex, region);
	return m_volume->getRegionStatus(region);
}

SectorMetadata* CMapBase::getSectorInfo(TLoadingSector::TId sectorIndex)
{
	CAutoMutex<CMutex> lock(m_volumeLock);
	// not a direct access to the volume but this list is modified by the volume through the pager

	// first, look into loaded sectors
	TSectorsContainer::iterator it = m_loadedSectors.find(sectorIndex);
	if (it != m_loadedSectors.end())
	{
		return &it->second;
	}
	else
	{
		// then in unloaded sectors
		it = m_unloadedSectors.find(sectorIndex);
		if (it != m_unloadedSectors.end())
		{
			return &it->second;
		}
		else
		{
			return nullptr;
		}
	}
}

bool CMapBase::emergeSector(TLoadingSector::TId sectorIndex)
{
	// check it's not already in memory
	if (isSectorLoaded(sectorIndex) == SectorStatus::NotAvailable)
	{
		TRegion regionToEmerge;
		TLoadingSector::getRegion(sectorIndex, regionToEmerge);
		m_volume->prefetch(regionToEmerge);
		return true;
	}
	return false;
}

void CMapBase::flushSectors(const TSectorsList& list)
{
	TRegion regionToFlush;
	for(TSectorsList::const_iterator it = list.begin(); it != list.end(); ++it)
	{
		TLoadingSector::TId id = *it;
		if (isSectorLoaded(id) != SectorStatus::NotAvailable)
		{
			TLoadingSector::getRegion(id, regionToFlush);
			//m_volume->flush(regionToFlush);
		}
	}
}

void CMapBase::diffSectors(const TSectorsList& input, TSectorsList& added, TSectorsList& removed)
{
	CAutoMutex<CMutex> lock(m_volumeLock);
	diffSectors<TLoadingSector>(m_loadedSectors, input, added, removed,
		[](TSectorsContainer::const_iterator it) { return it->first; },
		[](const TSectorsContainer& map, TLoadingSector::TId id) { return map.find(id); });
}

CVector CMapBase::getLocalPosition(TLoadingSector::TId localSector, const CVectorD &estimated)
{
	if (localSector == TLoadingSector::InvalidId)
		return estimated;

	sint32 sectorX, sectorY, sectorZ;
	TLoadingSector::s_idToPos(localSector, sectorX, sectorY, sectorZ);

	CVector sectorOrig(float(sectorX * TLoadingSector::SideSizeInVoxels), float(sectorY * TLoadingSector::SideSizeInVoxels), float(sectorZ * TLoadingSector::SideSizeInVoxels));
	return CVector((float)(+estimated.x-sectorOrig.x), (float)(+estimated.y-sectorOrig.y), (float)(estimated.z-sectorOrig.z));
}

CVector CMapBase::getGlobalPosition(TLoadingSector::TId localSector, const CVector &estimated)
{
	if (localSector == TLoadingSector::InvalidId)
		return estimated;

	sint32 sectorX, sectorY, sectorZ;
	TLoadingSector::s_idToPos(localSector, sectorX, sectorY, sectorZ);

	CVector sectorOrig(float(sectorX * TLoadingSector::SideSizeInVoxels), float(sectorY * TLoadingSector::SideSizeInVoxels), float(sectorZ * TLoadingSector::SideSizeInVoxels));
	return CVector(+estimated.x+sectorOrig.x, +estimated.y+sectorOrig.y, estimated.z+sectorOrig.z );
}

CVectorD CMapBase::getDoubleGlobalPosition(TLoadingSector::TId localSector, const CVector &estimated)
{
	if (localSector == TLoadingSector::InvalidId)
		return CVectorD(estimated);

	sint32 sectorX, sectorY, sectorZ;
	TLoadingSector::s_idToPos(localSector, sectorX, sectorY, sectorZ);

	CVectorD sectorOrig(double(sectorX * TLoadingSector::SideSizeInVoxels), double(sectorY * TLoadingSector::SideSizeInVoxels), double(sectorZ * TLoadingSector::SideSizeInVoxels));
	return CVectorD(+(double)estimated.x+sectorOrig.x, +(double)estimated.y+sectorOrig.y, (double)estimated.z+sectorOrig.z );
}

bool CMapBase::isValidPosition(TLoadingSector::TId localSector, const NLMISC::CVector &estimated)
{
	Voxel voxel = getVoxelAt(localSector, estimated);
	return voxel.getType() == Voxel::Air;
}

Voxel CMapBase::getVoxelAt(TLoadingSector::TId localSector, const NLMISC::CVector &estimated)
{
	CVectorD position = getDoubleGlobalPosition(localSector, estimated);
	position *= TLoadingSector::InvVoxelSize;
	return getVoxelAt(TVoxelPos(position));
}

Voxel CMapBase::getVoxelAt(const TVoxelPos& voxelPos)
{
	return m_volume->getVoxel(voxelPos.x, voxelPos.y, voxelPos.z);
}

void CMapBase::setVoxelAt(const TVoxelPos& voxelPos, Voxel voxel)
{
	Voxel oldVoxel = getVoxelAt(voxelPos);

	m_volume->setVoxel(voxelPos.x, voxelPos.y, voxelPos.z, voxel);
	voxelTouched(voxelPos, oldVoxel, voxel);
}

void CMapBase::getLoadedSectors(TSectorsList& outList) const
{
	CAutoMutex<CMutex> lock(m_volumeLock);
	// not a direct access to the volume but this list is modified by the volume through the pager
	outList.clear();
	outList.reserve(m_loadedSectors.size());
	for (auto it = m_loadedSectors.begin(); it != m_loadedSectors.end(); ++it)
	{
		outList.push_back(it->first);
	}
}

// This is the callback functor which is called by the raycast() function for every voxel it touches.
// It's primary purpose is to tell the raycast whether or not to continue (i.e. it tests whether the
// ray has hit a solid voxel). Because the instance of this class is passed to the raycast() function
// by reference we can also use it to encapsulate some state. We're testing this by counting the total
// number of voxels touched.
class RaycastTestFunctor
{
public:
	RaycastTestFunctor(ICastCollector* hitCollector, ICastCollector* emptyVoxelsCollector)
		: m_hitCollector(hitCollector)
		, m_emptyVoxelsCollector(emptyVoxelsCollector)
		, m_hitPosition(nullptr)
		, m_uVoxelsTouched(0)
	{
	}

	bool operator()(const TVolume::Sampler& sampler)
	{
		m_uVoxelsTouched++;

		bool isEmpty = (sampler.getVoxel().getType() == Voxel::Air);
		bool continueCast = true;
		if (!isEmpty)
		{
			m_hitPosition = sampler;
			if (m_hitCollector != nullptr)
			{
				continueCast = m_hitCollector->addHit(sampler);
			}
			if (!continueCast)
				return false;
		}
		else if (m_emptyVoxelsCollector != nullptr)
		{
			m_emptyVoxelsCollector->addHit(sampler);
		}

		// We are in the volume, so decide whether to continue based on the voxel value.
		return isEmpty && continueCast;
	}

	ICastCollector* m_hitCollector;
	ICastCollector* m_emptyVoxelsCollector;
	TVoxelIterator m_hitPosition;
	uint32 m_uVoxelsTouched;
};

float CMapBase::castRay(const NLMISC::CVector& start, const NLMISC::CVector& end, ICastCollector* hitCollector, ICastCollector* emptyVoxelsCollector)
{
	CVector startVoxel(start);
	startVoxel *= TLoadingSector::InvVoxelSize;

	CVector endVoxel(end);
	endVoxel *= TLoadingSector::InvVoxelSize;

	RaycastTestFunctor raycastTestFunctor(hitCollector, emptyVoxelsCollector);

	PolyVox::RaycastResult result;

	{
		//The doRaycast function is assuming that it is iterating over the areas defined between
		//voxels. We actually want to define the areas as being centered on voxels (as this is
		//what the CubicSurfaceExtractor generates). We add 0.5 here to adjust for this.
		const float x1 = startVoxel.x + 0.5f;
		const float y1 = startVoxel.y + 0.5f;
		const float z1 = startVoxel.z + 0.5f;
		const float x2 = endVoxel.x + 0.5f;
		const float y2 = endVoxel.y + 0.5f;
		const float z2 = endVoxel.z + 0.5f;

		int i = (int)floorf(x1);
		int j = (int)floorf(y1);
		int k = (int)floorf(z1);

		const int iend = (int)floorf(x2);
		const int jend = (int)floorf(y2);
		const int kend = (int)floorf(z2);
		TRegion castRegion(std::min(x1, x2), std::min(y1, y2), std::min(z1, z2), std::max(x1, x2), std::max(y1, y2), std::max(z1, z2));

		TVolume::AutoReadLockRegion(m_volume, castRegion);
		result = PolyVox::raycastWithEndpoints(m_volume, ToPolyVoxVectorF(startVoxel), ToPolyVoxVectorF(endVoxel), raycastTestFunctor);
	}

	float distValue = 1.f;
	if (result == PolyVox::RaycastResults::Interupted)
	{
		CVector voxelPos(ToNeLVectorI(raycastTestFunctor.m_hitPosition.getPosition()));
		voxelPos *= TLoadingSector::VoxelSize; // back to worlds coordinates

		CVector normal(ToNeLVectorF(PolyVox::computeCentralDifferenceGradient(raycastTestFunctor.m_hitPosition, MarchingCubesController{})));
		normal.normalize();

		CVector planeOffset = normal * (TLoadingSector::VoxelSize * raycastTestFunctor.m_hitPosition.getVoxel().getDensity() / float(Voxel::MaxDensity) * 2.f);
		voxelPos += planeOffset;

		CPlane plane;
		plane.make(normal, voxelPos);
		CVector hitPos = plane.intersect(start, end);
		CVector hitVector = hitPos - start;

		CVector ray = end - start;
		float rayNorm = ray.norm();
		distValue = hitVector * (ray / rayNorm);
		distValue /= rayNorm;
	}

	return distValue;
}

void CMapBase::registerVoxelListener(IVoxelListener* listener)
{
	m_voxelsListerners.push_back(listener);
}

void CMapBase::unregisterVoxelListener(IVoxelListener* listener)
{
	auto it = std::find(m_voxelsListerners.begin(), m_voxelsListerners.end(), listener);
	nlassert(it != m_voxelsListerners.end());
	if (it != m_voxelsListerners.end())
	{
		m_voxelsListerners.erase(it);
	}
}

void CMapBase::voxelTouched(const TVoxelPos& voxelPos, Voxel voxelBefore, Voxel voxelAfter)
{
	for (uint i = 0; i < m_voxelsListerners.size(); ++i)
	{
		m_voxelsListerners[i]->voxelTouched(voxelPos, voxelBefore, voxelAfter);
	}
}

void CMapBase::regionPagedIn(const TRegion& region, TVolume::Chunk* pChunk, const SectorMetadata& infos)
{
	NLMISC::CAutoMutex<NLMISC::CMutex> lock(m_volumeLock);

	TLoadingSector::TId sectorIndex = TLoadingSector::getRegionId(region);

#ifndef NL_NO_DEBUG
	TRegion testRegion;
	TLoadingSector::getRegion(sectorIndex, testRegion);
	nlassert(region == testRegion);
#endif

	nlassert(m_loadedSectors.find(sectorIndex) == m_loadedSectors.end());

	// add to loaded sectors
	m_loadedSectors[sectorIndex] = infos;

	// remove from unloaded sectors
	auto unloadedIt = m_unloadedSectors.find(sectorIndex);
	if (unloadedIt != m_unloadedSectors.end())
	{
		m_unloadedSectors.erase(unloadedIt);
	}

	for (uint i = 0; i < m_voxelsListerners.size(); ++i)
	{
		m_voxelsListerners[i]->regionPagedIn(region, pChunk, infos);
	}
}

void CMapBase::regionPagedOut(const TRegion& region, TVolume::Chunk* pChunk, SectorMetadata& outInfos)
{
	NLMISC::CAutoMutex<NLMISC::CMutex> lock(m_volumeLock);

	TLoadingSector::TId sectorIndex = TLoadingSector::getRegionId(region);

#ifndef NL_NO_DEBUG
	TRegion testRegion;
	TLoadingSector::getRegion(sectorIndex, testRegion);
	nlassert(region == testRegion);
#endif

	auto loadedIt = m_loadedSectors.find(sectorIndex);
	nlassert(loadedIt != m_loadedSectors.end());
	outInfos = loadedIt->second;
	m_unloadedSectors.insert(*loadedIt); // cache its metadata into this list
	m_loadedSectors.erase(loadedIt);

	for (uint i = 0; i < m_voxelsListerners.size(); ++i)
	{
		m_voxelsListerners[i]->regionPagedOut(region, pChunk);
	}
}

void CPrefetchSectorCommand::prefetchSector()
{
	m_map->emergeSector(m_sectorId);
}


CPrefetchSectorCommand::CPrefetchSectorCommand(CMapBase* map, TLoadingSector::TId sectorId, const IMapDeferredCommand* parent)
	: IMapDeferredCommand(parent)
	, m_map(map)
	, m_sectorId(sectorId)
{

}

bool CPrefetchSectorCommand::execute()
{
	NL_PROF_F("PrefetchSectorCommand(%p)", m_sectorId);
	prefetchSector();
	return true;
}
