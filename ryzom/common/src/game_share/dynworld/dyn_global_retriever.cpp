// DynWorld - based on 'Ryzom - MMORPG Framework <http://dev.ryzom.com/projects/ryzom/>'
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

#include "dyn_global_retriever.h"

#include "game_share/dynworld/map.h"
#include "game_share/dynworld/map_sector.h"

#include "nel/misc/async_file_manager.h"
#include "nel/misc/hierarchical_timer.h"

#include <limits>
#include <cmath>
using namespace std;
using namespace NLMISC;
using namespace NLPACS;

H_AUTO_DECL ( NLPACS_Refresh_LR_Around )
H_AUTO_DECL ( NLPACS_Retrieve_Position )

#define	NLPACS_HAUTO_REFRESH_LR_AROUND	H_AUTO_USE ( NLPACS_Refresh_LR_Around )
#define	NLPACS_HAUTO_RETRIEVE_POSITION	H_AUTO_USE ( NLPACS_Retrieve_Position )

CDynGlobalRetriever::CDynGlobalRetriever(const CDynRetrieverBank *bank, CMapBase* map)
	: NLPACS::CGlobalRetriever(bank)
	, m_map(map)
{
	_BBox.setCenter(CVector::Null);
	float maxFloat = std::numeric_limits<float>::max();
	_BBox.setHalfSize(CVector(maxFloat, maxFloat, maxFloat));
}

CVector g_prefetchBound(25.f, 25, 25); // to keep "hot" what is just around
bool g_forceRegenerate = false;
void CDynGlobalRetriever::refreshLrAroundNow(const CVector &position, float radius)
{
	refreshLrAroundMax(position, radius);
}

void CDynGlobalRetriever::refreshLrAroundMax(const CVector &position, float radius, uint maxEmerge)
{
	// Select new to load
// 	CAABBox	box;
// 	box.setCenter(position);
// 	box.setHalfSize(g_prefetchBound);
// 
// 	selectInstances(box, _InternalCST);

	if (g_forceRegenerate)
	{
		TSectorsList out;
		m_map->getLoadedSectors(out);
		// TODO: use map deferred command
		m_map->flushSectors(out);
		g_forceRegenerate = false;
	}

// 	uint emergeCount = _InternalCST.CollisionInstances.size();
// 	uint sectorsLoaded = 0;

	// load all possible sectors
// 	for (uint i = 0; i < emergeCount; ++i)
// 	{
// 		// TODO: use map deferred command
// 		if (m_map->emergeSector(_InternalCST.CollisionInstances[i]))
// 			++sectorsLoaded;
// 
// 		if (sectorsLoaded == maxEmerge)
// 			break;
// 	}
}

