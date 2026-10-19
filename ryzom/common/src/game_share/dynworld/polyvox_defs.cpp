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

#include "stdpch.h"

#include "polyvox_defs.h"

thread_local MultiThreadChunkCache::CacheData* MultiThreadChunkCache::mtls_cacheData{ nullptr };

MultiThreadChunkCache::CacheData& MultiThreadChunkCache::getCacheData()
{
	if (mtls_cacheData == nullptr)
	{
		mtls_cacheData = new CacheData;
	}

	return *mtls_cacheData;
}

const MultiThreadChunkCache::CacheData& MultiThreadChunkCache::getCacheData() const
{
	if (mtls_cacheData == nullptr)
	{
		mtls_cacheData = new CacheData();
	}

	return *mtls_cacheData;
}

std::weak_ptr<TVolume::Chunk> MultiThreadChunkCache::getChunkCached(const TVolume* pVolume, int32_t iChunkX, int32_t iChunkY, int32_t iChunkZ) const
{
	const CacheData& cache = getCacheData();
	if ((!cache.m_pLastAccessedChunk.expired()) &&
		(cache.m_pVolume == pVolume) &&
		(cache.m_v3dLastAccessedChunkX == iChunkX) &&
		(cache.m_v3dLastAccessedChunkY == iChunkY) &&
		(cache.m_v3dLastAccessedChunkZ == iChunkZ))
	{
		return cache.m_pLastAccessedChunk;
	}

	return std::weak_ptr<TVolume::Chunk>{};
}

void MultiThreadChunkCache::setChunkCached(const TVolume* pVolume, std::weak_ptr<TVolume::Chunk> pChunk, int32_t iChunkX, int32_t iChunkY, int32_t iChunkZ)
{
	CacheData& cache = getCacheData();
	cache.m_v3dLastAccessedChunkX = iChunkX;
	cache.m_v3dLastAccessedChunkY = iChunkY;
	cache.m_v3dLastAccessedChunkZ = iChunkZ;
	cache.m_pLastAccessedChunk = pChunk;
	cache.m_pVolume = pVolume;
}

void MultiThreadChunkCache::dirtyCache()
{
	getCacheData().m_pLastAccessedChunk.reset();
}
