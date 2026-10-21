#include <gtest/gtest.h>

#include <chrono>
#include <cstring>
#include <thread>
#include <vector>

#include <nel/misc/common.h>
#include <nel/misc/path.h>
// map_cache_manager.h relies on its consumer to include singleton.h
// (inside the gameshare target it comes from the stdmisc precompiled header)
#include <nel/misc/singleton.h>

#include <game_share/dynworld/map_cache_manager.h>
#include <game_share/dynworld/map_sector.h>

namespace {

	// PolyVox chunk construction asserts a non-null pager even though it is
	// only contacted on pageIn/pageOut; provide a no-op one
	class CDummyPager : public TVolume::Pager
	{
	public:
		void pageIn(const TRegion &region, TVolume::Chunk *pChunk) override {}
		void pageOut(const TRegion &region, TVolume::Chunk *pChunk) override {}
	};

	const uint32 chunkSize = 64;

	// Mirrors CDiskFileDataBase::getFileName() fallback resolution:
	// CPath::lookup stays empty when it is not initialized, so records end up
	// under "ms_defaultPath" (relative to the process working directory)
	std::string fileNameForSectorId(TLoadingSector::TId id)
	{
		return std::string("./cache/map/") + NLMISC::toString(id) + ".sector";
	}

	// Fills a 64-side chunk with a deterministic, heterogeneous voxel pattern
	void fillChunk(TVolume::Chunk &chunk)
	{
		for (uint32 z = 0; z < chunkSize; ++z)
		{
			for (uint32 y = 0; y < chunkSize; ++y)
			{
				for (uint32 x = 0; x < chunkSize; ++x)
				{
					Voxel voxel;
					if ((x + y + z) & 1)
					{
						voxel.setType(Voxel::Liquid);
						voxel.setLiquidType(Voxel::Sap);
						voxel.setDensity(uint8(x * 17 + z));
					}
					else
					{
						// biome is a 3-bit field (max 7), material and density are full bytes
						voxel.setType(Voxel::Raw);
						voxel.setBiome(uint8((x + z) & 7));
						voxel.setMaterial(uint8(y * 13 + z * 29));
						voxel.setDensity(uint8(x + y + z));
					}
					chunk.setVoxel(x, y, z, voxel);
				}
			}
		}
	}
} // anonymous namespace

// Test suite for CMapCacheManager (the dynworld sector cache): saves a voxel
// chunk into the disk cache asynchronously (LZMA compression through the
// parallel task manager) and reads it back, mimicking the slave pager usage.
TEST(DynworldMapCacheTest, SaveAndTryFillRegionRoundTrip)
{
	// The cache manager records under ./cache/map/, whose parent it cannot
	// create itself (CFile::createDirectory is single level)
	NLMISC::CFile::createDirectoryTree("./cache/map/");

	// The canonical loading-sector region for sector (0, 0, 0) at
	// SideSizeInVoxels = 64 voxels per side
	const TRegion region(0, 0, 0, 63, 63, 63);
	const TGenericSectorId sectorId = TLoadingSector::getRegionId(region);
	EXPECT_EQ(0x7ffff7ffffdfffffULL, sectorId);

	// Clear potential leftovers from earlier runs so the test is idempotent
	NLMISC::CFile::deleteFile(fileNameForSectorId(sectorId));

	// A second region id, used to check cache misses
	const TRegion otherRegion(65, 0, 0, 65 + 63, 63, 63);
	const TGenericSectorId otherSectorId = TLoadingSector::getRegionId(otherRegion);
	NLMISC::CFile::deleteFile(fileNameForSectorId(otherSectorId));

	CDummyPager pager;
	TVolume::Chunk chunk(PolyVox::Vector3DInt32(0, 0, 0), (uint16)chunkSize, &pager);
	fillChunk(chunk);

	// The saved raw content must come back bit exact after LZMA round trip
	std::vector<uint8> initialData(chunk.getDataSizeInBytes());
	memcpy(initialData.data(), chunk.getData(), chunk.getDataSizeInBytes());

	SectorMetadata infos;
	infos.m_revision = 42;
	infos.m_contentState = SectorMetadata::IsFull | SectorMetadata::HasObjects;

	// Cache misses before anything was saved
	CMapCacheManager &cacheManager = CMapCacheManager::instance();
	SectorMetadata readInfos;
	TVolume::Chunk otherChunk(PolyVox::Vector3DInt32(0, 0, 0), (uint16)chunkSize, &pager);
	EXPECT_FALSE(cacheManager.tryFillRegion(otherRegion, &otherChunk, readInfos));
	EXPECT_FALSE(cacheManager.tryFillRegion(region, &chunk, readInfos));

	// The compression + disk write happen asynchronously (scheduled with a
	// low priority on the parallel task manager): poll until the region
	// actually appears in the cache
	cacheManager.saveRegionToCache(region, &chunk, infos);

	bool filled = false;
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
	while (!filled)
	{
		filled = cacheManager.tryFillRegion(region, &chunk, readInfos);
		if (!filled && std::chrono::steady_clock::now() >= deadline)
			break;
		if (!filled)
			std::this_thread::sleep_for(std::chrono::milliseconds(2));
	}
	ASSERT_TRUE(filled) << "region never appeared in the cache";

	// The full voxel record and its metadata survived the compression round trip
	EXPECT_EQ(42u, (uint32)readInfos.m_revision);
	EXPECT_EQ(infos.m_contentState, readInfos.m_contentState);
	EXPECT_EQ(0, memcmp(chunk.getData(), initialData.data(), initialData.size()));

	// The other region id still has no record
	EXPECT_FALSE(cacheManager.tryFillRegion(otherRegion, &otherChunk, readInfos));

	// Leaving the cache as found: records live under ./cache/map/
	NLMISC::CFile::deleteFile(fileNameForSectorId(sectorId));
}

TEST(DynworldMapCacheTest, SectorIdPinsTheCacheRecordName)
{
	// The cache record id derives only from the region lower corner:
	// a region spanning a sector has the same id whatever its upper corner
	const TRegion region(0, 0, 0, 63, 63, 63);
	const TRegion wholeSectorWithoutUpperBound(0, 0, 0, 0, 0, 0);
	EXPECT_EQ(TLoadingSector::getRegionId(region), TLoadingSector::getRegionId(wholeSectorWithoutUpperBound));

	// One voxel further along x: the next sector id (1, 0, 0)
	const TRegion nextRegion(64, 0, 0, 64 + 63, 63, 63);
	// NOTE(dynworld): s_posToId = public wrapper of the private s_packId
	EXPECT_EQ(TLoadingSector::s_posToId(1, 0, 0), TLoadingSector::getRegionId(nextRegion));
}
