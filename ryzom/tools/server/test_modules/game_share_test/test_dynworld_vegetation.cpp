// Unit tests for the dynworld microvegetation placement engine
// (ryzomcore issue #7, plan P1). Tests the pure-data placement geometry:
// soft-soil filtering (all-3-corners rule), spatial bucket clustering,
// deterministic output, origin/scale transform, UV-to-position mapping, and
// the commit-phase assumption that CVegetable::generateGroup instance counts
// scale exactly with (bucket area x density).

#include <gtest/gtest.h>

#include "game_share/dynworld/vegetation_placement.h"
#include "game_share/dynworld/voxel.h"

#include "nel/3d/vegetable.h"
#include "nel/3d/vegetable_manager.h"
#include "nel/misc/fast_floor.h"

#include <cmath>

// NeL context for CPath/log singletons (vegetable shape lookup touches CPath)
static NLMISC::CApplicationContext vegetApplicationContext;

using namespace NLMISC;
using namespace DYNWORLD;

namespace
{

// test material ids (arbitrary; the soil table below binds them)
const Voxel::MaterialType SoilMaterial = 5;
const Voxel::MaterialType RockMaterial = 9;

// soil table: biome 0 plants on SoilMaterial, biome 1 on nothing
void makeSoilTable(uint8 (&soilIds)[VegetMaxBiomeCount])
{
	for (uint i = 0; i < VegetMaxBiomeCount; ++i)
		soilIds[i] = VegetNoSoil;
	soilIds[0] = SoilMaterial;
}

Voxel makeRawVoxel(Voxel::BiomeType biome, Voxel::MaterialType material, Voxel::DensityType density = 255)
{
	Voxel v;
	v.setType(Voxel::Raw);
	v.setBiome(biome);
	v.setMaterial(material);
	v.setDensity(density);
	return v;
}

Voxel makeWaterVoxel(Voxel::DensityType density = 255)
{
	Voxel v;
	v.setType(Voxel::Liquid);
	v.setLiquidType(Voxel::Water);
	v.setDensity(density);
	return v;
}

Voxel makeObjectVoxel()
{
	Voxel v;
	v.setType(Voxel::Object);
	return v;
}

TDecodedVertex makeDecodedVertex(float x, float y, float z, const Voxel& voxel)
{
	TDecodedVertex vertex;
	vertex.position = PolyVox::Vector3DFloat(x, y, z);
	vertex.normal = PolyVox::Vector3DFloat(0.f, 0.f, 1.f);
	vertex.data = voxel;
	return vertex;
}

/// Add a flat unit quad [x0..x1]x[y0..y1] at height z, all corners carrying
/// the same voxel, wound CCW seen from +Z (face normal up).
void addQuad(TDecodedMesh& mesh, float x0, float y0, float x1, float y1, const Voxel& voxel, float z = 0.f)
{
	const uint32 i00 = mesh.addVertex(makeDecodedVertex(x0, y0, z, voxel));
	const uint32 i10 = mesh.addVertex(makeDecodedVertex(x1, y0, z, voxel));
	const uint32 i11 = mesh.addVertex(makeDecodedVertex(x1, y1, z, voxel));
	const uint32 i01 = mesh.addVertex(makeDecodedVertex(x0, y1, z, voxel));
	mesh.addTriangle(i00, i10, i11);
	mesh.addTriangle(i00, i11, i01);
}

/// A flat square soil surface spanning [0..side]^2 (16 m^2 for side=4).
void addSoilSurface(TDecodedMesh& mesh, uint quadsPerSide, const Voxel& voxel, float z = 0.f)
{
	for (uint gy = 0; gy < quadsPerSide; ++gy)
		for (uint gx = 0; gx < quadsPerSide; ++gx)
			addQuad(mesh, float(gx), float(gy), float(gx + 1), float(gy + 1), voxel, z);
}

/// Sum of all member triangle areas across the set.
float totalArea(const CDynVegetPlacementSet& set)
{
	float sum = 0.f;
	for (const CVegetSurfaceGroup& group : set.Groups)
		for (const CVegetBucket& bucket : group.Buckets)
			sum += bucket.Area;
	return sum;
}

/// Point-in-triangle test with barycentric coordinates (small epsilon).
bool pointOnTriangle(const CVector& p, const CVector& p0, const CVector& p1, const CVector& p2)
{
	const CVector e1 = p1 - p0;
	const CVector e2 = p2 - p0;
	const CVector n = e1 ^ e2;
	const float nn = n.norm();
	if (nn < 1e-12f)
		return false;
	const float dist = fabs(((p - p0) * n) / nn);
	if (dist > 1e-4f)
		return false; // not in the triangle plane
	const float d11 = e1 * e1, d12 = e1 * e2, d22 = e2 * e2;
	const CVector q = p - p0;
	const float d31 = q * e1, d32 = q * e2;
	const float denom = d11 * d22 - d12 * d12;
	if (fabs(denom) < 1e-12f)
		return false;
	const float s = (d22 * d31 - d12 * d32) / denom;
	const float t = (d11 * d32 - d12 * d31) / denom;
	return s >= -1e-5f && t >= -1e-5f && (s + t) <= 1.f + 1e-5f;
}

} // namespace

