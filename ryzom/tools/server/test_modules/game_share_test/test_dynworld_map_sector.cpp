#include <gtest/gtest.h>

#include <set>

#include <game_share/dynworld/map_sector.h>

namespace {

	// The sector constants are defined by formulas, not by the (stale) comments
	// in the header: VoxelSizeShift = 1 is fixed, so a CSector<sidePow> always
	// measures SideSize = 1 << sidePow meters and SideSizeInVoxels = 2 * SideSize.
	//
	// TMeshSector = TMeshSectorLod<0> = CSector<4>  -> 16 m side, 32 voxels side
	// TLoadingSector = CSector<5>                   -> 32 m side, 64 voxels side

	const sint32 MeshSideSize = TMeshSector::SideSize;
	const sint32 LoadingSideSize = TLoadingSector::SideSize;

} // anonymous namespace

// Test suite for the CSector<sidePow> template (id packing, world<->sector id
// conversions) and the CSectorLod lod-dispatch helpers - ported from the legacy
// dynworld fork's mapsectorUnittest() plus coverage of the formula-based sizes.
// NOTE(dynworld): s_packId()/s_unpackId() are private and only reachable by the
// friend mapsectorUnittest() hook; s_posToId()/s_idToPos() are the public and
// momentarily identical wrappers used below to assert the same values.
TEST(DynworldMapSectorTest, LegacyUnittestPackingValues)
{
	// NOTE(dynworld): exact legacy mapsectorUnittest() expectations
	// (map_sector.cpp: readers should never "fix" these to a new mapping)
	sint32 x, y, z;

	TGenericSectorId sid = TMeshSector::s_posToId(0, 0, 0);
	EXPECT_EQ(0x7ffff7ffffdfffffULL, sid);
	TMeshSector::s_idToPos(sid, x, y, z);
	EXPECT_EQ(0, x);
	EXPECT_EQ(0, y);
	EXPECT_EQ(0, z);

	sid = TMeshSector::s_posToId(1, 1, 1);
	EXPECT_EQ(0x8000080000200000ULL, sid);
	TMeshSector::s_idToPos(sid, x, y, z);
	EXPECT_EQ(1, x);
	EXPECT_EQ(1, y);
	EXPECT_EQ(1, z);

	sid = TMeshSector::s_posToId(-1, -1, -1);
	EXPECT_EQ(0x7fffe7ffff9ffffeULL, sid);
	TMeshSector::s_idToPos(sid, x, y, z);
	EXPECT_EQ(-1, x);
	EXPECT_EQ(-1, y);
	EXPECT_EQ(-1, z);
}

TEST(DynworldMapSectorTest, LegacyUnittestWorldPosToIdBoundaries)
{
	// NOTE(dynworld): exact legacy mapsectorUnittest() world boundary cases
	sint32 x, y, z;
	const sint32 side = MeshSideSize;

	TGenericSectorId sid = TMeshSector::s_worldPosToId(0, 0, 0);
	EXPECT_EQ(TMeshSector::s_posToId(0, 0, 0), sid);
	TMeshSector::s_idToWorldPos(sid, x, y, z);
	EXPECT_EQ(0, x);
	EXPECT_EQ(0, y);
	EXPECT_EQ(0, z);

	sid = TMeshSector::s_worldPosToId(side - 1, side - 1, side - 1);
	EXPECT_EQ(TMeshSector::s_posToId(0, 0, 0), sid);
	TMeshSector::s_idToWorldPos(sid, x, y, z);
	EXPECT_EQ(0, x);
	EXPECT_EQ(0, y);
	EXPECT_EQ(0, z);

	sid = TMeshSector::s_worldPosToId(side, side, side);
	EXPECT_EQ(TMeshSector::s_posToId(1, 1, 1), sid);
	TMeshSector::s_idToWorldPos(sid, x, y, z);
	EXPECT_EQ(side, x);
	EXPECT_EQ(side, y);
	EXPECT_EQ(side, z);

	sid = TMeshSector::s_worldPosToId(-side, -side, -side);
	EXPECT_EQ(TMeshSector::s_posToId(-1, -1, -1), sid);
	TMeshSector::s_idToWorldPos(sid, x, y, z);
	EXPECT_EQ(-side, x);
	EXPECT_EQ(-side, y);
	EXPECT_EQ(-side, z);

	sid = TMeshSector::s_worldPosToId(-side + 1, -side + 1, -side + 1);
	EXPECT_EQ(TMeshSector::s_posToId(-1, -1, -1), sid);
	TMeshSector::s_idToWorldPos(sid, x, y, z);
	EXPECT_EQ(-side, x);
	EXPECT_EQ(-side, y);
	EXPECT_EQ(-side, z);

	// The last sector below the origin belongs to the next negative sector
	sid = TMeshSector::s_worldPosToId(-side - 1, -side - 1, -side - 1);
	EXPECT_EQ(TMeshSector::s_posToId(-2, -2, -2), sid);
	TMeshSector::s_idToWorldPos(sid, x, y, z);
	EXPECT_EQ(-side * 2, x);
	EXPECT_EQ(-side * 2, y);
	EXPECT_EQ(-side * 2, z);
}

