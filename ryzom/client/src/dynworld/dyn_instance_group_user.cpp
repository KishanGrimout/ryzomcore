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

#include "dyn_instance_group_user.h"

#include "nel/misc/debug.h"
#include "nel/3d/instance_group_user.h"
#include "nel/3d/scene_user.h"
#include "nel/3d/mesh_multi_lod_instance.h"
#include "nel/3d/text_context_user.h"
#include "nel/3d/particle_system_model.h"
#include "nel/misc/path.h"
#include "nel/misc/file.h"
#include "nel/3d/u_instance.h"

#include "dynworld/dyn_landscapeig_manager.h"
#include "dynworld/dyn_continent.h"
#include "dynworld/map_client.h"

#include "sheet_manager.h"


using namespace NLMISC;
using namespace NL3D;
using namespace std;

// ***************************************************************************

UInstanceGroup	*CDynInstanceGroupUser::createInstanceGroup(TGenericSectorId id)
{
	// Create the instance group
	CDynInstanceGroupUser *user = new CDynInstanceGroupUser;

	// Init the class
	if (!user->init (id))
	{
		// Prb, erase it
		delete user;

		// Return error code
		return NULL;
	}

	// return the good value
	return user;
}

// ***************************************************************************

void CDynInstanceGroupUser::createInstanceGroupAsync(TGenericSectorId id, UInstanceGroup	**pIG)
{
	//CAsyncFileManager3D::getInstance().loadIGUser (instanceGroup, pIG);
}

// ***************************************************************************

void CDynInstanceGroupUser::stopCreateInstanceGroupAsync(UInstanceGroup **ppIG)
{
	// Theorically should stop the async file manager but the async file manager can only be stopped
	// between tasks (a file reading) so that is no sense to do anything here
	while (*ppIG == NULL)
	{
		nlSleep (2);
	}
	if (*ppIG != (UInstanceGroup*)-1)
	{
		delete *ppIG;
	}
}

// ***************************************************************************
CDynInstanceGroupUser::CDynInstanceGroupUser()
{
	/* ***********************************************
	 *	WARNING: This Class/Method must be thread-safe (ctor/dtor/serial): no static access for instance
	 *	It can be loaded/called through CAsyncFileManager for instance
	 * ***********************************************/

	_AddToSceneState = StateNotAdded;

	// set user info for possible get
	_InstanceGroup.setUserInterface(this);
}

// ***************************************************************************
CDynInstanceGroupUser::~CDynInstanceGroupUser()
{
	/* ***********************************************
	 *	WARNING: This Class/Method must be thread-safe (ctor/dtor/serial): no static access for instance
	 *	It can be loaded/called through CAsyncFileManager for instance
	 * ***********************************************/

	// ensure all instances proxys are deleted
	removeInstancesUser();
}

// ***************************************************************************
bool CDynInstanceGroupUser::init(TGenericSectorId id, bool async)
{
	TRegion region;
	CDynLandscapeIGManager::TIGSector::getRegion(id, region);

	// prefetch and return if this will generate no vertices
	if (!CDynContinent::MAP->prepareExtraction(region))
		return true;

	NLMISC::CVectorI igPos;
	CDynLandscapeIGManager::TIGSector::s_idToWorldPos(id, igPos.x, igPos.y, igPos.z);

	TVoxelPos pos;
	CInstanceGroup::TInstanceArray instances;
	for (pos.x = region.getLowerX(); pos.x < region.getUpperX(); ++pos.x)
	{
		for (pos.y = region.getLowerY(); pos.y < region.getUpperY(); ++pos.y)
		{
			for (pos.z = region.getLowerZ(); pos.z < region.getUpperZ(); ++pos.z)
			{
				Voxel v = CDynContinent::MAP->getVoxelAt(pos);
				if (v.getType() == Voxel::Object)
				{
					CSheetId sheetId(v.getSheetId());
					CEntitySheet* sheet = SheetMngr.get(sheetId);
					if (sheet != nullptr && sheet->type() == CEntitySheet::PLANT)
					{
						CPlantSheet* plantSheet = static_cast<CPlantSheet*>(sheet);
						CInstanceGroup::CInstance instance;
						instance.Name = plantSheet->getShapeName();
						instance.Pos = CVector(pos) * CDynLandscapeIGManager::TIGSector::VoxelSize;
						instance.Pos.z -= CDynLandscapeIGManager::TIGSector::VoxelSize;
						instance.Scale = CVector(1.f, 1.f, 1.f);
						instances.push_back(instance);
					}
					else
					{
						nlwarning("Sheet Id %d is unknown or not a plant sheet", v.getSheetId());
					}
				}
			}
		}
	}

	std::vector<CCluster> dummyClusters;
	std::vector<CPortal> dummyPortals;
	_InstanceGroup.build(CVector(igPos), instances, dummyClusters, dummyPortals);
	
	return true;
}