// ****************************************************************************
// computeVegetPlacements

TEST(DynworldVegetationPlacementTest, EmptyMeshYieldsEmptySet)
{
	TDecodedMesh mesh;
	uint8 soilIds[VegetMaxBiomeCount];
	makeSoilTable(soilIds);

	CDynVegetPlacementSet set;
	computeVegetPlacements(mesh, soilIds, CVector::Null, 1.f, set);
	EXPECT_TRUE(set.empty());
	EXPECT_TRUE(set.Groups.empty());
}

TEST(DynworldVegetationPlacementTest, SoilSurfaceBuildsPlacementSet)
{
	TDecodedMesh mesh;
	addSoilSurface(mesh, 4, makeRawVoxel(0, SoilMaterial)); // 4x4 m, 32 triangles
	uint8 soilIds[VegetMaxBiomeCount];
	makeSoilTable(soilIds);

	CDynVegetPlacementSet set;
	computeVegetPlacements(mesh, soilIds, CVector::Null, 1.f, set);

	ASSERT_EQ(set.Groups.size(), 1u);
	const CVegetSurfaceGroup& group = set.Groups[0];
	EXPECT_EQ(group.Biome, 0);
	EXPECT_EQ(group.Material, SoilMaterial);

	// 16 m^2 of surface, clustered in 2 m cells -> 4 buckets
	ASSERT_EQ(group.Buckets.size(), 4u);

	float sumTriAreas = 0.f;
	for (const CVegetBucket& bucket : group.Buckets)
	{
		ASSERT_EQ(bucket.Triangles.size(), bucket.TriAreas.size());
		// centers quantized on the 2 m cell grid
		EXPECT_FLOAT_EQ(bucket.Center.x, (floorf(bucket.Center.x / VegetBucketCellSize) + 0.5f) * VegetBucketCellSize);
		EXPECT_FLOAT_EQ(bucket.Center.y, (floorf(bucket.Center.y / VegetBucketCellSize) + 0.5f) * VegetBucketCellSize);
		EXPECT_FLOAT_EQ(bucket.Center.z, 0.f);
		// flat ground: area-weighted normal points straight up
		EXPECT_NEAR(bucket.Normal.x, 0.f, 1e-4f);
		EXPECT_NEAR(bucket.Normal.y, 0.f, 1e-4f);
		EXPECT_FLOAT_EQ(bucket.Normal.z, 1.f);
		// every member triangle's centroid falls inside the bucket's cell
		for (uint tri = 0; tri < bucket.Triangles.size(); ++tri)
		{
			const uint32 firstCorner = bucket.Triangles[tri];
			const CVector p0 = ToNeLVectorF(mesh.getVertex(mesh.getIndex(firstCorner)).position);
			const CVector p1 = ToNeLVectorF(mesh.getVertex(mesh.getIndex(firstCorner + 1)).position);
			const CVector p2 = ToNeLVectorF(mesh.getVertex(mesh.getIndex(firstCorner + 2)).position);
			const float cx = (p0.x + p1.x + p2.x) / 3.f;
			const float cy = (p0.y + p1.y + p2.y) / 3.f;
			EXPECT_NEAR(cx, bucket.Center.x, VegetBucketCellSize * 0.5f);
			EXPECT_NEAR(cy, bucket.Center.y, VegetBucketCellSize * 0.5f);
			sumTriAreas += bucket.TriAreas[tri];
		}
	}

	EXPECT_NEAR(sumTriAreas, 16.f, 1e-4f);
	EXPECT_NEAR(totalArea(set), 16.f, 1e-4f);
}

