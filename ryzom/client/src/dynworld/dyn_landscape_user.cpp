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

#include "stdpch.h"

#include "user_entity.h"
#include "dynworld/dyn_landscape_user.h"
#include "dynworld/dyn_landscape_model.h"
#include "dynworld/dyn_landscape.h"
#include "dynworld/dyn_continent.h"
#include "dynworld/region_mesh.h"
#include "dynworld/map_client.h"
#include "game_share/dynworld/marching_controller.h"
#include "game_share/dynworld/material_library.h"
#include "game_share/dynworld/map_deferred_command_manager.h"

#include "nel/misc/common.h"
#include "nel/misc/file.h"
#include "nel/misc/path.h"
#include "nel/misc/hierarchical_timer.h"
#include "nel/misc/progress_callback.h"

#include "nel/3d/scene_user.h"

using namespace NLMISC;
using namespace NL3D;

H_AUTO_DECL( NL3D_UI_Landscape )
H_AUTO_DECL( NL3D_Render_Landscape_updateLightingAll )
H_AUTO_DECL( NL3D_Load_Landscape )

#define	NL3D_HAUTO_UI_LANDSCAPE						H_AUTO_USE( NL3D_UI_Landscape )
#define	NL3D_HAUTO_LANDSCAPE_UPDATE_LIGHTING_ALL	H_AUTO_USE( NL3D_Render_Landscape_updateLightingAll )
#define	NL3D_HAUTO_LOAD_LANDSCAPE					H_AUTO_USE( NL3D_Load_Landscape )

extern UCamera MainCam;

uint g_baselod = 0;
uint g_maxlod = CDynLandscapeUser::LODCOUNT - 1;
float g_farTransition[CDynLandscapeUser::LODCOUNT] = { 50.f, 150.f, 350.f }; // in meters
float g_igFar = 300.f;
MarchingCubesController::DensityType g_densityThreshold = (Voxel::MaxDensity / 2.f);
float g_defaultTransitionSpeed = 1.f;
float g_updateTransitionSpeed = .1f;
bool g_forUsePlayerPosPriority = false;

float g_camPosOffset = 16.f;
float getPriorityToPlayer(const CVectorI& sectorPosI)
{
	if (MainCam.getObjectPtr() == nullptr)
		return 0.f;

	CVector camFront;
	CVector camPos(MainCam.getPos());
	if (camPos == CVector::Null || g_forUsePlayerPosPriority) // not yet initialized, use player info
	{
		camPos = UserEntity->pos();
		camFront = UserEntity->front();
	}
	else
	{
		// Get rotation matrix
		CMatrix camMatrix;
		camMatrix.identity();
		if (MainCam.getTransformMode() == NL3D::ITransformable::RotEuler)
			camMatrix.rotate(MainCam.getRotEuler(), MainCam.getRotOrder());
		else
			camMatrix.rotate(MainCam.getRotQuat());

		camFront = camMatrix.getJ();
	}

	// Offset a bit the camera position to include sectors behind but still visible
	camPos -= (camFront * g_camPosOffset);

	CVector camToSector(CVector(sectorPosI) - camPos);
	float sectorDist = camToSector.norm();
	float frontFactor = (camFront * (camToSector / sectorDist)) * -1.f;
	float distFactor = 1.f - std::min(sectorDist / g_farTransition[g_maxlod], 1.f);

	return frontFactor * distFactor;
}

class CGenerateMeshCommand : public IMapDeferredCommand
{
public:
	CGenerateMeshCommand(CMapClient* map, TGenericSectorId sectorId, uint lod, const IMapDeferredCommand* parent = nullptr)
		: IMapDeferredCommand(parent)
		, m_map(map)
		, m_sectorId(sectorId)
		, m_lod(lod)
		, m_outMesh(nullptr)
		, m_prefetchDone(false)
	{
		TRegion regionToEmerge;
		CSectorLod::getRegion(m_lod, m_sectorId, regionToEmerge);

		CAABBox worldBox;
		worldBox.setCenter(ToNeLVectorI(regionToEmerge.getCentre()) * TLoadingSector::VoxelSize);
		CVectorI regionSize = ToNeLVectorI(regionToEmerge.getDimensionsInVoxels());

		worldBox.setHalfSize(CVector(float(regionSize.x + 1) * TLoadingSector::VoxelSize * 0.5f,
			float(regionSize.y + 1) * TLoadingSector::VoxelSize * 0.5f,
			float(regionSize.z + 1) * TLoadingSector::VoxelSize * 0.5f));
		m_map->selectSectorsNoOrder<TLoadingSector>(worldBox, m_loadingSectors);
	}

	virtual bool execute() override
	{
		//NLMISC-PIX-STRIPPED NL_PROF("CGenerateMeshCommand_execute");
		if (m_prefetchDone)
		{
			generateMesh();
			return true;
		}
		else
		{
			pushPrefetchSectorsCommands();
			return false;
		}
	}

	CMapClient* m_map;
	TGenericSectorId m_sectorId;
	uint m_lod;

	CRegionMesh* m_outMesh;

private:
	virtual void getName(std::string& name) const override
	{
		name = "Generate Mesh";
	}

	virtual float getPriority() const override
	{
		CVectorI sectorPosI;
		CSectorLod::idToWorldPos(m_lod, m_sectorId, sectorPosI);
		return getPriorityToPlayer(sectorPosI);
	}

	bool validatePrefetch()
	{

	}

	void generateMesh()
	{
		//NLMISC-PIX-STRIPPED NL_PROF_F("GenerateMesh%d(%p)", m_lod, m_sectorId);
		MarchingCubesController controller(g_densityThreshold);
		CRegionMesh* regMesh = new CRegionMesh;
		m_map->extractMesh(m_sectorId, *regMesh, controller, m_lod, m_lod);
		m_outMesh = regMesh;
	}

	void pushPrefetchSectorsCommands()
	{
		//NLMISC-PIX-STRIPPED NL_PROF_F("PrefetchSectors %d(%p)", m_loadingSectors.size(), m_sectorId);
		m_prefetchDone = true;

		// first, need to prefetch everything that will be needed
		for (TLoadingSector::TId sectorId : m_loadingSectors)
		{
			SectorStatus status = m_map->isSectorLoaded(sectorId);
			switch (status)
			{
			case SectorStatus::NotAvailable:
			{
				CPrefetchSectorCommand* command = new CPrefetchSectorCommand(m_map, sectorId, this);
				TMapDeferredCommandHandle handle = CMapDeferredCommandManager::instance().addCommand(command);
				m_prefetchDone = false;
			}
			break;

			case SectorStatus::Locked:
				m_prefetchDone = false;
				break;
			}
		}
	}

	TSectorsList m_loadingSectors;
	bool m_prefetchDone;
};

// ***************************************************************************
CDynLandscapeUser::CDynLandscapeUser(NL3D::UScene *scene)
	: NL3D::CLandscapeUser(&((CSceneUser*)scene)->getScene(), DynLandscapeModelId)
	, m_map(nullptr)
{
	m_igManager.init(scene);
}

// ***************************************************************************
CDynLandscapeUser::~CDynLandscapeUser()
{
	m_map->unregisterVoxelListener(this);
}


// ****************************************************************************
void	CDynLandscapeUser::setZonePath(const std::string &zonePath)
{
	NL3D_HAUTO_UI_LANDSCAPE;
	_ZoneManager.setZonePath(zonePath);
}

