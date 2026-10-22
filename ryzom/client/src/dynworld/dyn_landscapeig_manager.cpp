// DynWorld - based on 'Ryzom - MMORPG Framework <http://dev.ryzom.com/projects/ryzom/>'
// Copyright (C) 2016 Kishan Grimout <kishan.grimout@gmail.com>
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

#include "dyn_landscapeig_manager.h"
#include "dyn_instance_group_user.h"

#include "nel/3d/scene_user.h"
#include "nel/3d/instance_group_user.h"
#include "nel/3d/shape.h"
#include "nel/misc/common.h"
#include "nel/misc/debug.h"
#include "nel/misc/path.h"
#include "nel/misc/file.h"
#include "nel/misc/hierarchical_timer.h"

#include "game_share/dynworld/polyvox_defs.h"

#include "dynworld/dyn_continent.h"
#include "dynworld/map_client.h"

#include "continent_manager.h"
#include "ig_callback.h"


using namespace NLMISC;
using namespace NL3D;
using namespace std;

extern CContinentManager ContinentMngr;
extern float getPriorityToPlayer(const CVectorI& sectorPosI);

class CGenerateIgCommand : public IMapDeferredCommand
{
public:
	CGenerateIgCommand(CDynLandscapeIGManager& igManager, CMapClient* map, TGenericSectorId sectorId, const IMapDeferredCommand* parent = nullptr)
		: IMapDeferredCommand(parent)
		, m_igManager(igManager)
		, m_map(map)
		, m_sectorId(sectorId)
		, m_ig(nullptr)
		, m_prefetchDone(false)
	{
		TRegion regionToEmerge;
		CDynLandscapeIGManager::TIGSector::getRegion(m_sectorId, regionToEmerge);

		CAABBox worldBox;
		worldBox.setCenter(ToNeLVectorI(regionToEmerge.getCentre()) * TLoadingSector::VoxelSize);
		CVectorI regionSize = ToNeLVectorI(regionToEmerge.getDimensionsInVoxels());

		worldBox.setHalfSize(CVector(float(regionSize.x) * TLoadingSector::VoxelSize * 0.5f,
			float(regionSize.y) * TLoadingSector::VoxelSize * 0.5f,
			float(regionSize.z) * TLoadingSector::VoxelSize * 0.5f));
		m_map->selectSectorsNoOrder<TLoadingSector>(worldBox, m_loadingSectors);
	}

	virtual bool execute() override
	{
		//NLMISC-PIX-STRIPPED NL_PROF("CGenerateIgCommand_execute");
		if (m_prefetchDone)
		{
			generateIg();
			return true;
		}
		else
		{
			pushPrefetchSectorsCommands();
			return false;
		}
	}

	CDynLandscapeIGManager& m_igManager;
	CMapClient* m_map;
	TGenericSectorId m_sectorId;

	// Output
	UInstanceGroup* m_ig;

private:
	virtual void getName(std::string& name) const override
	{
		name = "Generate IGs";
	}

	virtual float getPriority() const override
	{
		CVectorI sectorPosI;
		CDynLandscapeIGManager::TIGSector::s_idToWorldPos(m_sectorId, sectorPosI.x, sectorPosI.y, sectorPosI.z);
		return getPriorityToPlayer(sectorPosI);
	}

	void generateIg()
	{
		//NLMISC-PIX-STRIPPED NL_PROF_F("GenerateIG(%p)", m_sectorId);
		// create the instanceGroup.
		m_ig = CDynInstanceGroupUser::createInstanceGroup(m_sectorId);
	}

	void pushPrefetchSectorsCommands()
	{
		//NLMISC-PIX-STRIPPED NL_PROF_F("PrefetchSectors %d(%p)", m_loadingSectors.size(), m_sectorId);
		m_prefetchDone = true;

		// first, need to prefetch everything that will be needed
		for (TLoadingSector::TId sectorId : m_loadingSectors)
		{
			SectorStatus status = m_map->isSectorLoaded(sectorId);
			if (status == SectorStatus::NotAvailable)
			{
				CPrefetchSectorCommand* command = new CPrefetchSectorCommand(m_map, sectorId, this);
				TMapDeferredCommandHandle handle = CMapDeferredCommandManager::instance().addCommand(command);
				m_prefetchDone = false;
			}
			else if (status == SectorStatus::Locked)
			{
				m_prefetchDone = false;
			}
		}
	}

	TSectorsList m_loadingSectors;
	bool m_prefetchDone;
};