TEST(DynworldVegetationPlacementTest, NonSoilMaterialExcluded)
{
	TDecodedMesh mesh;
	addSoilSurface(mesh, 4, makeRawVoxel(0, RockMaterial)); // rock, not soil
	uint8 soilIds[VegetMaxBiomeCount];
	makeSoilTable(soilIds);

	CDynVegetPlacementSet set;
	computeVegetPlacements(mesh, soilIds, CVector::Null, 1.f, set);
	EXPECT_TRUE(set.empty());
}

TEST(DynworldVegetationPlacementTest, NoSoilBiomeExcluded)
{
	TDecodedMesh mesh;
	addSoilSurface(mesh, 2, makeRawVoxel(1, SoilMaterial)); // biome 1 has no soil
	uint8 soilIds[VegetMaxBiomeCount];
	makeSoilTable(soilIds);

	CDynVegetPlacementSet set;
	computeVegetPlacements(mesh, soilIds, CVector::Null, 1.f, set);
	EXPECT_TRUE(set.empty());
}

TEST(DynworldVegetationPlacementTest, NoInstancesOnLiquidOrObjectTops)
{
	// water top surface at z=0 and soil surface at z=1 in the same mesh
	TDecodedMesh mesh;
	addSoilSurface(mesh, 2, makeWaterVoxel());
	addSoilSurface(mesh, 2, makeRawVoxel(0, SoilMaterial), 1.f);
	addSoilSurface(mesh, 2, makeObjectVoxel(), 2.f);
	uint8 soilIds[VegetMaxBiomeCount];
	makeSoilTable(soilIds);

	CDynVegetPlacementSet set;
	computeVegetPlacements(mesh, soilIds, CVector::Null, 1.f, set);

	// only the soil group survives, with exactly its 8 triangles / 4 m^2
	ASSERT_EQ(set.Groups.size(), 1u);
	EXPECT_EQ(set.Groups[0].Material, SoilMaterial);
	EXPECT_NEAR(totalArea(set), 4.f, 1e-4f);
	uint triCount = 0;
	for (const CVegetBucket& bucket : set.Groups[0].Buckets)
		triCount += (uint)bucket.Triangles.size();
	EXPECT_EQ(triCount, 8u);
}

TEST(DynworldVegetationPlacementTest, MixedMaterialFacetExcluded)
{
	// one clean soil triangle + one facet straddling soil and rock
	TDecodedMesh mesh;
	const Voxel soil = makeRawVoxel(0, SoilMaterial);
	const Voxel rock = makeRawVoxel(0, RockMaterial);
	const uint32 i0 = mesh.addVertex(makeDecodedVertex(0.f, 0.f, 0.f, soil));
	const uint32 i1 = mesh.addVertex(makeDecodedVertex(1.f, 0.f, 0.f, soil));
	const uint32 i2 = mesh.addVertex(makeDecodedVertex(1.f, 1.f, 0.f, soil));
	const uint32 i3 = mesh.addVertex(makeDecodedVertex(0.f, 1.f, 0.f, rock)); // mixed corner
	mesh.addTriangle(i0, i1, i2);  // clean soil facet
	mesh.addTriangle(i0, i2, i3);  // mixed facet: must not host vegetation
	uint8 soilIds[VegetMaxBiomeCount];
	makeSoilTable(soilIds);

	CDynVegetPlacementSet set;
	computeVegetPlacements(mesh, soilIds, CVector::Null, 1.f, set);

	ASSERT_EQ(set.Groups.size(), 1u);
	ASSERT_EQ(set.Groups[0].Buckets.size(), 1u);
	const CVegetBucket& bucket = set.Groups[0].Buckets[0];
	ASSERT_EQ(bucket.Triangles.size(), 1u);
	EXPECT_NEAR(bucket.Area, 0.5f, 1e-4f); // only the clean facet
}

