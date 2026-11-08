#include <gtest/gtest.h>

#include <cmath>

#include <game_share/dynworld/dyn_global_retriever.h>
#include <game_share/dynworld/map.h>

// Test suite for CDynGlobalRetriever::testCylinderMove, the real movement
// collision test added for GitHub issue #2 (phase B2 of
// docs/PLAYER_WALL_CLIFF_COLLISION_PLAN.md). It consumes the B1 CMapBase
// helpers (traceSegment / getGroundNormal / isBlockingVoxel) and returns
// PACS collision descs with CSurfaceIdent(-1,-1) so the move container's
// reaction path slides the primitive along the blocking face.
//
// Geometry conventions (voxel = 0.5 m; the 64^3 chunk spans 0..31.5 m):
// - ground slab: voxel z 0..2 filled with full-density Raw; the resulting
//   getMeanHeight stand z on flat ground is 1.5 m
// - the test entity stands on the ground (estimation z = stand z); its
//   feet cell is voxel z 3, and the body sweep cells are the three voxels
//   above it (z 4..6)
// moves in the tests are short, realistic per-tick displacements
// coordinates stay positive so voxel-space floor() is exact

namespace {

	// PolyVox chunk construction asserts a non-null pager even though it is
	// only contacted on pageIn/pageOut; provide a no-op one
	class CDummyPager : public TVolume::Pager
	{
	public:
		void pageIn(const TRegion &region, TVolume::Chunk *pChunk) override {}
		void pageOut(const TRegion &region, TVolume::Chunk *pChunk) override {}
	};

	const sint32 ChunkSide = TLoadingSector::SideSizeInVoxels; // 64
	const float VoxelWorld = TLoadingSector::VoxelSize; // 0.5 m
	const float InvVoxelWorld = TLoadingSector::InvVoxelSize; // 2 voxels per meter

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

	// fixture: a map volume with a fully materialized chunk holding a ground
	// slab; everything else is Air
	struct CTestMap
	{
		CDummyPager pager;
		CMapBase map;
		CDynGlobalRetriever retriever;

		CTestMap()
			: pager()
			, map()
			, retriever(nullptr, &map)
		{
			map.initMap(&pager, 64ull * 1024 * 1024);

			for (sint32 x = 0; x < ChunkSide; ++x)
				for (sint32 y = 0; y < ChunkSide; ++y)
					for (sint32 z = 0; z < ChunkSide; ++z)
						map.setVoxelAt(TVoxelPos(x, y, z), Voxel());

			for (sint32 x = 0; x < ChunkSide; ++x)
				for (sint32 y = 0; y < ChunkSide; ++y)
					for (sint32 z = 0; z <= GroundTopZ; ++z)
						map.setVoxelAt(TVoxelPos(x, y, z), fullSolid());
		}

		static const sint32 GroundTopZ = 2;
		static const float GroundStandZ;

		// a global position at (x,y) standing on the flat ground
		static NLPACS::UGlobalPosition posAt(float x, float y)
		{
			NLPACS::UGlobalPosition pos;
			pos.InstanceId = TLoadingSector::InvalidId;
			pos.LocalPosition.Surface = -1;
			pos.LocalPosition.Estimation = NLMISC::CVector(x, y, GroundStandZ);
			return pos;
		}
	};

	const float CTestMap::GroundStandZ = 1.5f;

	// vertical full wall of Raw at voxel x = WallX: a real wall face of any
	// height, well above the body sweep cells
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

	// a Liquid wall at voxel x = LiquidWallX with a Raw wall further east at
	// voxel x = LiquidWallX + 8
	struct CLiquidCurtainMap : public CTestMap
	{
		const sint32 LiquidWallX;

		CLiquidCurtainMap()
			: LiquidWallX(32)
		{
			for (sint32 y = 0; y < ChunkSide; ++y)
				for (sint32 z = 0; z < ChunkSide; ++z)
				{
					map.setVoxelAt(TVoxelPos(LiquidWallX, y, z), fullLiquid());
					map.setVoxelAt(TVoxelPos(LiquidWallX + 8, y, z), fullSolid());
				}
		}
	};

	// a bump/step: for columns x >= StepX the mass is filled up to
	// voxel z = GroundTopZ + ExtraVoxels (ExtraVoxels = 1 -> 1-voxel step,
	// 2 -> a one-metre face)
	struct CBumpMap : public CTestMap
	{
		const sint32 StepX;
		const sint32 ExtraVoxels;
		const sint32 StepTopZ;

