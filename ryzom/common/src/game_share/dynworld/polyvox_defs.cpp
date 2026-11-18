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

uint32_t MultiThreadChunkCache::slotIndex(int32_t iChunkX, int32_t iChunkY, int32_t iChunkZ)
{
	// spatial hash (odd multiplicative constants, XOR-mixed), direct-mapped by
	// the mask; sign wrap on negative coordinates is deterministic and fine.
	const uint32_t hash = uint32_t(iChunkX) * 73856093u
		^ uint32_t(iChunkY) * 19349663u
		^ uint32_t(iChunkZ) * 83492791u;
	return hash & CacheSlotMask;
}

std::weak_ptr<TVolume::Chunk> MultiThreadChunkCache::getChunkCached(const TVolume* pVolume, int32_t iChunkX, int32_t iChunkY, int32_t iChunkZ) const
{
	const CacheData& cache = getCacheData();
	const CacheData::Slot& slot = cache.m_slots[slotIndex(iChunkX, iChunkY, iChunkZ)];
	if ((!slot.m_pChunk.expired()) &&
		(slot.m_pVolume == pVolume) &&
		(slot.m_chunkX == iChunkX) &&
		(slot.m_chunkY == iChunkY) &&
		(slot.m_chunkZ == iChunkZ))
	{
		return slot.m_pChunk;
	}

	return std::weak_ptr<TVolume::Chunk>{};
}

void MultiThreadChunkCache::setChunkCached(const TVolume* pVolume, std::weak_ptr<TVolume::Chunk> pChunk, int32_t iChunkX, int32_t iChunkY, int32_t iChunkZ)
{
	CacheData& cache = getCacheData();
	CacheData::Slot& slot = cache.m_slots[slotIndex(iChunkX, iChunkY, iChunkZ)];
	slot.m_pVolume = pVolume;
	slot.m_chunkX = iChunkX;
	slot.m_chunkY = iChunkY;
	slot.m_chunkZ = iChunkZ;
	slot.m_pChunk = pChunk;
}

void MultiThreadChunkCache::dirtyCache()
{
	CacheData& cache = getCacheData();
	for (uint32_t i = 0; i < CacheSlotCount; ++i)
		cache.m_slots[i] = CacheData::Slot{};
}