// ****************************************************************************
void CDynLandscapeUser::invalidateAllTiles()
{
	_Landscape->getLandscape().invalidateAllTiles();
}

// ****************************************************************************
void	CDynLandscapeUser::loadBankFiles(const std::string &tileBankFile, const std::string &farBankFile)
{
	NL3D_HAUTO_LOAD_LANDSCAPE;

	CLandscape& landscape = _Landscape->getLandscape();

	// Release all OLD tiles
	landscape.releaseAllTiles();

	// Clear the bank
	landscape.TileBank.clear();

	// First, load the banks.
	//=======================
	CIFile bankFile(CPath::lookup(tileBankFile));
	landscape.TileBank.serial(bankFile);
	// All textures path are relative!
	landscape.TileBank.makeAllPathRelative();
	// Use DDS!!!
	landscape.TileBank.makeAllExtensionDDS();
	// No absolute path
	landscape.TileBank.setAbsPath("");

	CIFile farbankFile(CPath::lookup(farBankFile));
	landscape.TileFarBank.serial(farbankFile);
	bankFile.close();
	farbankFile.close();
}

// ****************************************************************************

void	CDynLandscapeUser::flushTiles (NLMISC::IProgressCallback &progress)
{
	// After loading the TileBank, and before initTileBanks(), must load the vegetables descritpor
	_Landscape->getLandscape().TileBank.loadTileVegetableDescs();

	// init the TileBanks descriptors
	if (!_Landscape->getLandscape().initTileBanks())
	{
		nlwarning( "You need to recompute bank.farbank for the far textures" );
	}

	CMaterialLibrary::getInstance().bindMaterials(_Landscape->getLandscape().TileBank);
}

// ****************************************************************************

void	CDynLandscapeUser::loadAllZonesAround(const CVector &pos, float radius, std::vector<std::string> &zonesAdded)
{
	NL3D_HAUTO_LOAD_LANDSCAPE;

	{
		refreshLodSectors(pos, radius);
	}

	zonesAdded.clear();
}

// ****************************************************************************
void	CDynLandscapeUser::refreshAllZonesAround(const CVector &pos, float radius, std::vector<std::string> &zonesAdded, std::vector<std::string> &zonesRemoved,
											  NLMISC::IProgressCallback &progress, const std::vector<uint16> *validZoneIds)
{
	NL3D_HAUTO_LOAD_LANDSCAPE;

	{
		if (m_map == nullptr)
		{
			m_map = CDynContinent::MAP;
			if (m_map != nullptr)
			{
				m_map->registerVoxelListener(this);
			}
		}

		refreshLodSectors(pos, radius);
	}

	zonesAdded.clear();
	zonesRemoved.clear();
}

// ***************************************************************************
void	CDynLandscapeUser::getAllZoneLoaded(std::vector<std::string>	&zoneLoaded) const
{
	// Build the list of zoneId.
	std::vector<uint16>	zoneIds;
	_Landscape->getLandscape().getZoneList(zoneIds);

	// transcript
	zoneLoaded.clear();
	zoneLoaded.resize(zoneIds.size());
	for(uint i=0;i<zoneLoaded.size();i++)
	{
		CLandscape::buildZoneName(zoneIds[i], zoneLoaded[i]);
	}
}

// ****************************************************************************
void	CDynLandscapeUser::loadAllZonesAround(const CVector &pos, float radius)
{
	std::vector<std::string>	dummy;
	loadAllZonesAround(pos, radius, dummy);
}


// ****************************************************************************
void	CDynLandscapeUser::refreshZonesAround(const CVector &pos, float radius)
{
	NL3D_HAUTO_LOAD_LANDSCAPE;

	std::string	dummy1, dummy2;
	refreshZonesAround(pos, radius, dummy1, dummy2);
}

// ****************************************************************************
bool g_forceRegenerateMesh = false;
bool g_reloadEnvConfig = false;
uint CDynLandscapeUser::generateMeshes(const TSectorsList& sectorsToGenerate, uint lod, uint maxExtract /*=0xFFFFFFFF*/)
{
	CDynLandscape& landscape = static_cast<CDynLandscape&>(_Landscape->getLandscape());

	if (g_reloadEnvConfig)
	{
		CMaterialLibrary::getInstance().initialize("material_library.cfg");
		CMaterialLibrary::getInstance().bindMaterials(_Landscape->getLandscape().TileBank);
		g_reloadEnvConfig = false;
	}

	if (g_forceRegenerateMesh)
	{
		landscape.clearGeneratedMeshes();
		for (uint i = 0; i < LOD_COUNT; ++i)
			m_generatedRegions[i].clear();
		g_forceRegenerateMesh = false;
	}

	uint sectorsCount = sectorsToGenerate.size();
	uint sectorsGenerated = 0;

	for (uint i = 0; i < sectorsCount; ++i)
	{
		TGenericSectorId sectorId = sectorsToGenerate[i];

		// already generated, should not happen (or need to figure in which situation it does)
		nlassert(landscape.m_regionsList[lod].end() == landscape.m_regionsList[lod].find(sectorId));
		if (landscape.m_regionsList[lod].end() != landscape.m_regionsList[lod].find(sectorId))
			continue;

		nlassert(m_generatedRegions[lod].end() == m_generatedRegions[lod].find(sectorId)); // both containers much match content

		IRegionMesh* regMesh = generateMesh(sectorId, lod);

		// Check if the new mesh in part of a transition
		auto inTransitionIt = m_pendingAddRegions[lod].find(sectorId);
		if (inTransitionIt != m_pendingAddRegions[lod].end())
		{
			inTransitionIt->second->addNextMesh(sectorId, regMesh, lod);
		}
		
		m_generatedRegions[lod].insert(sectorId);
		landscape.m_regionsList[lod][sectorId] = regMesh;
		
		++sectorsGenerated;
		if (sectorsGenerated == maxExtract)
			break;
	}

	return sectorsGenerated;
}

IRegionMesh* CDynLandscapeUser::generateMesh(TGenericSectorId sectorId, uint lod)
{
	MarchingCubesController controller(g_densityThreshold);
	CRegionMesh* regMesh = new CRegionMesh;
	m_map->extractMesh(sectorId, *regMesh, controller, lod, lod);

	bindRegionMeshTexture(regMesh);

	return regMesh;
}

void CDynLandscapeUser::bindRegionMeshTexture(CRegionMesh* mesh)
{
	for (CRegionSubMesh* submesh : mesh->m_submeshes)
	{
		if (submesh != nullptr)
		{
			const CMaterialDefinition* matDef = CMaterialLibrary::instance().getMaterialDef(submesh->m_materialType);
			const CMaterialTextureSet* textSet = matDef->getTextureSet(submesh->m_materialBiome);
			CTileBank& tileBank = _Landscape->getLandscape().TileBank;
			if (!textSet->m_textures.empty())
			{
				const CMaterialTextureDef& textDef = textSet->m_textures[0];
				if (!textDef.m_variations.empty())
				{
					uint16 tileID = textDef.m_variations[0];
					CTile* tile = tileBank.getTile(tileID);
					const std::string& textName = tile->getRelativeFileName(CTile::diffuse);
					submesh->DiffuseTop = _Landscape->getLandscape().findTileTexture(tileBank.getAbsPath() + textName, false);
				}
			}

			if (!textSet->m_texturesSides.empty())
			{
				const CMaterialTextureDef& textDef = textSet->m_texturesSides[0];
				if (!textDef.m_variations.empty())
				{
					uint16 tileID = textDef.m_variations[0];
					CTile* tile = tileBank.getTile(tileID);
					const std::string& textName = tile->getRelativeFileName(CTile::diffuse);
					submesh->DiffuseSide = _Landscape->getLandscape().findTileTexture(tileBank.getAbsPath() + textName, false);
				}
			}
		}
	}
}