		CBumpMap(sint32 extraVoxels)
			: StepX(24)
			, ExtraVoxels(extraVoxels)
			, StepTopZ(GroundTopZ + extraVoxels)
		{
			for (sint32 x = StepX; x < ChunkSide; ++x)
				for (sint32 y = 0; y < ChunkSide; ++y)
					for (sint32 z = 0; z <= StepTopZ; ++z)
						map.setVoxelAt(TVoxelPos(x, y, z), fullSolid());
		}
	};

	// a tall plateau east of voxel x = StepX, rising to voxel z = 20
	struct CStepMap : public CTestMap
	{
		const sint32 StepX;

		CStepMap()
			: StepX(24)
		{
			for (sint32 x = StepX; x < ChunkSide; ++x)
				for (sint32 y = 0; y < ChunkSide; ++y)
					for (sint32 z = 0; z <= 20; ++z)
						map.setVoxelAt(TVoxelPos(x, y, z), fullSolid());
		}
	};

	// a gentle downslope: east of voxel x = DipX the ground drops ONE voxel
	// (the top slab voxel is removed); the surface voxel west of the rim and
	// the dip floor voxels sit at half density like generated terrain does.
	// With the start z snapped onto the start column surface voxel base (the
	// previous accepted position), the start column enters getMeanHeight
	// through the climb-out path and the dip column through the descent path;
	// before both paths were unified the climb path read one voxel high and a
	// one-voxel descent was reported as a two-voxel drop (phantom block)
	struct CGentleDipMap : public CTestMap
	{
		static const sint32 DipX = 20; // world x = 10 m

		CGentleDipMap()
		{
			for (sint32 y = 0; y < ChunkSide; ++y)
			{
				// the start column (voxel x = DipX - 4): a partial surface
				// voxel, so the snapped stand z (its base) lies inside it
				Voxel rim = fullSolid();
				rim.setDensity(Voxel::MaxDensity / 2);
				map.setVoxelAt(TVoxelPos(DipX - 4, y, GroundTopZ), rim);

				// the dip: air above, half-density surface voxel below
				Voxel dipFloor = fullSolid();
				dipFloor.setDensity(Voxel::MaxDensity / 2);
				for (sint32 x = DipX; x < ChunkSide; ++x)
				{
					map.setVoxelAt(TVoxelPos(x, y, GroundTopZ), Voxel());
					map.setVoxelAt(TVoxelPos(x, y, GroundTopZ - 1), dipFloor);
				}
			}
		}
	};

	// fixture: a slab with a THIN marched-cubes surface band: the top slab
	// voxel sits at partial density (15%, a lattice sample just under
	// threshold) like generated terrain around its surface; pins how the
	// ground walk and the wall filter treat a partial surface voxel
	struct CPartialSheetMap : public CTestMap
	{
		static const sint32 SheetZ = 3; // the partial band voxel

		CPartialSheetMap()
		{
			for (sint32 y = 0; y < ChunkSide; ++y)
				for (sint32 x = 0; x < ChunkSide; ++x)
				{
					map.setVoxelAt(TVoxelPos(x, y, GroundTopZ + 1), fullSolid());
					Voxel sheet = fullSolid();
					sheet.setDensity(Voxel::MaxDensity * 15 / 100);
					map.setVoxelAt(TVoxelPos(x, y, GroundTopZ + 2), sheet);
				}
		}
	};

	// convenient accessors to the desc content
	const NLPACS::CCollisionSurfaceDesc &firstDesc(NLPACS::CCollisionSurfaceTemp &cst)
	{
		return *cst.CollisionDescs.begin();
	}

} // anonymous namespace

TEST(DynworldMoveCollisionTest, FlatSweepProducesNoCollisionDesc)
{
	CTestMap testMap;

	NLPACS::CCollisionSurfaceTemp cst;
	const NLPACS::TCollisionSurfaceDescVector *descs = testMap.retriever.testCylinderMove(
		CTestMap::posAt(10.f, 4.f),
		NLMISC::CVector(6.f, 0.f, 0.f),
		0.4f,
		cst);

	// the container contract: always the live desc vector, empty when free
	EXPECT_EQ(&cst.CollisionDescs, descs);
	ASSERT_TRUE(descs->empty());
}

