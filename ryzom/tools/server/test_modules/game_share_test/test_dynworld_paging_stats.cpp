#include <gtest/gtest.h>

#include <atomic>
#include <thread>

#include <game_share/dynworld/map.h>
#include <game_share/dynworld/voxel_paging_stats.h>

// Test suite for the voxel paging instrumentation registry
// (plan docs/VOXEL_PAGING_CONTENTION_PLAN.md P1; system doc
// docs/VoxelVolumePaging.md § 5): stat accounting integrity, the PolyVox
// PIX-macro hook routing, and the documented contention shape — readers
// queueing on a chunk whose pageIn is in progress — counted without deadlock.

namespace {

	const sint32 ChunkSide = TLoadingSector::SideSizeInVoxels; // 64

	// Pager that sleeps on pageIn (simulating a from-scratch generation under
	// the chunk write lock) and fills the chunk with a single solid voxel so
	// reads return something deterministic.
	class CSleepPager : public TVolume::Pager
	{
	public:
		void pageIn(const TRegion &region, TVolume::Chunk *pChunk) override
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(20));

			Voxel solid;
			solid.setType(Voxel::Raw);
			solid.setDensity(Voxel::MaxDensity);
			pChunk->setVoxel(0, 0, 0, solid);
		}

		void pageOut(const TRegion &region, TVolume::Chunk *pChunk) override {}
	};

	// helper: spin until roughly the given duration elapsed, returns nonzero when done
	uint64 spinForMs(uint ms)
	{
		NLMISC::TTicks start = NLMISC::CTime::getPerformanceTime();
		NLMISC::TTicks deadline = start + (NLMISC::TTicks)(NLMISC::CTime::ticksToSecond(1000000) * 1000.0 * ms);
		uint64 dummy = 0;
		while (NLMISC::CTime::getPerformanceTime() < deadline)
			for (uint i = 0; i < 1000; ++i)
				dummy += i;
		return dummy > 0 ? 1 : 0;
	}

	// Pager whose pageIn blocks until released, for deterministic
	// page-in-progress races. Registers sector metadata like CSlavePager does
	// (regionPagedIn at the end of the pager body) so the emerge/metadata
	// invariant can be tested.
	class CBlockPager : public TVolume::Pager
	{
	public:
		std::atomic<bool> pageInStarted{ false };
		std::atomic<bool> release{ false };
		CMapBase* m_map = nullptr;
		// last page-in region/chunk, so tests can release the metadata
		// explicitly for a clean CMapBase teardown
		TRegion m_lastRegion;
		TVolume::Chunk* m_lastChunk = nullptr;

		void pageIn(const TRegion &region, TVolume::Chunk *pChunk) override
		{
			pageInStarted.store(true, std::memory_order_seq_cst);
			while (!release.load(std::memory_order_seq_cst))
				std::this_thread::yield();

			Voxel solid;
			solid.setType(Voxel::Raw);
			solid.setDensity(Voxel::MaxDensity);
			pChunk->setVoxel(0, 0, 0, solid);

			m_lastRegion = region;
			m_lastChunk = pChunk;

			if (m_map != nullptr)
			{
				SectorMetadata infos;
				infos.m_contentState = SectorMetadata::IsFull;
				infos.m_revision = 0;
				m_map->regionPagedIn(region, pChunk, infos);
			}
		}

		void pageOut(const TRegion &region, TVolume::Chunk *pChunk) override
		{
			// mirrors CSlavePager::pageOut: release the sector metadata so the
			// map can be destroyed cleanly in tests that registered it
			if (m_map != nullptr)
			{
				SectorMetadata infos;
				m_map->regionPagedOut(region, pChunk, infos);
			}
		}
	};

	// expected voxel written by the block pager into chunk-local (0,0,0)
	Voxel expectedPagerVoxel()
	{
		Voxel solid;
		solid.setType(Voxel::Raw);
		solid.setDensity(Voxel::MaxDensity);
		return solid;
	}

	// the voxel a test writes on top of the pager voxel (same type, different
	// material byte, so the raw values differ)
	Voxel testWrittenVoxel()
	{
		Voxel edited = expectedPagerVoxel();
		edited.setMaterial(42);
		return edited;
	}

} // anonymous namespace