TEST(DynworldVegetationPlacementTest, Determinism)
{
	TDecodedMesh mesh;
	addSoilSurface(mesh, 4, makeRawVoxel(0, SoilMaterial));
	addSoilSurface(mesh, 2, makeRawVoxel(0, SoilMaterial), 1.f); // second level, same soil
	uint8 soilIds[VegetMaxBiomeCount];
	makeSoilTable(soilIds);

	CDynVegetPlacementSet setA, setB;
	computeVegetPlacements(mesh, soilIds, CVector::Null, 1.f, setA);
	computeVegetPlacements(mesh, soilIds, CVector::Null, 1.f, setB);

	// bit-identical output (same mesh -> same vegetation, no reshuffle)
	ASSERT_EQ(setA.Groups.size(), setB.Groups.size());
	for (uint g = 0; g < setA.Groups.size(); ++g)
	{
		EXPECT_EQ(setA.Groups[g].Biome, setB.Groups[g].Biome);
		EXPECT_EQ(setA.Groups[g].Material, setB.Groups[g].Material);
		ASSERT_EQ(setA.Groups[g].Buckets.size(), setB.Groups[g].Buckets.size());
		for (uint b = 0; b < setA.Groups[g].Buckets.size(); ++b)
		{
			const CVegetBucket& a = setA.Groups[g].Buckets[b];
			const CVegetBucket& c = setB.Groups[g].Buckets[b];
			EXPECT_EQ(a.Center, c.Center);
			EXPECT_EQ(a.Normal, c.Normal);
			EXPECT_EQ(a.Area, c.Area);
			EXPECT_EQ(a.Triangles, c.Triangles);
			ASSERT_EQ(a.TriAreas.size(), c.TriAreas.size());
			for (uint t = 0; t < a.TriAreas.size(); ++t)
				EXPECT_EQ(a.TriAreas[t], c.TriAreas[t]);
		}
	}
}

TEST(DynworldVegetationPlacementTest, OriginAndScaleRespected)
{
	TDecodedMesh mesh;
	addSoilSurface(mesh, 4, makeRawVoxel(0, SoilMaterial)); // 4x4 mesh-local
	uint8 soilIds[VegetMaxBiomeCount];
	makeSoilTable(soilIds);

	const CVector origin(10.f, 20.f, 0.f);
	const float scale = 0.5f; // LOD-1-like coarser voxels
	CDynVegetPlacementSet set;
	computeVegetPlacements(mesh, soilIds, origin, scale, set);

	// world footprint: 2x2 m at (10,20) -> area 4 m^2 in cells around (11,21)
	ASSERT_EQ(set.Groups.size(), 1u);
	ASSERT_EQ(set.Groups[0].Buckets.size(), 1u);
	const CVegetBucket& bucket = set.Groups[0].Buckets[0];
	EXPECT_FLOAT_EQ(bucket.Center.x, 11.f);
	EXPECT_FLOAT_EQ(bucket.Center.y, 21.f);
	EXPECT_NEAR(bucket.Area, 4.f, 1e-4f);
}

// ****************************************************************************
// vegetBucketResolvePosition

TEST(DynworldVegetationBucketResolveTest, PositionLandsOnSurfaceFacet)
{
	TDecodedMesh mesh;
	addSoilSurface(mesh, 4, makeRawVoxel(0, SoilMaterial));
	uint8 soilIds[VegetMaxBiomeCount];
	makeSoilTable(soilIds);

	CDynVegetPlacementSet set;
	computeVegetPlacements(mesh, soilIds, CVector::Null, 1.f, set);
	ASSERT_EQ(set.Groups.size(), 1u);
	ASSERT_FALSE(set.Groups[0].Buckets.empty());

	for (const CVegetBucket& bucket : set.Groups[0].Buckets)
	{
		const CVector2f uvs[] = {
			CVector2f(0.1f, 0.2f), CVector2f(0.9f, 0.1f), CVector2f(0.5f, 0.5f),
			CVector2f(0.95f, 0.95f), CVector2f(0.3f, 0.9f),
		};
		for (const CVector2f& uv : uvs)
		{
			CVector pos, normal;
			vegetBucketResolvePosition(mesh, bucket, uv, CVector::Null, 1.f, pos, normal);

			// the position must lie ON one of the bucket's triangles, and the
			// returned normal must be that triangle's face normal
			bool found = false;
			for (uint32 firstCorner : bucket.Triangles)
			{
				const CVector p0 = ToNeLVectorF(mesh.getVertex(mesh.getIndex(firstCorner)).position);
				const CVector p1 = ToNeLVectorF(mesh.getVertex(mesh.getIndex(firstCorner + 1)).position);
				const CVector p2 = ToNeLVectorF(mesh.getVertex(mesh.getIndex(firstCorner + 2)).position);
				if (pointOnTriangle(pos, p0, p1, p2))
				{
					CVector triNormal = (p1 - p0) ^ (p2 - p0);
					triNormal.normalize();
					EXPECT_NEAR(normal.x, triNormal.x, 1e-4f);
					EXPECT_NEAR(normal.y, triNormal.y, 1e-4f);
					EXPECT_NEAR(normal.z, triNormal.z, 1e-4f);
					found = true;
					break;
				}
			}
			EXPECT_TRUE(found);
		}
	}
}

