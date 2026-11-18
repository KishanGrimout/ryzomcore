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

#ifndef VOXEL_PAGING_STATS_HEADER
#define VOXEL_PAGING_STATS_HEADER

// Voxel paging instrumentation registry (plan docs/VOXEL_PAGING_CONTENTION_PLAN.md P1;
// system doc docs/VoxelVolumePaging.md § 5). Always-compiled, near-zero-cost atomic
// counters at per-page-in frequency (never per-voxel). Each marker is strippable per
// the plan's P5 strip gate; see the registry table in the system doc.

#include <atomic>
#include <string>
#include <cstring>

#include "nel/misc/time_nl.h"
#include "nel/misc/common.h"

// One timed site: call count, total elapsed ticks, worst-case elapsed ticks.
// All accounting is relaxed-order atomics (contended only across worker threads,
// at page-in frequency).
struct TVoxelPagingStat
{
	std::atomic<uint32> m_count{ 0 };
	std::atomic<uint64> m_totalTicks{ 0 };
	std::atomic<uint64> m_maxTicks{ 0 };

	void account(uint64 elapsedTicks)
	{
		m_count.fetch_add(1, std::memory_order_relaxed);
		m_totalTicks.fetch_add(elapsedTicks, std::memory_order_relaxed);
		uint64 prevMax = m_maxTicks.load(std::memory_order_relaxed);
		while (elapsedTicks > prevMax
			&& !m_maxTicks.compare_exchange_weak(prevMax, elapsedTicks, std::memory_order_relaxed))
		{
			// prevMax reloaded by compare_exchange_weak; retry while it is smaller
		}
	}

	void reset()
	{
		m_count.store(0, std::memory_order_relaxed);
		m_totalTicks.store(0, std::memory_order_relaxed);
		m_maxTicks.store(0, std::memory_order_relaxed);
	}

	double totalSeconds() const { return NLMISC::CTime::ticksToSecond((NLMISC::TTicks)m_totalTicks.load(std::memory_order_relaxed)); }
	double maxSeconds() const { return NLMISC::CTime::ticksToSecond((NLMISC::TTicks)m_maxTicks.load(std::memory_order_relaxed)); }
};

// All timed sites of the voxel paging pipeline. Grouped per the plan's markers:
//   VPI = pageIn timing split, VPL = chunk-lock read wait behind page-in,
//   VPW = volume internal write lock, VPM = CMapBase::m_volumeLock (metadata CMutex).
struct CVoxelPagingStats
{
	// PolyVox event sites (fed by the PIX macro hook in polyvox_defs.h)
	TVoxelPagingStat readLookupChunk;     // VPL: getChunk read-path lookup (no longer blocks on an in-progress page-in)
	TVoxelPagingStat pageInWaitChunk;     // VPL: reader spin on the chunk paging flag while the pager fills the buffer (P3a')
	TVoxelPagingStat pageInWaitChunkQuery; // VPL: subset of pageInWaitChunk paid by a movement/collision query thread (D4 cohort)
	TVoxelPagingStat getExistingChunk;    // VPW: write-branch re-lookup under the internal write lock
	TVoxelPagingStat createChunk;         // VPW: chunk alloc + registration prep
	TVoxelPagingStat searchFreeChunkSlot; // VPW: hash-slot insertion scan
	TVoxelPagingStat searchOldestChunk;   // VPW: the O(65536) array scan for the eviction candidate (only when over limit, P3b)
	TVoxelPagingStat deleteOldestChunk;   // VPW: eviction slot release
	TVoxelPagingStat pageInChunk;         // VPI: the pager body (runs with no locks held since P3a')
	TVoxelPagingStat pageInChunkQuery;    // VPI: page-in PERFORMED on a query thread (self page-in: no wait is paid)

	// Ryzom-side pageIn split (VPI)
	TVoxelPagingStat pagerPageIn;         // total pager->pageIn (per pager type aggregated)
	TVoxelPagingStat pagerPageInQuery;    // pager body paid by a query thread
	TVoxelPagingStat cacheTryFill;        // CSlavePager: cache load path (disk + LZMA unpack)
	TVoxelPagingStat cacheTryFillQuery;   // cache load path paid by a query thread (disk+LZMA on the movement thread)
	TVoxelPagingStat cacheReadChunk;      // inside tryFillRegion: the raw readChunk call
	TVoxelPagingStat mapGenMakeChunk;     // from-scratch terrain generation
	TVoxelPagingStat mapGenMakeChunkQuery; // from-scratch generation performed on a query thread
	TVoxelPagingStat cacheSaveCall;       // saveRegionToCache synchronous part (copy; compression is deferred)
	TVoxelPagingStat regionPagedIn;       // map notification incl. the metadata lock
	TVoxelPagingStat regionPagedOut;      // map notification incl. the metadata lock

