#include <gtest/gtest.h>

#include <cmath>

#include <game_share/dynworld/map.h>

// Test suite for the CMapBase movement collision queries added for GitHub
// issue #2 (phase B1 of docs/PLAYER_WALL_CLIFF_COLLISION_PLAN.md):
// isBlockingVoxel/isBlockingPosition, traceSegment (voxel DDA sweep) and
// getGroundNormal (ground search + density-field gradient normal).
//
// Geometry conventions for the tests (voxel = 0.5 m, 2 voxels per meter):
// - ground slab: voxel z 0..2 filled with full-density Raw
// - vertical features: full columns (voxel z 0..63) at one voxel x
// - everything else remains Air
// Coordinates in the helper API are 'local sector' estimations; passing
// TLoadingSector::InvalidId lets the tests use plain world coordinates.

namespace {

	// PolyVox chunk construction asserts a non-null pager even though it is
	// only contacted on pageIn/pageOut; provide a no-op one (same pattern as
	// test_dynworld_map_cache.cpp)
	class CDummyPager : public TVolume::Pager
	{
	public:
		void pageIn(const TRegion &region, TVolume::Chunk *pChunk) override {}
		void pageOut(const TRegion &region, TVolume::Chunk *pChunk) override {}
	};

	const sint32 ChunkSide = TLoadingSector::SideSizeInVoxels; // 64
	const float VoxelWorld = TLoadingSector::VoxelSize; // 0.5 m
	const float InvVoxelWorld = TLoadingSector::InvVoxelSize; // 2 voxels per meter

	const sint32 GroundTopZ = 2; // ground slab voxels z 0..2

	// helper: a fully dense Raw voxel
	Voxel fullSolid()
	{
		Voxel voxel;
		voxel.setType(Voxel::Raw);
		voxel.setDensity(Voxel::MaxDensity);
		return voxel;
	}

	// helper: a Liquid voxel (carries density but never blocks movement)
	Voxel fullLiquid()
	{
		Voxel voxel;
		voxel.setType(Voxel::Liquid);
		voxel.setLiquidType(Voxel::Water);
		voxel.setDensity(Voxel::MaxDensity);
		return voxel;
	}

	// helper: empty Air voxel (the Voxel default ctor value is 0 = Air)
	Voxel airVoxel()
	{
		return Voxel();
	}

	// fixture: a map volume with a fully materialized chunk: a ground slab
	// and selectable vertical features
	struct CTestMap
	{
		CDummyPager pager;
		CMapBase map;

		CTestMap()
			: pager()
			, map()
		{
			map.initMap(&pager, 64ull * 1024 * 1024);

			// prefill the whole chunk with Air so no cell read ever sees
			// uninitialized pager memory, then lay the ground slab
			for (sint32 x = 0; x < ChunkSide; ++x)
				for (sint32 y = 0; y < ChunkSide; ++y)
					for (sint32 z = 0; z < ChunkSide; ++z)
						map.setVoxelAt(TVoxelPos(x, y, z), airVoxel());

			for (sint32 x = 0; x < ChunkSide; ++x)
				for (sint32 y = 0; y < ChunkSide; ++y)
					for (sint32 z = 0; z <= GroundTopZ; ++z)
						map.setVoxelAt(TVoxelPos(x, y, z), fullSolid());
		}

		static NLMISC::CVector world(float x, float y, float z)
		{
			return NLMISC::CVector(x, y, z);
		}
	};

	// vertical full wall of Raw at voxel x = WallX
	struct CWallMap : public CTestMap
	{
		const sint32 WallX;

		CWallMap()
			: WallX(32)
		{
			for (sint32 y = 0; y < ChunkSide; ++y)
				for (sint32 z = 0; z < ChunkSide; ++z)
					map.setVoxelAt(TVoxelPos(WallX, y, z), fullSolid());
		}
	};