TEST(DynworldMoveCollisionTest, WallColumnBlocksAtTheFaceWithANormal)
{
	CWallMap testMap;

	// march east into the full-height wall face: the face is at world
	// x = 16.0 m, contact at t = (16 - 12) / 8 = 0.5, face normal toward -x
	NLPACS::CCollisionSurfaceTemp cst;
	const NLPACS::TCollisionSurfaceDescVector *descs = testMap.retriever.testCylinderMove(
		CTestMap::posAt(12.f, 4.f),
		NLMISC::CVector(8.f, 0.f, 0.f),
		0.4f,
		cst);

	ASSERT_EQ(1, descs->size());
	const NLPACS::CCollisionSurfaceDesc &desc = firstDesc(cst);

	EXPECT_NEAR(0.5, desc.ContactTime, 1e-5);
	EXPECT_NEAR(-1.0, desc.ContactNormal.x, 1e-5);
	EXPECT_NEAR(0.0, desc.ContactNormal.y, 1e-5);
	EXPECT_NEAR(0.0, desc.ContactNormal.z, 1e-9);

	// unresolvable surface: the move container treats this as a wall
	EXPECT_EQ(-1, desc.ContactSurface.RetrieverInstanceId);
	EXPECT_EQ(-1, desc.ContactSurface.SurfaceId);
}

TEST(DynworldMoveCollisionTest, SingleVoxelBumpIsAutoStepped)
{
	CBumpMap testMap(1);

	// run onto the 1-voxel step (0.5 m): accepted without any desc
	NLPACS::CCollisionSurfaceTemp cst;
	const NLPACS::TCollisionSurfaceDescVector *descs = testMap.retriever.testCylinderMove(
		CTestMap::posAt(8.f, 4.f),
		NLMISC::CVector(8.f, 0.f, 0.f),
		0.4f,
		cst);

	EXPECT_EQ(&cst.CollisionDescs, descs);
	ASSERT_TRUE(descs->empty());

	// and the destination ground is the step top
	NLPACS::UGlobalPosition endPos;
	endPos.InstanceId = TLoadingSector::InvalidId;
	endPos.LocalPosition.Estimation = NLMISC::CVector(16.f, 4.f, CTestMap::GroundStandZ);
	EXPECT_FLOAT_EQ(2.f, testMap.retriever.getMeanHeight(endPos));
}

TEST(DynworldMoveCollisionTest, GentleOneVoxelDownslopeIsNotPhantomBlocked)
{
	CGentleDipMap testMap;

	// the start column holds the query z inside its (half-density) surface
	// voxel, the dip column is entered from air above: both must read the
	// same kind of ground, exactly one voxel apart (was two before the
	// getMeanHeight climb/descent paths were unified)
	NLPACS::UGlobalPosition startPos = CTestMap::posAt(8.f, 4.f);
	startPos.LocalPosition.Estimation.z = 1.0f;
	NLPACS::UGlobalPosition endPos(startPos);
	endPos.LocalPosition.Estimation.x += 2.f; // world x = 10 m: dip column
	EXPECT_FLOAT_EQ(1.0f, testMap.retriever.getMeanHeight(startPos));
	EXPECT_FLOAT_EQ(0.5f, testMap.retriever.getMeanHeight(endPos));

	// a one-voxel descent is walkable: no desc of any kind
	NLPACS::CCollisionSurfaceTemp cst;
	const NLPACS::TCollisionSurfaceDescVector *descs = testMap.retriever.testCylinderMove(
		startPos,
		NLMISC::CVector(2.f, 0.f, 0.f),
		0.4f,
		cst);

	EXPECT_EQ(&cst.CollisionDescs, descs);
	ASSERT_TRUE(descs->empty());
}

	// fixture: the ground slab / flat surface positioned at a far world
	// offset (-100 m, +90 m): a real nonzero sector id, with a negative x
	// sector, exercising the exact id <-> position conversion the client
	// runtime uses for its primitives (all near-origin tests use InvalidId,
	// which short-circuits those conversions)
	struct CFarSectorMap : public CTestMap
	{
		static const sint32 VoxelOffsetX = -201; // world x = -100.5 m (covers the stand at -100.3)
		static const sint32 VoxelOffsetY = 180;  // world y = +90 m
		static const sint32 Extent = 64;         // 32 m, one full sector

		CFarSectorMap()
		{
			for (sint32 x = VoxelOffsetX; x < VoxelOffsetX + Extent; ++x)
				for (sint32 y = VoxelOffsetY; y < VoxelOffsetY + Extent; ++y)
					for (sint32 z = 0; z <= GroundTopZ; ++z)
						map.setVoxelAt(TVoxelPos(x, y, z), fullSolid());
		}

		NLPACS::UGlobalPosition farPosAtWorld(float worldX, float worldY)
		{
			// build the pair the way CDynGlobalRetriever::retrievePosition
			// keeps UGlobalPosition: the Estimation is SECTOR-LOCAL
			// (world - sector origin), not raw world coordinates
			NLPACS::UGlobalPosition pos;
			pos.InstanceId = TLoadingSector::s_worldPosToId((sint32)floor(worldX), (sint32)floor(worldY), 0);
			pos.LocalPosition.Surface = -1;
			pos.LocalPosition.Estimation = map.getLocalPosition(pos.InstanceId, NLMISC::CVectorD(worldX, worldY, 0));
			return pos;
		}
	};

