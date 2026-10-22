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

#ifndef CL_DYN_INSTANCE_GROUP_USER_H
#define CL_DYN_INSTANCE_GROUP_USER_H

#include "nel/misc/types_nl.h"
#include "nel/3d/u_instance_group.h"
#include "nel/3d/scene_group.h"
#include "nel/3d/scene_user.h"

#include "game_share/dynworld/map_sector.h"


namespace NLMISC
{
	class CVector;
	class CQuat;
}

namespace NL3D
{
	class UScene;
	class UInstanceGroup;
	class CInstanceUser;
}

/**
 * Implementation of the user interface managing instance groups.
 *
 * \author Cyril 'Hulud' Corvazier
 * \author Nevrax France
 * \date 2001
 */
class CDynInstanceGroupUser : public NL3D::UInstanceGroup
{
	/* ***********************************************
	 *	WARNING: This Class/Method must be thread-safe (ctor/dtor/serial): no static access for instance
	 *	It can be loaded/called through CAsyncFileManager for instance
	 * ***********************************************/

public:
	CDynInstanceGroupUser();
	virtual ~CDynInstanceGroupUser();
	// Init with a scene.
	//bool load (const std::string &instanceGroup);

	// Init without a scene
	bool init(TGenericSectorId id, bool async = false);

	/**
	*	This is the static function create a stand alone instance group.
	*/
	static NL3D::UInstanceGroup* createInstanceGroup(TGenericSectorId id);
	static void createInstanceGroupAsync(TGenericSectorId id, NL3D::UInstanceGroup **pIG);
	static void stopCreateInstanceGroupAsync(NL3D::UInstanceGroup **pIG);

private:
	// From UInstanceGroup
	void setTransformNameCallback (NL3D::ITransformName *pTN);
	void setAddRemoveInstanceCallback(NL3D::IAddRemoveInstance *callback);
	void setIGAddBeginCallback(NL3D::IIGAddBegin *callback);


	void addToScene(class NL3D::UScene& scene, NL3D::UDriver *driver, uint selectedTexture);
	void addToScene(class NL3D::CScene& scene, NL3D::IDriver *driver, uint selectedTexture);

	void addToSceneAsync(class NL3D::UScene& scene, NL3D::UDriver *driver, uint selectedTexture);
	TState getAddToSceneState ();
	void stopAddToSceneAsync ();

	virtual NL3D::UInstance		getInstance(uint instanceNb) const;
	virtual void			setDistMax(uint instance, float dist);
	virtual float			getDistMax(uint instance) const;
	virtual void		    setCoarseMeshDist(uint instance, float dist);
	virtual float           getCoarseMeshDist(uint instance) const;


	void removeFromScene(class NL3D::UScene& scene);
	uint getNumInstance () const;
	const std::string& getShapeName (uint instanceNb) const;
	const std::string& getInstanceName (uint instanceNb) const;
	virtual void				getInstanceMatrix(uint instanceNb, NLMISC::CMatrix &dest) const;
	const NLMISC::CVector& getInstancePos (uint instanceNb) const;
	const NLMISC::CQuat& getInstanceRot (uint instanceNb) const;
	const NLMISC::CVector& getInstanceScale (uint instanceNb) const;
	NL3D::UInstance getByName(const std::string& name) const;
	sint	  getIndexByName(const std::string &name) const;


	void setBlendShapeFactor (const std::string &bsName, float rFactor);

	void createRoot(NL3D::UScene &scene);
	void setClusterSystemForInstances(NL3D::UInstanceGroup *pClusterSystem);
	bool linkToParentCluster(NL3D::UInstanceGroup *father);
	NL3D::UInstanceGroup *getParentCluster() const;
	void getDynamicPortals (std::vector<std::string> &names);
	void setDynamicPortal (std::string& name, bool opened);
	bool getDynamicPortal (std::string& name);


	void setPos (const NLMISC::CVector &pos);
	void setRotQuat (const NLMISC::CQuat &q);

	bool getStaticLightSetup(NLMISC::CRGBA sunAmbient, uint retrieverIdentifier, sint surfaceId, const NLMISC::CVector &localPos,
		std::vector<NL3D::CPointLightInfluence> &pointLightList, uint8 &sunContribution, NLMISC::CRGBA &localAmbient);

	NLMISC::CVector getPos ();
	NLMISC::CQuat	getRotQuat ();

	// The real instance group
	NL3D::CInstanceGroup	_InstanceGroup;
	// For access through getInstance() and getByName()
	std::map<std::string, NL3D::CTransformShape*>	_InstanceMap;
	// Async stuff
	TState _AddToSceneState;
	NL3D::UScene *_AddToSceneTempScene;
	NL3D::UDriver *_AddToSceneTempDriver;

	virtual void			freezeHRC();
	virtual void			unfreezeHRC();

	virtual void			displayDebugClusters(NL3D::UDriver *drv, NL3D::UTextContext *txtCtx);

	virtual bool			dontCastShadowForInterior(uint instance) const;
	virtual bool			dontCastShadowForExterior(uint instance) const;

	friend class NL3D::CSceneUser;

	void		removeInstancesUser();

public:
	// Debug purpose only.
	NL3D::CInstanceGroup	&getInternalIG()
	{
		return _InstanceGroup;
	}
};

#endif // CL_DYN_INSTANCE_GROUP_USER_H

/* End of dyn_instance_group_user.h */