	// a wall of Liquid at voxel x = WallX with a Raw wall further east at
	// voxel x = WallX + 8
	struct CLiquidCurtainMap : public CTestMap
	{
		const sint32 WallX;

		CLiquidCurtainMap()
			: WallX(32)
		{
			for (sint32 y = 0; y < ChunkSide; ++y)
				for (sint32 z = 0; z < ChunkSide; ++z)
				{
					map.setVoxelAt(TVoxelPos(WallX, y, z), fullLiquid());
					map.setVoxelAt(TVoxelPos(WallX + 8, y, z), fullSolid());
				}
		}
	};

	// a step: for column x >= StepX the mass is filled up to voxel z = StepTop
	struct CStepMap : public CTestMap
	{
		const sint32 StepX;
		const sint32 StepTop;

		CStepMap()
			: StepX(24)
			, StepTop(20)
		{
			for (sint32 x = StepX; x < ChunkSide; ++x)
				for (sint32 y = 0; y < ChunkSide; ++y)
					for (sint32 z = 0; z <= StepTop; ++z)
						map.setVoxelAt(TVoxelPos(x, y, z), fullSolid());
		}
	};

} // anonymous namespace

TEST(DynworldMapCollisionTest, IsBlockingVoxelFollowsTheTypeRule)
{
	// Air and Liquid never block, everything else does (blanket Object rule)
	EXPECT_FALSE(CMapBase::isBlockingVoxel(airVoxel()));
	EXPECT_FALSE(CMapBase::isBlockingVoxel(fullLiquid()));
	EXPECT_TRUE(CMapBase::isBlockingVoxel(fullSolid()));

	Voxel objectVoxel;
	objectVoxel.setType(Voxel::Object);
	objectVoxel.setSheetId(42);
	EXPECT_TRUE(CMapBase::isBlockingVoxel(objectVoxel));
}

TEST(DynworldMapCollisionTest, IsBlockingPositionRespectsTheRuleInVolume)
{
	CWallMap testMap;

	// in front of the wall: air does not block
	EXPECT_FALSE(testMap.map.isBlockingPosition(TLoadingSector::InvalidId, CTestMap::world(2.f, 2.f, 2.f)));
	// inside the wall voxel: blocked
	EXPECT_TRUE(testMap.map.isBlockingPosition(TLoadingSector::InvalidId, CTestMap::world(float(testMap.WallX) / InvVoxelWorld, 2.f, 2.f)));
}

TEST(DynworldMapCollisionTest, TraceSegmentSweepsThroughAirOnly)
{
	CTestMap testMap;

	CMapBase::TSegmentHit hit;
	const bool blocked = testMap.map.traceSegment(
		TLoadingSector::InvalidId,
		CTestMap::world(1.f, 1.f, 2.51f), // air over the slab
		CTestMap::world(15.f, 1.f, 2.51f),
		hit);

	EXPECT_FALSE(blocked);
	EXPECT_FALSE(hit.m_hit);
	EXPECT_FLOAT_EQ(1.f, hit.m_t); // convention: t = 1 means the whole segment was free
	EXPECT_TRUE(hit.m_faceNormal.isNull());
}

TEST(DynworldMapCollisionTest, TraceSegmentHitsTheFirstBlockingWallFace)
{
	CWallMap testMap;

	// straight march east into the wall face: start voxel x = 4, end voxel
	// x = 36, wall cell x = 32: the face normal opposes the traversal move
	CMapBase::TSegmentHit hit;
	const bool blocked = testMap.map.traceSegment(
		TLoadingSector::InvalidId,
		CTestMap::world(2.f, 2.f, 2.f),
		CTestMap::world(18.f, 2.f, 2.f),
		hit);

	EXPECT_TRUE(blocked);
	EXPECT_TRUE(hit.m_hit);

	// first blocking cell is the west face of the wall column
	EXPECT_EQ(testMap.WallX, hit.m_voxelPos.x);
	EXPECT_EQ(4, hit.m_voxelPos.y); // world y = 2 m = voxel 4
	EXPECT_EQ(4, hit.m_voxelPos.z); // world z = 2 m = voxel 4 (air above the slab)

	// t at the crossing into the wall cell
	EXPECT_FLOAT_EQ((float(testMap.WallX) / InvVoxelWorld - 2.f) / 16.f, hit.m_t);

	EXPECT_FLOAT_EQ(-1.f, hit.m_faceNormal.x);
	EXPECT_FLOAT_EQ(0.f, hit.m_faceNormal.y);
	EXPECT_FLOAT_EQ(0.f, hit.m_faceNormal.z);
}

