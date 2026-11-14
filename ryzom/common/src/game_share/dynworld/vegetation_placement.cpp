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

#include "vegetation_placement.h"

#include "material_library.h"
#include "environment_definition.h"

#include <map>

namespace DYNWORLD
{

using namespace NLMISC;

// ****************************************************************************
void buildSoftSoilTable(uint8 (&soilIds)[VegetMaxBiomeCount])
{
	for (uint i = 0; i < VegetMaxBiomeCount; ++i)
		soilIds[i] = VegetNoSoil;

	const CMaterialLibrary& library = CMaterialLibrary::instance();
	const uint maxEnvId = library.getMaxEnvironmentId();
	for (uint envId = 0; envId <= maxEnvId; ++envId)
	{
		if (envId >= VegetMaxBiomeCount)
			break; // voxel biome is a 3-bit field; extra env ids cannot occur
		if (!library.isValidEnvironment(envId))
			continue;
		const CEnvironmentDefinition* envDef = library.getEnvironmentDef(envId);
		if (envDef == nullptr)
			continue;
		soilIds[envId] = (uint8)envDef->getDefaultSoilId();
	}
}

// ****************************************************************************
namespace
{

/// Degenerate-triangle cutoff (m^2). Marching-cubes slivers below this are
/// skipped: they host no visible surface and would pollute buckets.
const float VegetMinTriArea = 1e-6f;

struct TCellKey
{
	sint32 X;
	sint32 Y;
	bool operator<(const TCellKey& other) const
	{
		return (X != other.X) ? (X < other.X) : (Y < other.Y);
	}
};

/// Per-(biome, material) accumulator; flattened into CVegetSurfaceGroup at
/// the end so output ordering is deterministic.
struct TGroupAccum
{
	std::map<TCellKey, CVegetBucket> Cells;
};

inline TCellKey cellKeyOf(float worldX, float worldY)
{
	// plain floorf: thread-safe (OptFastFloor is NOT: process-global FPU CW)
	TCellKey key;
	key.X = (sint32)floorf(worldX / VegetBucketCellSize);
	key.Y = (sint32)floorf(worldY / VegetBucketCellSize);
	return key;
}

inline void crossProduct(const CVector& a, const CVector& b, CVector& out)
{
	out = a ^ b;
}

} // namespace

// ****************************************************************************
void computeVegetPlacements(const TDecodedMesh& mesh, const uint8 (&soilIds)[VegetMaxBiomeCount],
	const CVector& worldOrigin, float scale, CDynVegetPlacementSet& out)
{
	out.clear();

	const sint nbIndices = mesh.getNoOfIndices();
	if (nbIndices < 3)
		return;

	// ordered maps keep group/bucket/triangle ordering deterministic
	std::map<uint16, TGroupAccum> groupAccums;

	for (sint idx = 0; idx + 2 < nbIndices; idx += 3)
	{
		const uint32 i0 = mesh.getIndex(idx);
		const uint32 i1 = mesh.getIndex(idx + 1);
		const uint32 i2 = mesh.getIndex(idx + 2);
		const TDecodedVertex& v0 = mesh.getVertex(i0);
		const TDecodedVertex& v1 = mesh.getVertex(i1);
		const TDecodedVertex& v2 = mesh.getVertex(i2);

		// all-3-corners rule: the facet must be one single raw material.
		// NB: check type BEFORE getMaterialAndBiome() — it asserts Raw.
		const TVoxel& m0 = v0.data;
		if (m0.getType() != TVoxel::Raw)
			continue;
		if (v1.data.getType() != TVoxel::Raw || v1.data.getMaterialAndBiome() != m0.getMaterialAndBiome())
			continue;
		if (v2.data.getType() != TVoxel::Raw || v2.data.getMaterialAndBiome() != m0.getMaterialAndBiome())
			continue;

		const Voxel::BiomeType biome = m0.getBiome();
		const Voxel::MaterialType material = m0.getMaterial();
		if (biome >= VegetMaxBiomeCount)
			continue;
		if (soilIds[biome] != material)
			continue;

		// world-space facet geometry
		const CVector p0 = worldOrigin + ToNeLVectorF(v0.position) * scale;
		const CVector p1 = worldOrigin + ToNeLVectorF(v1.position) * scale;
		const CVector p2 = worldOrigin + ToNeLVectorF(v2.position) * scale;

		// face normal from winding (PolyVox MC triangles face outward)
		CVector normal;
		crossProduct(p1 - p0, p2 - p0, normal);
		const float area2 = normal.norm();
		const float area = 0.5f * area2;
		if (area <= VegetMinTriArea)
			continue;
		normal /= area2;

		const uint16 groupKey = (uint16)((biome << 8) | material);
		TGroupAccum& group = groupAccums[groupKey];

		// bucket by the facet's XY centroid footprint
		const float centroidX = (p0.x + p1.x + p2.x) / 3.f;
		const float centroidY = (p0.y + p1.y + p2.y) / 3.f;
		const TCellKey cellKey = cellKeyOf(centroidX, centroidY);
		CVegetBucket& bucket = group.Cells[cellKey];

		if (bucket.TriAreas.empty())
		{
			bucket.Center.set((cellKey.X + 0.5f) * VegetBucketCellSize,
				(cellKey.Y + 0.5f) * VegetBucketCellSize, 0.f);
			bucket.SurfaceCenter.set(0.f, 0.f, 0.f);
		}
		bucket.TriAreas.push_back(area);
		bucket.TriCorners.push_back(p0);
		bucket.TriCorners.push_back(p1);
		bucket.TriCorners.push_back(p2);
		bucket.Area += area;
		bucket.Normal += normal * area;
		// area-weighted centroid accumulation: sum(area * facetCentroid)
		// = sum over corners of (area/3 * corner); finalized below.
		bucket.SurfaceCenter += (p0 + p1 + p2) * (area / 3.f);
	}

	// flatten, normalizing bucket normals
	out.Groups.reserve(groupAccums.size());
	for (auto& groupIt : groupAccums)
	{
		CVegetSurfaceGroup surfaceGroup;
		surfaceGroup.Biome = (Voxel::BiomeType)(groupIt.first >> 8);
		surfaceGroup.Material = (Voxel::MaterialType)(groupIt.first & 0xFF);
		surfaceGroup.Buckets.reserve(groupIt.second.Cells.size());
		for (auto& cellIt : groupIt.second.Cells)
		{
			CVegetBucket& bucket = cellIt.second;
			if (bucket.Normal.norm() > 0.f)
				bucket.Normal.normalize();
			else
				bucket.Normal.set(0.f, 0.f, 1.f); // degenerate: all normals cancelled out
			if (bucket.Area > 0.f)
				bucket.SurfaceCenter /= bucket.Area;
			surfaceGroup.Buckets.push_back(bucket);
		}
		out.Groups.push_back(surfaceGroup);
	}
}

// ****************************************************************************
void vegetBucketResolvePosition(const CVegetBucket& bucket, const CVector2f& uv,
	CVector& outPos, CVector& outNormal)
{
	nlassert(bucket.TriAreas.size() * 3 == bucket.TriCorners.size());

	// deterministic area-proportional triangle selection from the UV pair
	uint32 h = (uint32)(uv.x * 16777216.f);
	h ^= (uint32)(uv.y * 16777216.f) * 0x9E3779B9u;
	h ^= h >> 16;
	h *= 0x735A2D97u;
	h ^= h >> 15;
	h *= 0xC2B2AE35u;
	h ^= h >> 16;
	const float r = (h & 0x00FFFFFF) * (1.f / 16777216.f);

	// area CDF walk (member triangles are in mesh-index order)
	float acc = 0.f;
	uint triIdx = 0;
	const uint nbTris = (uint)bucket.TriAreas.size();
	const float rTarget = r * bucket.Area;
	for (; triIdx + 1 < nbTris; ++triIdx)
	{
		acc += bucket.TriAreas[triIdx];
		if (acc > rTarget)
			break;
	}

	// folded barycentric position inside the selected triangle
	const CVector* tri = &bucket.TriCorners[triIdx * 3];
	const CVector& p0 = tri[0];
	const CVector& p1 = tri[1];
	const CVector& p2 = tri[2];

	float s = uv.x;
	float t = uv.y;
	if (s + t > 1.f)
	{
		s = 1.f - s;
		t = 1.f - t;
	}

	outPos = p0 + (p1 - p0) * s + (p2 - p0) * t;

	// face normal from winding (same rule as the placement walk)
	CVector normal;
	crossProduct(p1 - p0, p2 - p0, normal);
	if (normal.norm() > 0.f)
		normal.normalize();
	else
		normal.set(0.f, 0.f, 1.f);
	outNormal = normal;
}

} // namespace DYNWORLD
