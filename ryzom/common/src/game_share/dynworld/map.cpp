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

#include <limits>
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

	CVector sectorOrig(float(sectorX * TLoadingSector::SideSize), float(sectorY * TLoadingSector::SideSize), float(sectorZ * TLoadingSector::SideSize));
	nlassert(std::isfinite((float)(+estimated.x - sectorOrig.x)) && std::isfinite((float)(+estimated.y - sectorOrig.y)) && std::isfinite((float)(+estimated.z - sectorOrig.z)));
	return CVector((float)(+estimated.x-sectorOrig.x), (float)(+estimated.y-sectorOrig.y), (float)(estimated.z-sectorOrig.z));
}

CVector CMapBase::getGlobalPosition(TLoadingSector::TId localSector, const CVector &estimated)
{
	if (localSector == TLoadingSector::InvalidId)
		return estimated;

	sint32 sectorX, sectorY, sectorZ;
	TLoadingSector::s_idToPos(localSector, sectorX, sectorY, sectorZ);

	CVector sectorOrig(float(sectorX * TLoadingSector::SideSize), float(sectorY * TLoadingSector::SideSize), float(sectorZ * TLoadingSector::SideSize));
	return CVector(+estimated.x+sectorOrig.x, +estimated.y+sectorOrig.y, estimated.z+sectorOrig.z );
}