TEST(DynworldMapCollisionTest, TraceSegmentStartsEmbeddedHitsAtOnce)
{
	CWallMap testMap;

	// the start position sits inside the wall cell itself
	CMapBase::TSegmentHit hit;
	const bool blocked = testMap.map.traceSegment(
		TLoadingSector::InvalidId,
		CTestMap::world(float(testMap.WallX + 0.5f) / InvVoxelWorld, 2.f, 2.f),
		CTestMap::world(18.f, 2.f, 2.f),
		hit);

	EXPECT_TRUE(blocked);
	EXPECT_FLOAT_EQ(0.f, hit.m_t);
	EXPECT_EQ(testMap.WallX, hit.m_voxelPos.x);
	EXPECT_TRUE(hit.m_faceNormal.isNull()); // no face: callers treat this specially
}

TEST(DynworldMapCollisionTest, LiquidNeverBlocksButSolidsBehindStillDo)
{
	CLiquidCurtainMap testMap;

	{ // straight through the liquid curtain: nothing blocks (the segment ends
	  // between the liquid wall and the far solid wall)
		CMapBase::TSegmentHit hit;
		EXPECT_FALSE(testMap.map.traceSegment(
			TLoadingSector::InvalidId,
			CTestMap::world(2.f, 2.f, 2.f),
			CTestMap::world(18.f, 2.f, 2.f),
			hit));
		EXPECT_FALSE(hit.m_hit);
	}

	{ // deeper movement reaches the Raw wall behind the liquid
		CMapBase::TSegmentHit hit;
		const bool blocked = testMap.map.traceSegment(
			TLoadingSector::InvalidId,
			CTestMap::world(2.f, 2.f, 2.f),
			CTestMap::world(25.f, 2.f, 2.f),
			hit);

		EXPECT_TRUE(blocked);
		EXPECT_EQ(testMap.WallX + 8, hit.m_voxelPos.x);

		// t at the crossing into the far wall cell
		EXPECT_FLOAT_EQ((float(testMap.WallX + 8) / InvVoxelWorld - 2.f) / 23.f, hit.m_t);
	}
}

TEST(DynworldMapCollisionTest, TraceSegmentHandlesTheVerticalAxis)
{
	CStepMap testMap;

	// descend from high air west of the step into the ground slab
	CMapBase::TSegmentHit hit;
	const bool blocked = testMap.map.traceSegment(
		TLoadingSector::InvalidId,
		CTestMap::world(5.75f, 2.f, 15.f),  // start voxel (11, 4, 30): air
		CTestMap::world(5.75f, 2.f, 1.25f), // end voxel (11, 4, 2): into the slab
		hit);

	EXPECT_TRUE(blocked);
	// first blocking cell is the slab top voxel itself (descended into it)
	EXPECT_EQ(GroundTopZ, hit.m_voxelPos.z);
	EXPECT_EQ(11, hit.m_voxelPos.x);
	EXPECT_EQ(4, hit.m_voxelPos.y);

	// z crossings happen at integer voxel-space heights: the crossing into
	// the slab occurs at z = 3.0, see the param below (segments end z = 2.5)
	EXPECT_FLOAT_EQ(0.f, hit.m_faceNormal.x);
	EXPECT_FLOAT_EQ(0.f, hit.m_faceNormal.y);
	EXPECT_FLOAT_EQ(1.f, hit.m_faceNormal.z); // opposes the downward move
}