bool g_refresh = true;
void CDynGlobalRetriever::refreshLrAround(const CVector &position, float radius)
{
	NLPACS_HAUTO_REFRESH_LR_AROUND
	if (g_refresh)
	{
		refreshLrAroundMax(position, radius, 1); // only emerge one at a time
	}
	return;

	// check if retriever bank is all loaded, and if yes don't refresh it
	if (_RetrieverBank->allLoaded())
		return;

	std::list<CLrLoader>::iterator ite = _LrLoaderList.begin();
	while (ite != _LrLoaderList.end())
	{
		// Finished loaded a lr, stream it into rbank
		if (ite->Finished && ite->Successful)
		{
			if (!ite->_Buffer.isReading())
				ite->_Buffer.invert();

			ite->_Buffer.resetBufPos();

			//		NLMEMORY::CheckHeap (true);

			const_cast<CRetrieverBank*>(_RetrieverBank)->loadRetriever(ite->LrId, ite->_Buffer);

			//		NLMEMORY::CheckHeap (true);

			ite->_Buffer.clear();

			//		NLMEMORY::CheckHeap (true);

			//nlinfo("Lr '%s' loading task complete", ite->LoadFile.c_str());

			// Remove this entry
			_LrLoaderList.erase (ite);

			break;
		}

		// Next lr
		ite++;
	}

	CAABBox	box;
	box.setCenter(position);
	box.setHalfSize(CVector(radius, radius, radius));

	selectInstances(box, getInternalCST());

	set<uint>	newlr, in, out;
	map<uint, CVector>	lrPosition;

	uint	i;
	for (i=0; i<getInternalCST().CollisionInstances.size(); ++i)
	{
		uint lrId = (uint)(_Instances[getInternalCST().CollisionInstances[i]].getRetrieverId());
		newlr.insert(lrId);
		lrPosition.insert (map<uint, CVector>::value_type(lrId, _Instances[getInternalCST().CollisionInstances[i]].getBBox().getCenter()));
	}

	const_cast<CRetrieverBank*>(_RetrieverBank)->diff(newlr, in, out);

	set<uint>::iterator	it;

	// unload all possible retrievers
	for (it=out.begin(); it!=out.end(); ++it)
	{
		const_cast<CRetrieverBank*>(_RetrieverBank)->unloadRetriever(*it);
		//nlinfo("Freed Lr '%s'", (_RetrieverBank->getNamePrefix() + "_" + toString(*it) + ".lr").c_str());
	}

	// if load task idle and more lr to load, setup load task
	set<uint>::iterator iteIn = in.begin();
	while (iteIn != in.end())
	{
		// Already exist ?
		ite = _LrLoaderList.begin();
		while (ite != _LrLoaderList.end())
		{
			if (ite->LrId == *iteIn)
				break;

			ite++;
		}

		// Not found ?
		if (ite == _LrLoaderList.end())
		{
			// Get the position for this LR
			map<uint, CVector>::iterator iteLR = lrPosition.find(*iteIn);
			nlassert (iteLR != lrPosition.end());

			_LrLoaderList.push_back (CLrLoader (iteLR->second));
			CLrLoader &loader = _LrLoaderList.back();
			loader.Finished = false;
			loader.LrId = *iteIn;
			loader.LoadFile = _RetrieverBank->getNamePrefix() + "_" + toString(loader.LrId) + ".lr";

			CAsyncFileManager::getInstance().addLoadTask(&loader);

			//nlinfo("Lr '%s' added to load", loader.LoadFile.c_str());
		}

		// Next lr to load
		iteIn++;
	}
}

bool CDynGlobalRetriever::selectInstances(const NLMISC::CAABBox &bbox, CCollisionSurfaceTemp &cst, UGlobalPosition::TType /*type*/) const
{
	m_map->selectSectors<TLoadingSector>(bbox, cst.CollisionInstances);
	return true;
}