// ***************************************************************************
void CDynInstanceGroupUser::setTransformNameCallback(ITransformName *pTN)
{
	_InstanceGroup.setTransformNameCallback (pTN);
}


// ***************************************************************************
void CDynInstanceGroupUser::setAddRemoveInstanceCallback(IAddRemoveInstance *callback)
{
	_InstanceGroup.setAddRemoveInstanceCallback(callback);
}


// ***************************************************************************
void CDynInstanceGroupUser::setIGAddBeginCallback(IIGAddBegin *callback)
{
	_InstanceGroup.setIGAddBeginCallback(callback);
}



// ***************************************************************************
void CDynInstanceGroupUser::addToScene(class UScene& scene, UDriver *driver, uint selectedTexture)
{
	// Get driver pointer
	IDriver *cDriver= driver ? NLMISC::safe_cast<CDriverUser*>(driver)->getDriver() : NULL;

	// Add to the scene
	addToScene (((CSceneUser*)&scene)->getScene(), cDriver, selectedTexture);
}

// ***************************************************************************
void CDynInstanceGroupUser::getInstanceMatrix(uint instanceNb, NLMISC::CMatrix &dest) const
{
	_InstanceGroup.getInstanceMatrix(instanceNb, dest);
}


// ***************************************************************************
void CDynInstanceGroupUser::addToScene(class CScene& scene, IDriver *driver, uint selectedTexture)
{
	if (!_InstanceGroup.addToScene (scene, driver, selectedTexture))
		return;
	// Fill in the vector and the map accelerating search of instance by names
	for( uint32 i = 0; i < _InstanceGroup._Instances.size(); ++i)
	{
		string stmp;
		if (_InstanceGroup._Instances[i] != NULL)
		{
			// insert in map (may fail if double name)
			stmp = _InstanceGroup.getInstanceName (i);
			_InstanceMap.insert (map<string,CTransformShape*>::value_type(stmp, _InstanceGroup._Instances[i]));
		}
	}
}

// ***************************************************************************
void CDynInstanceGroupUser::addToSceneAsync(class UScene& scene, UDriver *driver, uint selectedTexture)
{
	IDriver *cDriver= driver ? NLMISC::safe_cast<CDriverUser*>(driver)->getDriver() : NULL;
	// Add to the scene
	_InstanceGroup.addToSceneAsync (((CSceneUser*)&scene)->getScene(), cDriver, selectedTexture);
	_AddToSceneState = StateAdding;
	_AddToSceneTempScene = &scene;
	_AddToSceneTempDriver = driver;
}

// ***************************************************************************
void CDynInstanceGroupUser::stopAddToSceneAsync()
{
	_InstanceGroup.stopAddToSceneAsync ();
}

// ***************************************************************************
UInstanceGroup::TState CDynInstanceGroupUser::getAddToSceneState()
{
	UInstanceGroup::TState newState = (UInstanceGroup::TState)_InstanceGroup.getAddToSceneState ();
	if ((_AddToSceneState == StateAdding) && (newState == StateAdded))
	{
		// Fill in the vector and the map accelerating search of instance by names
		for( uint32 i = 0; i < _InstanceGroup._Instances.size(); ++i)
		{
			string stmp;
			if (_InstanceGroup._Instances[i] != NULL)
			{
				// create but don't want to delete from scene, since added/removed with _InstanceGroup
				// insert in map (may fail if double name)
				stmp = _InstanceGroup.getInstanceName (i);
				_InstanceMap.insert (map<string,CTransformShape*>::value_type(stmp, _InstanceGroup._Instances[i]));
			}
		}
		_AddToSceneState = StateAdded;
	}
	return newState;
}

// ***************************************************************************
void CDynInstanceGroupUser::removeFromScene(class UScene& scene)
{
	_InstanceGroup.removeFromScene (((CSceneUser*)&scene)->getScene());
	// Remove all instance user object in the array/map
	removeInstancesUser();
}

// ***************************************************************************
uint CDynInstanceGroupUser::getNumInstance() const
{
	return _InstanceGroup.getNumInstance ();
}

// ***************************************************************************

const std::string& CDynInstanceGroupUser::getShapeName(uint instanceNb) const
{
	// Check args
	if (instanceNb>=_InstanceGroup.getNumInstance ())
		nlerror("getShapeName*(): bad instance Id");

	return _InstanceGroup.getShapeName (instanceNb);
}