TEST(DynworldMapCollisionTest, GroundNormalIsUpOnFlatGround)
{
	CTestMap testMap;

	// standing above the slab: descend to the ground top voxel, which on a
	// flat surface must yield the exact upward normal
	NLMISC::CVector normal;
	Voxel groundVoxel;
	EXPECT_TRUE(testMap.map.getGroundNormal(
		TLoadingSector::InvalidId,
		CTestMap::world(2.f, 2.f, 2.f),
		normal, &groundVoxel));

	EXPECT_EQ(Voxel::Raw, groundVoxel.getType());
	EXPECT_EQ(Voxel::MaxDensity, groundVoxel.getDensity());
	EXPECT_FLOAT_EQ(0.f, normal.x);
	EXPECT_FLOAT_EQ(0.f, normal.y);
	EXPECT_FLOAT_EQ(1.f, normal.z);
}

TEST(DynworldMapCollisionTest, GroundNormalPointsAwayFromAStairStep)
{
	CStepMap testMap;

	// standing on top of the step, directly above the step edge column
	// (voxel x = StepX): the ground voxel has flat ground below (dense) and
	// air above, but the drop-off lies one cell west (air one cell west at
	// the same z where mass continues east): the outward normal therefore
	// points up and west, away from the drop-off, at exactly 45 degrees
	NLMISC::CVector normal;
	EXPECT_TRUE(testMap.map.getGroundNormal(
		TLoadingSector::InvalidId,
		CTestMap::world(float(testMap.StepX) / InvVoxelWorld, 2.f, float(testMap.StepTop + 1) / InvVoxelWorld + VoxelWorld / 4),
		normal));

	EXPECT_NEAR(-1.f / sqrt(2.f), normal.x, 1e-4f);
	EXPECT_NEAR(0.f, normal.y, 1e-4f);
	EXPECT_NEAR(1.f / sqrt(2.f), normal.z, 1e-4f);
}

TEST(DynworldMapCollisionTest, GroundNormalSearchIsBounded)
{
	CTestMap testMap;

	// deep empty column: with a small explicit search bound the query must
	// report a miss instead of walking forever (or into paged garbage)
	NLMISC::CVector normal;
	EXPECT_FALSE(testMap.map.getGroundNormal(
		TLoadingSector::InvalidId,
		CTestMap::world(2.f, 2.f, 20.f),
		normal, nullptr, 10));
}

TEST(DynworldMapCollisionTest, GroundNormalOnDeepWaterStopsAtTheLiquidTop)
{
	CTestMap testMap;

	// fill a water column above the slab at voxel x = 12, z 3..10: descending
	// from above stops at the first non-Air voxel, which is the liquid top
	for (sint32 y = 0; y < ChunkSide; ++y)
		for (sint32 z = 3; z <= 10; ++z)
			testMap.map.setVoxelAt(TVoxelPos(12, y, z), fullLiquid());

	NLMISC::CVector normal;
	Voxel groundVoxel;
	EXPECT_TRUE(testMap.map.getGroundNormal(
		TLoadingSector::InvalidId,
		CTestMap::world(6.f, 2.f, 15.f), // voxel (12, 4, 30): air above the water
		normal, &groundVoxel));

	// the liquid top voxel is reported, like getMeanHeight walks onto it;
	// its flat surface still yields the upward normal
	EXPECT_EQ(Voxel::Liquid, groundVoxel.getType());
	EXPECT_EQ(Voxel::MaxDensity, groundVoxel.getDensity());
	EXPECT_FLOAT_EQ(0.f, normal.x);
	EXPECT_FLOAT_EQ(0.f, normal.y);
	EXPECT_FLOAT_EQ(1.f, normal.z);
}
