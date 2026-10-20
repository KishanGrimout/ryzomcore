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

#ifndef BIOME_MANAGER_HEADER
#define BIOME_MANAGER_HEADER

#include "voxel.h"

#include "nel/misc/singleton.h"
#include "nel/misc/tds.h"

#define BIOME_MANAGER_DEBUG_INFO 1

#if BIOME_MANAGER_DEBUG_INFO
#	include "nel/misc/mutex.h"
#endif

class CBiomeManagerImpl;

class CBiomeManager : public NLMISC::CSingleton <CBiomeManager>
{
	NLMISC::CTDS m_impl;
	CBiomeManagerImpl* getImpl();

#if BIOME_MANAGER_DEBUG_INFO
	std::vector<std::pair<size_t, CBiomeManagerImpl*>> m_tlsInternal;
	NLMISC::CMutex m_tlsArrayLock;
#endif

public:
	CBiomeManager();
	~CBiomeManager();

	Voxel::BiomeType getBiome(sint x, sint y);
};

#endif
