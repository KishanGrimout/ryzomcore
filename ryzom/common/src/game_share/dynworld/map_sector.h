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

#ifndef MAP_SECTOR_HEADER
#define MAP_SECTOR_HEADER

#include "nel/misc/vectori.h"
#include "polyvox_defs.h"

#define CONVERT_COORD_PACK(coordName, value) ((TId((value) + PositiveOffset##coordName) << Shift##coordName) & Mask##coordName)
#define CONVERT_COORD_UNPACK(coordName, value) (sint32(((value & Mask##coordName) >> Shift##coordName) - PositiveOffset##coordName))

typedef uint64 TGenericSectorId;

// There are 2 notions:
// - Region: a 3D zone of voxels, defined by a min and max, in voxel unit
// - Sector: an ID representing the position of a Region of a fixed size
// There are 3 units systems here:
// - world unit: in meters, a position in the 3D world
// - voxels unit: in number of voxels (typically 2 voxels per meter)
// - regions unit: in number of regions, used to identify regions
template <sint sidePow>
class CSector
{
	enum BitShift
	{
		SideSizePower = sidePow, // power of 2 of SideSize
		VoxelSizeShift = 1, // bit shift to convert between distance and voxel unit (eg. 1 is for 2 voxels per meter)
		SideSizePowerInVoxels = SideSizePower + VoxelSizeShift, // power of 2 of SideSize
	};

public:
	typedef TGenericSectorId TId;
	static const TId InvalidId = -1ULL;

	enum Size
	{
		SideSize = 1 << SideSizePower, // in distance unit (meters)
		HalfSideSize = SideSize / 2, // in distance unit (meters)
		SideSizeInVoxels = SideSize << VoxelSizeShift, // in number of voxels
		HalfSideSizeInVoxels = HalfSideSize << VoxelSizeShift, // in number of voxels
	};

	// Note that in this implementation, voxel size can be 1, 0.5, 0.25, etc. depending on VoxelSizeShift
	static const float VoxelSize;
	static const float InvVoxelSize;

	// static helpers
	// Get a region's ID (voxel space) 
	static TId getRegionId(const TRegion& region)
	{
		// Get center position
		sint32 x = region.getLowerX();
		sint32 y = region.getLowerY();
		sint32 z = region.getLowerZ();

		if (x < 0)
			x -= SideSizeInVoxels - 1;
		if (y < 0)
			y -= SideSizeInVoxels - 1;
		if (z < 0)
			z -= SideSizeInVoxels - 1;

		// back to destination sector coordinate
		x /= SideSizeInVoxels;
		y /= SideSizeInVoxels;
		z /= SideSizeInVoxels;

		return s_posToId(x, y, z);
	}

	// Build a region from ID
	static void getRegion(TId inId, TRegion& outRegion)
	{
		sint32 x, y, z;
		s_idToPos(inId, x, y, z);

		x *= SideSizeInVoxels;
		y *= SideSizeInVoxels;
		z *= SideSizeInVoxels;
		outRegion.setLowerX(x);
		outRegion.setLowerY(y);
		outRegion.setLowerZ(z);
		outRegion.setUpperX(x + SideSizeInVoxels - 1);
		outRegion.setUpperY(y + SideSizeInVoxels - 1);
		outRegion.setUpperZ(z + SideSizeInVoxels - 1);
	}

	// Convert region's position into ID (region space)
	static TId s_posToId(sint32 x, sint32 y, sint32 z)
	{
		TId result = s_packId(x, y, z);
		return result;
	}

	// Convert a world position into an ID
	static TId s_worldPosToId(sint32 x, sint32 y, sint32 z)
	{
		if (x < 0)
			x -= SideSize-1;
		if (y < 0)
			y -= SideSize-1;
		if (z < 0)
			z -= SideSize-1;

		// back to destination sector coordinate
		x /= SideSize;
		y /= SideSize;
		z /= SideSize;

		return s_posToId(x, y, z);
	}

	// Convert an ID to the region's position (region space)
	static void s_idToPos(TId inId, sint32& outX, sint32& outY, sint32& outZ)
	{
		s_unpackId(inId, outX, outY, outZ);
	}

	// Convert an ID into a world position
	static void s_idToWorldPos(TId inId, sint32& outX, sint32& outY, sint32& outZ)
	{
		s_idToPos(inId, outX, outY, outZ);

		// go to world coordinates
		outX *= SideSize;
		outY *= SideSize;
		outZ *= SideSize;
	}