// ****************************************************************************
void CDynLandscapeUser::removeMeshes(const TSectorsList& sectorsToRemove, uint lod)
{
	for (uint i = 0; i < sectorsToRemove.size(); ++i)
	{
		TGenericSectorId sectorId = sectorsToRemove[i];

		if (!isInTransition(sectorId, lod))
		{
			// If sector is not in a transition, remove it
			removeMesh(sectorId, lod);
		}
	}
}

// ****************************************************************************
void CDynLandscapeUser::removeMesh(TGenericSectorId sectorId, uint lod)
{
	CDynLandscape& landscape = static_cast<CDynLandscape&>(_Landscape->getLandscape());
	auto itMesh = landscape.m_regionsList[lod].find(sectorId);

	auto itSector = m_generatedRegions[lod].find(sectorId);
	if (itSector != m_generatedRegions[lod].end())
	{
		m_generatedRegions[lod].erase(itSector);
		nlassert(itMesh != landscape.m_regionsList[lod].end()); // must match content
	}
	else
		nlassert(itMesh == landscape.m_regionsList[lod].end()); // must match content
	
	if (itMesh != landscape.m_regionsList[lod].end())
	{
		landscape.m_regionsList[lod].erase(itMesh);
	}
}

// ****************************************************************************
bool CDynLandscapeUser::updateMesh(TGenericSectorId sectorId, uint lod)
{
	CDynLandscape& landscape = static_cast<CDynLandscape&>(_Landscape->getLandscape());
	auto regionMeshIt = landscape.m_regionsList[lod].find(sectorId);
	if (landscape.m_regionsList[lod].end() != regionMeshIt)
	{
		bool transitionInProgress = isInTransition(sectorId, lod);

		CSectorTransition* newTransition = nullptr;
		if (transitionInProgress)
		{
			return false; // wait until previous transition is completed
		}
		else
		{
			// If none exists, create a new one
			newTransition = new CSectorTransition();
			newTransition->m_transitionSpeed = g_updateTransitionSpeed;
			landscape.m_transitionsList.push_back(newTransition);
		}

		if (newTransition != nullptr)
		{
			{
				newTransition->addPreviousMesh(sectorId, regionMeshIt->second, lod);
				TTransitionRegionsContainer& pendingRemoveRegionsLod = m_pendingRemoveRegions[lod];
				auto transIt = pendingRemoveRegionsLod.find(sectorId);
				if (transIt != pendingRemoveRegionsLod.end())
				{ // debug helper, to break in this condition
					pendingRemoveRegionsLod[sectorId] = newTransition;
				}
				else
				{
					pendingRemoveRegionsLod[sectorId] = newTransition;
				}
			}
			//nlassert(m_pendingRemoveRegions[regionMeshIt->first] == nullptr); // just to remember there is an issue here

			// Register the transition
			{
				newTransition->m_nextLod = lod;
				TTransitionRegionsContainer& pendingAddRegionsLod = m_pendingAddRegions[lod];
				auto transIt = pendingAddRegionsLod.find(sectorId);
				if (transIt != pendingAddRegionsLod.end())
				{ // debug helper, to break in this condition
					pendingAddRegionsLod[sectorId] = newTransition;
				}
				else
				{
					pendingAddRegionsLod[sectorId] = newTransition;
				}
			}

			// Post the deferred command for generation
			CGenerateMeshCommand* command = new CGenerateMeshCommand(m_map, sectorId, lod);
			TMapDeferredCommandHandle handle = CMapDeferredCommandManager::instance().addCommand(command);
			m_deferredCommands.push_back(handle);
			m_deferredGenerations[lod].insert(sectorId);
		}
	}

	return true;
}

// ****************************************************************************
float g_OffsetFactor = 8.f;
void CDynLandscapeUser::refreshLodSectors(const CVector& pos, float radius, uint maxExtract /*=0xFFFFFFFF*/)
{
	// Check transient containers are clean
	for (auto& val : m_sectorsToGenerate) { nlassert(val.empty()); }
	for (auto& val : m_sectorsToRemove) { nlassert(val.empty()); }
	nlassert(m_additionalSectors.empty());

	CVector front(MainCam.getMatrix().getJ());

	CVector offsetedPosition = pos + g_OffsetFactor * front;

	// Choose the sectors that are needed
	// Larger LOD, full size
	CAABBox box;
	box.setCenter(offsetedPosition);
	box.setHalfSize(CVector(g_farTransition[g_maxlod], g_farTransition[g_maxlod], g_farTransition[g_maxlod]));
	m_map->selectSectors(box, m_sectorsToGenerate[g_maxlod], g_maxlod);
	CDynLandscape& landscape = static_cast<CDynLandscape&>(_Landscape->getLandscape());

	// Lower LODs
	for (sint lod = sint(g_maxlod) - 1; lod >= sint(g_baselod); --lod)
	{
		// Select the sectors for this LOD
		float factor = std::pow(2, float(lod));
		TSectorsList& currentLodList = m_sectorsToGenerate[lod];
		float transitionDistance = g_farTransition[lod];
		box.setHalfSize(CVector(transitionDistance, transitionDistance, transitionDistance / factor));
		m_map->selectSectors(box, currentLodList, lod);

		size_t sectorsCount = currentLodList.size(); // local size to avoid iterating on additional sectors
		for (uint i = 0; i < sectorsCount; ++i)
		{
			TGenericSectorId id = currentLodList[i];
			TGenericSectorId parentId = CSectorLod::getParentSector(lod, id);
			TSectorsList& parentList = m_sectorsToGenerate[lod + 1];

			// Remove from the upper LOD the sectors with lower LOD
			auto largerLod = std::find(parentList.begin(), parentList.end(), parentId);
			if (largerLod != parentList.end())
			{
				CSectorLod::getSubSectors(lod + 1, *largerLod, m_additionalSectors);

				// And eventually, from this removed sectors, add the missing ones in the current LOD
				for (uint j = 0; j < m_additionalSectors.size(); ++j)
				{
					TGenericSectorId additionalId = m_additionalSectors[j];
					if (std::find(currentLodList.begin(), currentLodList.end(), additionalId) == currentLodList.end())
					{
						currentLodList.push_back(additionalId);
					}
				}

				parentList.erase(largerLod);
				m_additionalSectors.clear();
			}
		}
	}

	// Ask to load transition meshes in priority to end them as quickly as possible
	for (uint lod = 0; lod < LODCOUNT; ++lod)
	{
		for (auto pendingAddIt : m_pendingAddRegions[lod])
		{
			CSectorTransition* transition = pendingAddIt.second;
			TSectorsList& sectorList = m_sectorsToGenerate[transition->m_nextLod];
			if (std::find(sectorList.begin(), sectorList.end(), pendingAddIt.first) == sectorList.end())
			{
				sectorList.push_back(pendingAddIt.first);
				std::swap(sectorList.front(), sectorList.back());
			}
		}
	}

	// Sort out which sector needs to be added and which one needs to be removed
	nlassert(m_added.empty());
	for (uint lod = 0; lod < LODCOUNT; ++lod)
	{
		m_map->diffSectors(m_generatedRegions[lod], m_sectorsToGenerate[lod], m_added, m_sectorsToRemove[lod], lod);
		m_sectorsToGenerate[lod] = m_added;
		m_added.clear();
	}

	// Some debug stuff
	if (g_reloadEnvConfig)
	{
		CMaterialLibrary::getInstance().initialize("material_library.cfg");
		CMaterialLibrary::getInstance().bindMaterials(_Landscape->getLandscape().TileBank);
		g_reloadEnvConfig = false;
	}

	if (g_forceRegenerateMesh)
	{
		landscape.clearGeneratedMeshes();
		for (uint i = 0; i < LOD_COUNT; ++i)
			m_generatedRegions[i].clear();
		g_forceRegenerateMesh = false;
	}

	// Create the transitions according to adds and removes
	buildTransitionMeshes(m_sectorsToGenerate, m_sectorsToRemove, LODCOUNT, maxExtract);

	// Remove all unnecessary sectors from "regular" meshes list
	for (uint lod = 0; lod < LODCOUNT; ++lod)
	{
		removeMeshes(m_sectorsToRemove[lod], lod);
	}

	// Request load all new sectors
	postGenerateMeshesCommands(m_sectorsToGenerate, LODCOUNT);

	// Clean transient containers
	for (auto& val : m_sectorsToGenerate) val.clear();
	for (auto& val : m_sectorsToRemove) val.clear();
	m_additionalSectors.clear();

	// Check for finished generation deferred commands
	updateGenerateMeshesCommands();

	// .
	updateTransitionMeshes();

	updateTouchedMeshes();

	// Update IG manager
	m_igManager.refreshZonesAround(pos, g_igFar);
}