TEST(DynworldMoveCollisionTest, PartialSurfaceSheetDoesNotPhantomBlock)
{
	CPartialSheetMap testMap;

	// ground walk: the input z = GroundStandZ already sits in the full-solid
	// voxel 3 below the sheet; walking down from there the first full
	// non-Air voxel (d255) reports its top, floored back to 2.0: the sheet's
	// 15% density fraction is below one voxel and the floor drops it
	NLPACS::UGlobalPosition pos = CTestMap::posAt(8.f, 4.f);
	const float ground = testMap.retriever.getMeanHeight(pos);
	EXPECT_NEAR(2.0f, ground, 0.001f);

	// a flat sweep must not wall-block against the partial sheet voxel
	// sitting two voxels above the reported ground (the runtime shape that
	// fired wallBlocked on flat terrain in a client session)
	NLPACS::CCollisionSurfaceTemp cst;
	const NLPACS::TCollisionSurfaceDescVector *descs = testMap.retriever.testCylinderMove(
		pos,
		NLMISC::CVector(2.f, 0.f, 0.f),
		0.4f,
		cst);

	EXPECT_EQ(&cst.CollisionDescs, descs);
	ASSERT_TRUE(descs->empty());
}

TEST(DynworldMoveCollisionTest, FarNegativeSectorGroundIsWhereTheSamplesIt)
{
	CFarSectorMap testMap;

	// the id/position roundtrip must land the voxel lookups exactly where
	// the world position says, whatever the sector-index side
	NLPACS::UGlobalPosition worldPos = testMap.farPosAtWorld(-100.3f, 90.7f);

	// flat ground at a far negative sector: the stand z is the full slab
	// surface, like the near-origin reference
	EXPECT_FLOAT_EQ(1.5f, testMap.retriever.getMeanHeight(worldPos));

	// a flat sweep there stays free: the body sweep must never clip the
	// start column's own ground voxel through a mirrored sector cell
	NLPACS::CCollisionSurfaceTemp cst;
	const NLPACS::TCollisionSurfaceDescVector *descs = testMap.retriever.testCylinderMove(
		worldPos,
		NLMISC::CVector(2.f, 0.f, 0.f),
		0.4f,
		cst);

	EXPECT_EQ(&cst.CollisionDescs, descs);
	ASSERT_TRUE(descs->empty());
}

TEST(DynworldMoveCollisionTest, GentleFullVoxelDownslopeIsNotPhantomBlocked)
{
	CTestMap testMap;

	// a one-voxel descent on FULL-density ground (the runtime shape that
	// regressed when ground was read one voxel low): the endpoint ground is
	// the voxel base 1.0 while the surface voxel top is 1.5 — the descent is
	// walkable and the start column's own ground voxel must never be swept
	// as a wall
	NLPACS::UGlobalPosition startPos = CTestMap::posAt(8.f, 4.f); // z = 1.5
	NLPACS::UGlobalPosition endPos(startPos);
	endPos.LocalPosition.Estimation.x += 2.f; // world x = 10 m
	for (sint32 y = 0; y < ChunkSide; ++y)
		for (sint32 x = 20; x < ChunkSide; ++x)
			testMap.map.setVoxelAt(TVoxelPos(x, y, CTestMap::GroundTopZ), Voxel());

	// ground reads: top 1.5 at the start, base 1.0 in the dip
	EXPECT_FLOAT_EQ(1.5f, testMap.retriever.getMeanHeight(startPos));
	EXPECT_FLOAT_EQ(1.0f, testMap.retriever.getMeanHeight(endPos));

	NLPACS::CCollisionSurfaceTemp cst;
	const NLPACS::TCollisionSurfaceDescVector *descs = testMap.retriever.testCylinderMove(
		startPos,
		NLMISC::CVector(2.f, 0.f, 0.f),
		0.4f,
		cst);

	EXPECT_EQ(&cst.CollisionDescs, descs);
	ASSERT_TRUE(descs->empty());
}