CVectorD CMapBase::getDoubleGlobalPosition(TLoadingSector::TId localSector, const CVector &estimated)
{
	if (localSector == TLoadingSector::InvalidId)
		return CVectorD(estimated);

	sint32 sectorX, sectorY, sectorZ;
	TLoadingSector::s_idToPos(localSector, sectorX, sectorY, sectorZ);

	CVectorD sectorOrig(double(sectorX * TLoadingSector::SideSize), double(sectorY * TLoadingSector::SideSize), double(sectorZ * TLoadingSector::SideSize));
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
	// floor per axis, NOT the truncating CVectorI float path: in the
	// negative-coordinate quadrants trunc toward zero samples the
	// NEIGHBORING column (e.g. voxel y -67658.88 read as -67658 instead of
	// the true -67659), one full voxel off in x/y; the ground walk then
	// reads a neighbor's ground, the player snaps half a voxel under the
	// true surface and the body sweep hits the real column's sheet band
	// (phantom blocks on flat ground, found in a client session)
	return getVoxelAt(TVoxelPos(floor(position.x), floor(position.y), floor(position.z)));
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

bool CMapBase::isBlockingPosition(TLoadingSector::TId localSector, const NLMISC::CVector &estimated)
{
	return isBlockingVoxel(getVoxelAt(localSector, estimated));
}

bool CMapBase::traceSegment(TLoadingSector::TId localSector, const NLMISC::CVector &start, const NLMISC::CVector &end, TSegmentHit& outHit)
{
	outHit = TSegmentHit();

	// to global coordinates, then to voxel space (double precision, floor
	// per axis so the sweep behaves the same on negative coordinates)
	CVectorD globalStart = getDoubleGlobalPosition(localSector, start);
	CVectorD globalEnd = getDoubleGlobalPosition(localSector, end);
	globalStart *= TLoadingSector::InvVoxelSize;
	globalEnd *= TLoadingSector::InvVoxelSize;

	const double origin[3] = { globalStart.x, globalStart.y, globalStart.z };
	const double dir[3] = { globalEnd.x - globalStart.x, globalEnd.y - globalStart.y, globalEnd.z - globalStart.z };

	// DDA over the voxel cells the segment crosses (Amanatides & Woo): at
	// every step advance to the nearest next cell boundary, then test that
	// cell; tMax values are the crossing parameters along the whole segment
	sint32 cell[3];
	sint32 step[3];
	double tMax[3], tDelta[3];
	const double inf = std::numeric_limits<double>::infinity();
	for (uint axis = 0; axis < 3; ++axis)
	{
		cell[axis] = (sint32)floor(origin[axis]);
		if (dir[axis] == 0.0)
		{
			step[axis] = 0;
			tMax[axis] = inf;
			tDelta[axis] = inf;
		}
		else
		{
			step[axis] = dir[axis] > 0.0 ? 1 : -1;
			const double nextBoundary = dir[axis] > 0.0 ? double(cell[axis] + 1) : double(cell[axis]);
			tMax[axis] = (nextBoundary - origin[axis]) / dir[axis];
			tDelta[axis] = fabs(1.0 / dir[axis]);
		}
	}

	{
		const sint32 lowerX = (sint32)floor(std::min(globalStart.x, globalEnd.x));
		const sint32 lowerY = (sint32)floor(std::min(globalStart.y, globalEnd.y));
		const sint32 lowerZ = (sint32)floor(std::min(globalStart.z, globalEnd.z));
		const sint32 upperX = (sint32)floor(std::max(globalStart.x, globalEnd.x));
		const sint32 upperY = (sint32)floor(std::max(globalStart.y, globalEnd.y));
		const sint32 upperZ = (sint32)floor(std::max(globalStart.z, globalEnd.z));
		const TRegion sweepRegion(lowerX, lowerY, lowerZ, upperX, upperY, upperZ);
		TVolume::AutoReadLockRegion lock(m_volume, sweepRegion);

		// starting inside a blocking voxel is a hit at once (no face to
		// project on: leave the normal empty, callers treat this specially)
		if (isBlockingVoxel(m_volume->getVoxel(cell[0], cell[1], cell[2])))
		{
			outHit.m_hit = true;
			outHit.m_t = 0.f;
			outHit.m_voxelPos = TVoxelPos(cell[0], cell[1], cell[2]);
			return true;
		}

		while (true)
		{
			// nearest next cell boundary over the three axes
			const uint axis = (tMax[0] < tMax[1])
				? (tMax[0] < tMax[2] ? 0 : 2)
				: (tMax[1] < tMax[2] ? 1 : 2);
			const double crossing = tMax[axis];
			if (crossing > 1.0)
				break; // past the segment end, no hit

			tMax[axis] += tDelta[axis];
			cell[axis] += step[axis];

			const Voxel voxel = m_volume->getVoxel(cell[0], cell[1], cell[2]);
			if (isBlockingVoxel(voxel))
			{
				outHit.m_hit = true;
				outHit.m_t = (float)crossing;
				outHit.m_voxelPos = TVoxelPos(cell[0], cell[1], cell[2]);
				// outward face normal: opposite to the traversal direction
				const float sign = (float)-step[axis];
				switch (axis)
				{
				case 0: outHit.m_faceNormal = CVector(sign, 0.f, 0.f); break;
				case 1: outHit.m_faceNormal = CVector(0.f, sign, 0.f); break;
				default: outHit.m_faceNormal = CVector(0.f, 0.f, sign); break;
				}
				return true;
			}
		}
	}

	return false;
}

bool CMapBase::getGroundNormal(TLoadingSector::TId localSector, const NLMISC::CVector &estimated, NLMISC::CVector& outNormal, Voxel* voxelOut, sint searchMaxVoxels)
{
	CVectorD globalPos = getDoubleGlobalPosition(localSector, estimated);
	CVectorD voxelPos(globalPos);
	voxelPos *= TLoadingSector::InvVoxelSize;

	const sint32 cellX = (sint32)floor(voxelPos.x);
	const sint32 cellY = (sint32)floor(voxelPos.y);
	const sint32 cellZ = (sint32)floor(voxelPos.z);
	const TVoxelPos startCell(cellX, cellY, cellZ);

	// find the ground like CDynGlobalRetriever::getMeanHeight treats the
	// data: a non-Air entry climbs to the first Air above (the ground being
	// the last non-Air voxel below it), an Air entry descends to the first
	// non-Air voxel below, and liquids stop the descent exactly as 'air'
	// stops the climb (standing on the liquid top)
	TVoxelPos groundCell = startCell;
	Voxel groundVoxel = getVoxelAt(groundCell);

	if (groundVoxel.getType() != Voxel::Air)
	{
		while (true)
		{
			if (groundVoxel.getType() == Voxel::Air)
				break;
			++groundCell.z;
			groundVoxel = getVoxelAt(groundCell);
			if (groundCell.z - cellZ > searchMaxVoxels)
				return false; // no air above within the search bound
		}
		--groundCell.z;
		groundVoxel = getVoxelAt(groundCell);
	}
	else
	{
		while (true)
		{
			if (groundVoxel.getType() != Voxel::Air)
				break;
			--groundCell.z;
			groundVoxel = getVoxelAt(groundCell);
			if (cellZ - groundCell.z > searchMaxVoxels)
				return false; // no ground within the search bound
		}
		groundVoxel = getVoxelAt(groundCell);
	}

	// density-field gradient at the ground voxel (same density semantics as
	// MarchingCubesController: 'no density' types count as 0 through
	// Voxel::getDensity): central differences over the 6-neighborhood, then
	// flipped so the normal points away from the solid into the air
	// (a flat floor yields (0, 0, +1))
	const TRegion gradientRegion(groundCell.x - 1, groundCell.y - 1, groundCell.z - 1,
		groundCell.x + 1, groundCell.y + 1, groundCell.z + 1);
	TVolume::AutoReadLockRegion lock(m_volume, gradientRegion);

	auto densityAt = [&](sint32 x, sint32 y, sint32 z)
	{
		return static_cast<float>(m_volume->getVoxel(x, y, z).getDensity());
	};

	NLMISC::CVector outward(
		densityAt(groundCell.x - 1, groundCell.y, groundCell.z) - densityAt(groundCell.x + 1, groundCell.y, groundCell.z),
		densityAt(groundCell.x, groundCell.y - 1, groundCell.z) - densityAt(groundCell.x, groundCell.y + 1, groundCell.z),
		densityAt(groundCell.x, groundCell.y, groundCell.z - 1) - densityAt(groundCell.x, groundCell.y, groundCell.z + 1));

	if (outward.norm() > 1e-6f)
		outward.normalize();
	else
		outward = CVector(0.f, 0.f, 1.f); // no density contrast: fall back to flat
	outNormal = outward;

	if (voxelOut != nullptr)
		*voxelOut = groundVoxel;

	return true;
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
	//NL_PROF_F("PrefetchSectorCommand(%p)", m_sectorId);
	prefetchSector();
	return true;
}