// ****************************************************************************
static bool findTransition(uint lod, TGenericSectorId parentSector, const TSectorsList& candidates, TSectorsList& children)
{
	uint childrenFound = 0;
	CSectorLod::getSubSectors(lod, parentSector, children);
	for (TGenericSectorId childId : children)
	{
		for (TGenericSectorId candidateId : candidates)
		{
			if (childId == candidateId)
			{
				++childrenFound;
			}
		}
	}
	return childrenFound == children.size();
}

static CSectorTransition* findExistingTransition(const TTransitionContainer& transitionsList, TGenericSectorId addedSector, bool& outIsNext)
{
	for (const CSmartPtr<CSectorTransition>& transition : transitionsList)
	{
		const std::vector<TGenericSectorId>& previousContainer = transition->m_previousSectorIds;
		const std::vector<TGenericSectorId>& nextContainer = transition->m_nextSectorIds;

		// First, look inside "previous" sectors
		{
			uint previousSectorsCount = previousContainer.size();
			for (uint i = 0; i < previousSectorsCount; ++i)
			{
				if (previousContainer[i] == addedSector)
				{
					outIsNext = false;
					return transition;
				}
			}
		}

		// Then, check in "next" sectors
		{
			uint nextSectorsCount = nextContainer.size();
			for (uint i = 0; i < nextSectorsCount; ++i)
			{
				if (nextContainer[i] == addedSector)
				{
					outIsNext = true;
					return transition;
				}
			}
		}
	}

	return nullptr;
}

// ****************************************************************************
void CDynLandscapeUser::buildTransitionMeshes(const TSectorsList* sectorsToGenerate, const TSectorsList* sectorsToRemove, uint lodCount, uint maxExtract)
{
	// TODO: the function supports only transitions between 1 level of LOD, while CTransitionRegionMesh supports any LOD difference.
	// Could be worthwhile to support this in here.
	CDynLandscape& landscape = static_cast<CDynLandscape&>(_Landscape->getLandscape());

	nlassert(m_added.empty());
	TSectorsList& childrenSectors = m_added;
	for (uint lod = 1; lod < lodCount; ++lod)
	{
		uint childLod = lod - 1;

		// Transitions from low rez LOD to higher rez LOD
		{
			const TSectorsList& added = sectorsToGenerate[lod];
			const TSectorsList& removed = sectorsToRemove[childLod];
			for (uint i = 0; i < added.size(); ++i)
			{
				TGenericSectorId addedSector = added[i];

				childrenSectors.clear();
				bool transitionOk = findTransition(lod, addedSector, removed, childrenSectors);
				if (transitionOk)
				{
					nlassert(childrenSectors.size() == 8);

					// Find possible existing transition
					bool transitionInProgress = false;
					for (TGenericSectorId childId : childrenSectors)
					{
						if (isInTransition(childId, childLod))
						{
							transitionInProgress = true;
							break;
						}
					}

					if (isInTransition(addedSector, lod))
					{
						transitionInProgress = true;
					}

					CSectorTransition* newTransition = nullptr;
					if (transitionInProgress)
					{
						continue; // wait until previous transition is completed
					}
					else
					{
						// If none exists, create a new one
						newTransition = new CSectorTransition();
						newTransition->m_transitionSpeed = g_defaultTransitionSpeed;
						landscape.m_transitionsList.push_back(newTransition);
					}

					if (newTransition != nullptr)
					{
						// Register the children meshes that need to disappear
						for (TGenericSectorId childId : childrenSectors)
						{
							auto regionMeshIt = landscape.m_regionsList[childLod].find(childId);
							nlassert(landscape.m_regionsList[childLod].end() != regionMeshIt);
							if (landscape.m_regionsList[childLod].end() != regionMeshIt)
							{
								newTransition->addPreviousMesh(childId, regionMeshIt->second, childLod);
								TTransitionRegionsContainer& pendingRemoveRegionsLod = m_pendingRemoveRegions[childLod];
								auto transIt = pendingRemoveRegionsLod.find(childId);
								if (transIt != pendingRemoveRegionsLod.end())
								{ // debug helper, to break in this condition
									pendingRemoveRegionsLod[childId] = newTransition;
								}
								else
								{
									pendingRemoveRegionsLod[childId] = newTransition;
								}
								//nlassert(m_pendingRemoveRegions[regionMeshIt->first] == nullptr); // just to remember there is an issue here
							}
						}

						// Maybe added sector already exist?
						auto regionMeshIt = landscape.m_regionsList[lod].find(addedSector);
						if (landscape.m_regionsList[lod].end() != regionMeshIt)
						{ // debug helper, to break in this condition
							// Should not happen, keep a breakpoint here
							newTransition->addNextMesh(addedSector, regionMeshIt->second, lod);
						}

						// Register the transition
						newTransition->m_nextLod = lod;
						TTransitionRegionsContainer& pendingAddRegionsLod = m_pendingAddRegions[lod];
						auto transIt = pendingAddRegionsLod.find(addedSector);
						if (transIt != pendingAddRegionsLod.end())
						{ // debug helper, to break in this condition
							pendingAddRegionsLod[addedSector] = newTransition;
						}
						else
						{
							pendingAddRegionsLod[addedSector] = newTransition;
						}
					}
				}
			}
		}

		// Transitions from high rez LOD to lower rez LOD
		{
			const TSectorsList& removed = sectorsToRemove[lod];
			const TSectorsList& added = sectorsToGenerate[childLod];
			for (uint i = 0; i < removed.size(); ++i)
			{
				TGenericSectorId removedSector = removed[i];

				childrenSectors.clear();
				bool transitionOk = findTransition(lod, removedSector, added, childrenSectors);
				if (transitionOk)
				{
					nlassert(childrenSectors.size() == 8);

					// Find possible existing transition
					bool transitionInProgress = false;
					if (isInTransition(removedSector, lod))
					{
						transitionInProgress = true;
					}

					for (TGenericSectorId childId : childrenSectors)
					{
						if (isInTransition(childId, childLod))
						{
							transitionInProgress = true;
							break;
						}
					}

					CSectorTransition* newTransition = nullptr;
					if (transitionInProgress)
					{
						continue;
					}
					else
					{
						// If none exists, create a new one
						newTransition = new CSectorTransition();
						newTransition->m_transitionSpeed = g_defaultTransitionSpeed;
						landscape.m_transitionsList.push_back(newTransition);
					}

					if (newTransition != nullptr)
					{
						// Register the mesh that needs to disappear
						auto regionMeshIt = landscape.m_regionsList[lod].find(removedSector);
						nlassert(landscape.m_regionsList[lod].end() != regionMeshIt);
						if (landscape.m_regionsList[lod].end() != regionMeshIt)
						{
							newTransition->addPreviousMesh(removedSector, regionMeshIt->second, lod);
							TTransitionRegionsContainer& pendingRemoveRegionsLod = m_pendingRemoveRegions[lod];
							auto transIt = pendingRemoveRegionsLod.find(removedSector);
							if (transIt != pendingRemoveRegionsLod.end())
							{ // debug helper, to break in this condition
								pendingRemoveRegionsLod[removedSector] = newTransition;
							}
							else
							{
								pendingRemoveRegionsLod[removedSector] = newTransition;
							}
							//nlassert(m_pendingRemoveRegions[regionMeshIt->first] == nullptr); // just to remember there is an issue here
						}

						// Register the transition
						newTransition->m_nextLod = childLod;
						for (TGenericSectorId childSector : childrenSectors)
						{
							// Maybe added sector already exist?
							auto regionMeshIt = landscape.m_regionsList[childLod].find(childSector);
							if (landscape.m_regionsList[childLod].end() != regionMeshIt)
							{ // debug helper, to break in this condition
								// Should not happen, keep a breakpoint here
								newTransition->addNextMesh(childSector, regionMeshIt->second, childLod);
							}

							TTransitionRegionsContainer& pendingAddRegionsLod = m_pendingAddRegions[childLod];
							auto transIt = pendingAddRegionsLod.find(childSector);
							if (transIt != pendingAddRegionsLod.end())
							{ // debug helper, to break in this condition
								pendingAddRegionsLod[childSector] = newTransition;
							}
							else
							{
								pendingAddRegionsLod[childSector] = newTransition;
							}
						}
					}
				}
			}
		}
	}
	m_added.clear();
}

