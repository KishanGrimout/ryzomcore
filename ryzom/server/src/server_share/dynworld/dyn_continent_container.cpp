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



#include "../stdpch.h"

#include "dyn_continent_container.h"

#include "game_share/dynworld/map_server.h"
#include "game_share/dynworld/dyn_retriever_bank.h"
#include "game_share/dynworld/dyn_global_retriever.h"
#include "game_share/dynworld/dyn_move_container.h"


using namespace std;
using namespace NLMISC;
using namespace NLPACS;
using namespace NLGEORGES;


// Constructor
CDynContinentContainer::CDynContinentContainer()
{
}

void CDynContinentContainer::loadContinent(const string& name, const string& file, sint index, bool allowAutoSpawn)
{
	if ((sint)_Continents.size() <= index)
		_Continents.resize(index+1);

	if (_Continents[index].RetrieverBank != NULL ||
		_Continents[index].GlobalRetriever != NULL ||
		_Continents[index].MoveContainer != NULL)
	{
		nlwarning("Init retriever bank failed, index %d already used by continent '%s'", index, _Continents[index].Name.c_str());
		return;
	}

	_Continents[index].Name = name;
	_Continents[index].AllowAutoSpawn = allowAutoSpawn;

	// load the rbank
	_Continents[index].RetrieverBank = new CDynRetrieverBank(true);
	if( _Continents[index].RetrieverBank == NULL )
	{
		nlwarning("Init retriever bank failed, file load is %s", name.c_str() );
		return;
	}

	// load the dynamic map
	_Continents[index].Map = new CMapServer();
	if( _Continents[index].Map == NULL )
	{
		nlwarning("Init dynamic map failed, file load is %s", name.c_str() );
		delete _Continents[index].RetrieverBank;
		_Continents[index].RetrieverBank = NULL;
		return;
	}

	// load the gr
	_Continents[index].GlobalRetriever = new CDynGlobalRetriever((CDynRetrieverBank*)_Continents[index].RetrieverBank, _Continents[index].Map);
	if( _Continents[index].GlobalRetriever == NULL )
	{
		nlwarning("Init global retriever failed, file load is %s", name.c_str() );
		delete _Continents[index].RetrieverBank;
		delete _Continents[index].Map;
		_Continents[index].Map = NULL;
		_Continents[index].RetrieverBank = NULL;
		return;
	}

	uint	gw = _GridWidth;
	uint	gh = _GridHeight;

	if (_CellSize != 0.0)
	{
		CAABBox		cbox = _Continents[index].GlobalRetriever->getBBox();

		gw = (uint)(cbox.getHalfSize().x*2.0 / _CellSize) + 1;
		gh = (uint)(cbox.getHalfSize().y*2.0 / _CellSize) + 1;
	}

	// create the move container
	_Continents[index].MoveContainer = new CDynMoveContainer (((CDynGlobalRetriever*)_Continents[index].GlobalRetriever), 16, 16, _PrimitiveMaxSize, _NbWorldImages, NELPACS_DEFAULT_MAX_TEST_ITERATION, NELPACS_DEFAULT_OT_SIZE);

	if( _Continents[index].MoveContainer == NULL )
	{
		nlwarning("Init Move container failed, continent %s", name.c_str());
		delete _Continents[index].RetrieverBank;
		delete _Continents[index].GlobalRetriever;
		delete _Continents[index].Map;
		_Continents[index].Map = NULL;
		_Continents[index].GlobalRetriever = NULL;
		_Continents[index].RetrieverBank = NULL;
	}

	_Continents[index].MoveContainer->setAsStatic(0);

	nlinfo("Loaded continent, initialized move container to %dx%d cells", gw, gh);

// 	if (found != _SheetMap.end())
// 		loadPacsPrims((*found).second, _Continents[index].MoveContainer);
}
