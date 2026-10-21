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

#ifndef MAP_CACHE_MANAGER_HEADER
#define MAP_CACHE_MANAGER_HEADER

#include "polyvox_defs.h"

class CMapCacheManagerImpl;
struct SectorMetadata;

class CMapCacheManager : public NLMISC::CSingleton<CMapCacheManager>
{
	std::unique_ptr<CMapCacheManagerImpl> m_impl;

public:
	CMapCacheManager();
	~CMapCacheManager();

	bool tryFillRegion(const TRegion& region, TVolume::Chunk* pChunk, SectorMetadata& outInfos);
	void saveRegionToCache(const TRegion& region, TVolume::Chunk* pChunk, const SectorMetadata& infos);
};

#endif