// ****************************************************************************
void CDynLandscapeUser::updateTransitionMeshes()
{
	CDynLandscape& landscape = static_cast<CDynLandscape&>(_Landscape->getLandscape());
	auto transitionIt = landscape.m_transitionsList.begin();
	while (transitionIt != landscape.m_transitionsList.end())
	{
		CSectorTransition* transition = *transitionIt;
		if (transition->isReady())
		{
			if (transition->isComplete())
			{
				extractCompletedTransition(transition);
				transitionIt = landscape.m_transitionsList.erase(transitionIt);
			}
			else
			{
				transition->updateFactors(DT);
				if (transition->m_previousLod == transition->m_nextLod && transition->m_nextFactor >= 1.f)
				{
					for (uint i = 0; i < transition->m_nextMeshes.size(); ++i)
					{
						m_generatedRegions[transition->m_nextLod].insert(transition->m_nextSectorIds[i]);
						landscape.m_regionsList[transition->m_nextLod][transition->m_nextSectorIds[i]] = transition->m_nextMeshes[i];
					}
				}
				++transitionIt;
			}
		}
		else
		{
			++transitionIt;
		}
	}
}

// ****************************************************************************
void CDynLandscapeUser::extractCompletedTransition(CSectorTransition* transition)
{
	CDynLandscape& landscape = static_cast<CDynLandscape&>(_Landscape->getLandscape());

	// Remove "previous" meshes from pending removes
	uint prevMeshesCount = transition->m_previousMeshes.size();
	nlassert(transition->m_previousMeshes.size() == transition->m_previousSectorIds.size()); // sanity check
	for (uint i = 0; i < prevMeshesCount; ++i)
	{
		nlassert(transition->m_previousLod < LODCOUNT);
		TTransitionRegionsContainer& pendingRemoveRegionsLod = m_pendingRemoveRegions[transition->m_previousLod];
		auto pendingMeshIt = pendingRemoveRegionsLod.find(transition->m_previousSectorIds[i]);
		nlassert(pendingMeshIt != pendingRemoveRegionsLod.end());
		if (pendingMeshIt != pendingRemoveRegionsLod.end())
		{
			pendingRemoveRegionsLod.erase(pendingMeshIt);
		}

		if (transition->m_nextLod != transition->m_previousLod)
		{
			removeMesh(transition->m_previousSectorIds[i], transition->m_previousLod);
		}
	}

	// Remove "next" meshes from pending adds
	uint nextMeshesCount = transition->m_nextMeshes.size();
	nlassert(transition->m_nextMeshes.size() == transition->m_nextSectorIds.size()); // sanity check
	for (uint i = 0; i < nextMeshesCount; ++i)
	{
		nlassert(transition->m_nextLod < LODCOUNT);
		TTransitionRegionsContainer& pendingAddRegionsLod = m_pendingAddRegions[transition->m_nextLod];
		auto pendingMeshIt = pendingAddRegionsLod.find(transition->m_nextSectorIds[i]);
		nlassert(pendingMeshIt != pendingAddRegionsLod.end());
		if (pendingMeshIt != pendingAddRegionsLod.end())
		{
			pendingAddRegionsLod.erase(pendingMeshIt);
		}

		// sanity check
		if (transition->m_nextLod != transition->m_previousLod)
		{
			auto meshIt = landscape.m_regionsList[transition->m_nextLod].find(transition->m_nextSectorIds[i]);
			nlassert(meshIt != landscape.m_regionsList[transition->m_nextLod].end());
			nlassert(meshIt->second == transition->m_nextMeshes[i]);
		}
	}
}

// ****************************************************************************
void CDynLandscapeUser::abortTransition(CSectorTransition* transition)
{
	// Remove "previous" meshes from pending removes
	uint prevMeshesCount = transition->m_previousMeshes.size();
	nlassert(transition->m_previousMeshes.size() == transition->m_previousSectorIds.size()); // sanity check
	for (uint i = 0; i < prevMeshesCount; ++i)
	{
		nlassert(transition->m_previousLod < LODCOUNT);
		TTransitionRegionsContainer& pendingRemoveRegionsLod = m_pendingRemoveRegions[transition->m_previousLod];
		auto pendingMeshIt = pendingRemoveRegionsLod.find(transition->m_previousSectorIds[i]);
		nlassert(pendingMeshIt != pendingRemoveRegionsLod.end());
		if (pendingMeshIt != pendingRemoveRegionsLod.end())
		{
			pendingRemoveRegionsLod.erase(pendingMeshIt);
		}
	}

	// Remove "next" meshes from pending adds
	uint nextMeshesCount = transition->m_nextMeshes.size();
	nlassert(transition->m_nextMeshes.size() == transition->m_nextSectorIds.size()); // sanity check
	for (uint i = 0; i < nextMeshesCount; ++i)
	{
		nlassert(transition->m_nextLod < LODCOUNT);
		TTransitionRegionsContainer& pendingAddRegionsLod = m_pendingAddRegions[transition->m_nextLod];
		auto pendingMeshIt = pendingAddRegionsLod.find(transition->m_nextSectorIds[i]);
		nlassert(pendingMeshIt != pendingAddRegionsLod.end());
		if (pendingMeshIt != pendingAddRegionsLod.end())
		{
			pendingAddRegionsLod.erase(pendingMeshIt);
		}
	}

	transition->abort();
}