// ***************************************************************************
CDynLandscapeIGManager::CInstanceGroupElement::CInstanceGroupElement(UInstanceGroup *ig, TGenericSectorId id)
{
	Ig = ig;
	AddedToScene = false;
	SectorId = id;
}

// ***************************************************************************
void	CDynLandscapeIGManager::CInstanceGroupElement::release()
{
	delete Ig;
	Ig= NULL;
}


// ***************************************************************************
CDynLandscapeIGManager::CDynLandscapeIGManager()
{
	_Scene=NULL;
}
// ***************************************************************************
CDynLandscapeIGManager::~CDynLandscapeIGManager()
{
	// reset should have been called.
	if(_Scene != NULL)
		nlwarning ("CDynLandscapeIGManager not reseted");
}
// ***************************************************************************
void	CDynLandscapeIGManager::init(UScene *scene)
{
	nlassert(scene);
	_Scene= scene;
}

// ***************************************************************************
void CDynLandscapeIGManager::refreshZonesAround(const NLMISC::CVector &pos, float radius)
{
	CAABBox box;
	box.setCenter(pos);
	box.setHalfSize(CVector(radius, radius, radius));
	TSectorsList selectedSectors; selectedSectors.reserve(64);
	CDynContinent::MAP->selectSectors<TIGSector>(box, selectedSectors);

	TSectorsList toAddSectors, toRemoveSectors; toAddSectors.reserve(64); toRemoveSectors.reserve(64);
	auto convertIt2Id = [](TSectorsContainer::const_iterator it) { return *it; };
	auto findId = [](const TSectorsContainer& list, TGenericSectorId id) { return list.find(id); };
	CDynContinent::MAP->diffSectors<TIGSector>(m_generatedSectors, selectedSectors, toAddSectors, toRemoveSectors, convertIt2Id, findId);

	sint count = toRemoveSectors.size();
	for (sint i = 0; i < count; ++i)
	{
		TGenericSectorId id = toRemoveSectors[i];
		unloadZoneIG(id);
	}

	postGenerateIgCommands(toAddSectors);

	updateGenerateIgCommands();
}

// ****************************************************************************
void CDynLandscapeIGManager::postGenerateIgCommands(const TSectorsList& sectorsToGenerate)
{
	for (TGenericSectorId sectorId : sectorsToGenerate)
	{
		if (m_pendingGenerations.find(sectorId) == m_pendingGenerations.end())
		{
			CGenerateIgCommand* command = new CGenerateIgCommand(*this, CDynContinent::MAP, sectorId);
			TMapDeferredCommandHandle handle = CMapDeferredCommandManager::instance().addCommand(command);
			m_deferredCommands.push_back(handle);
			m_pendingGenerations.insert(sectorId);
		}
	}

}

// ****************************************************************************
sint g_maxIgExtract = 6;
void CDynLandscapeIGManager::updateGenerateIgCommands()
{
	uint selectedTexture = ContinentMngr.cur()->Season;
	uint igExtracted = 0;
	for (uint i = 0; i < m_deferredCommands.size() && igExtracted < g_maxIgExtract; ++i)
	{
		TMapDeferredCommandHandle handle = m_deferredCommands[i];
		const CMapDeferredCommandResult& commandRes = CMapDeferredCommandManager::instance().getCommand(handle);
		if (commandRes.isReady())
		{
			CGenerateIgCommand* command = static_cast<CGenerateIgCommand*>(commandRes.getCommand());
			CMapDeferredCommandManager::instance().removeCommand(handle);

			TGenericSectorId sectorId = command->m_sectorId;
			UInstanceGroup* ig = command->m_ig;

			if (ig != nullptr)
			{
				// Init the IG
				initZoneIG(ig, sectorId, selectedTexture);

				// Add the IG to the scene
				loadZoneIG(sectorId, selectedTexture);

				++igExtracted;
			}

			// Delete and remove command
			m_pendingGenerations.erase(sectorId);
			std::swap(m_deferredCommands[i--], m_deferredCommands[m_deferredCommands.size() - 1]);
			m_deferredCommands.pop_back();

			delete command;
		}
	}
}

