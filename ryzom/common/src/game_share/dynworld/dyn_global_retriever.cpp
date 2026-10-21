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
	bool hadToGoUp = false;

	// Check if we are not in the ground. If so, get up
	// TODO: use map deferred command
	while (!m_map->isValidPosition(pos.InstanceId, testPos))
	{
		hadToGoUp = true;
		testPos.z += TLoadingSector::VoxelSize;
	}

	if (!hadToGoUp)
	{
		// Check if we are in air. If so, get down to the ground.
		// TODO: use map deferred command
		while (m_map->isValidPosition(pos.InstanceId, testPos))
		{
			testPos.z -= TLoadingSector::VoxelSize;
		}

		// And go back in the air by the amount of density of the voxel.
		// TODO: use map deferred command
		Voxel voxel = m_map->getVoxelAt(pos.InstanceId, testPos);
		testPos.z += (TLoadingSector::VoxelSize * voxel.getDensity() / Voxel::MaxDensity);
	}

	if (testPos.z > 0.f)
		testPos.z = floor(testPos.z * TLoadingSector::InvVoxelSize) * TLoadingSector::VoxelSize;
	else
		testPos.z = ceil(testPos.z * TLoadingSector::InvVoxelSize) * TLoadingSector::VoxelSize;

	return testPos.z;
#if 0 // temp, for reference
	// for wrong positions, leave it unchanged
	if ((pos.InstanceId==-1)||(pos.LocalPosition.Surface==-1))
		return pos.LocalPosition.Estimation.z;

	// get instance/localretriever.
	const CRetrieverInstance	&instance = getInstance(pos.InstanceId);
	const CLocalRetriever		&retriever= _RetrieverBank->getRetriever(instance.getRetrieverId());

	if (!retriever.isLoaded())
		return pos.LocalPosition.Estimation.z;

	// return height from local retriever
	return retriever.getHeight(pos.LocalPosition);
#endif
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

	// compute end with real delta position.
	UGlobalPosition endPos(startPos);
	endPos.LocalPosition.Estimation = start + delta * t;

	float startHeight = getMeanHeight(startPos);
	float endHeight = getMeanHeight(endPos);

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

const TCollisionSurfaceDescVector* CDynGlobalRetriever::testCylinderMove(const UGlobalPosition &startPos, const CVector &delta, float radius, CCollisionSurfaceTemp &cst) const
{
	// result.
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