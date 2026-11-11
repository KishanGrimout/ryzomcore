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

// Microvegetation placement geometry (ryzomcore issue #7).
//
// Worker-side placement engine for the stock NeL microvegetation system
// reused on the dynworld voxel terrain. It walks a decoded marching-cubes
// mesh and clusters the plantable triangles into spatial buckets; the
// main-thread commit phase (dyn_landscape, P2) resolves the tile-vegetable
// descriptors per (biome, material) and plants with
// CVegetable::generateGroup using each bucket as the generating "patch"
// (stock equivalent: CPatch::generateTileVegetable with a 16 m^2 tile).
//
// Invariants:
// - No NL3D call and no singleton/manager state inside computeVegetPlacements
//   (it reads only its arguments): safe to run on the mesh-extraction worker
//   thread. buildSoftSoilTable is the exception and must run on the main
//   thread (it reads the immutable-after-init CMaterialLibrary).
// - Deterministic: identical (mesh, soil table, origin, scale) inputs produce
//   bit-identical placement sets, so re-plants after voxel edits do not
//   reshuffle vegetation on unchanged surface (edit regions only change the
//   buckets they touch).
// - Positions are produced in WORLD units (see computeVegetPlacements);
//   the decoded mesh carries sector-local voxel units.

#ifndef VEGETATION_PLACEMENT_HEADER
#define VEGETATION_PLACEMENT_HEADER

#include "game_share/dynworld/polyvox_defs.h"

#include "nel/misc/vector.h"
#include "nel/misc/vector_2f.h"

#include <cstdint>
#include <vector>

namespace DYNWORLD
{

/// Bucket cell size in meters. Marching-cubes triangles are ~voxel scale
/// (~0.5 m edges, ~0.1 m^2), far below the stock 16 m^2 vegetable tile, so
/// per-triangle generateGroup would floor(density*area) to ~0 instances
/// almost everywhere (plan D3). Triangles are therefore clustered into
/// ~4 m^2 cells, which is the planting granularity of generateGroup.
const float VegetBucketCellSize = 2.0f;

/// Voxel biome is a 3-bit field: at most 8 biome encodings (see Voxel::Biome).
const uint VegetMaxBiomeCount = 8;

/// Soil-table entry meaning "this biome hosts no plantable material".
const uint8 VegetNoSoil = 0xFF;

/// One plantable-surface bucket: the geometry data the commit phase needs to
/// plant with CVegetable::generateGroup (Center is the noise/density seed,
/// Area the density driver, the triangle list the UV-to-position mapping
/// surface). Triangles are stored in mesh-index order.
struct CVegetBucket
{
	/// Quantized cell center in world XY (z = 0). Commit-time noise seed.
	NLMISC::CVector Center;
	/// Area-weighted average of the member triangle face normals (normalized).
	NLMISC::CVector Normal;
	/// Sum of the member triangle areas, in m^2. Equals sum(TriAreas).
	float Area;
	/// Member triangles: indices into the decoded mesh index array, value =
	/// index of the triangle's first corner (corner stride is 3).
	std::vector<uint32> Triangles;
	/// Per-member triangle area, parallel to Triangles.
	std::vector<float> TriAreas;
};

/// All plantable buckets of one (biome, material) soft-soil surface.
struct CVegetSurfaceGroup
{
	Voxel::BiomeType Biome;
	Voxel::MaterialType Material;
	/// Ordered by (cellX, cellY).
	std::vector<CVegetBucket> Buckets;
};

/// The complete placement geometry of one decoded mesh. Ordered by
/// (biome, material); only filter-passing groups are present.
struct CDynVegetPlacementSet
{
	std::vector<CVegetSurfaceGroup> Groups;

	void clear() { Groups.clear(); }
	bool empty() const { return Groups.empty(); }
};

/// Build the per-biome soft-soil table from the material library
/// environments: soilIds[biome] = the environment's default soil material id,
/// or VegetNoSoil when the biome id has no environment (mapgen's default-soil
/// rule, the D2 (a) filter). MAIN THREAD ONLY: reads CMaterialLibrary,
/// immutable after initialize().
void buildSoftSoilTable(uint8 (&soilIds)[VegetMaxBiomeCount]);

/// Compute the placement geometry set of a decoded marching-cubes mesh.
/// A triangle is plantable iff all three corners share the same (biome,
/// material) pair, the voxel type is Raw, and the material equals the
/// biome's soft-soil table entry (all-3-corners rule: boundary facets between
/// soil and rock/water host no vegetation and cannot be double-planted).
/// \param mesh decoded mesh (sector-local voxel units, as extracted)
/// \param soilIds per-biome soft-soil table (see buildSoftSoilTable)
/// \param worldOrigin world position of the mesh origin (sector origin)
/// \param scale voxel size of the mesh LOD (mesh-local unit -> world meters)
/// \param out cleared and filled; WORKER-SAFE (pure data, no singletons)
void computeVegetPlacements(const TDecodedMesh& mesh, const uint8 (&soilIds)[VegetMaxBiomeCount],
	const NLMISC::CVector& worldOrigin, float scale, CDynVegetPlacementSet& out);

/// Map one instance UV (from CVegetable::generateGroup, [0..1]^2) onto the
/// bucket's triangle surface: a deterministic hash of the UV pair selects a
/// member triangle area-proportionally, then the standard unit-square
/// barycentric fold positions the instance inside it. Deterministic given
/// (mesh, bucket, uv). \param outPos world position on the facet;
/// \param outNormal the landing triangle's face normal (orientation basis).
void vegetBucketResolvePosition(const TDecodedMesh& mesh, const CVegetBucket& bucket,
	const NLMISC::CVector2f& uv, const NLMISC::CVector& worldOrigin, float scale,
	NLMISC::CVector& outPos, NLMISC::CVector& outNormal);

} // namespace DYNWORLD

#endif
