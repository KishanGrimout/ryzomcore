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

#ifndef CL_DYN_LANDSCAPE_USER_H
#define CL_DYN_LANDSCAPE_USER_H

#include "nel/misc/smart_ptr.h"
#include "nel/3d/landscape_user.h"
#include "game_share/dynworld/map_sector.h"
#include "game_share/dynworld/map.h"
#include "game_share/dynworld/map_deferred_command_manager.h"

#include "dynworld/dyn_landscapeig_manager.h"

class CMapClient;
class MarchingCubesController;
class IRegionMesh;
class CRegionMesh;

// ****************************************************************************
/**
* A sector transition is used to control blending between
* a "previous" set of meshes and a "next" set of meshes,
* typically used to blend between LOD transitions
* \author Kishan Grimout
* \date 2014
*/
class CSectorTransition : public NLMISC::CRefCount
{
	enum { InvalidLod = 0xFFFFFFFF };

public:
	CSectorTransition();

	bool isReady() const;
	bool isComplete() const;

	void updateFactors(float dt);
	void abort();
	void swap();

	void addPreviousMesh(TGenericSectorId sectorId, NLMISC::CSmartPtr<IRegionMesh> mesh, uint lod);
	void addNextMesh(TGenericSectorId sectorId, NLMISC::CSmartPtr<IRegionMesh> mesh, uint lod);

	std::vector<TGenericSectorId> m_previousSectorIds;
	std::vector<TGenericSectorId> m_nextSectorIds;

	std::vector<NLMISC::CSmartPtr<IRegionMesh>> m_previousMeshes;
	std::vector<NLMISC::CSmartPtr<IRegionMesh>> m_nextMeshes;

	float m_previousFactor;
	float m_nextFactor;

	uint32 m_previousLod;
	uint32 m_nextLod;

	float m_transitionSpeed;
};

// ****************************************************************************
/**
 * ULandscape Implementation for dynamic worlds
 * \author Kishan Grimout
 * \date 2014
 */
class CDynLandscapeUser : public NL3D::CLandscapeUser, public IVoxelListener
{
private:
	CMapClient* m_map;
	CDynLandscapeIGManager m_igManager;

	typedef std::set<TGenericSectorId> TLodRegionsContainer;
	typedef std::map<TGenericSectorId, NLMISC::CSmartPtr<CSectorTransition>> TTransitionRegionsContainer;

	// Already generated regions
	TLodRegionsContainer m_generatedRegions[LOD_COUNT];

	// Regions we asked for a deferred generation
	TLodRegionsContainer m_deferredGenerations[LOD_COUNT];

	// Regions part of a transition that are being added by the transition
	TTransitionRegionsContainer m_pendingAddRegions[LOD_COUNT];

	// Regions part of a transition that are being removed by the transition
	TTransitionRegionsContainer m_pendingRemoveRegions[LOD_COUNT];

	// Regions that need to renew their meshes (following an edition for example)
	TLodRegionsContainer m_meshUpdatesNeeded[LOD_COUNT];

	// All the deferred commands running
	std::vector<TMapDeferredCommandHandle> m_deferredCommands;

	virtual void voxelTouched(const TVoxelPos& voxelPos, Voxel voxelBefore, Voxel voxelAfter) override;

public:

	/// \name Object
	// @{
	CDynLandscapeUser(NL3D::UScene *scene);
	virtual	~CDynLandscapeUser();
	// @}


