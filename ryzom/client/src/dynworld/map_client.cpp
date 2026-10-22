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

#include "map_client.h"
#include "nel/3d/u_camera.h"
#include "nel/3d/u_driver.h"
#include "game_share/dynworld/slave_pager.h"
#include "game_share/dynworld/cast_collector.h"
#include "game_share/dynworld/marching_controller.h"
#include "game_share/dynworld/region_mesh_interface.h"
#include "client_cfg.h"

#include <PolyVox/MarchingCubesSurfaceExtractor.h>
#include <PolyVox/CubicSurfaceExtractor.h>
#include <PolyVox/VolumeResampler.h>
#include <PolyVox/RawVolume.h>

using namespace NL3D;
using namespace NLMISC;

extern class CView View;
extern UCamera MainCam;
extern UDriver *Driver;
extern CClientConfig ClientCfg;

CMapClient::CMapClient()
	: m_voxelSelected(false)
	, m_voxelSelectionActive(false)
{
	m_pager = new CSlavePager(*this);
	initMap(m_pager, ClientCfg.ChunksMemoryCache * 1024U * 1024U);
}

CMapClient::~CMapClient()
{
	delete m_pager;
	m_pager = NULL;
}

void CMapClient::selectSectors(const NLMISC::CAABBox &bbox, TSectorsList& sectorsOut, uint lod) const
{
	switch (lod)
	{
	case 0:
		CMapBase::selectSectorsNoOrder<TMeshSectorLod<0>>(bbox, sectorsOut);
		break;
	case 1:
		CMapBase::selectSectorsNoOrder<TMeshSectorLod<1>>(bbox, sectorsOut);
		break;
	case 2:
		CMapBase::selectSectorsNoOrder<TMeshSectorLod<2>>(bbox, sectorsOut);
		break;
	default:
		nlwarning("LOD not supported");
		break;
	}
}

void CMapClient::selectSectors(const NLMISC::CVector& center, uint radius, TSectorsList& sectorsOut, uint lod, uint startRadius /*= 0*/) const
{
	switch (lod)
	{
	case 0:
		CMapBase::selectSectors<TMeshSectorLod<0>>(center, radius, sectorsOut, startRadius);
		break;
	case 1:
		CMapBase::selectSectors<TMeshSectorLod<1>>(center, radius, sectorsOut, startRadius);
		break;
	case 2:
		CMapBase::selectSectors<TMeshSectorLod<2>>(center, radius, sectorsOut, startRadius);
		break;
	default:
		nlwarning("LOD not supported");
		break;
	}
}

class VolumeResampler
{
public:
	VolumeResampler(TVolume* pVolSrc, TSampleVolume* pVolDst, const TRegion& regSrc);
	void execute(sint factor);

private:
	//Source data
	TVolume* m_pVolSrc;

	//Destination data
	TSampleVolume* m_pVolDst;

	TRegion m_region;
};

VolumeResampler::VolumeResampler(TVolume* pVolSrc, TSampleVolume* pVolDst, const TRegion& region)
	: m_pVolSrc(pVolSrc), m_pVolDst(pVolDst), m_region(region)
{

}