	// Main-loop stall detector (fed once per frame from
	// CDynGlobalRetriever::refreshLrAround): count/total/max of frame gaps
	// >= 100 ms. Count == the user's hitch count over the window.
	TVoxelPagingStat frameHitch;

	// CMapBase::m_volumeLock (exclusive CMutex) hold times (VPM)
	TVoxelPagingStat mapLockGetSectorInfo;	TVoxelPagingStat mapLockDiffSectors;
	TVoxelPagingStat mapLockGetLoadedSectors;
	TVoxelPagingStat mapLockRegionPagedIn;
	TVoxelPagingStat mapLockRegionPagedOut;

	void reset()
	{
		readLookupChunk.reset();
		pageInWaitChunk.reset();
		pageInWaitChunkQuery.reset();
		getExistingChunk.reset();
		createChunk.reset();
		searchFreeChunkSlot.reset();
		searchOldestChunk.reset();
		deleteOldestChunk.reset();
		pageInChunk.reset();
		pageInChunkQuery.reset();
		pagerPageIn.reset();
		pagerPageInQuery.reset();
		cacheTryFill.reset();
		cacheTryFillQuery.reset();
		cacheReadChunk.reset();
		mapGenMakeChunk.reset();
		mapGenMakeChunkQuery.reset();
		cacheSaveCall.reset();
		regionPagedIn.reset();
		regionPagedOut.reset();
		frameHitch.reset();
		mapLockGetSectorInfo.reset();
		mapLockDiffSectors.reset();
		mapLockGetLoadedSectors.reset();
		mapLockRegionPagedIn.reset();
		mapLockRegionPagedOut.reset();
	}

	// human-readable table for logs / debug commands
	std::string dump() const
	{
		std::string out;
		auto line = [&out](const char* name, const TVoxelPagingStat& stat)
		{
			out += NLMISC::toString("%-22s count=%-10u total=%.3f s max=%.3f s\n",
				name, stat.m_count.load(std::memory_order_relaxed),
				stat.totalSeconds(), stat.maxSeconds());
		};
		line("readLookupChunk", readLookupChunk);
		line("pageInWaitChunk", pageInWaitChunk);
		line("pageInWaitChunkQuery", pageInWaitChunkQuery);
		line("getExistingChunk", getExistingChunk);
		line("createChunk", createChunk);
		line("searchFreeChunkSlot", searchFreeChunkSlot);
		line("searchOldestChunk", searchOldestChunk);
		line("deleteOldestChunk", deleteOldestChunk);
		line("pageInChunk", pageInChunk);
		line("pageInChunkQuery", pageInChunkQuery);
		line("pagerPageIn", pagerPageIn);
		line("pagerPageInQuery", pagerPageInQuery);
		line("cacheTryFill", cacheTryFill);
		line("cacheTryFillQuery", cacheTryFillQuery);
		line("cacheReadChunk", cacheReadChunk);
		line("mapGenMakeChunk", mapGenMakeChunk);
		line("mapGenMakeChunkQuery", mapGenMakeChunkQuery);
		line("cacheSaveCall", cacheSaveCall);
		line("regionPagedIn", regionPagedIn);
		line("regionPagedOut", regionPagedOut);
		line("frameHitch", frameHitch);
		line("mapLockGetSectorInfo", mapLockGetSectorInfo);
		line("mapLockDiffSectors", mapLockDiffSectors);
		line("mapLockGetLoadedSectors", mapLockGetLoadedSectors);
		line("mapLockRegionPagedIn", mapLockRegionPagedIn);
		line("mapLockRegionPagedOut", mapLockRegionPagedOut);
		return out;
	}
};

inline CVoxelPagingStats& voxelPagingStats()
{
	static CVoxelPagingStats s_stats;
	return s_stats;
}

// Thread role marker (docs/VoxelVolumePaging.md § 6.7): lets the dump separate
// the waits that a movement/collision query thread pays (the user-visible hitch
// cohort, D4) from the waits of the paging/extraction workers. thread_local, so
// it costs one TLS read at each WaitPagingChunk end event.
enum class EVoxelPagingRole
{
	None,
	Query // inside CDynGlobalRetriever::testCylinderMove / doMove
};

inline EVoxelPagingRole& voxelPagingRoleRef()
{
	thread_local EVoxelPagingRole s_role = EVoxelPagingRole::None;
	return s_role;
}

inline EVoxelPagingRole voxelPagingRole()
{
	return voxelPagingRoleRef();
}

inline void setVoxelPagingRole(EVoxelPagingRole role)
{
	voxelPagingRoleRef() = role;
}

// RAII: mark the current thread as a movement/collision query thread for the
// duration of the scope (restores the previous role, so nesting is safe).
class CVoxelPagingRoleScope
{
public:
	explicit CVoxelPagingRoleScope(EVoxelPagingRole role)
		: m_previous(voxelPagingRole())
	{
		setVoxelPagingRole(role);
	}

