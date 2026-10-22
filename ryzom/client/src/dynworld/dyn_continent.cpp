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

/////////////
// INCLUDE //
/////////////
///////////// 


#include "stdpch.h"

// Misc
#include "nel/misc/vectord.h"
#include "nel/misc/path.h"
#include "nel/misc/smart_ptr.h"
#include "nel/misc/progress_callback.h"
#include "nel/misc/async_file_manager.h"
#include "nel/misc/bitmap.h"
#include "nel/misc/polygon.h"
// 3D Interface.
#include "nel/3d/u_landscape.h"
#include "nel/3d/u_scene.h"
#include "nel/3d/u_driver.h"
#include "nel/3d/u_texture.h"
// Georges
#include "nel/georges/u_form.h"
#include "nel/georges/u_form_elm.h"
#include "nel/georges/u_form_loader.h"
// Client
#include "global.h"
#include "dynworld/dyn_continent.h"
#include "light_cycle_manager.h"	// \todo GUIGUI : maybe change this when the time will be better managed.
#include "door_manager.h"
#include "client_cfg.h"
#include "ig_client.h"
#include "pacs_client.h"
#include "village.h"
#include "ig_callback.h"
#include "streamable_ig.h"
#include "weather_manager_client.h"
#include "weather.h"
#include "entities.h"
#include "fog_map.h"
#include "demo.h"
#include "ingame_database_manager.h"
#include "sky_render.h"
#include "fix_season_data.h"
#include "ig_season_callback.h"
#include "user_entity.h"
#include "micro_life_manager.h"
#include "zone_util.h"
#include "mesh_camera_col_manager.h"
#include "misc.h"
#include "sheet_manager.h"
#include "continent_manager.h"
#include "connection.h"
#include "water_env_map_rdr.h"
#include "input.h"
#include "bg_downloader_access.h"

// Dynworld
#include "dynworld/map_client.h"

// Game Share
#include "game_share/georges_helper.h"
#include "game_share/season_file_ext.h"

// R2 Share
#include "game_share/scenario_entry_points.h"


#include "r2/editor.h"

// std
#include <vector>
#include <string>
#include <memory>
#include <algorithm>

///////////
// USING //
///////////
using namespace NLMISC;
using namespace NL3D;
using namespace std;


////////////
// EXTERN //
////////////
extern ULandscape *Landscape;
extern UScene	  *Scene;
extern UScene	  *SceneRoot;
extern UInstanceGroup *BackgroundIG;
extern UDriver	  *Driver;
extern UVisualCollisionManager	*CollisionManager;
extern bool		   InitCloudScape;
extern bool			FirstFrame;
extern CContinentManager ContinentMngr;

CMapClient* CDynContinent::MAP = NULL;

namespace
{
	class CChangeTaskPriority : public CTaskManager::IChangeTaskPriority
	{
		virtual float getTaskPriority(const IRunnable &runnable, float currentPriority) override
		{
			const NLMISC::IRunnablePos *_3dRunnable = dynamic_cast<const NLMISC::IRunnablePos*>(&runnable);
			if (_3dRunnable)
			{
				return (_3dRunnable->Position - UserEntity->pos()).norm();
			}
			return currentPriority;
		}
	} ChangeTaskPriority;
}

void CDynContinent::setup()
{
	if (SheetName.empty())
	{
		nlwarning("SheetName not set : cannot setup continent");
		return;
	}

	CEntitySheet *pES = SheetMngr.get(CSheetId (SheetName));
	CContinentSheet *pCS = NULL;
	if (pES->type() == CEntitySheet::CONTINENT)
		pCS = (CContinentSheet*)pES;

	if (pCS == NULL)
	{
		nlwarning("Bad type for continent form %s.", SheetName.c_str());
		return;
	}

	// copy basics parameters of sheet
	CContinentParameters::operator = (pCS->Continent);

	// setup weather functions
	for(uint k = 0; k < EGSPD::CSeason::Invalid; ++k)
	{
		WeatherFunction[k].buildFromSheet(pCS->WeatherFunction[k], WeatherManager);
	}

	uint k;

	// setup fog of war
	FoW.load(Name);
}