// Retrieves the position of an estimated point in the global retriever (double instead.)
NLPACS::UGlobalPosition	CDynGlobalRetriever::retrievePosition(const CVectorD &estimated, double /* threshold */, NLPACS::UGlobalPosition::TType retrieveSpec) const
{
	// TODOKG: do snapping?
	NLPACS_HAUTO_RETRIEVE_POSITION

	// the retrieved position
	CGlobalPosition result = CGlobalPosition(-1, CLocalRetriever::CLocalPosition(-1, estimated));

	// get the best matching instances
	CAABBox	bbpos;
	bbpos.setCenter(estimated);
	bbpos.setHalfSize(CVector(0.f, 0.f, 0.f));
	if (!selectInstances(bbpos, getInternalCST(), retrieveSpec))
	{
		return result;
	}

	if (getInternalCST().CollisionInstances.empty())
	{
		return result;
	}

	TLoadingSector::TId id = getInternalCST().CollisionInstances[0];

	// get the UGlobalPosition of the estimation for this surface
	result.InstanceId = id;
	result.LocalPosition.Surface = 0;
	result.LocalPosition.Estimation = m_map->getLocalPosition(id, estimated);

#if 0 // temp, for reference
	if (!_BBox.include(CVector((float)estimated.x, (float)estimated.y, (float)estimated.z)))
	{
		_ForbiddenInstances.clear();
		return result;
	}


	// get the best matching instances
	CAABBox	bbpos;
	bbpos.setCenter(estimated);
	bbpos.setHalfSize(CVector(0.5f, 0.5f, 0.5f));
	if (!selectInstances(bbpos, _InternalCST, retrieveSpec))
	{
		return result;
	}

	uint	i;

	_InternalCST.SortedSurfaces.clear();

	// for each instance, try to retrieve the position
	for (i=0; i<_InternalCST.CollisionInstances.size(); ++i)
	{
		uint32							id = _InternalCST.CollisionInstances[i];
		const CRetrieverInstance		&instance = _Instances[id];
		const CLocalRetriever			&retriever = _RetrieverBank->getRetriever(instance.getRetrieverId());

		uint	j;
		for (j=0; j<_ForbiddenInstances.size(); ++j)
			if (_ForbiddenInstances[j] == (sint32)id)
				break;

		if (j<_ForbiddenInstances.size() || !retriever.isLoaded())
			continue;

		instance.retrievePosition(estimated, retriever, _InternalCST);
	}

	_ForbiddenInstances.clear();

	if (!_InternalCST.SortedSurfaces.empty())
	{
		// if there are some selected surfaces, sort them
		std::sort(_InternalCST.SortedSurfaces.begin(), _InternalCST.SortedSurfaces.end(), CCollisionSurfaceTemp::CDistanceSurface());

		uint	selInstance;
		float	bestDist = 1.0e10f;
		for (selInstance=0; selInstance<_InternalCST.SortedSurfaces.size(); ++selInstance)
		{
			uint32						id = _InternalCST.SortedSurfaces[selInstance].Instance;
			const CRetrieverInstance	&instance = _Instances[id];

			if (instance.getType() == CLocalRetriever::Interior && _InternalCST.SortedSurfaces[selInstance].Distance < bestDist+6.0f)
				break;

			if (selInstance == 0)
				bestDist = _InternalCST.SortedSurfaces[0].Distance;
		}

		if (selInstance >= _InternalCST.SortedSurfaces.size())
			selInstance = 0;

		uint32							id = _InternalCST.SortedSurfaces[selInstance].Instance;
		const CRetrieverInstance		&instance = _Instances[id];
		const CLocalRetriever			&retriever = _RetrieverBank->getRetriever(instance.getRetrieverId());

		// get the UGlobalPosition of the estimation for this surface
		result.InstanceId = id;
		result.LocalPosition.Surface = _InternalCST.SortedSurfaces[selInstance].Surface;
		result.LocalPosition.Estimation = instance.getLocalPosition(estimated);

		CRetrieverInstance::snapVector(result.LocalPosition.Estimation);

		// if there are more than 1 one possible (and best matching) surface, insure the position within the surface (by moving the point)
		//		if (_InternalCST.SortedSurfaces.size() >= 2 &&
		//			_InternalCST.SortedSurfaces[1].Distance-_InternalCST.SortedSurfaces[0].Distance < InsureSurfaceThreshold)
		if (_InternalCST.SortedSurfaces[selInstance].FoundCloseEdge)
		{
			bool	moved;
			uint	numMove = 0;
			do
			{
				moved = retriever.insurePosition(result.LocalPosition);
				++numMove;
			}
			while (moved && numMove < 100);
			// the algo won't loop infinitely

			if (numMove > 1)
			{
				nldebug("PACS: insured position inside surface (%d,%d)-(%f,%f,%f), %d moves needed", result.InstanceId, result.LocalPosition.Surface, estimated.x, estimated.y, estimated.z, numMove-1);
			}

			if (moved)
			{
				nlwarning ("PACS: couldn't insure position (%.f,%.f) within the surface (surf=%d,inst=%d) after 100 retries", result.LocalPosition.Estimation.x, result.LocalPosition.Estimation.y, result.LocalPosition.Surface, result.InstanceId);
			}
		}

		// and after selecting the best surface (and some replacement) snap the point to the surface
		instance.snap(result.LocalPosition, retriever);


		if (PacsRetrieveVerbose)
			nlinfo("retrievePosition(%f,%f,%f) -> %d/%d/(%f,%f,%f) - %s/%s",
			estimated.x, estimated.y, estimated.z,
			result.InstanceId, result.LocalPosition.Surface,
			result.LocalPosition.Estimation.x, result.LocalPosition.Estimation.y, result.LocalPosition.Estimation.z,
			retriever.getIdentifier().c_str(),
			retriever.getType() == CLocalRetriever::Interior ? "Interior" : "Landscape");
	}
	else
	{
		if (PacsRetrieveVerbose)
			nlwarning("PACS: unable to retrieve correct position (%f,%f,%f)", estimated.x, estimated.y, estimated.z);
		//		nlSleep(1);
	}
#endif

	return result;
}