// ****************************************************************************
bool CDynLandscapeUser::isInTransition(TGenericSectorId sectorId, uint lod) const
{
	const TTransitionRegionsContainer& pendingRemoveRegionsLod = m_pendingRemoveRegions[lod];
	const TTransitionRegionsContainer& pendingAddRegionsLod = m_pendingAddRegions[lod];
	return pendingRemoveRegionsLod.find(sectorId) != pendingRemoveRegionsLod.end()
		|| pendingAddRegionsLod.find(sectorId) != pendingAddRegionsLod.end();
}

// ****************************************************************************
void CDynLandscapeUser::postGenerateMeshesCommands(const TSectorsList* sectorsToGenerate, uint lodCount)
{
	CDynLandscape& landscape = static_cast<CDynLandscape&>(_Landscape->getLandscape());

	for (uint lod = 0; lod < lodCount; ++lod)
	{
		for (TGenericSectorId sectorId : sectorsToGenerate[lod])
		{
			// already generated, should not happen (or need to figure in which situation it does)
			nlassert(landscape.m_regionsList[lod].end() == landscape.m_regionsList[lod].find(sectorId));
			if (landscape.m_regionsList[lod].end() != landscape.m_regionsList[lod].find(sectorId))
				continue;

			nlassert(m_generatedRegions[lod].end() == m_generatedRegions[lod].find(sectorId)); // both containers much match content

			if (m_deferredGenerations[lod].find(sectorId) == m_deferredGenerations[lod].end())
			{
				CGenerateMeshCommand* command = new CGenerateMeshCommand(m_map, sectorId, lod);
				TMapDeferredCommandHandle handle = CMapDeferredCommandManager::instance().addCommand(command);
				m_deferredCommands.push_back(handle);
				m_deferredGenerations[lod].insert(sectorId);
			}
		}
	}
}

// ****************************************************************************
void CDynLandscapeUser::updateGenerateMeshesCommands()
{
	CDynLandscape& landscape = static_cast<CDynLandscape&>(_Landscape->getLandscape());

	for (uint i = 0; i < m_deferredCommands.size(); ++i)
	{
		TMapDeferredCommandHandle handle = m_deferredCommands[i];
		const CMapDeferredCommandResult& commandRes = CMapDeferredCommandManager::instance().getCommand(handle);
		if (commandRes.isReady())
		{
			CGenerateMeshCommand* command = static_cast<CGenerateMeshCommand*>(commandRes.getCommand());
			CMapDeferredCommandManager::instance().removeCommand(handle);

			bindRegionMeshTexture(command->m_outMesh);

			// Check if the new mesh in part of a transition
			auto inTransitionIt = m_pendingAddRegions[command->m_lod].find(command->m_sectorId);
			if (inTransitionIt != m_pendingAddRegions[command->m_lod].end())
			{
				inTransitionIt->second->addNextMesh(command->m_sectorId, command->m_outMesh, command->m_lod);
			}

			// Check if a mesh already exists for this sector (happens when a transition updates a mesh, with same LOD)
			// It will be updated by the transition later otherwise
			if (landscape.m_regionsList[command->m_lod].find(command->m_sectorId) == landscape.m_regionsList[command->m_lod].end())
			{
				landscape.m_regionsList[command->m_lod][command->m_sectorId] = command->m_outMesh;
				m_generatedRegions[command->m_lod].insert(command->m_sectorId);
			}

			// Delete and remove command
			auto deferredRegionGenIt = m_deferredGenerations[command->m_lod].find(command->m_sectorId);
			nlassert(deferredRegionGenIt != m_deferredGenerations[command->m_lod].end());
			m_deferredGenerations[command->m_lod].erase(deferredRegionGenIt);

			std::swap(m_deferredCommands[i--], m_deferredCommands[m_deferredCommands.size() - 1]);
			m_deferredCommands.pop_back();

			delete command;
		}
	}
}

// ****************************************************************************
void CDynLandscapeUser::updateTouchedMeshes()
{
	for (uint lod = 0; lod < LOD_COUNT; ++lod)
	{
		auto sectorIt = m_meshUpdatesNeeded[lod].begin();
		auto sectorsEnd = m_meshUpdatesNeeded[lod].end();
		while (sectorIt != sectorsEnd)
		{
			if (updateMesh(*sectorIt, lod))
			{
				sectorIt = m_meshUpdatesNeeded[lod].erase(sectorIt);
			}
			else
			{
				// The sector is in a transition, will do it later
				++sectorIt;
			}
		}
	}
}

// ****************************************************************************
extern bool g_refresh;
uint g_maxToGeneratePerFrame = 12;
void	CDynLandscapeUser::refreshZonesAround(const CVector &pos, float radius, std::string &zoneAdded, std::string &zoneRemoved, const std::vector<uint16> *validZoneIds)
{
	NL3D_HAUTO_LOAD_LANDSCAPE;

	if (g_refresh)
	{
		refreshLodSectors(pos, radius, g_maxToGeneratePerFrame);
	}

	zoneRemoved= "";
	zoneAdded= "";
	/*CZoneManager::SZoneManagerWork Work;
	// Check if new zone must be added to landscape
	if (_ZoneManager.isWorkComplete(Work))
	{
		if (Work.ZoneAdded)
		{
			if (Work.Zone == (CZone*)-1)
			{
				//nlwarning ("Can't load zone %s", Work.NameZoneAdded.c_str ());
			}
			else
			{
				_Landscape->getLandscape().addZone(*Work.Zone);

				// Yoyo: must check the binds of the new inserted zone.
				try
				{
					_Landscape->getLandscape().checkBinds(Work.Zone->getZoneId());
				}
				catch (const EBadBind &e)
				{
					nlwarning ("Bind error : %s", e.what());
					nlstopex(("Zone Data Bind Error. Please send a report. You may continue but it should crash!"));
				}

				delete Work.Zone;
				zoneAdded = Work.NameZoneAdded;
				zoneAdded = zoneAdded.substr(0, zoneAdded.find('.'));
			}
		}

		// Check if a zone must be removed from landscape
		if (Work.ZoneRemoved)
		{
			_Landscape->getLandscape().removeZone(Work.IdZoneToRemove);
			zoneRemoved = Work.NameZoneRemoved;
			zoneRemoved = zoneRemoved.substr(0, zoneRemoved.find('.'));
		}
	}

	_ZoneManager.checkZonesAround((uint)pos.x, (uint)(-pos.y), (uint)radius, validZoneIds);*/
}