bool g_interpolateDensity = true;
void VolumeResampler::execute(sint factor)
{
	sint srcLowerX = m_region.getLowerX() * factor;
	sint srcLowerY = m_region.getLowerY() * factor;
	sint srcLowerZ = m_region.getLowerZ() * factor;

	TVoxelIterator volSrcSampler(m_pVolSrc);
	for (sint sz = 0, dz = m_region.getLowerZ(); dz <= m_region.getUpperZ(); sz++, dz++)
	{
		for (sint sy = 0, dy = m_region.getLowerY(); dy <= m_region.getUpperY(); sy++, dy++)
		{
			for (sint sx = 0, dx = m_region.getLowerX(); dx <= m_region.getUpperX(); sx++, dx++)
			{
				sint ssz = static_cast<sint>(srcLowerZ + sz * factor);
				sint ssx = static_cast<sint>(srcLowerX + sx * factor);
				sint ssy = static_cast<sint>(srcLowerY + sy * factor);

				volSrcSampler.setPosition(ssx, ssy, ssz);

				Voxel tSrcVoxel = volSrcSampler.getVoxel();

				if (g_interpolateDensity && factor != 1)
				{
					bool airVoxel = true;
					sint maxZSolid = -1;
					uint32 tInterpolatedValue = 0;
					uint32 kernelSize = 0;
					for (sint i = 0; i < factor; ++i)
					{
						for (sint j = 0; j < factor; ++j)
						{
							for (sint k = 0; k < factor; ++k)
							{
								Voxel curVoxel = volSrcSampler.getVoxel();
								tInterpolatedValue += curVoxel.getDensity();

								// This is to take this highest solid voxel to represent the downsampled voxel.
								// This should be a good enough approximation to have the ground surface look like high rez ground
								if (curVoxel.getType() > Voxel::LastNoDensity && k > maxZSolid)
								{
									airVoxel = false;
									tSrcVoxel = curVoxel;
									maxZSolid = k;
								}

								++kernelSize;

								// go forward
								volSrcSampler.movePositiveZ();
							}

							// go backward
							for (sint k = 0; k < factor; ++k)
							{
								volSrcSampler.moveNegativeZ();
							}

							// go forward
							volSrcSampler.movePositiveY();
						}

						// go backward
						for (sint j = 0; j < factor; ++j)
						{
							volSrcSampler.moveNegativeY();
						}

						// go forward
						volSrcSampler.movePositiveX();
					}
					tInterpolatedValue /= kernelSize;

					if (!airVoxel)
						tSrcVoxel.setDensity(Voxel::DensityType(tInterpolatedValue));
				}

				m_pVolDst->setVoxel(dx, dy, dz, tSrcVoxel);
			}
		}
	}
}

bool CMapClient::prepareExtraction(const TRegion& regionToEmerge)
{
	H_AUTO(MapClient_PrepareExtraction);

	TSectorsList loadingSectors;
	CAABBox worldBox;
	worldBox.setCenter(ToNeLVectorI(regionToEmerge.getCentre()) * TLoadingSector::VoxelSize);
	CVectorI regionSize = ToNeLVectorI(regionToEmerge.getDimensionsInVoxels());

	worldBox.setHalfSize(CVector(float(regionSize.x + 1) * TLoadingSector::VoxelSize * 0.5f,
		float(regionSize.y + 1) * TLoadingSector::VoxelSize * 0.5f,
		float(regionSize.z + 1) * TLoadingSector::VoxelSize * 0.5f));
	selectSectorsNoOrder<TLoadingSector>(worldBox, loadingSectors);

	// first, need to prefetch everything that will be needed
	bool willHaveVertices = false;
	uint combinedStates = 0;
	for (TLoadingSector::TId sectorId : loadingSectors)
	{
		SectorMetadata* infos = getSectorInfo(sectorId);
		if (infos == nullptr)
		{
			// this sector has never been loaded, load it once to retrieve its metadata
			emergeSector(sectorId);
			infos = getSectorInfo(sectorId);
		}

		nlassert(infos != nullptr);

		combinedStates |= (1 << infos->m_contentState);
		
		if (infos->m_contentState & SectorMetadata::IsHeterogeneous)
		{
			willHaveVertices = true;
		}
	}

	// If there is a mix of full and empty sectors, there will be vertices between them
	if (combinedStates & (SectorMetadata::IsEmpty | SectorMetadata::IsFull) == (SectorMetadata::IsEmpty | SectorMetadata::IsFull))
	{
		willHaveVertices = true;
	}

	if (willHaveVertices)
	{
		// now, do the real prefetch
		for (TLoadingSector::TId sectorId : loadingSectors)
		{
			emergeSector(sectorId);
		}
	}

	return willHaveVertices;
}