// ***************************************************************************
void CDynLandscapeIGManager::initZoneIG(UInstanceGroup* ig, TGenericSectorId id, uint selectedTexture)
{
	if (ig != nullptr)
	{
		// add it to the map.
		_ZoneInstanceGroupMap[id] = CInstanceGroupElement(ig, id);

		// Add a reference on the shapes
		CInstanceGroup &_ig = static_cast<CInstanceGroupUser*>(ig)->getInternalIG();
		CScene &_scene = static_cast<CSceneUser*>(_Scene)->getScene();
		uint i;
		for (i = 0; i<_ig.getNumInstance(); i++)
		{
			// Get the instance name
			string shapeName;
			_ig.getShapeName(i, shapeName);
			if (!shapeName.empty())
			{
				if (toLower(CFile::getExtension(shapeName)) != "pacs_prim")
				{
					// Insert a new shape ?
					if (_ShapeAdded.find(shapeName) == _ShapeAdded.end())
					{
						// Shape present ?
						CShapeBank *shapeBank = _scene.getShapeBank();
						IShape *shape = NULL;
						if (shapeBank->getPresentState(shapeName) == CShapeBank::NotPresent)
							shapeBank->load(shapeName);
						if (shapeBank->getPresentState(shapeName) == CShapeBank::Present)
							shape = shapeBank->addRef(shapeName);

						// Shape loaded ?
						if (shape)
						{
							// Insert the shape
							CSmartPtr<IShape> *smartPtr = new CSmartPtr<IShape>;
							*smartPtr = shape;
							_ShapeAdded.insert(TShapeMap::value_type(shapeName, smartPtr));

							// Flush the shape
							IDriver	*_driver = _scene.getDriver();
							shape->flushTextures(*_driver, selectedTexture);
						}
					}
				}
			}
		}
	}
	else
	{
		nlwarning("CDynLandscapeIGManager::loadIG() Can't load instance group '%d'", id);
	}
}

// ***************************************************************************
extern CIGCallback* IGCallbacks;
UInstanceGroup *CDynLandscapeIGManager::loadZoneIG(TGenericSectorId id, uint selectedTexture)
{
	if (id == TLoadingSector::InvalidId)
		return NULL;

	// try to find this InstanceGroup.
	ItZoneInstanceGroupMap	it;
	it= _ZoneInstanceGroupMap.find( id );

	// if found.
	if( it != _ZoneInstanceGroupMap.end() )
	{
		CInstanceGroupElement* igElement = &it->second;

		// if not already added to the scene.
		if (!igElement->AddedToScene)
		{
			// add to the scene.
			if (igElement->Ig != NULL)
			{
				if (IGCallbacks)
					IGCallbacks->addIG(igElement->Ig);

				igElement->Ig->addToScene(*_Scene);
				igElement->AddedToScene = true;
				m_generatedSectors.insert(id);
			}
		}
		return igElement->Ig;
	}

	return nullptr;
}

// ***************************************************************************
void	CDynLandscapeIGManager::loadArrayZoneIG(const std::vector<TGenericSectorId> &ids, uint selectedTexture, std::vector<UInstanceGroup *> *dest /*= NULL*/)
{
	if (dest)
	{
		dest->clear();
		dest->reserve(ids.size());
	}
	for(uint i=0; i<ids.size(); i++)
	{
		UInstanceGroup *ig = loadZoneIG(ids[i], selectedTexture);
		if (dest && ig)
		{
			dest->push_back(ig);
		}
	}
}

// ***************************************************************************
void	CDynLandscapeIGManager::unloadArrayZoneIG(const std::vector<TGenericSectorId> &ids)
{
	for(uint i=0; i<ids.size(); i++)
	{
		unloadZoneIG(ids[i]);
	}
}

// ***************************************************************************
void	CDynLandscapeIGManager::unloadZoneIG(TGenericSectorId id)
{
	if (id == TLoadingSector::InvalidId)
		return;

	// try to find this InstanceGroup.
	ItZoneInstanceGroupMap	it;
	it= _ZoneInstanceGroupMap.find( id );

	// if found.
	if( it!= _ZoneInstanceGroupMap.end() )
	{
		// if really added to the scene.
		if( it->second.AddedToScene )
		{
			// remove from the scene.
			it->second.Ig->removeFromScene(*_Scene);
			it->second.AddedToScene= false;

			auto it = m_generatedSectors.find(id);
			nlassert(it != m_generatedSectors.end());
			if (it != m_generatedSectors.end())
				m_generatedSectors.erase(it);
		}
	}
}