TEST(DynworldMapSectorTest, WorldPosToIdFloorsNegativeCoordinates)
{
	// Negative positions floor toward -infinite: the whole range
	// [-side, -1] belongs to the sector -1 (implemented with a
	// conditional offset because C++ division truncates toward zero)
	for (sint32 c = -1; c >= -MeshSideSize; --c)
	{
		EXPECT_EQ(TMeshSector::s_posToId(-1, -1, -1), TMeshSector::s_worldPosToId(c, c, c))
		    << "world coordinate " << c << " must land in sector -1";
	}
}

TEST(DynworldMapSectorTest, IdPackingRoundTripsAcrossCoordinateRange)
{
	// The packed fields allow x/y in [-2097151, 2097152] but only z in
	// [-524287, 524288] (20-bit field with a 19-bit positive offset):
	// loop over safely within-range coordinates here
	const sint32 coordinates[] = { -500000, -MeshSideSize - 1, -1, 0, 1,
	    MeshSideSize, MeshSideSize + 1, 500000 };

	for (sint32 coord : coordinates)
	{
		sint32 x, y, z;

		TGenericSectorId sid = TMeshSector::s_posToId(coord, coord, -coord);
		TMeshSector::s_idToPos(sid, x, y, z);
		EXPECT_EQ(coord, x) << "coord " << coord;
		EXPECT_EQ(coord, y) << "coord " << coord;
		EXPECT_EQ(-coord, z) << "coord " << coord;

		// World-space round trip keeps the sector position stable
		TGenericSectorId fromWorld = TMeshSector::s_worldPosToId(
		    coord * MeshSideSize,
		    coord * MeshSideSize,
		    -coord * MeshSideSize);
		EXPECT_EQ(sid, fromWorld) << "coord " << coord;
	}

	// Extreme corners of the id coding itself
	sint32 x, y, z;
	TGenericSectorId sid = TMeshSector::s_posToId(2097151, -2097151, 0);
	TMeshSector::s_idToPos(sid, x, y, z);
	EXPECT_EQ(2097151, x);
	EXPECT_EQ(-2097151, y);
	EXPECT_EQ(0, z);

	sid = TMeshSector::s_posToId(0, 0, 524287);
	TMeshSector::s_idToPos(sid, x, y, z);
	EXPECT_EQ(0, x);
	EXPECT_EQ(0, y);
	EXPECT_EQ(524287, z);

	sid = TMeshSector::s_posToId(0, 0, -524287);
	TMeshSector::s_idToPos(sid, x, y, z);
	EXPECT_EQ(0, x);
	EXPECT_EQ(0, y);
	EXPECT_EQ(-524287, z);
}

TEST(DynworldMapSectorTest, SectorConstantsFollowFormulas)
{
	// Header comments claim 64 m/128 voxels for TLoadingSector, but the
	// implemented formulas define the sizes below - tests pin the formulas
	EXPECT_EQ(16, (int)TMeshSector::SideSize);
	EXPECT_EQ(8, (int)TMeshSector::HalfSideSize);
	EXPECT_EQ(32, (int)TMeshSector::SideSizeInVoxels);
	EXPECT_EQ(16, (int)TMeshSector::HalfSideSizeInVoxels);

	EXPECT_EQ(32, (int)TLoadingSector::SideSize);
	EXPECT_EQ(16, (int)TLoadingSector::HalfSideSize);
	EXPECT_EQ(64, (int)TLoadingSector::SideSizeInVoxels);
	EXPECT_EQ(32, (int)TLoadingSector::HalfSideSizeInVoxels);

	// VoxelSizeShift is 1 for every instantiation: 2 voxels per meter
	EXPECT_FLOAT_EQ(0.5f, TMeshSector::VoxelSize);
	EXPECT_FLOAT_EQ(2.f, TMeshSector::InvVoxelSize);
	EXPECT_FLOAT_EQ(0.5f, TLoadingSector::VoxelSize);
	EXPECT_FLOAT_EQ(2.f, TLoadingSector::InvVoxelSize);
}