bool g_checkIsLoaded = false;
bool g_useCubicExtractor = false;
void CMapClient::extractMesh(TGenericSectorId sectorId, IRegionMesh& outputMesh, MarchingCubesController& controller, uint sectorLod, uint meshLod)
{
	TRegion regionToEmerge;
	CSectorLod::getRegion(sectorLod, sectorId, regionToEmerge);

	// prefetch and return if this will generate no vertices
	if (!prepareExtraction(regionToEmerge))
		return;

	H_AUTO(MapClient_ExtractMesh);

	// Lod region
	uint factor = pow(2, meshLod);

	PolyVox::Vector3DInt32 lowerCorner = regionToEmerge.getLowerCorner();
	lowerCorner /= factor;

	PolyVox::Vector3DInt32 upperCorner = regionToEmerge.getUpperCorner();
	upperCorner /= factor;
	if (meshLod > 0)
	{
		if (upperCorner.getX() <= 0)
			upperCorner.setX(upperCorner.getX() - 1);
		if (upperCorner.getY() <= 0)
			upperCorner.setY(upperCorner.getY() - 1);
		if (upperCorner.getZ() <= 0)
			upperCorner.setZ(upperCorner.getZ() - 1);
	}

	// Expand the region for the Sample Volume
	upperCorner.setX(upperCorner.getX() + 2);
	upperCorner.setY(upperCorner.getY() + 2);
	upperCorner.setZ(upperCorner.getZ() + 2);

	lowerCorner.setX(lowerCorner.getX() - 1);
	lowerCorner.setY(lowerCorner.getY() - 1);
	lowerCorner.setZ(lowerCorner.getZ() - 1);

	TRegion lodRegion(lowerCorner, upperCorner);
	TSampleVolume lodVolume(lodRegion);
	if (factor > 1)
	{
		if (!g_useCubicExtractor)
		{
			H_AUTO(MapClient_Downsampling);
			//CAutoMutex<CMutex> lock(m_volumeLock); // remove once safe
			TVolume::AutoReadLockRegion(m_volume, lodRegion);
			VolumeResampler volumeResampler(m_volume, &lodVolume, lodRegion);
			volumeResampler.execute(factor);
		}
	}

	//Extract the surface
	TDecodedMesh decodedMesh;
// 	if (g_useCubicExtractor)
// 	{
// 		auto mesh = PolyVox::extractCubicMesh(m_volume, regionToEmerge);
// 		decodedMesh = PolyVox::decodeMesh(mesh);
// 	}
// 	else
	{
		// Reduce the size back for the region to extract
		lodRegion.setUpperX(lodRegion.getUpperX() - 1);
		lodRegion.setUpperY(lodRegion.getUpperY() - 1);
		lodRegion.setUpperZ(lodRegion.getUpperZ() - 1);

		lodRegion.setLowerX(lodRegion.getLowerX() + 1);
		lodRegion.setLowerY(lodRegion.getLowerY() + 1);
		lodRegion.setLowerZ(lodRegion.getLowerZ() + 1);

		if (factor > 1)
		{
			TEncodedMesh mesh = PolyVox::extractMarchingCubesMesh(&lodVolume, lodRegion, controller);
			decodedMesh = PolyVox::decodeMesh(mesh);
		}
		else
		{
			//CAutoMutex<CMutex> lock(m_volumeLock); // remove once safe
			TEncodedMesh mesh;
			{
				TVolume::AutoReadLockRegion(m_volume, lodRegion);
				mesh = PolyVox::extractMarchingCubesMesh(m_volume, lodRegion, controller);
			}
			
			decodedMesh = PolyVox::decodeMesh(mesh);
		}
	}

	outputMesh.initMesh(decodedMesh);
}

void CMapClient::regionPagedIn(const TRegion& region, TVolume::Chunk* pChunk, const SectorMetadata& infos)
{
	CMapBase::regionPagedIn(region, pChunk, infos);
}

void CMapClient::regionPagedOut(const TRegion& region, TVolume::Chunk* pChunk, SectorMetadata& outInfos)
{
	CMapBase::regionPagedOut(region, pChunk, outInfos);
}

void CMapClient::selectVoxelUnderPos(float cursX, float cursY)
{
	// build the ray
	CMatrix camMatrix = MainCam.getMatrix();
	CFrustum camFrust = MainCam.getFrustum();
	CViewport viewport = Driver->getViewport();

	// Get the Ray made by the mouse.
	CVector pos, dir;
	viewport.getRayWithPoint(cursX, cursY, pos, dir, camMatrix, camFrust);
	// Normalize the direction.
	dir.normalize();

	CVector endPoint = pos + dir * ClientCfg.SelectionDist;

	CCastCollectorSingleHit hitCastCollector;
	CCastCollectorSingleHit lastEmptyCastCollector;
	castRay(pos, endPoint, &hitCastCollector, &lastEmptyCastCollector);

	if (hitCastCollector.getNumHits() != 0)
	{
		m_voxelUnderCursor = hitCastCollector.getVoxelPos(0);
		m_voxelSelected = true;
	}
	else
	{
		m_voxelSelected = false;
	}

	if (lastEmptyCastCollector.getNumHits() != 0)
	{
		m_emptyVoxelUnderCursor = lastEmptyCastCollector.getVoxelPos(0);
		m_emptyVoxelSelected = true;
	}
	else
	{
		m_emptyVoxelSelected = false;
	}
}

void CMapClient::unselectVoxel()
{
	m_voxelSelected = false;
}