float CDynGlobalRetriever::getMeanHeight(const UGlobalPosition &pos) const
{
	// TODOKG: check whether a real 'mean' is needed
	CVector testPos = pos.LocalPosition.Estimation;

	// a non-finite input would climb/descend forever on garbage; proceed from
	// the chunk-bottom ground level instead so the caller keeps a usable z
	if (!std::isfinite(testPos.x) || !std::isfinite(testPos.y) || !std::isfinite(testPos.z))
	{
		nlwarning("MVNAN: getMeanHeight refuses non-finite input (%f,%f,%f)",
			testPos.x, testPos.y, testPos.z);
		testPos = CVector::Null;
	}

	// Check if we are not in the ground. If so, get up, bounded like the
	// descent below: a fully-solid column (volume top without air above)
	// must not run the z to +infinity
	const sint climbStartCellZ = (sint)floor(testPos.z * TLoadingSector::InvVoxelSize);
	while (!m_map->isValidPosition(pos.InstanceId, testPos))
	{
		testPos.z += TLoadingSector::VoxelSize;
		if ((sint)floor(testPos.z * TLoadingSector::InvVoxelSize) - climbStartCellZ > 4 * TLoadingSector::SideSizeInVoxels)
			break;
	}

	// Get down to the ground, bounded like CMapBase::getGroundNormal: an
	// all-Air column (unpaged/empty sector) must stop the descent, not run
	// the z to -infinity (client-session hang of testCylinderMove's ground
	// read when the far-sector id conversions sample an empty column)
	const sint startCellZ = (sint)floor(testPos.z * TLoadingSector::InvVoxelSize);
	while (m_map->isValidPosition(pos.InstanceId, testPos))
	{
		testPos.z -= TLoadingSector::VoxelSize;
		if (startCellZ - (sint)floor(testPos.z * TLoadingSector::InvVoxelSize) > 4 * TLoadingSector::SideSizeInVoxels)
			break;
	}

	// And go back in the air by the amount of density of the voxel: for a
	// fully solid voxel this lands exactly on the voxel top (the tested
	// reference: flat full ground = base + VoxelSize). Subtracting instead
	// sinks the ground inside/below the solid voxel, which puts the next
	// move's feet cell onto the surface voxel itself: the body sweep then
	// clips the start column's own ground (embedded hit at t=0) and any
	// descent puts the start ground top outside the auto-step zone anchored
	// on the (lower) end ground -> phantom wall on gentle downslopes.
	// TODO: use map deferred command
	Voxel voxel = m_map->getVoxelAt(pos.InstanceId, testPos);
	testPos.z += (TLoadingSector::VoxelSize * voxel.getDensity() / Voxel::MaxDensity);

	// quantize to the voxel base (the density fraction of a partial voxel is
	// below one voxel and the floor drops it; full voxels keep their top) so
	// the ground measure is exact voxel multiples and the 1-voxel rule
	// compares cleanly
	if (testPos.z > 0.f)
		testPos.z = floor(testPos.z * TLoadingSector::InvVoxelSize) * TLoadingSector::VoxelSize;
	else
		testPos.z = ceil(testPos.z * TLoadingSector::InvVoxelSize) * TLoadingSector::VoxelSize;

	return testPos.z;
}

CVector CDynGlobalRetriever::getGlobalPosition(const NLPACS::UGlobalPosition &global) const
{
	return m_map->getGlobalPosition(global.InstanceId, global.LocalPosition.Estimation);
}

CVectorD CDynGlobalRetriever::getDoubleGlobalPosition(const NLPACS::UGlobalPosition &global) const
{
	return m_map->getDoubleGlobalPosition(global.InstanceId, global.LocalPosition.Estimation);
}