TEST(DynWorldPagingStats, StatAccountingBasics)
{
	TVoxelPagingStat stat;
	EXPECT_EQ(stat.m_count.load(), 0u);
	EXPECT_EQ(stat.m_totalTicks.load(), 0u);

	{
		CVoxelPagingStatScope scope(stat);
		EXPECT_NE(spinForMs(5), 0u);
	}

	EXPECT_EQ(stat.m_count.load(), 1u);
	EXPECT_GT(stat.m_totalTicks.load(), 0u);
	EXPECT_EQ(stat.m_maxTicks.load(), stat.m_totalTicks.load());

	// a second, shorter scope updates count and keeps the max
	{
		CVoxelPagingStatScope scope(stat);
	}

	EXPECT_EQ(stat.m_count.load(), 2u);
	EXPECT_GE(stat.m_maxTicks.load(), stat.m_totalTicks.load() / 2);

	stat.reset();
	EXPECT_EQ(stat.m_count.load(), 0u);
	EXPECT_EQ(stat.m_totalTicks.load(), 0u);
	EXPECT_EQ(stat.m_maxTicks.load(), 0u);
}

TEST(DynWorldPagingStats, StatConcurrencyIntegrity)
{
	TVoxelPagingStat stat;
	stat.reset();

	const uint threadCount = 8;
	const uint accountsPerThread = 10000;
	const uint64 accountValue = 7;

	std::vector<std::thread> threads;
	for (uint t = 0; t < threadCount; ++t)
	{
		threads.emplace_back([&stat, accountsPerThread, accountValue]
		{
			for (uint i = 0; i < accountsPerThread; ++i)
				stat.account(accountValue);
		});
	}
	for (auto &thread : threads)
		thread.join();

	EXPECT_EQ(stat.m_count.load(), threadCount * accountsPerThread);
	EXPECT_EQ(stat.m_totalTicks.load(), threadCount * accountsPerThread * accountValue);
	EXPECT_EQ(stat.m_maxTicks.load(), accountValue);
}

TEST(DynWorldPagingStats, PixMacroHookRoutesSites)
{
	voxelPagingStats().reset();

	// begin/end pair around a real work scope feeds the named site
	voxelPagingPix().beginEvent("PageInChunk");
	EXPECT_NE(spinForMs(2), 0u);
	voxelPagingPix().endEvent();

	EXPECT_EQ(voxelPagingStats().pageInChunk.m_count.load(), 1u);
	EXPECT_GT(voxelPagingStats().pageInChunk.m_totalTicks.load(), 0u);

	// endEvent without beginEvent is a no-op
	voxelPagingPix().endEvent();
	EXPECT_EQ(voxelPagingStats().pageInChunk.m_count.load(), 1u);

	// unknown event names are silently ignored (forward compatibility)
	voxelPagingPix().beginEvent("NotAKnownSite");
	voxelPagingPix().endEvent();
	EXPECT_EQ(voxelPagingStats().pageInChunk.m_count.load(), 1u);

	// the RAII scope variant (PIXScopedEvent expansion)
	{
		CVoxelPagingPixScope scope("SearchOldestChunk");
		EXPECT_NE(spinForMs(1), 0u);
	}
	EXPECT_EQ(voxelPagingStats().searchOldestChunk.m_count.load(), 1u);

	// the dump formatter produces the full table
	const std::string dump = voxelPagingStats().dump();
	EXPECT_NE(dump.find("pageInChunk"), std::string::npos);
	EXPECT_NE(dump.find("searchOldestChunk"), std::string::npos);
}