TEST(DynworldMapSectorTest, GetRegionIdFloorsOnRegionLowerCorner)
{
	// getRegionId() only looks at the region's lower corner and floors it
	// into the enclosing loading sector (in voxel units)
	TRegion region(0, 0, 0, 63, 63, 63);
	EXPECT_EQ(TLoadingSector::s_posToId(0, 0, 0), TLoadingSector::getRegionId(region));

	region = TRegion(64, 64, 64, 127, 127, 127);
	EXPECT_EQ(TLoadingSector::s_posToId(1, 1, 1), TLoadingSector::getRegionId(region));

	// Any lower corner inside a negative sector works with the same floor
	region = TRegion(-64, -64, -64, -1, -1, -1);
	EXPECT_EQ(TLoadingSector::s_posToId(-1, -1, -1), TLoadingSector::getRegionId(region));

	region = TRegion(-64, 0, 0, -1, 63, 63);
	EXPECT_EQ(TLoadingSector::s_posToId(-1, 0, 0), TLoadingSector::getRegionId(region));

	// One voxel past a sector boundary in negative space moves to sector -2
	region = TRegion(-65, 0, 0, -65, 63, 63);
	EXPECT_EQ(TLoadingSector::s_posToId(-2, 0, 0), TLoadingSector::getRegionId(region));
}

TEST(DynworldMapSectorTest, GetRegionBuildsCanonicalBoundsFromId)
{
	TRegion region;
	TLoadingSector::getRegion(TLoadingSector::s_posToId(2, -1, 3), region);

	EXPECT_EQ(2 * TLoadingSector::SideSizeInVoxels, region.getLowerX());
	EXPECT_EQ(-1 * TLoadingSector::SideSizeInVoxels, region.getLowerY());
	EXPECT_EQ(3 * TLoadingSector::SideSizeInVoxels, region.getLowerZ());

	// The region spans exactly one sector of voxels, upper corner included
	EXPECT_EQ(region.getLowerX() + TLoadingSector::SideSizeInVoxels - 1, region.getUpperX());
	EXPECT_EQ(region.getLowerY() + TLoadingSector::SideSizeInVoxels - 1, region.getUpperY());
	EXPECT_EQ(region.getLowerZ() + TLoadingSector::SideSizeInVoxels - 1, region.getUpperZ());
}

TEST(DynworldMapSectorTest, SectorLodScalesSizesByLod)
{
	// TMeshSectorLod<lod> = CSector<lod + 4>: lod 0 is 16 m, lod 1 is 32 m, lod 2 is 64 m
	EXPECT_FLOAT_EQ(16.f, CSectorLod::getSideSize(0));
	EXPECT_FLOAT_EQ(32.f, CSectorLod::getSideSize(1));
	EXPECT_FLOAT_EQ(64.f, CSectorLod::getSideSize(2));

	// Two voxels per meter whatever the lod
	EXPECT_FLOAT_EQ(0.5f, CSectorLod::getVoxelSize(0));
	EXPECT_FLOAT_EQ(0.5f, CSectorLod::getVoxelSize(1));
	EXPECT_FLOAT_EQ(0.5f, CSectorLod::getVoxelSize(2));
}

TEST(DynworldMapSectorTest, SectorLodConvertsIdsBetweenLods)
{
	// A lod 0 sector id maps to its enclosing loading sector id when the world
	// positions align: sector (2, 2, 2) @ 16 m = world (32, 32, 32) = sector
	// (1, 1, 1) @ 32 m (TLoadingSector)
	TGenericSectorId meshId = TMeshSectorLod<0>::s_posToId(2, 2, 2);
	EXPECT_EQ(TLoadingSector::s_posToId(1, 1, 1), CSectorLod::convertId(0, meshId));

	// A lod 2 sector (64 m) maps up to a finer lod 0 (16 m): sector (1, 1, 1)
	// @ 64 m = world (64, 64, 64) = sector (2, 2, 2) @ 32 m TLoadingSector
	TGenericSectorId lod2Id = TMeshSectorLod<2>::s_posToId(1, 1, 1);
	EXPECT_EQ(TLoadingSector::s_posToId(2, 2, 2), CSectorLod::convertId(2, lod2Id));

	// The conversion round trips through the world position: the reverse
	// direction (loading -> mesh sector) uses the public static of the
	// target sector (CSectorLod::convertId only goes toward TLoadingSector)
	TGenericSectorId back = TMeshSector::s_convertId<TLoadingSector>(TLoadingSector::s_posToId(1, 1, 1));
	EXPECT_EQ(TMeshSector::s_posToId(2, 2, 2), back);
}

TEST(DynworldMapSectorTest, SectorLodSubSectorsCarryParentOwnership)
{
	TGenericSectorId parent = TMeshSectorLod<1>::s_posToId(1, 1, 1);
	TSectorsList children;
	CSectorLod::getSubSectors(1, parent, children);

	EXPECT_EQ(8u, children.size());

	// The 8 children are exactly the lod 0 sectors around the doubled parent
	// position and each one knows the parent back
	std::set<TGenericSectorId> unique;
	NLMISC::CVectorI parentPos;
	CSectorLod::idToPos(1, parent, parentPos);
	EXPECT_EQ(1, parentPos.x);
	EXPECT_EQ(1, parentPos.y);
	EXPECT_EQ(1, parentPos.z);

	for (TGenericSectorId child : children)
	{
		EXPECT_EQ(parent, CSectorLod::getParentSector(0, child));
		unique.insert(child);
	}
	EXPECT_EQ(8u, unique.size());
}
