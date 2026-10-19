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

#include "map_sector.h"

using namespace NLMISC;

void CSectorLod::idToPos(uint lod, TGenericSectorId sectorId, CVectorI& outPos)
{
	switch (lod)
	{
	case 0:
		TMeshSectorLod<0>::s_idToPos(sectorId, outPos.x, outPos.y, outPos.z);
		break;
	case 1:
		TMeshSectorLod<1>::s_idToPos(sectorId, outPos.x, outPos.y, outPos.z);
		break;
	case 2:
		TMeshSectorLod<2>::s_idToPos(sectorId, outPos.x, outPos.y, outPos.z);
		break;
	default:
		nlwarning("Invalid lod");
	}
}

TGenericSectorId CSectorLod::posToId(uint lod, const CVectorI& pos)
{
	switch (lod)
	{
	case 0:
		return TMeshSectorLod<0>::s_posToId(pos.x, pos.y, pos.z);
	case 1:
		return TMeshSectorLod<1>::s_posToId(pos.x, pos.y, pos.z);
	case 2:
		return TMeshSectorLod<2>::s_posToId(pos.x, pos.y, pos.z);
	default:
		nlwarning("Invalid lod");
		return TLoadingSector::InvalidId;
	}
}

void CSectorLod::idToWorldPos(uint lod, TGenericSectorId sectorId, NLMISC::CVectorI& outPos)
{
	switch (lod)
	{
	case 0:
		TMeshSectorLod<0>::s_idToWorldPos(sectorId, outPos.x, outPos.y, outPos.z);
		break;
	case 1:
		TMeshSectorLod<1>::s_idToWorldPos(sectorId, outPos.x, outPos.y, outPos.z);
		break;
	case 2:
		TMeshSectorLod<2>::s_idToWorldPos(sectorId, outPos.x, outPos.y, outPos.z);
		break;
	default:
		nlwarning("Invalid lod");
	}
}

TGenericSectorId CSectorLod::worldPosToId(uint lod, const NLMISC::CVectorI& pos)
{
	switch (lod)
	{
	case 0:
		return TMeshSectorLod<0>::s_worldPosToId(pos.x, pos.y, pos.z);
	case 1:
		return TMeshSectorLod<1>::s_worldPosToId(pos.x, pos.y, pos.z);
	case 2:
		return TMeshSectorLod<2>::s_worldPosToId(pos.x, pos.y, pos.z);
	default:
		nlwarning("Invalid lod");
		return TLoadingSector::InvalidId;
	}
}

float CSectorLod::getSideSize(uint lod)
{
	switch (lod)
	{
	case 0:
		return TMeshSectorLod<0>::SideSize;
	case 1:
		return TMeshSectorLod<1>::SideSize;
	case 2:
		return TMeshSectorLod<2>::SideSize;
	default:
		nlwarning("Invalid lod");
		return 1.f;
	}
}

float CSectorLod::getVoxelSize(uint lod)
{
	switch (lod)
	{
	case 0:
		return TMeshSectorLod<0>::VoxelSize;
	case 1:
		return TMeshSectorLod<1>::VoxelSize;
	case 2:
		return TMeshSectorLod<2>::VoxelSize;
	default:
		nlwarning("Invalid lod");
		return 1.f;
	}
}

void CSectorLod::getRegion(uint lod, TGenericSectorId sectorId, TRegion& region)
{
	switch (lod)
	{
	case 0:
		TMeshSectorLod<0>::getRegion(sectorId, region);
		break;
	case 1:
		TMeshSectorLod<1>::getRegion(sectorId, region);
		break;
	case 2:
		TMeshSectorLod<2>::getRegion(sectorId, region);
		break;
	default:
		nlwarning("Bad lod region used");
		break;
	}
}

TLoadingSector::TId CSectorLod::convertId(uint lod, TGenericSectorId sectorId)
{
	switch (lod)
	{
	case 0:
		return TLoadingSector::s_convertId<TMeshSectorLod<0>>(sectorId);
	case 1:
		return TLoadingSector::s_convertId<TMeshSectorLod<1>>(sectorId);
	case 2:
		return TLoadingSector::s_convertId<TMeshSectorLod<2>>(sectorId);
	default:
		nlwarning("Bad lod sector used");
		return TLoadingSector::InvalidId;
	}
}