// The documented contention shape (VoxelVolumePaging §4 H1): several reader
// threads hammering voxel reads while a chunk pageIn (generation under the
// chunk write lock) is in progress. The read lookups must queue on the chunk
// read lock (counted by ReadLookupChunk), nothing must deadlock, and the
// volume-level counters must reflect the traffic.
TEST(DynWorldPagingStats, ReadersQueueWhilePageInProgressWithoutDeadlock)
{
	voxelPagingStats().reset();

	CSleepPager pager;
	CMapBase map;
	map.initMap(&pager, 64ull * 1024 * 1024);

	// page chunk (0,0,0) synchronously first (reader-chunk setup)
	Voxel first = map.getVoxelAt(TVoxelPos(0, 0, 0));
	EXPECT_EQ(first.getType(), Voxel::Raw);
	EXPECT_EQ(voxelPagingStats().pageInChunk.m_count.load(), 1u);

	// the pager-fed totals are not fed by CSleepPager (that is CSlavePager/
	// CMasterPager's job); the volume-level sites are fed by any pager
	EXPECT_EQ(voxelPagingStats().readLookupChunk.m_count.load(), 1u);

	std::atomic<bool> stop(false);
	std::atomic<uint32> readersDone(0);

	const uint readerCount = 4;
	std::vector<std::thread> readers;
	for (uint r = 0; r < readerCount; ++r)
	{
		readers.emplace_back([&map, &stop, &readersDone]
		{
			uint32 aNotAir = 0;    // chunk A stays pure Air away from (0,0,0)
			uint32 bNotRaw = 0;    // chunk B reads must never see uninitialized data
			// chunk A voxel: already paged; chunk B voxel: paged during the run
			while (!stop.load(std::memory_order_relaxed))
			{
				Voxel a = map.getVoxelAt(TVoxelPos(1, 1, 1));
				if (a.getType() != Voxel::Air)
					++aNotAir;

				// each loop alternates chunk A and chunk B reads: the
				// alternation defeats the single-slot thread-local chunk
				// cache, so every read goes through the getChunk lookup
				// (the ReadLookupChunk site); the first B read triggers the
				// pageIn, further B reads queue on the paging flag (P3a')
				Voxel b = map.getVoxelAt(TVoxelPos(ChunkSide, 0, 0));
				if (b.getType() != Voxel::Raw)
					++bNotRaw;
			}
			EXPECT_EQ(aNotAir, 0u); // chunk A never changes
			EXPECT_EQ(bNotRaw, 0u); // the paging flag hid the uninitialized buffer
			++readersDone;
		});
	}

	// let the readers run while paging chunk B from this thread: its pageIn
	// sleeps, so the readers' B-reads queue in ReadLookupChunk meanwhile
	Voxel b = map.getVoxelAt(TVoxelPos(ChunkSide, 0, 0));
	EXPECT_EQ(b.getType(), Voxel::Raw);

	// re-page protection: chunk B is now cached, wait a bit more for the
	// readers to accumulate queued lookups, then stop them
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	stop.store(true, std::memory_order_relaxed);
	for (auto &reader : readers)
		reader.join();

	EXPECT_EQ(readersDone.load(), readerCount);
	EXPECT_EQ(voxelPagingStats().pageInChunk.m_count.load(), 2u);
	EXPECT_GE(voxelPagingStats().readLookupChunk.m_count.load(), 4u);
	EXPECT_GT(voxelPagingStats().readLookupChunk.m_maxTicks.load(), 0u);
	EXPECT_GT(voxelPagingStats().pageInWaitChunk.m_count.load(), 0u); // P3a': the wait moved to the flag spin
}