TEST(DynworldVegetationBucketResolveTest, ResolveIsDeterministic)
{
	TDecodedMesh mesh;
	addSoilSurface(mesh, 4, makeRawVoxel(0, SoilMaterial));
	uint8 soilIds[VegetMaxBiomeCount];
	makeSoilTable(soilIds);

	CDynVegetPlacementSet set;
	computeVegetPlacements(mesh, soilIds, CVector::Null, 1.f, set);
	ASSERT_EQ(set.Groups.size(), 1u);
	const CVegetBucket& bucket = set.Groups[0].Buckets[0];

	const CVector2f uvs[] = { CVector2f(0.25f, 0.75f), CVector2f(0.66f, 0.13f) };
	for (const CVector2f& uv : uvs)
	{
		CVector posA, normA, posB, normB;
		vegetBucketResolvePosition(mesh, bucket, uv, CVector::Null, 1.f, posA, normA);
		vegetBucketResolvePosition(mesh, bucket, uv, CVector::Null, 1.f, posB, normB);
		EXPECT_EQ(posA, posB);
		EXPECT_EQ(normA, normB);
	}
}

// ****************************************************************************
// commit-phase assumption: CVegetable::generateGroup instance counts scale
// exactly with (bucket area x density) when noise randomization is zero
// (plan D3 validation numbers)

TEST(DynworldVegetGenerateGroupTest, InstanceCountScalesWithAreaAndDensity)
{
	// 4x4 m soil surface -> 4 buckets of ~4 m^2 each
	TDecodedMesh mesh;
	addSoilSurface(mesh, 4, makeRawVoxel(0, SoilMaterial));
	uint8 soilIds[VegetMaxBiomeCount];
	makeSoilTable(soilIds);

	CDynVegetPlacementSet set;
	computeVegetPlacements(mesh, soilIds, CVector::Null, 1.f, set);
	ASSERT_EQ(set.Groups.size(), 1u);
	ASSERT_EQ(set.Groups[0].Buckets.size(), 4u);

	// standalone manager; the shape name is fake (generateGroup does not need
	// the loaded shape, only generateInstance does)
	NL3D::CVegetableManager manager(1000, 1000);
	NL3D::CVegetable veget;
	veget.ShapeName = "test_fake_shape.veget";
	veget.Density = CNoiseValue(1.f, 0.f, 1.f); // 1 instance/m^2, no noise random
	veget.registerToManager(&manager);

	uint total = 0;
	OptFastFloorBegin();
	for (const CVegetBucket& bucket : set.Groups[0].Buckets)
	{
		std::vector<CVector2f> uvs;
		veget.generateGroup(bucket.Center, bucket.Normal, bucket.Area, 0, uvs);
		total += (uint)uvs.size();
	}
	OptFastFloorEnd();

	// 16 m^2 at 1 instance/m^2 -> exactly 16 (floor(area*dens + [0..1)) has
	// no fractional headroom)
	EXPECT_EQ(total, 16u);

	// doubling density doubles the count
	veget.Density = CNoiseValue(2.f, 0.f, 1.f);
	total = 0;
	OptFastFloorBegin();
	for (const CVegetBucket& bucket : set.Groups[0].Buckets)
	{
		std::vector<CVector2f> uvs;
		veget.generateGroup(bucket.Center, bucket.Normal, bucket.Area, 0, uvs);
		total += (uint)uvs.size();
	}
	OptFastFloorEnd();
	EXPECT_EQ(total, 32u);
}
