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

#include "slave_pager.h"

#include "game_share/dynworld/map.h"
#include "game_share/dynworld/map_cache_manager.h"
#include "game_share/dynworld/mapgen/mapgen.h"
#include "game_share/dynworld/voxel_paging_stats.h"

#include "seven_zip/7zCrc.h"


CSlavePager::CSlavePager(CMapBase& map)
	: CPagerBase(map)
{
	m_mapgen = new CMapGen;
	CrcGenerateTable();
}

CSlavePager::~CSlavePager()
{
	delete m_mapgen;
}

bool g_saveSectors = true;
bool g_loadFromCache = true;
void CSlavePager::pageIn(const TRegion& region, TVolume::Chunk* pChunk)
{
	// VPI: total pageIn (runs under the chunk write lock, see VoxelVolumePaging §3)
	CVoxelPagingStatScope pageInScope(voxelPagingStats().pagerPageIn);

	SectorMetadata infos;
	bool filledFromCache = false;
	if (g_loadFromCache)
	{
		CVoxelPagingStatScope tryFillScope(voxelPagingStats().cacheTryFill);
		filledFromCache = CMapCacheManager::instance().tryFillRegion(region, pChunk, infos);
	}

	if (!filledFromCache)
	{
		nlassert(pChunk != nullptr);
		CBlockMakeData makeData(region, *pChunk);
		{
			CVoxelPagingStatScope makeScope(voxelPagingStats().mapGenMakeChunk);
			m_mapgen->makeChunk(makeData);
		}

		infos.m_contentState = makeData.m_isEmpty ? SectorMetadata::IsEmpty : (makeData.m_isFull ? SectorMetadata::IsFull : SectorMetadata::IsHeterogeneous);
		infos.m_contentState |= (makeData.m_hasObjects ? SectorMetadata::HasObjects : 0);
		infos.m_revision = 0;

		if (g_saveSectors)
		{
			// synchronous part only (data copy + db insert); compression runs deferred
			CVoxelPagingStatScope saveScope(voxelPagingStats().cacheSaveCall);
			CMapCacheManager::instance().saveRegionToCache(region, pChunk, infos);
		}
	}

	// DYNHITCH attribution (docs/VoxelVolumePaging.md § 6.7): a page-in
	// performed on a movement/collision query thread is either prefetch lag
	// (the chunk is next to the player) or a wrong-position lookup (the chunk
	// is far away). Log the region; the DYNHITCH line carries the camera
	// position at the same instant, so the distance can be compared.
	if (voxelPagingRole() == EVoxelPagingRole::Query)
	{
		nlwarning("DYNQPAGEIN: page-in performed on the query thread: region v=(%d,%d,%d)-(%d,%d,%d) source=%s",
			region.getLowerX(), region.getLowerY(), region.getLowerZ(),
			region.getUpperX(), region.getUpperY(), region.getUpperZ(),
			filledFromCache ? "cache" : "generated");
	}

	// notify the map
	{
		CVoxelPagingStatScope notifyScope(voxelPagingStats().regionPagedIn);
		m_map.regionPagedIn(region, pChunk, infos);
	}
}

void CSlavePager::pageOut(const TRegion& region, TVolume::Chunk* pChunk)
{
	// notify the map and get chunk metadata
	SectorMetadata infos;
	m_map.regionPagedOut(region, pChunk, infos);

	if (g_saveSectors && pChunk->getDataModified())
		CMapCacheManager::instance().saveRegionToCache(region, pChunk, infos);
}
