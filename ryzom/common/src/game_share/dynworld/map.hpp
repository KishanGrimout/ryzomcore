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


template <typename SectorType>
void CMapBase::selectSectors(const NLMISC::CAABBox &bbox, TSectorsList& sectorsOut, uint startRadius /*= 0*/) const
{
	sectorsOut.clear();

	NLMISC::CVectorI sectorMin, sectorMax, sectorCenter, sectorExtent;
	SectorType::s_worldToSector(bbox.getMin(), sectorMin);
	SectorType::s_worldToSector(bbox.getMax(), sectorMax);
	sectorCenter = sectorMax + sectorMin;
	sectorCenter.x /= 2;
	sectorCenter.y /= 2;
	sectorCenter.z /= 2;
	sectorExtent = sectorMax - sectorMin;
	sint maxRadius = std::max(sectorExtent.x, std::max(sectorExtent.y, sectorExtent.z)) / 2;

	for (sint radius = startRadius; radius <= maxRadius; ++radius)
	{
		sint minX = sectorCenter.x - radius;
		sint minY = sectorCenter.y - radius;
		sint minZ = sectorCenter.z - radius;
		sint maxX = sectorCenter.x + radius;
		sint maxY = sectorCenter.y + radius;
		sint maxZ = sectorCenter.z + radius;
		for (sint i = minX; i <= maxX; ++i)
		{
			for (sint j = minY; j <= maxY; ++j)
			{
				for (sint k = minZ; k <= maxZ; ++k)
				{
					if (((i == minX || i == maxX) && maxX <= sectorMax.x)
						|| ((j == minY || j == maxY) && maxY <= sectorMax.y)
						|| ((k == minZ || k == maxZ) && maxZ <= sectorMax.z))
					{
						typename SectorType::TId sectorId = SectorType::s_posToId(i, j, k);

						sectorsOut.push_back(sectorId);

#ifndef NL_NO_DEBUG
						// this is kind of a unit test of the conversion posToId/idToPos
						sint testX, testY, testZ;
						SectorType::s_idToPos(sectorId, testX, testY, testZ);
						nlassert(testX == i && testY == j && testZ == k);
#endif
					}
				}
			}
		}
	}
}

template <typename SectorType>
void CMapBase::selectSectors(const NLMISC::CVector& center, uint maxRadius, TSectorsList& sectorsOut, uint startRadius /*= 0*/) const
{
	sectorsOut.clear();

	NLMISC::CVectorI sectorCenter, extent(maxRadius, maxRadius, maxRadius);
	SectorType::s_worldToSector(center, sectorCenter);

	NLMISC::CVectorI sectorMax(sectorCenter + extent);

	for (sint radius = startRadius; radius <= maxRadius; ++radius)
	{
		sint minX = sectorCenter.x - radius;
		sint minY = sectorCenter.y - radius;
		sint minZ = sectorCenter.z - radius;
		sint maxX = sectorCenter.x + radius;
		sint maxY = sectorCenter.y + radius;
		sint maxZ = sectorCenter.z + radius;
		for (sint i = minX; i <= maxX; ++i)
		{
			for (sint j = minY; j <= maxY; ++j)
			{
				for (sint k = minZ; k <= maxZ; ++k)
				{
					if (((i == minX || i == maxX) && maxX <= sectorMax.x)
						|| ((j == minY || j == maxY) && maxY <= sectorMax.y)
						|| ((k == minZ || k == maxZ) && maxZ <= sectorMax.z))
					{
						typename SectorType::TId sectorId = SectorType::s_posToId(i, j, k);

						sectorsOut.push_back(sectorId);

#ifndef NL_NO_DEBUG
						// this is kind of a unit test of the conversion posToId/idToPos
						sint testX, testY, testZ;
						SectorType::s_idToPos(sectorId, testX, testY, testZ);
						nlassert(testX == i && testY == j && testZ == k);
#endif
					}
				}
			}
		}
	}
}

template <typename SectorType>
void CMapBase::selectSectorsNoOrder(const NLMISC::CAABBox &bbox, TSectorsList& sectorsOut) const
{
	sectorsOut.clear();

	NLMISC::CVectorI sectorMin, sectorMax, sectorCenter, sectorExtent;
	SectorType::s_worldToSector(bbox.getMin(), sectorMin);
	SectorType::s_worldToSector(bbox.getMax(), sectorMax);

	// use "<=" instead of "<" to actually select a sector when the bbox is a point
	for (sint i = sectorMin.x; i <= sectorMax.x; ++i)
	{
		for (sint j = sectorMin.y; j <= sectorMax.y; ++j)
		{
			for (sint k = sectorMin.z; k <= sectorMax.z; ++k)
			{
				typename SectorType::TId sectorId = SectorType::s_posToId(i, j, k);

				sectorsOut.push_back(sectorId);

#ifndef NL_NO_DEBUG
				// this is kind of a unit test of the conversion posToId/idToPos
				sint testX, testY, testZ;
				SectorType::s_idToPos(sectorId, testX, testY, testZ);
				nlassert(testX == i && testY == j && testZ == k);
#endif
			}
		}
	}
}

template <typename SectorType, typename LoadedContainer, typename It2IdFunc, typename FindFunc>
void CMapBase::diffSectors(const LoadedContainer& loaded, const TSectorsList& input, TSectorsList& added, TSectorsList& removed, It2IdFunc getIdFunc, FindFunc findFunc)
{
	TSectorsList inputLoaded;

	// separate loaded and not loaded sectors from input
	// loaded ones go to 'inputLoaded', others go to 'added' list
	for (uint i = 0; i < input.size(); ++i)
	{
		typename SectorType::TId sectorId = input[i];
		typename LoadedContainer::const_iterator it = findFunc(loaded, sectorId);
		if (it == loaded.end())
		{
			added.push_back(sectorId);
		}
		else
		{
			inputLoaded.push_back(sectorId);
		}
	}

	// to fill 'removed' list, look for the loaded sectors not in intersection list
	TSectorsList::const_iterator beginInt = inputLoaded.begin(), endInt = inputLoaded.end();
	for (typename LoadedContainer::const_iterator it = loaded.begin(); it != loaded.end(); ++it)
	{
		TSectorsList::const_iterator elemIt = std::find(beginInt, endInt, getIdFunc(it));
		if (elemIt == endInt)
		{
			removed.push_back(getIdFunc(it));
		}
	}
}