UGlobalPosition CDynGlobalRetriever::doMove(const UGlobalPosition &startPos, const CVector &delta, float t, CCollisionSurfaceTemp &cst, bool rebuildChains) const
{
	UGlobalPosition result = startPos;

	// clamp factor.
	clamp(t, 0.0f, 1.0f);

	// reset CollisionDescs.
	cst.CollisionDescs.clear();

	CVector		start = startPos.LocalPosition.Estimation;

	// guard against non-finite input (margins: a NaN/inf delta or a poisoned
	// primitive position would propagate into the voxel conversions and hit
	// the CVectorI checkedComponent assert; log and refuse the move instead)
	if (!std::isfinite(start.x) || !std::isfinite(start.y) || !std::isfinite(start.z)
		|| !std::isfinite(delta.x) || !std::isfinite(delta.y) || !std::isfinite(delta.z))
	{
		nlwarning("MVNAN: doMove refuses non-finite input, start=(%f,%f,%f) delta=(%f,%f,%f)",
			start.x, start.y, start.z, delta.x, delta.y, delta.z);
		return startPos;
	}

	// compute end with real delta position.
	UGlobalPosition endPos(startPos);
	endPos.LocalPosition.Estimation = start + delta * t;

	float startHeight = getMeanHeight(startPos);
	float endHeight = getMeanHeight(endPos);

	if (!std::isfinite(startHeight) || !std::isfinite(endHeight))
	{
		nlwarning("MVNAN: doMove refuses non-finite ground, start=(%f,%f,%f) delta=(%f,%f,%f) ground=%f..%f",
			start.x, start.y, start.z, delta.x, delta.y, delta.z, startHeight, endHeight);
		return startPos;
	}

	result.LocalPosition.Estimation = endPos.LocalPosition.Estimation;
	result.LocalPosition.Estimation.z = endHeight;

	// update instance
	CVectorD globalEstimation = getDoubleGlobalPosition(result);
	result = retrievePosition(globalEstimation);

	return result;
#if 0
//	H_AUTO(PACS_GR_doMove);

	CSurfaceIdent	startSurface(startPos.InstanceId, startPos.LocalPosition.Surface);

	// clamp factor.
	clamp(t, 0.0f, 1.0f);

	// 0. reset.
	//===========
	// reset CollisionDescs.
	cst.CollisionDescs.clear();

	// In a surface ?
	if (startPos.InstanceId==-1)
	{
		// Warining: this primitive is not on a surface
		//nlassertonce (0);

		// Return startpos
		return startPos;
	}

	if(!rebuildChains)
	{
		// same move request than prec testMove() ??.
		if( cst.PrecStartSurface != startSurface ||
			cst.PrecStartPos!=startPos.LocalPosition.Estimation ||
			cst.PrecDeltaPos!=delta ||
			!cst.PrecValid)
		{
			// if not, just return start.
			//nlstop;
			//nlwarning ("BEN: you must fix this, it s happen!!!");
			return startPos;
		}
		// Since we are sure we have same movement than prec testMove(), no need to rebuild cst.CollisionChains.
	}
	else
	{
		// we don't have same movement than prec testMove(), we must rebuild cst.CollisionChains.
		// Prec settings no more valids.
		cst.PrecValid= false;
	}




	// 1. Choose a local basis (same than in testMove()).
	//===========
	// Take the retrieverInstance of startPos as a local basis.
	CVector		origin;
	origin= getInstance(startPos.InstanceId).getOrigin();


	// 2. test collisions with CollisionChains.
	//===========
	CVector		start= startPos.LocalPosition.Estimation;
	// compute end with real delta position.
	CVector		end= start + delta*t;

	// If asked, we must rebuild array of collision chains.
	if(rebuildChains)
	{
//		H_AUTO(PACS_GR_doMove_rebuildChains);

		// compute bboxmove.
		CAABBox		bboxMove;
		// must add some extent, to be sure to include snapped CLocalRetriever vertex (2.0f/256 should be sufficient).
		// Nb: this include the precision problem just below (move a little).
		float	radius= 4.0f/Vector2sAccuracy;
		bboxMove.setCenter(start-CVector(radius, radius, 0));
		bboxMove.extend(start+CVector(radius, radius, 0));
		bboxMove.extend(end-CVector(radius, radius, 0));
		bboxMove.extend(end+CVector(radius, radius, 0));

		// find possible collisions in bboxMove+origin. fill cst.CollisionChains.
		findCollisionChains(cst, bboxMove, origin);
	}


	// look where we arrive.
	CSurfaceIdent	endSurface;
	CVector			endRequest= end;
	const sint		maxPbPrec= 32;	// move away from 4 mm at max, in each 8 direction.
	sint			pbPrecNum= 0;

	// must snap the end position.
	CRetrieverInstance::snapVector(endRequest);
	end= endRequest;

	// verify start is already snapped
	{
		CVector		startTest= start;
		CRetrieverInstance::snapVector(startTest);
		nlassert( start == startTest );
	}


	// Normally, just one iteration is made in this loop (but if precision problem (stopOnEdge, startOnEdge....).
	for(;;)
	{
		// must snap the end position.
		CRetrieverInstance::snapVector(end);

		CVector2f	startCol(start.x, start.y);
		CVector2f	endCol(end.x, end.y);

		// If same 2D position, just return startPos (suppose no movement)
		if(endCol==startCol)
		{
			UGlobalPosition		res;
			res= startPos;
			// keep good z movement.
			res.LocalPosition.Estimation.z= end.z;
			return res;
		}

		// search destination problem.
		UGlobalPosition	restart;
		endSurface= testMovementWithCollisionChains(cst, startCol, endCol, startSurface, restart);

		// if no precision problem, Ok, we have found our destination surface (or anormal collide against a wall).
		if (endSurface.SurfaceId >= -1)
		{
			break;
		}
		// left an interior, retrieved position and ask to restart collision from retrieved position
		else if (endSurface.SurfaceId == -3)
		{
			start = getDoubleGlobalPosition(restart) - origin;
			startSurface.RetrieverInstanceId = restart.InstanceId;
			startSurface.SurfaceId = restart.LocalPosition.Surface;
			// should be snapped here
			CVector		startTest= start;
			CRetrieverInstance::snapVector(startTest);
			nlassert( start == startTest );
		}
		/* else we are in deep chit, for one on those reason:
			- traverse on point.
			- stop on a edge (dist==0).
			- start on a edge (dist==0).
			- run // on a edge (NB: dist==0 too).
		*/
		else if (endSurface.SurfaceId == -2)
		{
			// For simplicty, just try to move a little the end position
			if(pbPrecNum<maxPbPrec)
			{
				static struct	{sint x,y;}   dirs[8]= { {1,0}, {1,1}, {0,1}, {-1,1}, {-1,0}, {-1,-1}, {0,-1}, {1,-1}};
				sint	dir= pbPrecNum%8;
				sint	dist= pbPrecNum/8+1;
				CVector		dta;

				// compute small move.
				dta.x= dirs[dir].x * dist * 1.0f/SnapPrecision;
				dta.y= dirs[dir].y * dist * 1.0f/SnapPrecision;
				dta.z= 0;

				// add it to the original end pos requested.
				end= endRequest + dta;

				pbPrecNum++;
			}
			else
			{
				// do not move at all.
				endSurface= CSurfaceIdent(-1,-1);
				break;
			}
		}
	}

	// 3. return result.
	//===========
	// Problem?? do not move.
	if(endSurface.SurfaceId==-1)
		return startPos;
	else
	{
		// else must return good GlobalPosition.
		CGlobalPosition		res;

		res.InstanceId= endSurface.RetrieverInstanceId;
		res.LocalPosition.Surface= endSurface.SurfaceId;

		// compute newPos, localy to the endSurface.
		// get delta between startPos.instance and curInstance.
		// NB: for float precision, it is important to compute deltaOrigin, and after compute newPos in local.
		CVector		deltaOrigin;
		deltaOrigin= origin - getInstance(res.InstanceId).getOrigin();

		// Because Origin precision is 1 meter, and end precision is 1/1024 meter, we have no precision problem.
		// this is true because we cannot move more than, say 4*160 meters in one doMove().
		// So global position should not be bigger than 1024 * 1024/1024 meters.  => Hence 20 bits of precision is
		// required. We have 23 with floats.
		res.LocalPosition.Estimation= end + deltaOrigin;


		// result.
		return res;
	}
#endif
}