// P3a' flag semantics, deterministic: while a chunk's pageIn is blocked in the
// pager, a setVoxel and further reads issued for it must wait, region status
// must report Locked, and everything must complete with the pager data (or a
// later write on top of it) once released.
TEST(DynWorldPagingStats, ConcurrentAccessWaitsForPageInProgress)
{
	voxelPagingStats().reset();

	CBlockPager pager;
	CMapBase map;
	map.initMap(&pager, 64ull * 1024 * 1024);

	const TVoxelPos bPos(ChunkSide, 0, 0);

	std::atomic<bool> stop(false);
	std::thread reader([&map, &stop, &bPos]
	{
		while (!stop.load(std::memory_order_relaxed))
			map.getVoxelAt(bPos); // triggers the pageIn, then spins on the flag
	});

	// wait until the pageIn is definitely started and blocked
	while (!pager.pageInStarted.load(std::memory_order_seq_cst))
		std::this_thread::yield();
	std::this_thread::sleep_for(std::chrono::milliseconds(5));

	// a write issued while the pager fills the buffer must block
	std::atomic<bool> setterDone(false);
	std::thread setter([&map, &bPos, &setterDone]
	{
		map.setVoxelAt(bPos, testWrittenVoxel());
		setterDone.store(true, std::memory_order_seq_cst);
	});
	std::this_thread::sleep_for(std::chrono::milliseconds(5));
	EXPECT_FALSE(setterDone.load(std::memory_order_seq_cst)); // still blocked while paging in

	// release the pager; the write must land on the completed buffer
	pager.release.store(true, std::memory_order_seq_cst);
	setter.join();
	EXPECT_TRUE(setterDone.load(std::memory_order_seq_cst));
	stop.store(true, std::memory_order_relaxed);
	reader.join();

	// the pager wrote chunk-local (0,0,0) = world (ChunkSide,0,0); the test
	// write overwrote it after the pageIn completed, so the written voxel wins
	Voxel final = map.getVoxelAt(bPos);
	EXPECT_EQ(final.getRawValue(), testWrittenVoxel().getRawValue());
}

// P3a' region-status semantics: getRegionStatus must report Locked for a
// chunk whose page-in is in progress (isLocked() ORs the paging flag), even
// though the chunk write lock is no longer held during the page-in.
TEST(DynWorldPagingStats, RegionStatusLockedWhilePagingInProgress)
{
	voxelPagingStats().reset();

	CBlockPager pager;
	MultiThreadChunkCache cache;
	// NOTE: the volume is heap-allocated - PagedVolume embeds the 65536-entry
	// chunk array (~1 MB), too large for the test thread stack.
	std::unique_ptr<TVolume> volume(new TVolume(&pager, 64ull * 1024 * 1024, ChunkSide, &cache));

	// region covering exactly chunk (1,0,0)
	const PolyVox::Region bRegion(
		PolyVox::Vector3DInt32(ChunkSide, 0, 0),
		PolyVox::Vector3DInt32(2 * ChunkSide - 1, ChunkSide - 1, ChunkSide - 1));

	// not paged yet: NotAvailable
	EXPECT_EQ(volume->getRegionStatus(bRegion), PolyVox::RegionStatus::NotAvailable);

	std::atomic<bool> stop(false);
	std::thread reader([&volume, &stop]
	{
		while (!stop.load(std::memory_order_relaxed))
			volume->getVoxel(ChunkSide, 0, 0);
	});

	while (!pager.pageInStarted.load(std::memory_order_seq_cst))
		std::this_thread::yield();
	std::this_thread::sleep_for(std::chrono::milliseconds(5));

	// paging in progress: Locked (the old scheme only covered this while the
	// chunk write lock was held; P3a' moved that hold into the flag)
	EXPECT_EQ(volume->getRegionStatus(bRegion), PolyVox::RegionStatus::Locked);

	pager.release.store(true, std::memory_order_seq_cst);
	stop.store(true, std::memory_order_relaxed);
	reader.join();

	// paged in and ready: Available
	EXPECT_EQ(volume->getRegionStatus(bRegion), PolyVox::RegionStatus::Available);
}

