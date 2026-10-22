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

#ifndef CL_DYN_LANDSCAPEIG_MANAGER_H
#define CL_DYN_LANDSCAPEIG_MANAGER_H

#include "nel/misc/types_nl.h"

#include "game_share/dynworld/map_sector.h"
#include "game_share/dynworld/map_deferred_command_manager.h"

#include <vector>
#include <map>
#include <string>


namespace NLMISC
{
class IProgressCallback;
}

namespace NL3D
{
	class	UInstanceGroup;
	class	UScene;
	class	UDriver;
}


// ***************************************************************************
/**
 * This class is used to load and unload IG of dyn landscape. Used in conjunction with ULandscape.
 * \author Kishan Grimout
 * \date 2016
 */
class CDynLandscapeIGManager
{
public:

	typedef CSector<6> TIGSector;

	/// Constructor
	CDynLandscapeIGManager();
	~CDynLandscapeIGManager();

	/** Initialize and load Instances Group from a text file. no-op if not found.
	 *	the file is just a list of filename eg: "150_EM.ig", separated with return.
	 *	At this time, All UInstanceGroup are loaded, but none are addToScene() ed.
	 */
	void	init(NL3D::UScene *scene);

	void	refreshZonesAround(const NLMISC::CVector &pos, float radius);

	/** load of an instanceGroup of a zone. name is like "150_EM". no-op if "".
	 *	If exist (see initIG), The instanceGroup is added to the scene.
	 *	call after ULandscape::refreshZonesAround()
	 */
	NL3D::UInstanceGroup *loadZoneIG(TGenericSectorId id, uint selectedTexture);

	/** same as prec loadZoneIG, but with an array of name. no-op if "".
	 *	call after ULandscape::loadAllZonesAround().
	 * \param names the igs to load
	 * \param dest if not NUL, pointer to the created igs
	 */
	void	loadArrayZoneIG(const std::vector<TGenericSectorId> &ids, uint selectedTexture, std::vector<NL3D::UInstanceGroup *> *dest = NULL);

	/** unload of an instanceGroup of a zone. name is like "150_EM". no-op if "".
	 *	If exist (see initIG), The instanceGroup is removed from the scene.
	 *	call after ULandscape::refreshZonesAround()
	 */
	void	unloadZoneIG(TGenericSectorId id);

	/** same as prec unloadZoneIG, but with an array of name. no-op if "".
	 *	call after ULandscape::refreshAllZonesAround().
	 */
	void	unloadArrayZoneIG(const std::vector<TGenericSectorId> &ids);

	/// is the Ig added to scene? name is like "150_EM". false if not found.
	bool	isIGAddedToScene(TGenericSectorId id) const;

	/// get the Ig. name is like "150_EM". NULL if not found.
	NL3D::UInstanceGroup	*getIG(TGenericSectorId id) const;

	/// unload and delete all the zoneIgs. Call before deletion of the scene (else exception).
	void	reset();


	/// reload all the Igs and re-add to scene.
	void	reloadAllIgs(uint selectedTexture);

	/// get a list of the ig
	void			getAllIG(std::vector<NL3D::UInstanceGroup	*> &dest) const;

	/// get a list of the ig, with their names
	void			getAllIGWithNames(std::vector<std::pair<NL3D::UInstanceGroup *, TGenericSectorId> > &dest) const;


private:
	NL3D::UScene			*_Scene;
	struct			CInstanceGroupElement
	{
		NL3D::UInstanceGroup	*Ig;
		bool					AddedToScene;
		TGenericSectorId		SectorId;

		CInstanceGroupElement(NL3D::UInstanceGroup	*ig = NULL, TGenericSectorId id = TLoadingSector::InvalidId);

		// delete the ig.
		void	release();
	};
	void initZoneIG(NL3D::UInstanceGroup* ig, TGenericSectorId id, uint selectedTexture);

	std::vector<TMapDeferredCommandHandle> m_deferredCommands;
	std::set<TGenericSectorId> m_pendingGenerations;
	void postGenerateIgCommands(const TSectorsList& sectorsToGenerate);
	void updateGenerateIgCommands();

	typedef	std::map<std::string, void*>							TShapeMap;
	typedef	std::map<TGenericSectorId, CInstanceGroupElement>	TZoneInstanceGroupMap;
	typedef	TZoneInstanceGroupMap::iterator							ItZoneInstanceGroupMap;
	typedef	TZoneInstanceGroupMap::const_iterator					ConstItZoneInstanceGroupMap;
	TZoneInstanceGroupMap			_ZoneInstanceGroupMap;
	TShapeMap						_ShapeAdded;

	typedef std::set<TGenericSectorId> TSectorsContainer;
	TSectorsContainer m_generatedSectors;

	friend class CGenerateIgCommand;
};


#endif // CL_DYN_LANDSCAPEIG_MANAGER_H

/* End of dyn_landscapeig_manager.h */
