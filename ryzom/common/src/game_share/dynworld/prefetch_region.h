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

#ifndef PREFETCH_REGION_HEADER
#define PREFETCH_REGION_HEADER

#include <cmath>

#include "nel/misc/vector.h"

#include "game_share/dynworld/polyvox_defs.h"
#include "game_share/dynworld/map_sector.h"

// Hot-region helpers for the voxel prefetch worker
// (docs/VoxelVolumePaging.md § 6.7).
//
// Positions are in METERS (world space, the frame CDynGlobalRetriever uses);
// the returned TRegion bounds are in GLOBAL VOXEL coordinates (what the pager
// and traceSegment speak). Kept header-only and free of CMapBase so the region
// math is unit-testable without a volume.

// Two hot regions at most this far apart (one chunk, 64 voxels = 32 m) are
// prefetched as a single union: separate requests would page in almost the
// same cells twice, and the union costs one pass instead of two.
constexpr sint32 PrefetchMergeMarginVoxels = 64;

// Region covering +/- halfSizeMeters around positionMeters, shifted along
// velocityMps * leadSeconds so the prefetch worker starts on the chunks the
// hot point is heading towards instead of the ones it is leaving. The lead is
// clamped per axis to the box half-size: the box may be dragged by at most its
// own extent, so a teleport or a spike cannot fling the prefetch across the
// world and thrash the chunk limit.
inline TRegion makePrefetchRegion(const NLMISC::CVector &positionMeters,
	const NLMISC::CVector &velocityMps,
	const NLMISC::CVector &halfSizeMeters,
	float leadSeconds)
{
	NLMISC::CVector lead = velocityMps * leadSeconds;
	if (lead.x > halfSizeMeters.x) lead.x = halfSizeMeters.x;
	else if (lead.x < -halfSizeMeters.x) lead.x = -halfSizeMeters.x;
	if (lead.y > halfSizeMeters.y) lead.y = halfSizeMeters.y;
	else if (lead.y < -halfSizeMeters.y) lead.y = -halfSizeMeters.y;
	if (lead.z > halfSizeMeters.z) lead.z = halfSizeMeters.z;
	else if (lead.z < -halfSizeMeters.z) lead.z = -halfSizeMeters.z;

	const NLMISC::CVector center = positionMeters + lead;
	const NLMISC::CVector lo = center - halfSizeMeters;
	const NLMISC::CVector hi = center + halfSizeMeters;

	// floor per axis (the CVectorI truncating conversion samples the
	// neighbouring column in the negative quadrants)
	return TRegion(
		(sint32)floor(lo.x * TLoadingSector::InvVoxelSize),
		(sint32)floor(lo.y * TLoadingSector::InvVoxelSize),
		(sint32)floor(lo.z * TLoadingSector::InvVoxelSize),
		(sint32)floor(hi.x * TLoadingSector::InvVoxelSize),
		(sint32)floor(hi.y * TLoadingSector::InvVoxelSize),
		(sint32)floor(hi.z * TLoadingSector::InvVoxelSize));
}

// True when two prefetch regions overlap or are at most marginVoxels apart on
// every axis: near hot points (camera and player in third-person view) share
// one prefetch pass instead of two.
inline bool prefetchRegionsNear(const TRegion &a, const TRegion &b, sint32 marginVoxels)
{
	return a.getLowerX() <= b.getUpperX() + marginVoxels && b.getLowerX() <= a.getUpperX() + marginVoxels
		&& a.getLowerY() <= b.getUpperY() + marginVoxels && b.getLowerY() <= a.getUpperY() + marginVoxels
		&& a.getLowerZ() <= b.getUpperZ() + marginVoxels && b.getLowerZ() <= a.getUpperZ() + marginVoxels;
}

// Smallest region containing both (used when the two hot points are near).
inline TRegion prefetchUnion(const TRegion &a, const TRegion &b)
{
	return TRegion(
		(a.getLowerX() < b.getLowerX()) ? a.getLowerX() : b.getLowerX(),
		(a.getLowerY() < b.getLowerY()) ? a.getLowerY() : b.getLowerY(),
		(a.getLowerZ() < b.getLowerZ()) ? a.getLowerZ() : b.getLowerZ(),
		(a.getUpperX() > b.getUpperX()) ? a.getUpperX() : b.getUpperX(),
		(a.getUpperY() > b.getUpperY()) ? a.getUpperY() : b.getUpperY(),
		(a.getUpperZ() > b.getUpperZ()) ? a.getUpperZ() : b.getUpperZ());
}

#endif // PREFETCH_REGION_HEADER
