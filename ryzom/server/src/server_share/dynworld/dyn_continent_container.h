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


#ifndef SRV_DYN_CONTINENT_CONTAINER_H
#define SRV_DYN_CONTINENT_CONTAINER_H

#include "../continent_container.h"


class CDynContinentContainer : public CContinentContainer
{
public:

	/// Constructor
	CDynContinentContainer();

	/// Load continent
	void loadContinent(const std::string& name, const std::string& file, sint index, bool allowAutoSpawn = true);

	/// Init pacs prims
	void initPacsPrim(const std::string &path = std::string("landscape_col_prim_pacs_list.txt")) {}

	/* Interface, reimplement what's needed
	/// Init whole continent container
	void	init(uint gridWidth, uint gridHeight, double primitiveMaxSize, uint nbWorldImages, const std::string &packedSheetsDirectory, double cellSize=0.0, bool loadPacsPrims = true);
	
	/// Build sheets
	void	buildSheets(const std::string &packedSheetsDirectory);

	/// Init pacs prims
	void	initPacsPrim(const std::string &path = std::string("landscape_col_prim_pacs_list.txt"));


	/// Load continent
	void	loadContinent(const std::string& name, const std::string& file, sint index, bool allowAutoSpawn = true);

	/// Remove continent
	void	removeContinent(sint index);


	/// Get move container for continent
	NLPACS::UMoveContainer	*getMoveContainer(sint index)
	{
		if (index < 0 || index >= (sint)_Continents.size())
		{
			nlwarning("getMoveContainer(): invalid index %d", index);
			return NULL;
		}
		return _Continents[index].MoveContainer;
	}

	/// Get global retriever for continent
	NLPACS::UGlobalRetriever	*getRetriever(sint index)
	{
		if (index < 0 || index >= (sint)_Continents.size())
		{
			nlwarning("getRetriever(): invalid index %d", index);
			return NULL;
		}
		return _Continents[index].GlobalRetriever;
	}

	/// Get retriever bank for continent
	NLPACS::URetrieverBank	*getRetrieverBank(sint index)
	{
		if (index < 0 || index >= (sint)_Continents.size())
		{
			nlwarning("getRetriverBank(): invalid index %d", index);
			return NULL;
		}
		return _Continents[index].RetrieverBank;
	}


	/// Find continent to spawn in
	sint	findContinent(const NLMISC::CVectorD &worldPosition, const NLMISC::CEntityId& id = NLMISC::CEntityId::Unknown)
	{
		sint	continent = -1;
		// finds the continent that fits given position
		uint	i;
		for (i=0; i<_Continents.size(); ++i)
		{
			if (_Continents[i].GlobalRetriever == NULL ||
				!_Continents[i].AllowAutoSpawn)
				continue;

			NLPACS::UGlobalPosition		globalPosition = _Continents[i].GlobalRetriever->retrievePosition(worldPosition);
			if (globalPosition.InstanceId != -1 && globalPosition.LocalPosition.Surface != -1)
			{
				if (continent != -1)
				{
					nlwarning("findContinent%s: continent not provided, and sole position (%.3f, %.3f, %.3f) refers to several continents (%d, %d) -- use first found", 
						id.toString().c_str(),
						worldPosition.x, worldPosition.y, worldPosition.z,
						i, continent);
					continue;
				}

				continent = i;
			}
		}

		if (continent == -1)
		{
			if (worldPosition.x != 0.0 || worldPosition.y != 0.0 || worldPosition.z != 0.0)
				nlwarning("findContinent%s: can't find best fitting continent for position (%.3f, %.3f, %.3f) -- use first available continent", id.toString().c_str(), worldPosition.x, worldPosition.y, worldPosition.z);

			for (uint i=0; i<_Continents.size(); ++i)
			{
				if (_Continents[i].MoveContainer != NULL)
				{
					continent = i;
					break;
				}
			}
		}

		return continent;
	}

	/// Get container size
	sint	size() const { return (sint)_Continents.size(); }

	/// clear the container -- WARNING 
	void	clear()
	{ 
		uint	i;
		for (i=0; i<_Continents.size(); ++i)
			removeContinent(i);
		_Continents.clear();
	}

	/// get trigger position
	NLMISC::CVectorD	getTriggerPosition(uint i) const
	{
		TTriggerMap::const_iterator	it = _TriggerMap.find(i);
		return  (it == _TriggerMap.end()) ? NLMISC::CVectorD::Null : (*it).second;
	}

	/// Display triggers
	void				displayTriggers(NLMISC::CLog *log = NLMISC::InfoLog) const
	{
		TTriggerMap::const_iterator	it;
		for (it=_TriggerMap.begin(); it!=_TriggerMap.end(); ++it)
			log->displayNL("Trigger %d: (%.3f, %.3f, %.3f)", (*it).first, (*it).second.x, (*it).second.y, (*it).second.z);
	}

protected:

	void	loadPacsPrims(const CSheet &sheet, NLPACS::UMoveContainer *moveContainer);
	*/
};


#endif // NL_CONTINENT_CONTAINER_H

/* End of continent_container.h */