// P3e (docs/VoxelVolumePaging.md § 6.7): the TLS chunk cache must survive
// chunk-alternating sweeps. With the old single-slot cache, every voxel read
// alternating across a chunk boundary fell through to getChunk's internal-lock
// lookup (33-56 M lookups per session, F4). With the direct-mapped multi-slot
// cache, warm alternating reads across three distinct chunks must produce ZERO
// ReadLookupChunk events, and each chunk's voxels must keep their own values
// (no cross-slot mixing).
TEST(DynWorldPagingStats, MultiSlotChunkCacheSurvivesChunkAlternation)
{
	CBlockPager pager;
	pager.release.store(true, std::memory_order_seq_cst); // page-in completes immediately
	MultiThreadChunkCache cache;
	// NOTE: the volume is heap-allocated - PagedVolume embeds the 65536-entry
	// chunk array (~1 MB), too large for the test thread stack.
	std::unique_ptr<TVolume> volume(new TVolume(&pager, 64ull * 1024 * 1024, ChunkSide, &cache));

	// one marker voxel per chunk, distinct materials, same chunk-local position
	// (so a cross-slot mixup would swap the values)
	Voxel markers[3];
	for (uint i = 0; i < 3; ++i)
	{
		markers[i] = expectedPagerVoxel();
		markers[i].setMaterial(10 + i);
		volume->setVoxel(i * ChunkSide + 5, 5, 5, markers[i]);
	}

	auto expectMarker = [&](uint chunkIndex)
	{
		Voxel v = volume->getVoxel(chunkIndex * ChunkSide + 5, 5, 5);
		EXPECT_EQ(v.getMaterial(), markers[chunkIndex].getMaterial()) << "chunk " << chunkIndex;
	};

	// warm all three slots, verifying values while at it
	for (uint round = 0; round < 4; ++round)
		for (uint i = 0; i < 3; ++i)
			expectMarker(i);

	// counted window: alternating reads must be pure TLS hits
	voxelPagingStats().reset();
	for (uint round = 0; round < 100; ++round)
		for (uint i = 0; i < 3; ++i)
			expectMarker(i);

	EXPECT_EQ(voxelPagingStats().readLookupChunk.m_count.load(), 0u)
		<< "warm alternating reads must not fall through to getChunk";
}

// P4-lite (docs/VoxelVolumePaging.md § 6.7): the async movement prefetch. A
// region requested through prefetchRegionAsync must become fully paged in on
// the worker (isSectorLoaded Available) without the caller blocking, and the
// map must still tear down cleanly (the worker joins in ~CMapBase before the
// volume dies).
TEST(DynWorldPagingStats, PrefetchRegionAsyncPagesInBackground)
{
	CSleepPager pager;
	CMapBase map;
	map.initMap(&pager, 64ull * 1024 * 1024);

	// region covering exactly chunk (3,0,0)
	const TLoadingSector::TId sectorId = TLoadingSector::s_posToId(3, 0, 0);
	TRegion region;
	TLoadingSector::getRegion(sectorId, region);
	EXPECT_EQ(map.isSectorLoaded(sectorId), PolyVox::RegionStatus::NotAvailable);

	// async request: must not block on the pager's 20 ms page-in more than
	// once - the call itself returns promptly
	const NLMISC::TTicks start = NLMISC::CTime::getPerformanceTime();
	map.prefetchRegionAsync(region);
	const double requestMs = NLMISC::CTime::ticksToSecond(NLMISC::CTime::getPerformanceTime() - start) * 1000.0;
	EXPECT_LT(requestMs, 15.0) << "async prefetch must not block on generation";

	// the worker pages it in shortly after
	bool available = false;
	for (uint i = 0; i < 500 && !available; ++i)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
		available = map.isSectorLoaded(sectorId) == PolyVox::RegionStatus::Available;
	}
	EXPECT_TRUE(available) << "prefetch worker must page the region in";

	// and the content matches the pager
	const Voxel v = map.getVoxelAt(TVoxelPos(3 * ChunkSide, 0, 0));
	EXPECT_EQ(v.getType(), Voxel::Raw);

	// two back-to-back requests (latest-wins): both regions must end paged
	const TLoadingSector::TId sectorId2 = TLoadingSector::s_posToId(4, 0, 0);
	TRegion region2;
	TLoadingSector::getRegion(sectorId2, region2);
	map.prefetchRegionAsync(region);
	map.prefetchRegionAsync(region2);
	bool available2 = false;
	for (uint i = 0; i < 500 && !available2; ++i)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
		available2 = map.isSectorLoaded(sectorId2) == PolyVox::RegionStatus::Available;
	}
	EXPECT_TRUE(available2) << "latest-wins prefetch must still complete the last request";
}