	/// \name Load
	/// All those load methods use CPath to search files.
	// @{
	/// Set the zonePath from where zones are loaded.
	virtual	void	setZonePath(const std::string &zonePath);
	/// Load the tile banks:  the ".bank" and the  ".farbank".
	virtual	void	loadBankFiles(const std::string &tileBankFile, const std::string &farBankFile);
	/// Flush the tiles
	virtual void	flushTiles (NLMISC::IProgressCallback &progress);
	/// Postfix tile filename
	virtual void	postfixTileFilename (const char *postfix);
	/// Postfix vegetable filename
	virtual void	postfixTileVegetableDesc (const char *postfix);
	/// Load all Zones around a position. Call at init only!! (no zone must exist before). This is a blocking call.
	virtual	void	loadAllZonesAround(const NLMISC::CVector &pos, float radius);
	virtual	void	loadAllZonesAround(const NLMISC::CVector &pos, float radius, std::vector<std::string> &zonesAdded);
	/// Delete old zones, or load new zones, around a position. new Zones are loaded async.
	virtual	void	refreshZonesAround(const NLMISC::CVector &pos, float radius);
	virtual	void	refreshZonesAround(const NLMISC::CVector &pos, float radius, std::string &zoneAdded, std::string &zoneRemoved, const std::vector<uint16> *validZoneIds = NULL);
	/// Delete old zones, or load new zones, around a position, until it is finished. This is a blocking call.
	virtual	void	refreshAllZonesAround(const NLMISC::CVector &pos, float radius, std::vector<std::string> &zonesAdded, std::vector<std::string> &zonesRemoved,
		NLMISC::IProgressCallback &progress, const std::vector<uint16> *validZoneIds = NULL);
	virtual	void	getAllZoneLoaded(std::vector<std::string>	&zoneLoaded) const;
	virtual void    invalidateAllTiles();
	virtual void	removeAllZones();
	// @}


	/// \name Lighting
	// @{
	/**
	  *  Setup the light color use for static illumination.
	  *  NB: This setup will be visible only for new texture far/near computed (when player move or see dynamic lighting).
	  *
	  *  \param diffuse is the color of the diffuse componante of the lighting.
	  *  \param ambiant is the color of the ambiante componante of the lighting.
	  *  \param multiply is the multiply factor. Final color is (diffuse*multiply*shading+ambiant*(1.0-shading))
	  */
	virtual	void	setupStaticLight (const NLMISC::CRGBA &diffuse, const NLMISC::CRGBA &ambiant, float multiply);

	virtual	void	setPointLightDiffuseMaterial(NLMISC::CRGBA diffuse);
	virtual	NLMISC::CRGBA	getPointLightDiffuseMaterial () const;


	virtual	void	setUpdateLightingFrequency(float freq);

	virtual	void	updateLightingAll();

	// @}


	/// \name Parameters
	// @{
	/// Set threshold for subdivsion quality. The lower is threshold, the more the landscape is subdivided. Default: 0.001.
	virtual	void	setThreshold (float thre);
	/// Get threshold.
	virtual	float	getThreshold () const;
	/// Set tile near distance. Default 50.f. maximized to length of Far alpha transition).
	virtual	void	setTileNear (float tileNear);
	/// Get tile near distance.
	virtual	float	getTileNear () const;
	/// Set Maximum Tile subdivision. Valid values must be in [0..4]  (assert). Default is 0 (for now :) ).
	virtual	void	setTileMaxSubdivision (uint tileDiv);
	/// Get Maximum Tile subdivision.
	virtual	uint 	getTileMaxSubdivision ();
	/// Set all zones monochromatic or colored
	virtual	void 	setTileColor (bool monochrome, float factor) { _ZoneManager.setZoneTileColor(monochrome, factor); }
	// @}


	/// \name Misc
	// @{
	/// Return the name of the zone around a particular position (in NL3D basis!).
	virtual	std::string	getZoneName(const NLMISC::CVector &pos);

	virtual	void		show()
	{
		_Landscape->show();
	}
	virtual	void		hide()
	{
		_Landscape->hide();
	}

	virtual	void		enableAdditive (bool enable);
	virtual	bool		isAdditiveEnabled () const;

	virtual	void			setRefineCenterAuto(bool mode);
	virtual bool			getRefineCenterAuto() const;
	virtual void			setRefineCenterUser(const NLMISC::CVector &refineCenter);
	virtual const NLMISC::CVector	&getRefineCenterUser() const;