// ***************************************************************************
bool	CDynLandscapeIGManager::isIGAddedToScene(TGenericSectorId id) const
{
	if (id == TLoadingSector::InvalidId)
		return false;

	// try to find this InstanceGroup.
	ConstItZoneInstanceGroupMap	it;
	it= _ZoneInstanceGroupMap.find( id );

	// if found.
	if( it!= _ZoneInstanceGroupMap.end() )
		return	it->second.AddedToScene;
	else
		return false;
}

// ***************************************************************************
UInstanceGroup	*CDynLandscapeIGManager::getIG(TGenericSectorId id) const
{
	if (id == TLoadingSector::InvalidId)
		return NULL;

	// try to find this InstanceGroup.
	ConstItZoneInstanceGroupMap	it;
	it= _ZoneInstanceGroupMap.find( id );

	// if found.
	if( it!= _ZoneInstanceGroupMap.end() )
		return it->second.Ig;
	else
		return NULL;
}


// ***************************************************************************
void	CDynLandscapeIGManager::reset()
{
	while( _ZoneInstanceGroupMap.begin() != _ZoneInstanceGroupMap.end() )
	{
		TGenericSectorId id = _ZoneInstanceGroupMap.begin()->first;
		// first remove from scene
		unloadZoneIG( id );

		// then delete this entry.
		_ZoneInstanceGroupMap.begin()->second.release();
		_ZoneInstanceGroupMap.erase(_ZoneInstanceGroupMap.begin());
	}

	// For all shape reference
	TShapeMap::iterator ite = _ShapeAdded.begin ();
	while (ite != _ShapeAdded.end())
	{
		// Unreference shape
		CScene &_scene = static_cast<CSceneUser*>(_Scene)->getScene();
		CSmartPtr<IShape> *smartPtr = (CSmartPtr<IShape> *)(ite->second);
		IShape *shapeToRelease = *smartPtr;
		*smartPtr = NULL;
		_scene.getShapeBank()->release(shapeToRelease);
		delete smartPtr;

		// Next
		ite++;
	}
	_ShapeAdded.clear ();

	_Scene=NULL;
}


// ***************************************************************************
void	CDynLandscapeIGManager::reloadAllIgs(uint selectedTexture)
{
	vector<TGenericSectorId>		bkupIgFileNameList;
	vector<bool>			bkupIgAddedToScene;

	// First, erase all igs.
	while( _ZoneInstanceGroupMap.begin() != _ZoneInstanceGroupMap.end() )
	{
		TGenericSectorId id = _ZoneInstanceGroupMap.begin()->first;

		// bkup the state of this ig.
		bkupIgFileNameList.push_back(_ZoneInstanceGroupMap.begin()->second.SectorId);
		bkupIgAddedToScene.push_back(_ZoneInstanceGroupMap.begin()->second.AddedToScene);

		// first remove from scene
		unloadZoneIG( id );

		// then delete this entry.
		_ZoneInstanceGroupMap.begin()->second.release();
		_ZoneInstanceGroupMap.erase(_ZoneInstanceGroupMap.begin());
	}

	// Then reload all Igs.
	for(uint i=0; i<bkupIgFileNameList.size(); i++)
	{
		TGenericSectorId id = bkupIgFileNameList[i];
		UInstanceGroup	*ig = CDynInstanceGroupUser::createInstanceGroup(id); // TODO Create the instance group
		// add it to the map.
		_ZoneInstanceGroupMap[id]= CInstanceGroupElement(ig, id);

		// If was addedToScene before, re-add to scene now.
		if(bkupIgAddedToScene[i])
		{
			loadZoneIG( id, selectedTexture );
		}
	}
}


// ***************************************************************************
void CDynLandscapeIGManager::getAllIG(std::vector<UInstanceGroup *> &dest) const
{
	dest.clear();
	dest.reserve(_ZoneInstanceGroupMap.size());
	// add the instances
	for(TZoneInstanceGroupMap::const_iterator it = _ZoneInstanceGroupMap.begin(); it != _ZoneInstanceGroupMap.end(); ++it)
	{
		dest.push_back(it->second.Ig);
	}
}

// ***************************************************************************
void CDynLandscapeIGManager::getAllIGWithNames(std::vector<std::pair<UInstanceGroup *, TGenericSectorId> > &dest) const
{
	dest.clear();
	dest.reserve(_ZoneInstanceGroupMap.size());
	// add the instances
	for(TZoneInstanceGroupMap::const_iterator it = _ZoneInstanceGroupMap.begin(); it != _ZoneInstanceGroupMap.end(); ++it)
	{
		dest.push_back(std::make_pair(it->second.Ig, it->second.SectorId));
	}
}