void CSectorLod::getSubSectors(uint sectorLod, TGenericSectorId sectorId, TSectorsList& outChildren)
{
	CVectorI parentPos;
	idToPos(sectorLod, sectorId, parentPos);

	// create first coordinates of lower LOD
	parentPos += parentPos;

	outChildren.reserve(outChildren.size() + 8);

	for (uint i = 0; i < 2; ++i)
	{
		for (uint j = 0; j < 2; ++j)
		{
			for (uint k = 0; k < 2; ++k)
			{
				CVectorI childPos(parentPos.x + i, parentPos.y + j, parentPos.z + k);
				TGenericSectorId childId = posToId(sectorLod - 1, childPos);
				nlassert(sectorId == getParentSector(sectorLod - 1, childId));
				outChildren.push_back(childId);
			}
		}
	}
}

TGenericSectorId CSectorLod::getParentSector(uint sectorLod, TGenericSectorId sectorId)
{
	CVectorI pos;
	idToPos(sectorLod, sectorId, pos);

	if (pos.x < 0)
		--pos.x;
	if (pos.y < 0)
		--pos.y;
	if (pos.z < 0)
		--pos.z;

	pos.x /= 2;
	pos.y /= 2;
	pos.z /= 2;

	return posToId(sectorLod + 1, pos);
}

void mapsectorUnittest()
{
	sint32 x, y, z;
	const sint32 side = TMeshSector::SideSize;

	TGenericSectorId sid = TMeshSector::s_packId(0, 0, 0);
	nlassert(0x7ffff7ffffdfffffULL == sid);
	TMeshSector::s_unpackId(sid, x, y, z);
	nlassert(x == 0 && y == 0 && z == 0);

	sid = TMeshSector::s_packId(1, 1, 1);
	nlassert(0x8000080000200000 == sid);
	TMeshSector::s_unpackId(sid, x, y, z);
	nlassert(x == 1 && y == 1 && z == 1);

	sid = TMeshSector::s_packId(-1, -1, -1);
	nlassert(0x7fffe7ffff9ffffe == sid);
	TMeshSector::s_unpackId(sid, x, y, z);
	nlassert(x == -1 && y == -1 && z == -1);

	sid = TMeshSector::s_worldPosToId(0, 0, 0);
	nlassert(sid == TMeshSector::s_packId(0, 0, 0));
	TMeshSector::s_idToWorldPos(sid, x, y, z);
	nlassert(x == 0 && y == 0 && z == 0);

	sid = TMeshSector::s_worldPosToId(side - 1, side - 1, side - 1);
	nlassert(sid == TMeshSector::s_packId(0, 0, 0));
	TMeshSector::s_idToWorldPos(sid, x, y, z);
	nlassert(x == 0 && y == 0 && z == 0);

	sid = TMeshSector::s_worldPosToId(side, side, side);
	nlassert(sid == TMeshSector::s_packId(1, 1, 1));
	TMeshSector::s_idToWorldPos(sid, x, y, z);
	nlassert(x == side && y == side && z == side);

	sid = TMeshSector::s_worldPosToId(-side, -side, -side);
	nlassert(sid == TMeshSector::s_packId(-1, -1, -1));
	TMeshSector::s_idToWorldPos(sid, x, y, z);
	nlassert(x == -side && y == -side && z == -side);

	sid = TMeshSector::s_worldPosToId(-side + 1, -side + 1, -side + 1);
	nlassert(sid == TMeshSector::s_packId(-1, -1, -1));
	TMeshSector::s_idToWorldPos(sid, x, y, z);
	nlassert(x == -side && y == -side && z == -side);

	sid = TMeshSector::s_worldPosToId(-side - 1, -side - 1, -side - 1);
	nlassert(sid == TMeshSector::s_packId(-2, -2, -2));
	TMeshSector::s_idToWorldPos(sid, x, y, z);
	nlassert(x == -side * 2 && y == -side * 2 && z == -side * 2);
}