// ***************************************************************************
const std::string& CDynInstanceGroupUser::getInstanceName(uint instanceNb) const
{
	// Check args
	if (instanceNb>=_InstanceGroup.getNumInstance ())
		nlerror("getInstanceName*(): bad instance Id");

	return _InstanceGroup.getInstanceName (instanceNb);
}

// ***************************************************************************
const NLMISC::CVector& CDynInstanceGroupUser::getInstancePos(uint instanceNb) const
{
	// Check args
	if (instanceNb>=_InstanceGroup.getNumInstance ())
		nlerror("getInstancePos*(): bad instance Id");

	return _InstanceGroup.getInstancePos (instanceNb);
}

// ***************************************************************************
const NLMISC::CQuat& CDynInstanceGroupUser::getInstanceRot(uint instanceNb) const
{
	// Check args
	if (instanceNb>=_InstanceGroup.getNumInstance ())
		nlerror("getInstanceRot*(): bad instance Id");

	return _InstanceGroup.getInstanceRot (instanceNb);
}

// ***************************************************************************
const NLMISC::CVector& CDynInstanceGroupUser::getInstanceScale(uint instanceNb) const
{
	// Check args
	if (instanceNb>=_InstanceGroup.getNumInstance ())
		nlerror("getInstanceScale*(): bad instance Id");

	return _InstanceGroup.getInstanceScale (instanceNb);
}

// ***************************************************************************

UInstance CDynInstanceGroupUser::getByName(const std::string &name) const
{
	map<string,CTransformShape*>::const_iterator it = _InstanceMap.find (name);
	if (it != _InstanceMap.end())
		return UInstance (it->second);
	else
		return UInstance ();
}

// ***************************************************************************
sint CDynInstanceGroupUser::getIndexByName(const std::string &name) const
{
	map<string,CTransformShape*>::const_iterator it = _InstanceMap.find (name);
	if (it == _InstanceMap.end()) return -1;
	for(uint k = 0; k < _InstanceGroup._Instances.size(); ++k)
	{
		if (_InstanceGroup._Instances[k] == it->second) return (sint) k;
	}
	return -1;
}


// ***************************************************************************
void CDynInstanceGroupUser::setBlendShapeFactor(const std::string &bsName, float rFactor)
{
	_InstanceGroup.setBlendShapeFactor (bsName, rFactor);
}

// ***************************************************************************

void CDynInstanceGroupUser::createRoot(UScene &scene)
{
	_InstanceGroup.createRoot (((CSceneUser*)&scene)->getScene());
}

// ***************************************************************************
void CDynInstanceGroupUser::setClusterSystemForInstances(UInstanceGroup *pClusterSystem)
{
	_InstanceGroup.setClusterSystemForInstances(&((CDynInstanceGroupUser*)pClusterSystem)->_InstanceGroup);
}

// ***************************************************************************
bool CDynInstanceGroupUser::linkToParentCluster(UInstanceGroup *father)
{
	if (father)
		return _InstanceGroup.linkToParent(&(NLMISC::safe_cast<CDynInstanceGroupUser *>(father)->_InstanceGroup));
	else
	{
		nlwarning("Trying to link a cluster system to a NULL parent cluster");
		return false;
	}
}

// ***************************************************************************
void CDynInstanceGroupUser::getDynamicPortals(std::vector<std::string> &names)
{
	_InstanceGroup.getDynamicPortals (names);
}

// ***************************************************************************
void CDynInstanceGroupUser::setDynamicPortal(std::string& name, bool opened)
{
	_InstanceGroup.setDynamicPortal (name, opened);
}

// ***************************************************************************
bool CDynInstanceGroupUser::getDynamicPortal(std::string& name)
{
	return _InstanceGroup.getDynamicPortal (name);
}

// ***************************************************************************
void CDynInstanceGroupUser::setPos(const NLMISC::CVector &pos)
{
	_InstanceGroup.setPos (pos);
}

// ***************************************************************************
void CDynInstanceGroupUser::setRotQuat(const NLMISC::CQuat &q)
{
	_InstanceGroup.setRotQuat (q);
}

// ***************************************************************************
CVector CDynInstanceGroupUser::getPos()
{
	return _InstanceGroup.getPos ();
}

// ***************************************************************************
CQuat CDynInstanceGroupUser::getRotQuat()
{
	return _InstanceGroup.getRotQuat();
}


// ***************************************************************************
void			CDynInstanceGroupUser::freezeHRC()
{
	_InstanceGroup.freezeHRC();
}

// ***************************************************************************
void			CDynInstanceGroupUser::unfreezeHRC()
{
	_InstanceGroup.unfreezeHRC();
}