// ****************************************************************************
void CDynLandscapeUser::removeAllZones()
{
	NL3D_HAUTO_LOAD_LANDSCAPE;

	// Ensure Async Loading is ended
	CZoneManager::SZoneManagerWork Work;
	while (_ZoneManager.isLoading())
	{
		if (_ZoneManager.isWorkComplete(Work))
		{
			// Zone to add?
			if (Work.ZoneAdded)
			{
				if (Work.Zone == (CZone*)-1)
				{
					nlwarning ("Can't load zone %s", Work.NameZoneAdded.c_str ());
				}
				else
				{
					// just discard it! cause will be all cleared below
					delete Work.Zone;
				}
			}

			// Zone to remove?
			if (Work.ZoneRemoved)
			{
				_Landscape->getLandscape().removeZone(Work.IdZoneToRemove);
			}
		}
		else
		{
			nlSleep(1);
		}
	}

	// Then do a full clear
	_Landscape->getLandscape().clear();
	_ZoneManager.clear();
}

// ****************************************************************************
void	CDynLandscapeUser::setupStaticLight (const CRGBA &diffuse, const CRGBA &ambiant, float multiply)
{
	NL3D_HAUTO_UI_LANDSCAPE;
	_Landscape->getLandscape().setupStaticLight(diffuse, ambiant, multiply);
}



// ****************************************************************************
void	CDynLandscapeUser::setThreshold (float thre)
{
	NL3D_HAUTO_UI_LANDSCAPE;
	_Landscape->getLandscape().setThreshold(thre);
}
// ****************************************************************************
float	CDynLandscapeUser::getThreshold () const
{
	NL3D_HAUTO_UI_LANDSCAPE;
	return _Landscape->getLandscape().getThreshold();
}
// ****************************************************************************
void	CDynLandscapeUser::setTileNear (float tileNear)
{
	NL3D_HAUTO_UI_LANDSCAPE;
	_Landscape->getLandscape().setTileNear(tileNear);
}
// ****************************************************************************
float	CDynLandscapeUser::getTileNear () const
{
	NL3D_HAUTO_UI_LANDSCAPE;
	return _Landscape->getLandscape().getTileNear();
}
// ****************************************************************************
void	CDynLandscapeUser::setTileMaxSubdivision (uint tileDiv)
{
	NL3D_HAUTO_UI_LANDSCAPE;
	_Landscape->getLandscape().setTileMaxSubdivision(tileDiv);
}
// ****************************************************************************
uint	CDynLandscapeUser::getTileMaxSubdivision ()
{
	NL3D_HAUTO_UI_LANDSCAPE;
	return _Landscape->getLandscape().getTileMaxSubdivision();
}


// ****************************************************************************
std::string	CDynLandscapeUser::getZoneName(const CVector &pos)
{
	NL3D_HAUTO_UI_LANDSCAPE;
	return _ZoneManager.getZoneName((uint)pos.x, (uint)(-pos.y), 0, 0).first;
}


// ****************************************************************************
CVector		CDynLandscapeUser::getHeightFieldDeltaZ(float x, float y) const
{
	NL3D_HAUTO_UI_LANDSCAPE;
	return _Landscape->getLandscape().getHeightFieldDeltaZ(x, y);
}

// ****************************************************************************
void		CDynLandscapeUser::setHeightField(const CHeightMap &hf)
{
	NL3D_HAUTO_UI_LANDSCAPE;
	_Landscape->getLandscape().setHeightField(hf);
}


// ****************************************************************************
void		CDynLandscapeUser::enableVegetable(bool enable)
{
	NL3D_HAUTO_UI_LANDSCAPE;
	_Landscape->getLandscape().enableVegetable(enable);
}

// ****************************************************************************
void		CDynLandscapeUser::loadVegetableTexture(const std::string &textureFileName)
{
	NL3D_HAUTO_LOAD_LANDSCAPE;
	_Landscape->getLandscape().loadVegetableTexture(textureFileName);
}

// ****************************************************************************
void		CDynLandscapeUser::setupVegetableLighting(const CRGBA &ambient, const CRGBA &diffuse, const CVector &directionalLight)
{
	NL3D_HAUTO_UI_LANDSCAPE;
	_Landscape->getLandscape().setupVegetableLighting(ambient, diffuse, directionalLight);
}

// ****************************************************************************
void		CDynLandscapeUser::setVegetableWind(const CVector &windDir, float windFreq, float windPower, float windBendMin)
{
	NL3D_HAUTO_UI_LANDSCAPE;
	_Landscape->getLandscape().setVegetableWind(windDir, windFreq, windPower, windBendMin);
}

// ****************************************************************************
void		CDynLandscapeUser::setVegetableUpdateLightingFrequency(float freq)
{
	NL3D_HAUTO_UI_LANDSCAPE;
	_Landscape->getLandscape().setVegetableUpdateLightingFrequency(freq);
}


// ****************************************************************************
void		CDynLandscapeUser::setUpdateLightingFrequency(float freq)
{
	NL3D_HAUTO_UI_LANDSCAPE;
	_Landscape->getLandscape().setUpdateLightingFrequency(freq);
}


// ****************************************************************************
void		CDynLandscapeUser::enableAdditive (bool enable)
{
	NL3D_HAUTO_UI_LANDSCAPE;
	_Landscape->enableAdditive(enable);
}
// ****************************************************************************
bool		CDynLandscapeUser::isAdditiveEnabled () const
{
	NL3D_HAUTO_UI_LANDSCAPE;
	return _Landscape->isAdditive ();
}

// ****************************************************************************
void		CDynLandscapeUser::setPointLightDiffuseMaterial(CRGBA diffuse)
{
	NL3D_HAUTO_UI_LANDSCAPE;
	_Landscape->getLandscape().setPointLightDiffuseMaterial(diffuse);
}
// ****************************************************************************
CRGBA		CDynLandscapeUser::getPointLightDiffuseMaterial () const
{
	NL3D_HAUTO_UI_LANDSCAPE;
	return _Landscape->getLandscape().getPointLightDiffuseMaterial();
}

// ****************************************************************************
void		CDynLandscapeUser::setDLMGlobalVegetableColor(CRGBA gvc)
{
	NL3D_HAUTO_UI_LANDSCAPE;
	_Landscape->getLandscape().setDLMGlobalVegetableColor(gvc);
}
// ****************************************************************************
CRGBA		CDynLandscapeUser::getDLMGlobalVegetableColor() const
{
	NL3D_HAUTO_UI_LANDSCAPE;
	return _Landscape->getLandscape().getDLMGlobalVegetableColor();
}
// ****************************************************************************
void		CDynLandscapeUser::updateLightingAll()
{
	NL3D_HAUTO_LANDSCAPE_UPDATE_LIGHTING_ALL;
	_Landscape->getLandscape().updateLightingAll();
}
// ****************************************************************************
void		CDynLandscapeUser::postfixTileFilename (const char *postfix)
{
	NL3D_HAUTO_LANDSCAPE_UPDATE_LIGHTING_ALL;
	_Landscape->getLandscape().TileBank.postfixTileFilename(postfix);
}
// ****************************************************************************
void		CDynLandscapeUser::postfixTileVegetableDesc (const char *postfix)
{
	NL3D_HAUTO_LANDSCAPE_UPDATE_LIGHTING_ALL;
	_Landscape->getLandscape().TileBank.postfixTileVegetableDesc(postfix);
}

// ***************************************************************************
void		CDynLandscapeUser::enableReceiveShadowMap(bool state)
{
	_Landscape->enableReceiveShadowMap(state);
}

// ***************************************************************************
bool		CDynLandscapeUser::canReceiveShadowMap() const
{
	return _Landscape->canReceiveShadowMap();
}