// P3a' invariant regression: while a sector's page-in is in progress on
// another thread, getRegionStatus reports Locked (the chunk write lock is no
// longer held during the page-in) but the sector metadata is only registered
// by regionPagedIn at the END of the pager body. emergeSector must therefore
// NOT treat Locked as "already loaded" and must wait; otherwise
// CMapClient::prepareExtraction's getSectorInfo returns nullptr (live crash
// found 2026-11-17 in CGenerateIgCommand::execute).
TEST(DynWorldPagingStats, EmergeSectorWaitsForInProgressPageIn)
{
	voxelPagingStats().reset();

	CBlockPager pager;
	CMapBase map;
	map.initMap(&pager, 64ull * 1024 * 1024);
	pager.m_map = &map;

	const TLoadingSector::TId bSectorId = TLoadingSector::s_posToId(1, 0, 0);

	// trigger the page-in and keep it blocked inside the pager
	std::thread reader([&map]
	{
		map.getVoxelAt(TVoxelPos(ChunkSide, 0, 0));
	});

	while (!pager.pageInStarted.load(std::memory_order_seq_cst))
		std::this_thread::yield();
	std::this_thread::sleep_for(std::chrono::milliseconds(5));

	// emergeSector called while the page-in is in progress: it must block
	// (old behavior: returned immediately, leaving the metadata unregistered)
	std::atomic<bool> emergerDone(false);
	std::thread emerger([&map, bSectorId, &emergerDone]
	{
		map.emergeSector(bSectorId);
		emergerDone.store(true, std::memory_order_seq_cst);
	});
	std::this_thread::sleep_for(std::chrono::milliseconds(5));
	EXPECT_FALSE(emergerDone.load(std::memory_order_seq_cst));

	// release the pager: the page-in completes, the emerge follows, and the
	// metadata must be registered when emergeSector returns
	pager.release.store(true, std::memory_order_seq_cst);
	emerger.join();
	EXPECT_TRUE(emergerDone.load(std::memory_order_seq_cst));

	SectorMetadata* infos = map.getSectorInfo(bSectorId);
	EXPECT_NE(infos, nullptr);
	if (infos != nullptr)
		EXPECT_EQ(infos->m_contentState, SectorMetadata::IsFull);

	reader.join();

	// and the sector is now available
	EXPECT_EQ(map.isSectorLoaded(bSectorId), PolyVox::RegionStatus::Available);

	// teardown: release the sector metadata explicitly (CMapBase::~CMapBase
	// asserts m_loadedSectors.empty() BEFORE delete m_volume runs the pageOut
	// cleanup). Nulling m_map avoids a double regionPagedOut from ~Chunk's
	// pageOut when the volume is destroyed (the metadata is already unloaded).
	SectorMetadata outInfos;
	map.regionPagedOut(pager.m_lastRegion, pager.m_lastChunk, outInfos);
	pager.m_map = nullptr;
}