// ***************************************************************************
bool			CDynInstanceGroupUser::getStaticLightSetup(NLMISC::CRGBA sunAmbient,
		uint retrieverIdentifier, sint surfaceId, const NLMISC::CVector &localPos,
		std::vector<CPointLightInfluence> &pointLightList, uint8 &sunContribution, NLMISC::CRGBA &localAmbient)
{
	return _InstanceGroup.getStaticLightSetup(sunAmbient, retrieverIdentifier, surfaceId, localPos, pointLightList,
		sunContribution, localAmbient);
}

// ***************************************************************************
/*virtual*/ void CDynInstanceGroupUser::setDistMax(uint instance, float dist)
{
	if (instance > _InstanceGroup.getNumInstance())
	{
		nlwarning("CDynInstanceGroupUser::setDistMax : instance index %d is invalid", instance);
		return;
	}
	if (_InstanceGroup._Instances[instance]) _InstanceGroup._Instances[instance]->setDistMax(dist);
}

// ***************************************************************************
/*virtual*/ float CDynInstanceGroupUser::getDistMax(uint instance) const
{
	if (instance > _InstanceGroup.getNumInstance())
	{
		nlwarning("CDynInstanceGroupUser::getDistMax : instance index %d is invalid", instance);
		return -1.f;
	}
	if (_InstanceGroup._Instances[instance]) return _InstanceGroup._Instances[instance]->getDistMax();
	else return -1.f;
}

// ***************************************************************************
/*virtual*/ void CDynInstanceGroupUser::setCoarseMeshDist(uint instance, float dist)
{
	if (instance > _InstanceGroup.getNumInstance())
	{
		nlwarning("CDynInstanceGroupUser::setCoarseMeshDist : instance index %d is invalid", instance);
		return;
	}
	if (_InstanceGroup._Instances[instance])
	{
		CMeshMultiLodInstance *mmli = dynamic_cast<CMeshMultiLodInstance *>(_InstanceGroup._Instances[instance]);
		if (mmli) mmli->setCoarseMeshDist(dist);
	}
}

// ***************************************************************************
/*virtual*/ float CDynInstanceGroupUser::getCoarseMeshDist(uint instance) const
{
	if (instance > _InstanceGroup.getNumInstance())
	{
		nlwarning("getCoarseMeshDist::getDistMax : instance index %d is invalid", instance);
		return -1.f;
	}
	if (_InstanceGroup._Instances[instance])
	{
		CMeshMultiLodInstance *mmli = dynamic_cast<CMeshMultiLodInstance *>(_InstanceGroup._Instances[instance]);
		if (mmli) return mmli->getCoarseMeshDist();
		else return -1.f;
	}
	else return -1.f;
}

// ***************************************************************************
UInstance	CDynInstanceGroupUser::getInstance(uint instanceNb) const
{
	if(instanceNb<_InstanceGroup._Instances.size())
		return UInstance (_InstanceGroup._Instances[instanceNb]);
	else
		return UInstance ();
}

// ***************************************************************************
void		CDynInstanceGroupUser::removeInstancesUser()
{
	/* ***********************************************
	 *	WARNING: This Class/Method must be thread-safe (ctor/dtor/serial): no static access for instance
	 *	It can be loaded/called through CAsyncFileManager for instance
	 * ***********************************************/

	// clear the array and the map
	_InstanceMap.clear();
}

// ***************************************************************************
UInstanceGroup *CDynInstanceGroupUser::getParentCluster() const
{
	CInstanceGroup	*parent= _InstanceGroup.getParentClusterSystem();
	if(parent)
		// NB: return NULL if this is the GlobalInstanceGroup.
		return parent->getUserInterface();
	else
		return NULL;
}

// ***************************************************************************
void			CDynInstanceGroupUser::displayDebugClusters(UDriver *drv, UTextContext *txtCtx)
{
	if(!drv)
		return;
	CTextContext	*pTxtCtx= NULL;
	if(txtCtx)
		pTxtCtx= &((CTextContextUser*)txtCtx)->getTextContext();
	_InstanceGroup.displayDebugClusters(((CDriverUser*)drv)->getDriver(), pTxtCtx);

	// restore the matrix context cause of font rendering
	((CDriverUser*)drv)->restoreMatrixContext();
}

// ***************************************************************************
bool			CDynInstanceGroupUser::dontCastShadowForInterior(uint instance) const
{
	if (instance>=_InstanceGroup.getNumInstance ())
		return false;
	return _InstanceGroup.getInstance(instance).DontCastShadowForInterior;
}

// ***************************************************************************
bool			CDynInstanceGroupUser::dontCastShadowForExterior(uint instance) const
{
	if (instance>=_InstanceGroup.getNumInstance ())
		return false;
	return _InstanceGroup.getInstance(instance).DontCastShadowForExterior;
}