// ***************************************************************************
void		CDynLandscapeUser::setRefineCenterAuto(bool mode)
{
	_Landscape->setRefineCenterAuto(mode);
}

// ***************************************************************************
void		CDynLandscapeUser::setRefineCenterUser(const CVector &refineCenter)
{
	_Landscape->setRefineCenterUser(refineCenter);
}

// ***************************************************************************
bool		CDynLandscapeUser::getRefineCenterAuto() const
{
	return _Landscape->getRefineCenterAuto();
}

// ***************************************************************************
const CVector	&CDynLandscapeUser::getRefineCenterUser() const
{
	return _Landscape->getRefineCenterUser();
}


// ***************************************************************************
void CDynLandscapeUser::addTileCallback(ULandscapeTileCallback *cb)
{
	NL3D_HAUTO_UI_LANDSCAPE;
	_Landscape->getLandscape().addTileCallback(cb);
}

// ***************************************************************************
void CDynLandscapeUser::removeTileCallback(ULandscapeTileCallback *cb)
{
	NL3D_HAUTO_UI_LANDSCAPE;
	_Landscape->getLandscape().removeTileCallback(cb);
}

// ***************************************************************************
bool CDynLandscapeUser::isTileCallback(ULandscapeTileCallback *cb)
{
	NL3D_HAUTO_UI_LANDSCAPE;
	return _Landscape->getLandscape().isTileCallback(cb);
}

// ***************************************************************************
void CDynLandscapeUser::setZFunc(UMaterial::ZFunc val)
{
	NL3D_HAUTO_UI_LANDSCAPE;
	_Landscape->getLandscape().setZFunc((CMaterial::ZFunc)val);
}

// ***************************************************************************
const CZone*	CDynLandscapeUser::getZone (sint zoneId) const
{
	NL3D_HAUTO_UI_LANDSCAPE;
	return _Landscape->getLandscape().getZone(zoneId);
}

// ***************************************************************************
void		CDynLandscapeUser::setVegetableDensity(float density)
{
	NL3D_HAUTO_UI_LANDSCAPE;
	_Landscape->getLandscape().setVegetableDensity(density);
}

// ***************************************************************************
float		CDynLandscapeUser::getVegetableDensity() const
{
	NL3D_HAUTO_UI_LANDSCAPE;
	return _Landscape->getLandscape().getVegetableDensity();
}

// ***************************************************************************
float CDynLandscapeUser::getRayCollision(const NLMISC::CVector &start, const NLMISC::CVector &end)
{
	NL3D_HAUTO_UI_LANDSCAPE;
	return _Landscape->getLandscape().getRayCollision(start, end);
}

// ***************************************************************************
void CDynLandscapeUser::voxelTouched(const TVoxelPos& voxelPos, Voxel voxelBefore, Voxel voxelAfter)
{
	if (	voxelBefore != voxelAfter
		&& (voxelBefore.getType() == Voxel::Raw || voxelAfter.getType() == Voxel::Raw))
	{
		TSectorsList sectors;
		sectors.reserve(8);
		for (uint lod = 0; lod < LODCOUNT; ++lod)
		{
			sectors.clear();
			float factor = std::pow(2.f, float(lod));

			float voxelSize = TLoadingSector::VoxelSize * factor;
			CVector voxelsExtent(voxelSize * 2.f, voxelSize * 2.f, voxelSize * 2.f);
			CVector voxelWorldPos(voxelPos);
			voxelWorldPos *= TLoadingSector::VoxelSize;

			CAABBox voxelsAffected;
			voxelsAffected.setCenter(voxelWorldPos);
			voxelsAffected.setHalfSize(voxelsExtent);
			m_map->selectSectors(voxelsAffected, sectors, lod);

			// register the mesh update
			m_meshUpdatesNeeded[lod].insert(sectors.begin(), sectors.end());
		}
	}
}

CSectorTransition::CSectorTransition()
	: m_previousFactor(1.f)
	, m_nextFactor(0.f)
	, m_previousLod(InvalidLod)
	, m_nextLod(InvalidLod)
	, m_transitionSpeed(1.f)
{

}

bool CSectorTransition::isReady() const
{
	uint deltaLod = std::abs(sint(m_previousLod) - sint(m_nextLod));
	uint minMeshCount = std::min(m_previousMeshes.size(), m_nextMeshes.size());
	uint maxMeshCount = std::max(m_previousMeshes.size(), m_nextMeshes.size());
	return minMeshCount > 0 && (maxMeshCount / minMeshCount) == std::powf(8.f, float(deltaLod));
}

bool CSectorTransition::isComplete() const
{
	return m_nextFactor >= 1.f && m_previousFactor <= 0.f;
}

void CSectorTransition::updateFactors(float dt)
{
	const float transitionFactor = dt / m_transitionSpeed;
	if (m_nextFactor < 1.f)
	{
		m_nextFactor = std::min(m_nextFactor + transitionFactor, 1.f);

		for (CSmartPtr<IRegionMesh>& meshPtr : m_nextMeshes)
		{
			CRegionMesh* mesh = static_cast<CRegionMesh*>(meshPtr.getPtr());
			mesh->m_displayFactor = m_nextFactor;
		}
	}

	if (m_nextFactor >= 1.f)
	{
		m_previousFactor = std::max(m_previousFactor - transitionFactor, 0.f);

		for (CSmartPtr<IRegionMesh>& meshPtr : m_previousMeshes)
		{
			CRegionMesh* mesh = static_cast<CRegionMesh*>(meshPtr.getPtr());
			mesh->m_displayFactor = m_previousFactor;
		}
	}
}

void CSectorTransition::abort()
{
	m_previousFactor = 1.f;
	m_nextFactor = 0.f;

	for (CSmartPtr<IRegionMesh>& meshPtr : m_previousMeshes)
	{
		CRegionMesh* mesh = static_cast<CRegionMesh*>(meshPtr.getPtr());
		mesh->m_displayFactor = m_previousFactor;
	}

	for (CSmartPtr<IRegionMesh>& meshPtr : m_nextMeshes)
	{
		CRegionMesh* mesh = static_cast<CRegionMesh*>(meshPtr.getPtr());
		mesh->m_displayFactor = m_nextFactor;
	}
}

void CSectorTransition::swap()
{

}

void CSectorTransition::addPreviousMesh(TGenericSectorId sectorId, NLMISC::CSmartPtr<IRegionMesh> mesh, uint lod)
{
	nlassert(m_previousLod == InvalidLod || m_previousLod == lod);
	nlassert(std::find(m_previousSectorIds.begin(), m_previousSectorIds.end(), sectorId) == m_previousSectorIds.end());
	m_previousLod = lod;
	m_previousMeshes.push_back(mesh);
	m_previousSectorIds.push_back(sectorId);
	static_cast<CRegionMesh*>(mesh.getPtr())->m_displayFactor = m_previousFactor;
}

void CSectorTransition::addNextMesh(TGenericSectorId sectorId, NLMISC::CSmartPtr<IRegionMesh> mesh, uint lod)
{
	nlassert(m_nextLod == InvalidLod || m_nextLod == lod);
	nlassert(std::find(m_nextSectorIds.begin(), m_nextSectorIds.end(), sectorId) == m_nextSectorIds.end());
	m_nextLod = lod;
	m_nextMeshes.push_back(mesh);
	m_nextSectorIds.push_back(sectorId);
	static_cast<CRegionMesh*>(mesh.getPtr())->m_displayFactor = m_nextFactor;
}