// --- movement collision parameters (issue #2, phase B2 of
// docs/PLAYER_WALL_CLIFF_COLLISION_PLAN.md; decided with the user: slide-along
// wall, climb up/down at most ONE voxel regardless of slope/density, no
// falling) ---

// acceptance rule (plan §4.2): climb up/down ONE voxel per move at most;
// anything taller is a wall. Keep this at 1 voxel: raising it auto-climbs
// metre-high faces and breaks the block tests
static const float g_AutoStep = TLoadingSector::VoxelSize;
// blocked-by-ground-shape contacts fire at once (a contact near the end of the
// move makes the PACS reaction's push-back term DIST_BACK/(dt-ContactTime)
// explode: the remaining gap shrinks by 0.001 each blocked tick so the kick
// grows x1000 per tick, compounding _Speed to inf then NaN)
static const float g_GroundBlockedContactTime = 0.f;
// body wall spans: the three voxel cells above the feet cell
static const sint g_BodySpanCount = 3;

const TCollisionSurfaceDescVector* CDynGlobalRetriever::testCylinderMove(const UGlobalPosition &startPos, const CVector &delta, float /*radius*/, CCollisionSurfaceTemp &cst) const
{
	// real movement test on the voxel terrain. v1 note: the radius is ignored
	// (the body is swept as a few sample points above the feet, not a true
	// cylinder; a span-based approximation was the plan's step 2)
	cst.CollisionDescs.clear();

	const CVector &start = startPos.LocalPosition.Estimation;

	// non-finite input would poison the descs (normals, contact times) — refuse
	if (!std::isfinite(start.x) || !std::isfinite(start.y) || !std::isfinite(start.z)
		|| !std::isfinite(delta.x) || !std::isfinite(delta.y) || !std::isfinite(delta.z))
	{
		nlwarning("MVNAN: testCylinderMove refuses non-finite input, start=(%f,%f,%f) delta=(%f,%f,%f)",
			start.x, start.y, start.z, delta.x, delta.y, delta.z);
		return &cst.CollisionDescs;
	}

	// MVDBG (temporary, strip with the MVNAN set at B6/B5): identifies the
	// sector-id <-> estimation frame mismatch that lands the body sweep on a
	// mirrored/doubled column for nonzero sector ids (embedded t=0 hits on
	// gentle slopes, confirmed in a client session with the debugger: id
	// sector decodes negated against the true sector, and the converter
	// scales the origin by SideSizeInVoxels instead of SideSize)
	{
		sint dbgSx, dbgSy, dbgSz;
		TLoadingSector::s_idToPos(startPos.InstanceId, dbgSx, dbgSy, dbgSz);
		const CVectorD dbgGlobal = m_map->getDoubleGlobalPosition(startPos.InstanceId, start);
		nlwarning("MVDBG: id=%" NL_I64 "u sector=(%d,%d,%d) est=(%.3f,%.3f,%.3f) global=(%.3f,%.3f,%.3f)",
			(sint64)startPos.InstanceId, dbgSx, dbgSy, dbgSz,
			start.x, start.y, start.z, dbgGlobal.x, dbgGlobal.y, dbgGlobal.z);
	}

	// horizontal movement only: the vertical is owned by the ground search
	// (no falling in v1), and a zero-length move has nothing to test
	CVector move(delta.x, delta.y, 0.f);
	const float moveLen = move.norm();
	if (moveLen < 0.001f)
		return &cst.CollisionDescs;

	// destination ground, evaluated with the same walk getMeanHeight uses for
	// the doMove z snap (so the test and the accept agree about the ground)
	UGlobalPosition endPos(startPos);
	endPos.LocalPosition.Estimation.x += move.x;
	endPos.LocalPosition.Estimation.y += move.y;

	const float startGroundZ = getMeanHeight(startPos);
	const float endGroundZ = getMeanHeight(endPos);

	// MVDBG (temporary, strip with the MVNAN set): the walkability inputs
	// next to the frame trace — the stand z against the two grounds tells
	// whether the snap itself is one voxel low (surface inside the next
	// voxel up)
	nlwarning("MVDBG2: startGround=%.3f endGround=%.3f est.z=%.3f",
		startGroundZ, endGroundZ, startPos.LocalPosition.Estimation.z);

	// ground walkability: the destination ground may rise or fall by at most ONE
	// voxel (climb up/down 1 voxel only, whatever the local slope or voxel
	// density; this also blocks walking off any higher ledge: no falling)
	const bool groundBlocked = fabsf(endGroundZ - startGroundZ) > g_AutoStep + 0.001f;

	// wall detection: sweep the three body cells above the feet cell; a hit
	// whose voxel top face is more than a voxel above the destination ground
	// is a wall, closer hits are just the terrain surface rising (the auto
	// step zone). Skipped while the ground itself is blocked: the slope
	// contact below covers that case (walking into a mass not reachable on
	// foot is a contour slide, not a wall face slide)
	bool wallBlocked = false;
	float wallTime = 1.f;
	CVector wallNormal;
	if (!groundBlocked)
	{
		const sint feetCell = (sint)floor(start.z * TLoadingSector::InvVoxelSize);
		for (sint span = 1; span <= g_BodySpanCount && !wallBlocked; ++span)
		{
			const float sweepZ = (float(feetCell + span) + 0.5f) * TLoadingSector::VoxelSize;
			CMapBase::TSegmentHit hit;
			const CVector sweepEnd(start.x + move.x, start.y + move.y, sweepZ);
			if (m_map->traceSegment(startPos.InstanceId, CVector(start.x, start.y, sweepZ), sweepEnd, hit))
			{
				const float hitTop = (float(hit.m_voxelPos.z) + 1.f) * TLoadingSector::VoxelSize;

				// MVDBG3 (temporary): is the hit voxel a real terrain lip at
				// our own column, or a mirrored-volume sample? prints the
				// hit cell's content and its point-mirrored counterpart's
				{
					const Voxel hitVoxel = m_map->getVoxelAt(hit.m_voxelPos);
					TVoxelPos mirrorPos(-hit.m_voxelPos.x, -hit.m_voxelPos.y, hit.m_voxelPos.z);
					const Voxel mirrorVoxel = m_map->getVoxelAt(mirrorPos);
					nlwarning("MVDBG3: hit=(%d,%d,%d) type=%d density=%u | mirror type=%d density=%u | feetCell=%d hitTop=%.3f endGround=%.3f",
					    hit.m_voxelPos.x, hit.m_voxelPos.y, hit.m_voxelPos.z,
					    (int)hitVoxel.getType(), (uint)hitVoxel.getDensity(),
					    (int)mirrorVoxel.getType(), (uint)mirrorVoxel.getDensity(),
					    feetCell, hitTop, endGroundZ);

					// MVDBG4 (temporary): the vertical profile of the hit
					// column around the sweep's z — voxel hitZ-2..+5 —
					// resolves where the marched surface band caps
					// relative to the reported ground
					std::string profile;
					for (sint32 vz = hit.m_voxelPos.z - 2; vz <= hit.m_voxelPos.z + 5; ++vz)
					{
						const Voxel v = m_map->getVoxelAt(TVoxelPos(hit.m_voxelPos.x, hit.m_voxelPos.y, vz));
						profile += toString("[%d:t%d,d%u] ", vz, (int)v.getType(), (uint)v.getDensity());
					}
					nlwarning("MVDBG4: profile z=%d..%d  %s",
						hit.m_voxelPos.z - 2, hit.m_voxelPos.z + 5, profile.c_str());
				}

				if (hitTop <= endGroundZ + g_AutoStep + 0.001f)
					continue; // terrain surface rising: destination voxel

				wallBlocked = true;
				wallTime = hit.m_t;
				wallNormal = hit.m_faceNormal;
				if (wallNormal.norm() < 1e-6f)
				{
					// embedded start has no face normal: oppose the move head-on
					wallNormal = move * (-1.f / moveLen);
				}
			}
		}
	}

	if (wallBlocked)
	{
		// wall face contact: the face normal already opposes the traversal
		// (horizontal by construction: the sweep keeps z constant)
		CCollisionSurfaceDesc desc;
		desc.ContactNormal = CVectorD(wallNormal.x, wallNormal.y, 0.0);
		desc.ContactTime = wallTime;
		// (-1,-1) is unresolvable through CGlobalRetriever::getSurfaceById:
		// the move container treats the contact as a wall
		desc.ContactSurface = CSurfaceIdent(-1, -1);
		cst.CollisionDescs.push_back(desc);
	}

	if (groundBlocked)
	{
		// ground shape contact: the move is refused head-on (no gradient work;
		// the slide that remains is the move projected on the obstacle plane)
		CVector contactNormal = move * (-1.f / moveLen);
		contactNormal.z = 0.f;
		contactNormal.normalize();

		CCollisionSurfaceDesc desc;
		desc.ContactNormal = CVectorD(contactNormal.x, contactNormal.y, 0.0);
		desc.ContactTime = g_GroundBlockedContactTime;
		desc.ContactSurface = CSurfaceIdent(-1, -1);
		cst.CollisionDescs.push_back(desc);
	}

	return &cst.CollisionDescs;
}

/// Test if the position is an interior
bool CDynGlobalRetriever::isInterior(const UGlobalPosition &pos) const
{
	return false;
}

/// Test if the position is in water
bool CDynGlobalRetriever::isWaterPosition(const UGlobalPosition &pos, float &waterHeight) const
{
	return false;
}

const string& CDynGlobalRetriever::getIdentifier(const UGlobalPosition &position) const
{
	static const string nullString = string("");
	return nullString;
}

void CDynGlobalRetriever::getBorders(const UGlobalPosition &pos, std::vector<std::pair<CLine, uint8> > &edges)
{
	
}