void CDynContinent::select(const CVectorD &pos, NLMISC::IProgressCallback &progress, bool complete, bool unhibernate, EGSPD::CSeason::TSeason season)
{
#ifdef RYZOM_BG_DOWNLOADER
	pauseBGDownloader();
#endif
	CNiceInputAuto niceInputs;
	// Season has changed ?
	Season = season;

	if (Landscape) Landscape->setTileColor(TileColorMono[Season], TileColorFactor[Season]);

	// First frame
	FirstFrame = true;

	{
		H_AUTO(InitRZWorldLanscapeShow)
			// If continent is an indoor, hde the landscape
			if (Landscape)
			{
				if (Indoor)
					Landscape->hide();
				else
					Landscape->show();
			}
	}

	bool toto = false;
	if(complete)
	{
		if (MAP != NULL)
			delete MAP;

		MAP = new CMapClient();

		// Progress bar
		progress.progress (0);
		progress.pushCropedValues (0, 1.f/3.f);

		{
			H_AUTO(InitRZWorldIndoor)
				if (!ClientCfg.Light)
				{
					if (Landscape)
					{
						Landscape->removeAllZones();
						toto= true;
						// If continent is an indoor, hide the landscape
						if (!Indoor)
						{
							// Change the MicroVeget Texture.
							string microVegetSeason = MicroVeget;
							SeasonPostfixTextureFilename (microVegetSeason, Season);
							Landscape->loadVegetableTexture (microVegetSeason);

							// Change the Material for static and dynamic PointLights
							Landscape->setPointLightDiffuseMaterial(LandscapePointLightMaterial);
						}
					}
				}
		}
		// Initialize Landscape IG Callbacks.
		{
			H_AUTO(InitRZWorldInitCallbacks)
				initLandscapeIGCallbacks();
			nlassert(IGCallbacks); // forgot to call ::initLandscapeIGCallbacks()
			if (!ClientCfg.Light)
			{
				// Add weather as an observer (to know when an ig is added, and to set its lightmaps color accordingly)
				registerObserver(&LightCycleManager);
				IGCallbacks->registerObserver(&LightCycleManager);

				// register the LightCycleManager as an observer to Objects that may create IGs
				IGSeasonCallback.Season = Season;
				registerObserver(&IGSeasonCallback);
				IGCallbacks->registerObserver(&IGSeasonCallback);

				// register the Camera collision manager
				registerObserver(&MeshCameraColManager);

				if (!Indoor)
				{
					// Change LOD texture
					try
					{
						// Get the CoarseMeshMap for this season
						string seasonname = CoarseMeshMap;
						SeasonPostfixTextureFilename (seasonname, Season);

						// Set the texture for the coarse mesh manager
						Scene->setCoarseMeshManagerTexture (CPath::lookup(seasonname).c_str());
					}
					catch (const Exception &e)
					{
						nlwarning (e.what());
					}
				}
			}
			// Register door handling (when an ig is added check all shapes for doors and add to the door manager)
			registerObserver(&IGDoorCallback);
		}

		{
			H_AUTO(InitRZWorldPacs)
				releasePACS();
			// Init PACS
			std::string pacsRBankPath = CPath::lookup(PacsRBank, false);
			std::string pacsGRPath = CPath::lookup(PacsGR, false);
			if(!pacsRBankPath.empty() && !pacsGRPath.empty())
				initPACS(pacsRBankPath.c_str(), pacsGRPath.c_str(), progress);
			else
				initPACS(0, 0, progress);
			// Set the move container
			if (IGCallbacks) IGCallbacks->setMoveContainer(PACS);
		}

		{
			H_AUTO(InitRZWorldBuildings)
				// No landscape IG in indoors
				if (0)//!Indoor)
				{
					if (!ClientCfg.Light && Landscape)
					{
						// Init ig
						try
						{
							LandscapeIGManager.initIG (Scene, CPath::lookup(LandscapeIG).c_str(), Driver, Season, &progress);
						}
						catch (const Exception &e)
						{
							nlwarning (e.what());
						}
					}

					// Get IGs for the continent with their name.
					vector<pair<UInstanceGroup *, string> > igsWithNames;
					LandscapeIGManager.getAllIGWithNames(igsWithNames);

					// Associate IGs with the ZC number or -1 if there is no ZC.
					for(uint i = 0; i<igsWithNames.size(); ++i)
					{
						string igZone = strlwr(CFile::getFilenameWithoutExtension(igsWithNames[i].second));

						// Search for the IG name in the ZC list to associate.
						for(uint j = 0; j<ZCList.size(); ++j)
						{
							// Get the outpost ZC
							COutpost	*outpost = getOutpost (j);
							if (outpost)
							{
								// If name matching -> this zone should be a ZC.
								string outpostZone = strlwr(CFile::getFilenameWithoutExtension(ZCList[j].Name));
								if(igZone == outpostZone)
								{
									nlctassert(RZ_MAX_BUILDING_PER_OUTPOST==4);
									// Check there is all information in the IG for a ZC and initialize.
									bool zcOK[RZ_MAX_BUILDING_PER_OUTPOST+1] = {false, false, false, false, false};
									UInstanceGroup *ig = igsWithNames[i].first;
									uint k;
									for(k=0; k<ig->getNumInstance(); ++k)
									{
										if(ig->getInstanceName(k) == "flag_zc")
										{
											// todo hulud handle outpost flag
											zcOK[0] = true;
										}

										// For each building
										uint b;
										for (b=0; b<RZ_MAX_BUILDING_PER_OUTPOST; b++)
										{
											// Good one ?
											if (ig->getInstanceName(k) == ("bat_zc_0"+toString(b+1)))
											{
												// Set the position
												CVector pos = ig->getInstancePos(k);
												CQuat rot = ig->getInstanceRot(k);
												outpost->setBuildingPosition (b, rot, pos);
												zcOK[b+1] = true;
											}
										}
									}

									// There are not enough information in the IG for ZC.
									for (k=0; k<RZ_MAX_BUILDING_PER_OUTPOST+1; k++)
										if (!zcOK[k])
											break;

									// Failed ?
									if (k != RZ_MAX_BUILDING_PER_OUTPOST+1)
										nlwarning("CContinent::select : Zone '%s' should be a ZC but some doomy are missing.", igsWithNames[i].second.c_str());
									// Outpost found, and setuped. stop
									break;
								}
							}
						}
						// Add IGs with the Num ZC associated in the callback system.
						if (IGCallbacks) IGCallbacks->addIG(igsWithNames[i].first);
					}

					// Register outpost collisions and Display
					//initOutpost();
				}
		}

		if (!ClientCfg.Light)
		{
			// Progress bar
			progress.popCropedValues ();
			progress.progress (1.f/3.f);
			progress.pushCropedValues (1.f/3.f, 2.f/3.f);

			{
				H_AUTO(InitRZWorldTileBank)
					// Select the tile bank
					if (Landscape)
					{
						if (!Indoor)
						{
							string farBankSeason = FarBank;
							SeasonPostfixTextureFilename (farBankSeason, Season);
							if (!SmallBank.empty() && !farBankSeason.empty())
							{
								std::string smallBankPath = CPath::lookup(SmallBank, false);
								std::string farBankSeasonPath = CPath::lookup(farBankSeason, false);
								if (!smallBankPath.empty() && !farBankSeasonPath.empty())
								{
									Landscape->loadBankFiles (smallBankPath, farBankSeasonPath);
								}
								// Postfix tiles and vegetset by the season string
								Landscape->postfixTileFilename (CSeasonFileExt::getExtension (Season));
								Landscape->postfixTileVegetableDesc (CSeasonFileExt::getExtension (Season));

								// Flush the tiles
								Landscape->flushTiles (progress);
							}
						}
					}
			}

			// Not an indoor ?
			if (!Indoor)
			{
				{
					H_AUTO(InitRZWorldSky)
						// load the sky if any
						initSky();
					// build the canopee
					try
					{
						nlassert(CurrSeason < EGSPD::CSeason::Invalid);
						if (!CanopyIGfileName[CurrSeason].empty() && CurrSeason != EGSPD::CSeason::Invalid)
						{
							BackgroundIG = UInstanceGroup::createInstanceGroup(CanopyIGfileName[CurrSeason]);
						}
						else
						{
							BackgroundIG = UInstanceGroup::createInstanceGroup(BackgroundIGName);
						}
					}
					catch (const Exception &e)
					{
						nlwarning (e.what());
					}
					if(0)//BackgroundIG)
					{
						if (SceneRoot)
						{
							// Add the IG to the scene
							BackgroundIG->addToScene(*SceneRoot, Driver);

							// See for which objects distance should be overriden (object which use a .PLANT sheet)
							uint numInstances = BackgroundIG->getNumInstance();
							uint k;
							for(k = 0; k < numInstances; ++k)
							{
								// Select the texture function of the season
								UInstance instance = BackgroundIG->getInstance (k);
								if (!instance.empty())
								{
									instance.selectTextureSet (Season);
								}
							}
						}
					}
				}
			}
		}

		// Progress bar
		progress.popCropedValues ();
		progress.progress (2.f/3.f);
		progress.pushCropedValues (2.f/3.f, 1);
	}// complete

	if (complete || unhibernate)
	{
		H_AUTO(InitRZWorldLightCycleManager)
			// Touch light setup
			LightCycleManager.touch();
	}

	if (complete)
	{
		H_AUTO(InitRZWorldWaterMap)
			// init water map
			//initWaterMap();
	}

	const R2::CScenarioEntryPoints::CCompleteIsland *completeIsland = NULL;
	{
		progress.pushCropedValues (0, 3.f/4.f);

		if (!ClientCfg.Light)
		{
			if (!Indoor)
			{
				if (Landscape)
				{
					H_AUTO(InitRZWorldLandscapeIGManager)
						// If any zone already loaded into the landscape, must load them to the IGManager.
						//	Yoyo: this is important for teleportation near a current position, since not so many landscape zones
						//	will be removed/added.
						//	Hence, a solution is to reload all igs.
						vector<string>		zonesLoaded;
					Landscape->getAllZoneLoaded(zonesLoaded);
					// and try to load the ones which fit the IGManager
					vector<NL3D::UInstanceGroup *> igAdded;
					LandscapeIGManager.loadArrayZoneIG(zonesLoaded, &igAdded);

					// Refresh zone to load/remove.
					vector<string>		zonesAdded;
					vector<string>		zonesRemoved;
					completeIsland = R2::CScenarioEntryPoints::getInstance().getCompleteIslandFromCoords(CVector2f((float) UserEntity->pos().x, (float) UserEntity->pos().y));
					Landscape->refreshAllZonesAround(pos, ClientCfg.Vision + ExtraZoneLoadingVision, zonesAdded, zonesRemoved, progress, completeIsland ? &(completeIsland->ZoneIDs) : NULL);
					LandscapeIGManager.unloadArrayZoneIG(zonesRemoved);
					LandscapeIGManager.loadArrayZoneIG(zonesAdded, &igAdded);
				}
			}
		}

		progress.popCropedValues ();
		progress.progress (3.f/4.f);
		progress.pushCropedValues (3.f/4.f, 1);

		// Refresh PACS
		if(GR)
		{
			H_AUTO(InitRZWorldRefreshPacs)
				GR->refreshLrAroundNow(pos, LRRefeshRadius);
		}

		// Refresh streamable object (villages ..)
		if (!ClientCfg.Light)
		{
			if (0)//ClientCfg.VillagesEnabled)
			{
				H_AUTO(InitRZWorldForceUpdateStreamable)
					forceUpdateStreamable(pos, progress);
			}
		}

		progress.popCropedValues ();
	}

	// Execute only when you change the targeted continent is not the current one.
	if(complete || unhibernate)
	{
		// Continent changed -> update entities.
		EntitiesMngr.changeContinent();
		R2::getEditor().onContinentChanged();

		// Ask to reinit cloudscape
		InitCloudScape  = true;

		// Set the entity sun contribution parameters
		CollisionManager->setSunContributionPower (EntitySunContributionPower, EntitySunContributionMaxThreshold);
	}

	if(complete)
	{
		reloadFogMap();
		// temp for debug : create visualisation of current pacs primitives
		// createInstancesFromMoveContainer(Scene, IGCallbacks->getMoveContainer(), NULL);
		//
		// Progress bar
		progress.popCropedValues ();
	}

	if (complete || unhibernate)
	{
		if (!ClientCfg.Light && ClientCfg.LandscapeEnabled && ClientCfg.MicroLifeEnabled)
		{
			H_AUTO(InitRZWorldLoadMicroLife)
				loadMicroLife();
		}
	}
	// Register callback on streaming
	CAsyncFileManager::getInstance().registerTaskPriorityCallback(&ChangeTaskPriority);
}// select //