TEST(DynworldMoveCollisionTest, OneMetreFaceBlocksARealisticTickMove)
{
	CBumpMap testMap(2);

	// a typical short per-tick move ending inside the 1 m face: destination
	// ground rises 1 m (> 0.4 * 1 + 0.5 auto step) -> ground-shape block
	NLPACS::CCollisionSurfaceTemp cst;
	const NLPACS::TCollisionSurfaceDescVector *descs = testMap.retriever.testCylinderMove(
		CTestMap::posAt(11.8f, 4.f),
		NLMISC::CVector(0.4f, 0.f, 0.f),
		0.4f,
		cst);

	ASSERT_EQ(1, descs->size());
	const NLPACS::CCollisionSurfaceDesc &desc = firstDesc(cst);

	// contact fires just before the end of the move, slide along the contour:
	// outward ground normal flattened to horizontal, facing -x
	EXPECT_NEAR(0.0, desc.ContactTime, 1e-5);
	EXPECT_NEAR(-1.0, desc.ContactNormal.x, 1e-5);
	EXPECT_NEAR(0.0, desc.ContactNormal.y, 1e-5);
	EXPECT_EQ(-1, desc.ContactSurface.RetrieverInstanceId);
	EXPECT_EQ(-1, desc.ContactSurface.SurfaceId);
}

TEST(DynworldMoveCollisionTest, CliffFaceBlocksBeforeTheTeleportOnTop)
{
	CStepMap testMap;

	// walk east into the plateau foot: 9 m of rise over a 0.4 m move
	NLPACS::CCollisionSurfaceTemp cst;
	const NLPACS::TCollisionSurfaceDescVector *descs = testMap.retriever.testCylinderMove(
		CTestMap::posAt(11.8f, 4.f),
		NLMISC::CVector(0.4f, 0.f, 0.f),
		0.4f,
		cst);

	ASSERT_EQ(1, descs->size());
	const NLPACS::CCollisionSurfaceDesc &desc = firstDesc(cst);

	EXPECT_NEAR(0.0, desc.ContactTime, 1e-5);
	EXPECT_NEAR(-1.0, desc.ContactNormal.x, 1e-5);
	EXPECT_NEAR(0.0, desc.ContactNormal.y, 1e-5);
}

TEST(DynworldMoveCollisionTest, LedgeEdgeDoesNotTriggerAFreeFall)
{
	CStepMap testMap;

	// stand on the plateau top near its west edge and walk west off it:
	// a 9 m drop must be refused, the slide is pushed back east (the ground
	// gradient there is flat, so the normal falls back to opposing the move)
	NLPACS::CCollisionSurfaceTemp cst;
	const NLPACS::TCollisionSurfaceDescVector *descs = testMap.retriever.testCylinderMove(
		CTestMap::posAt(12.1f, 4.f),
		NLMISC::CVector(-0.4f, 0.f, 0.f),
		0.4f,
		cst);

	ASSERT_EQ(1, descs->size());
	const NLPACS::CCollisionSurfaceDesc &desc = firstDesc(cst);

	EXPECT_NEAR(0.0, desc.ContactTime, 1e-5);
	EXPECT_NEAR(1.0, desc.ContactNormal.x, 1e-5);
	EXPECT_NEAR(0.0, desc.ContactNormal.y, 1e-5);
}

TEST(DynworldMoveCollisionTest, LiquidCurtainFreeWade)
{
	CLiquidCurtainMap testMap;

	// wade through the liquid curtain; the Raw wall behind (voxel x = 40,
	// world x = 20 m) stays beyond the move endpoints
	NLPACS::CCollisionSurfaceTemp cst;
	const NLPACS::TCollisionSurfaceDescVector *descs = testMap.retriever.testCylinderMove(
		CTestMap::posAt(14.f, 4.f),
		NLMISC::CVector(4.f, 0.f, 0.f),
		0.4f,
		cst);

	EXPECT_EQ(&cst.CollisionDescs, descs);
	EXPECT_TRUE(descs->empty());
}

TEST(DynworldMoveCollisionTest, LiquidCurtainSolidBehindStillBlocks)
{
	CLiquidCurtainMap testMap;

	// even straight into the curtain for several metres stops only at the
	// solid wall behind it
	NLPACS::CCollisionSurfaceTemp cst;
	const NLPACS::TCollisionSurfaceDescVector *descs = testMap.retriever.testCylinderMove(
		CTestMap::posAt(14.f, 4.f),
		NLMISC::CVector(9.f, 0.f, 0.f),
		0.4f,
		cst);

	// contact at the far Raw wall face: world x = 20.0 m, t = (20 - 14)/9
	ASSERT_EQ(1, descs->size());
	EXPECT_NEAR(6.f / 9.f, descs->begin()->ContactTime, 1e-5);
}