	// @}


	/// \name HeightField DeltaZ.
	// @{
	virtual	NLMISC::CVector		getHeightFieldDeltaZ(float x, float y) const;
	virtual	void		setHeightField(const NL3D::CHeightMap &hf);
	// @}

	/// Micro-Vegetation.
	// @{
	virtual	void		enableVegetable(bool enable);
	virtual	void		loadVegetableTexture(const std::string &textureFileName);
	virtual	void		setupVegetableLighting(const NLMISC::CRGBA &ambient, const NLMISC::CRGBA &diffuse, const NLMISC::CVector &directionalLight);
	virtual	void		setVegetableWind(const NLMISC::CVector &windDir, float windFreq, float windPower, float windBendMin);
	virtual	void		setVegetableUpdateLightingFrequency(float freq);
	virtual	void		setVegetableDensity(float density);
	virtual	float		getVegetableDensity() const;
	// @}


	/// \name Dynamic Lighting management
	// @{
	virtual	void		setDLMGlobalVegetableColor(NLMISC::CRGBA gvc);
	virtual	NLMISC::CRGBA		getDLMGlobalVegetableColor() const;
	// @}

	/// \name ShadowMapping
	// @{
	virtual void			enableReceiveShadowMap(bool state);
	virtual bool			canReceiveShadowMap() const;
	// @}

	/// \name TileCallback
	// @{
	virtual	void					addTileCallback(NL3D::ULandscapeTileCallback *cb);
	virtual	void					removeTileCallback(NL3D::ULandscapeTileCallback *cb);
	virtual	bool					isTileCallback(NL3D::ULandscapeTileCallback *cb);
	// @}

	// modify ZBuffer test of landscape material
	virtual	void					setZFunc(NL3D::UMaterial::ZFunc val);

	/// \name getZone
	// @{
	// Get a zone pointer.
	virtual const NL3D::CZone*	getZone (sint zoneId) const;
	// @}

	/// \name raytrace
	// @{
	virtual float			getRayCollision(const NLMISC::CVector &start, const NLMISC::CVector &end);
	// @}

public:
	/// \name Accessor for CLandscapeUser.
	// @{
	NL3D::CLandscapeModel		*getLandscape()
	{
		return _Landscape;
	}
	// @}

	static const int LODCOUNT = 3;

private:
	void refreshLodSectors(const NLMISC::CVector& pos, float radius, uint maxExtract = 0xFFFFFFFF);

	// returns the number of meshes generated
	uint generateMeshes(const TSectorsList& sectorsToGenerate, uint lod, uint maxExtract = 0xFFFFFFFF);
	IRegionMesh* generateMesh(TGenericSectorId sectorId, uint lod);
	void bindRegionMeshTexture(CRegionMesh* mesh);
	void removeMeshes(const TSectorsList& sectorsToRemove, uint lod);
	void removeMesh(TGenericSectorId sectorId, uint lod);
	bool updateMesh(TGenericSectorId sectorId, uint lod);
	void buildTransitionMeshes(const TSectorsList* sectorsToGenerate, const TSectorsList* sectorsToRemove, uint lodCount, uint maxExtract);
	void updateTransitionMeshes();
	void extractCompletedTransition(CSectorTransition* transition);
	void abortTransition(CSectorTransition* transition);
	bool isInTransition(TGenericSectorId sectorId, uint lod) const;
	void postGenerateMeshesCommands(const TSectorsList* sectorsToGenerate, uint lodCount);
	void updateGenerateMeshesCommands();
	void updateTouchedMeshes();

	// Transient lists
	TSectorsList m_sectorsToGenerate[LODCOUNT];
	TSectorsList m_sectorsToRemove[LODCOUNT];
	TSectorsList m_additionalSectors;
	TSectorsList m_added;
};

#endif