	~CVoxelPagingRoleScope()
	{
		setVoxelPagingRole(m_previous);
	}

private:
	EVoxelPagingRole m_previous;
};

// Single accounting entry point: feeds the one stat, and additionally splits
// page-in waits paid by a query thread into `pageInWaitChunkQuery`.
inline void accountVoxelPagingStat(TVoxelPagingStat* stat, uint64 elapsedTicks)
{
	if (stat == nullptr)
		return;
	stat->account(elapsedTicks);
	if (voxelPagingRole() != EVoxelPagingRole::Query)
		return;
	// Query-role splits: the movement/collision thread either WAITS for a
	// page-in owned by someone else (pageInWaitChunkQuery) or PERFORMS the
	// page-in itself (the *Query work* counters below - no wait then, so the
	// wait counter alone cannot see that stall).
	CVoxelPagingStats& stats = voxelPagingStats();
	if (stat == &stats.pageInWaitChunk)
		stats.pageInWaitChunkQuery.account(elapsedTicks);
	else if (stat == &stats.pageInChunk)
		stats.pageInChunkQuery.account(elapsedTicks);
	else if (stat == &stats.pagerPageIn)
		stats.pagerPageInQuery.account(elapsedTicks);
	else if (stat == &stats.cacheTryFill)
		stats.cacheTryFillQuery.account(elapsedTicks);
	else if (stat == &stats.mapGenMakeChunk)
		stats.mapGenMakeChunkQuery.account(elapsedTicks);
}

// RAII scope timing into one stat
class CVoxelPagingStatScope
{
public:
	explicit CVoxelPagingStatScope(TVoxelPagingStat& stat)
		: m_stat(&stat)
		, m_start(NLMISC::CTime::getPerformanceTime())
	{
	}

	~CVoxelPagingStatScope()
	{
		accountVoxelPagingStat(m_stat, uint64(NLMISC::CTime::getPerformanceTime() - m_start));
	}

private:
	TVoxelPagingStat* m_stat;
	NLMISC::TTicks m_start;
};

// PolyVox PIX-macro hook: PagedVolume.inl's event sites are driven through the
// PIX* macros defined in polyvox_defs.h; begin remembers the event name, end
// accounts the elapsed ticks. The event name -> stat mapping ignores unknown
// names so PolyVox can add sites without touching this file.
class CVoxelPagingPix
{
public:
	void beginEvent(const char* name)
	{
		m_openName = name;
		m_openStart = NLMISC::CTime::getPerformanceTime();
	}

	void endEvent()
	{
		if (m_openName != nullptr)
		{
			account(m_openName, uint64(NLMISC::CTime::getPerformanceTime() - m_openStart));
			m_openName = nullptr;
		}
	}

	static TVoxelPagingStat* statFor(const char* name)
	{
		CVoxelPagingStats& stats = voxelPagingStats();
		if (strcmp(name, "ReadLookupChunk") == 0) return &stats.readLookupChunk;
		if (strcmp(name, "WaitPagingChunk") == 0) return &stats.pageInWaitChunk;
		if (strcmp(name, "GetExistingChunk") == 0) return &stats.getExistingChunk;
		if (strcmp(name, "CreateChunk") == 0) return &stats.createChunk;
		if (strcmp(name, "SearchFreeChunkSlot") == 0) return &stats.searchFreeChunkSlot;
		if (strcmp(name, "SearchOldestChunk") == 0) return &stats.searchOldestChunk;
		if (strcmp(name, "DeleteOldestChunk") == 0) return &stats.deleteOldestChunk;
		if (strcmp(name, "PageInChunk") == 0) return &stats.pageInChunk;
		return nullptr;
	}

private:
	void account(const char* name, uint64 elapsedTicks)
	{
		accountVoxelPagingStat(statFor(name), elapsedTicks);
	}

	const char* m_openName = nullptr;
	NLMISC::TTicks m_openStart = 0;
};

inline CVoxelPagingPix& voxelPagingPix()
{
	thread_local CVoxelPagingPix s_pix;
	return s_pix;
}

// RAII scope for the PIXScopedEvent sites
class CVoxelPagingPixScope
{
public:
	explicit CVoxelPagingPixScope(const char* name)
		: m_stat(CVoxelPagingPix::statFor(name))
		, m_start(NLMISC::CTime::getPerformanceTime())
	{
	}

	~CVoxelPagingPixScope()
	{
		accountVoxelPagingStat(m_stat, uint64(NLMISC::CTime::getPerformanceTime() - m_start));
	}

private:
	TVoxelPagingStat* m_stat;
	NLMISC::TTicks m_start;
};

#endif // VOXEL_PAGING_STATS_HEADER