// § 6.7 movement-wait attribution: a page-in wait paid by a thread marked as a
// movement/collision query (CDynGlobalRetriever::testCylinderMove / doMove) is
// split into pageInWaitChunkQuery; waits on any other thread (paging worker,
// extraction) count in pageInWaitChunk only. The role is thread-local and the
// RAII scope restores it, so nested scopes cannot leak the marker.
TEST(DynWorldPagingStats, PageInWaitAttributionFollowsQueryRole)
{
	voxelPagingStats().reset();

	CBlockPager pager;
	CMapBase map;
	map.initMap(&pager, 64ull * 1024 * 1024);

	const TVoxelPos bPos(ChunkSide, 0, 0);

	// this thread owns the page-in and blocks inside the pager
	std::thread pageinOwner([&map, &bPos]
	{
		map.getVoxelAt(bPos);
	});
	while (!pager.pageInStarted.load(std::memory_order_seq_cst))
		std::this_thread::yield();

	// release the chunk while the main thread is spinning on the paging flag
	std::thread releaser([&pager]
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(20));
		pager.release.store(true, std::memory_order_seq_cst);
	});

	{
		// movement/collision query: this wait belongs to the query bucket
		CVoxelPagingRoleScope role(EVoxelPagingRole::Query);
		map.getVoxelAt(bPos); // chunk is published with the flag raised: waits
	}

	releaser.join();
	pageinOwner.join();

	const uint32 totalWaits = voxelPagingStats().pageInWaitChunk.m_count.load();
	const uint32 queryWaits = voxelPagingStats().pageInWaitChunkQuery.m_count.load();
	EXPECT_GT(totalWaits, 0u);
	EXPECT_EQ(queryWaits, totalWaits) << "the only waiter was the Query-marked thread";

	// off-role wait (worker/other thread): total only, query bucket untouched
	voxelPagingPix().beginEvent("WaitPagingChunk");
	voxelPagingPix().endEvent();
	EXPECT_EQ(voxelPagingStats().pageInWaitChunk.m_count.load(), totalWaits + 1);
	EXPECT_EQ(voxelPagingStats().pageInWaitChunkQuery.m_count.load(), queryWaits);

	// thread-local, nesting-safe role marker
	EXPECT_EQ(voxelPagingRole(), EVoxelPagingRole::None);
	{
		CVoxelPagingRoleScope outer(EVoxelPagingRole::Query);
		{
			CVoxelPagingRoleScope inner(EVoxelPagingRole::None);
			EXPECT_EQ(voxelPagingRole(), EVoxelPagingRole::None);
		}
		EXPECT_EQ(voxelPagingRole(), EVoxelPagingRole::Query);
	}
	EXPECT_EQ(voxelPagingRole(), EVoxelPagingRole::None);

	// page-in WORK on a query thread (self page-in, no wait paid): the
	// *Query work* split must reveal a stall that the wait counter cannot see
	const uint32 workTotal = voxelPagingStats().pageInChunk.m_count.load();
	const uint32 workQuery = voxelPagingStats().pageInChunkQuery.m_count.load();
	{
		CVoxelPagingRoleScope role(EVoxelPagingRole::Query);
		map.getVoxelAt(TVoxelPos(2 * ChunkSide, 0, 0)); // absent chunk: this thread pages it in
	}
	EXPECT_EQ(voxelPagingStats().pageInChunk.m_count.load(), workTotal + 1);
	EXPECT_EQ(voxelPagingStats().pageInChunkQuery.m_count.load(), workQuery + 1)
		<< "a page-in performed by the query thread must be attributed to it";

	// off-role page-in work: total only
	map.getVoxelAt(TVoxelPos(3 * ChunkSide, 0, 0));
	EXPECT_EQ(voxelPagingStats().pageInChunk.m_count.load(), workTotal + 2);
	EXPECT_EQ(voxelPagingStats().pageInChunkQuery.m_count.load(), workQuery + 1);
}