	template <typename SectorType>
	static TId s_convertId(TGenericSectorId inId)
	{
		sint32 x, y, z;
		SectorType::s_idToWorldPos(inId, x, y, z);
		return s_worldPosToId(x, y, z);
	}

	// Convert world coordinates to sector coordinates
	static void s_worldToSector(const NLMISC::CVector& worldCoord, NLMISC::CVectorI& outSectorCoord)
	{
		float fx = worldCoord.x;
		float fy = worldCoord.y;
		float fz = worldCoord.z;
		float epsilon = std::numeric_limits<float>::epsilon();

		if (fx < 0.f)
			fx -= (SideSize - epsilon);
		if (fy < 0.f)
			fy -= (SideSize - epsilon);
		if (fz < 0.f)
			fz -= (SideSize - epsilon);

		outSectorCoord.x = fx / SideSize;
		outSectorCoord.y = fy / SideSize;
		outSectorCoord.z = fz / SideSize;
	}

private:
	// Coordinates to ID packing constants
	enum : uint64
	{
		// This mapping can be changed
		BitCountX = 22,
		BitCountY = 22,
		BitCountZ = 20,

		// These should make the id <-> coordinate working
		ShiftX = 0,
		ShiftY = BitCountX,
		ShiftZ = BitCountX + BitCountY,
		MaskX = -1ULL >> (64 - BitCountX),
		MaskZ = -1ULL << ShiftZ,
		MaskY = ~(MaskX | MaskZ),
		PositiveOffsetX = -1ULL >> (64 - BitCountX + 1),
		PositiveOffsetY = -1ULL >> (64 - BitCountY + 1),
		PositiveOffsetZ = -1ULL >> (64 - BitCountZ + 1),
	};

	static TId s_packId(sint32 x, sint32 y, sint32 z)
	{
		TId result = (CONVERT_COORD_PACK(Z, z) | CONVERT_COORD_PACK(Y, y) | CONVERT_COORD_PACK(X, x));
		nlassert(result != InvalidId);
		return result;
	}

	static void s_unpackId(TId inId, sint32& outX, sint32& outY, sint32& outZ)
	{
		nlassert(inId != InvalidId);
		outX = CONVERT_COORD_UNPACK(X, inId);
		outY = CONVERT_COORD_UNPACK(Y, inId);
		outZ = CONVERT_COORD_UNPACK(Z, inId);
	}
	friend void mapsectorUnittest();
};

template <sint sidePow>
const float CSector<sidePow>::VoxelSize = 1.f / float(1 << CSector<sidePow>::VoxelSizeShift);

template <sint sidePow>
const float CSector<sidePow>::InvVoxelSize = float(1 << CSector<sidePow>::VoxelSizeShift);

typedef CSector<5> TLoadingSector; // 64 m, 128 voxels side

template <int lod>
class TMeshSectorLod : public CSector<lod + 4> // (32 m, 64 voxels side) pow (lod index + 1)
{};

typedef TMeshSectorLod<0> TMeshSector;

typedef std::vector<TGenericSectorId> TSectorsList;

class CSectorLod
{
public:
	static void idToPos(uint lod, TGenericSectorId sectorId, NLMISC::CVectorI& outPos);
	static TGenericSectorId posToId(uint lod, const NLMISC::CVectorI& pos);
	static void idToWorldPos(uint lod, TGenericSectorId sectorId, NLMISC::CVectorI& outPos);
	static TGenericSectorId worldPosToId(uint lod, const NLMISC::CVectorI& pos);
	static float getSideSize(uint lod);
	static float getVoxelSize(uint lod);
	static void getRegion(uint lod, TGenericSectorId sectorId, TRegion& region);
	static TLoadingSector::TId convertId(uint lod, TGenericSectorId sectorId);
	static void getSubSectors(uint sectorLod, TGenericSectorId sectorId, TSectorsList& children);
	static TGenericSectorId getParentSector(uint sectorLod, TGenericSectorId sectorId);
};

enum { LOD_COUNT = 3 };

struct SectorMetadata
{
	enum ContentState
	{
		IsEmpty = 0,
		IsFull = 1,
		IsHeterogeneous = 2,
		HasObjects = 4
	};
	uint32 m_revision;
	uint8 m_contentState;
	SectorMetadata() : m_revision(0), m_contentState(IsEmpty) {}

	void serial(NLMISC::IStream	&f)
	{
		f.serial(m_revision);
		uint8 t = m_contentState;
		f.serial(t);
		if (f.isReading())
			m_contentState = t;
	}
};

void mapsectorUnittest();

#endif
