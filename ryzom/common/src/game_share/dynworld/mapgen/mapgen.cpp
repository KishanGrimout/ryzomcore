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

/* Inspired by Minetest:
Minetest
Copyright (C) 2010-2013 celeron55, Perttu Ahola <celeron55@gmail.com>
Copyright (C) 2010-2013 kwolekr, Ryan Kwolek <kwolekr@minetest.net>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU Lesser General Public License as published by
the Free Software Foundation; either version 2.1 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include "mapgen.h"

#include "nel/misc/hierarchical_timer.h"

#include "game_share/utils.h"
#include "game_share/dynworld/map_sector.h"
#include "game_share/dynworld/material_library.h"
#include "game_share/dynworld/biome_manager.h"
#include "game_share/dynworld/mapgen/mapgen_params.h"
#include "game_share/dynworld/mapgen/cavegen.h"

#include <ANL/Noise/anl_noise.h>

#define rangelim(d, min, max) ((d) < (min) ? (min) : ((d)>(max)?(max):(d)))

using namespace NLMISC;

float g_floorHeight = 1.f;
uint32 g_groundDensity = Voxel::MaxDensity;
uint g_AdditionnalBorder = 5;

CBlockMakeData::CBlockMakeData(const TRegion& region, TVolume::Chunk& pChunk, uint lodScale) :
	m_region(region),
	m_chunk(pChunk),
	m_lodScale(lodScale),
	m_invLodScale(1.f / lodScale),
	m_isFull(true),
	m_isEmpty(true),
	m_hasObjects(false)
{}

CMapGen::CMapGen()
	: flags(CMapgenParams::Caves)
	, csize(64, 64, 64)
{

}


CMapGen::~CMapGen()
{
	
}


uint32 CMapGen::getBlockSeed(uint64 seed, const CVectorI& p)
{
	sint32 x = p.x, y = p.y, z = p.z;
	return (uint32)(seed % 0x100000000ULL) + z * 38134234 + y * 42123 + x * 23;
}

Voxel::BiomeType CMapGen::getBiome(sint x, sint y)
{
	return CBiomeManager::instance().getBiome(x, y);
}

bool g_generateCaves = false;
void CMapGen::makeChunk(CBlockMakeData& data)
{
	H_AUTO(MapGen_MakeChunk);

	// Maximum height of the stone surface and obstacles.
	// This is used to guide the cave generation
	sint32 stone_surface_max_z;

	// Generate general ground level to full area
	stone_surface_max_z = generateGround(data);

	generateTrees(data);

	const sint max_spread_amount = 16;


	// Loop this part, it will make stuff look older and newer nicely
	const uint age_loops = 2;
	for (uint i_age = 0; i_age < age_loops; i_age++) // Aging loop
	{
		// Make caves (this code is relatively horrible)
		if (g_generateCaves && flags & CMapgenParams::Caves)
			generateCaves(data, stone_surface_max_z);
	}
}

float g_maxSurfaceDensity = Voxel::MaxDensity;
bool g_hasSurfaceDensity = true;
float g_surfaceDensityOffset = 0.f;
uint g_underSurfaceDensity = Voxel::MaxDensity;
uint g_averageFactor = 26;
float g_logMinSoil = 100.f;
float g_logMaxSoil = -100.f;
float g_invXYdisplacementSpread = 1.f / 25.f;

float CMapGen::cacheTerrainLevel(CBlockMakeData& data, const NLMISC::CVectorI& position, sint& stone_surface_max_z)
{
	uint width = data.m_region.getWidthInVoxels() + 2 * g_AdditionnalBorder;
	sint minX = data.m_region.getLowerX() - g_AdditionnalBorder, maxX = data.m_region.getUpperX() + g_AdditionnalBorder;
	sint minY = data.m_region.getLowerY() - g_AdditionnalBorder, maxY = data.m_region.getUpperY() + g_AdditionnalBorder;
	uint index = (position.x - minX) / data.m_lodScale + (position.y - minY) * width / data.m_lodScale;

	Voxel::BiomeType bt = getBiome(position.x, position.y);
	data.m_biomeMap[index] = bt;
	const CEnvironmentDefinition* envDef = CMaterialLibrary::instance().getEnvironmentDef(bt);
	const CMapgenParams* envParams = &envDef->getMapgenParams();

	// Surface height
	float surface_z = baseTerrainLevelFromNoise(position, envParams);

	float surface_z_floor = std::floor(surface_z);
	sint surface_z_int = static_cast<sint>(surface_z_floor);

	// check we don't need a larger integer type
	nlassert(surface_z_int > std::numeric_limits<sint8>::min() && surface_z_int < std::numeric_limits<sint8>::max());
	nlassert(data.m_hardFloorZMap[index] == std::numeric_limits<sint8>::min()); // check it is not already initialized
	data.m_hardFloorZMap[index] = static_cast<sint8>(surface_z_int);

	// Log it
	if (surface_z_int > stone_surface_max_z)
		stone_surface_max_z = surface_z_int;

	// Randomize mud amount.
	float soil_add_amount = getSoilAmount(position, envParams) * 0.5f + 0.5f;
	if (soil_add_amount <= 0.f)
		soil_add_amount = 1.f - soil_add_amount;

	soil_add_amount *= 2.f;

	if (surface_z < 20.f)
	{
		// put more soil as it becomes deeper
		soil_add_amount += (20.f - surface_z) / 5.f;
	}

	if (soil_add_amount > g_logMaxSoil)
		g_logMaxSoil = soil_add_amount;
	if (soil_add_amount < g_logMinSoil)
		g_logMinSoil = soil_add_amount;

	float soil_surface_z = surface_z + soil_add_amount;

	data.m_softFloorZMap[index] = soil_surface_z;

	return surface_z_floor;
}

float CMapGen::baseTerrainLevelFromNoiseAndCache(CBlockMakeData& data, const NLMISC::CVectorI& position, sint& stone_surface_max_z)
{
	const sint minX = data.m_region.getLowerX() - g_AdditionnalBorder, maxX = data.m_region.getUpperX() + g_AdditionnalBorder;
	const sint minY = data.m_region.getLowerY() - g_AdditionnalBorder, maxY = data.m_region.getUpperY() + g_AdditionnalBorder;

	// Check if we can use a cached value
	if (position.x < minX ||
		position.x > maxX ||
		position.y < minY ||
		position.y > maxY)
	{
		// We are out of bounds of the chunk: always compute
		Voxel::BiomeType bt = getBiome(position.x, position.y);
		const CEnvironmentDefinition* envDef = CMaterialLibrary::instance().getEnvironmentDef(bt);
		const CMapgenParams* envParams = &envDef->getMapgenParams();

		// Surface height
		float surface_z = baseTerrainLevelFromNoise(position, envParams);

		float surface_z_floor = std::floor(surface_z);

		return surface_z_floor;
	}
	else
	{
		const uint width = data.m_region.getWidthInVoxels() + 2 * g_AdditionnalBorder;
		const uint index = (position.x - minX) + (position.y - minY) * width;
		return float(data.m_softFloorZMap[index]);
	}
}

sint CMapGen::generateNoiseMaps(CBlockMakeData& data)
{
	//NL_PROF("GenerateNoiseMaps");

	// Return value
	sint hardGroundMaxZ = std::numeric_limits<sint>::min();

	// Some utility values
	const uint regionWidth = data.m_region.getWidthInVoxels();
	const uint chunkWidth = regionWidth * data.m_invLodScale;
	const uint extraBorder = g_AdditionnalBorder * data.m_invLodScale;
	const uint mapWidth = chunkWidth + 2 * extraBorder;
	const uint sqMapWidth = mapWidth * mapWidth;

	const sint regionMinX = data.m_region.getLowerX(), regionMaxX = data.m_region.getUpperX();
	const sint regionMinY = data.m_region.getLowerY(), regionMaxY = data.m_region.getUpperY();
	const sint regionMinZ = data.m_region.getLowerZ(), regionMaxZ = data.m_region.getUpperZ();

	const sint mapMinX = regionMinX * data.m_invLodScale - extraBorder, mapMaxX = regionMaxX * data.m_invLodScale + extraBorder;
	const sint mapMinY = regionMinY * data.m_invLodScale - extraBorder, mapMaxY = regionMaxY * data.m_invLodScale + extraBorder;

	// Here we try to figure if the current chunk contains or not the surface level,
	// in order to optimize generation and skip the noise computation
	bool chunkAtSurface = true;
	float initHeight = 0.f;
	if (regionMaxZ < CMaterialLibrary::instance().getMinGroundHeight())
	{
		nlassert(CMaterialLibrary::instance().getMinGroundHeight() + 1 >= std::numeric_limits<sint8>::min());
		initHeight = CMaterialLibrary::instance().getMinGroundHeight() + 1;
		chunkAtSurface = false;
	}
	else if (regionMinZ > CMaterialLibrary::instance().getMaxGroundHeight())
	{
		nlassert(CMaterialLibrary::instance().getMinGroundHeight() + 1 <= std::numeric_limits<sint8>::max());
		initHeight = CMaterialLibrary::instance().getMaxGroundHeight() - 1;
		chunkAtSurface = false;
	}

	// Initialize the noise maps
	data.m_biomeMap.resize(sqMapWidth);
	data.m_hardFloorZMap.resize(sqMapWidth, initHeight);
	data.m_softFloorZMap.resize(sqMapWidth, initHeight);

	// Fill the noise maps using noise functions
	if (chunkAtSurface)
	{
		NLMISC::CVectorI position(0, 0, 0);
		for (sint y = mapMinY; y <= mapMaxY; ++y)
		{
			for (sint x = mapMinX; x <= mapMaxX; ++x)
			{
				// Set the position at global scale
				position.x = x * data.m_lodScale;
				position.y = y * data.m_lodScale;

				cacheTerrainLevel(data, position, hardGroundMaxZ);
			}
		}
	}
	else
	{
		// If the chunk is not at the surface level, the hard and soft floor maps are
		// already initialized with the dummy 'initHeight'
		uint32 index = 0;
		for (sint y = mapMinY; y <= mapMaxY; ++y)
		{
			for (sint x = mapMinX; x <= mapMaxX; ++x, ++index)
			{
				data.m_biomeMap[index] = getBiome(x * data.m_lodScale, y * data.m_lodScale);
			}
		}
	}

	return hardGroundMaxZ;
}

float g_maxXyDisplacement = 5.f;
void CMapGen::generateDensityCubeMap(CBlockMakeData& data)
{
	//NL_PROF("GenerateDensityCubeMap");

	// Some utility values
	const uint regionWidth = data.m_region.getWidthInVoxels();
	const uint chunkWidth = regionWidth * data.m_invLodScale;
	const uint extraBorder = g_AdditionnalBorder * data.m_invLodScale;
	const uint xyWidth = chunkWidth + 2 * extraBorder;
	const uint zWidth = chunkWidth;

	const CVectorI extraBorderV(extraBorder, extraBorder, extraBorder);
	const CVectorI regionMin(data.m_region.getLowerX(), data.m_region.getLowerY(), data.m_region.getLowerZ());
	const CVectorI regionMax(data.m_region.getUpperX(), data.m_region.getUpperY(), data.m_region.getUpperZ());
	const CVectorI cubeMin(regionMin * data.m_invLodScale - extraBorderV);
	const CVectorI cubeMax(regionMax * data.m_invLodScale + extraBorderV);

	// The noise functions for the X and Y displacement
	anl::CImplicitBasisFunction displacementX(anl::VALUE, anl::LINEAR);
	displacementX.setSeed(0);
	anl::CImplicitBasisFunction displacementY(anl::VALUE, anl::LINEAR);
	displacementY.setSeed(1);

	// Initialize the density cube map
	data.m_chunkDensities.resize(xyWidth * xyWidth * zWidth, 0.f);

	const float xyMaxDisplacement = std::min(float(extraBorder), g_maxXyDisplacement);
	for (sint y = 0; y < xyWidth; ++y)
	{
		const sint noiseInputY = (y + cubeMin.y) * data.m_lodScale;

		for (sint x = 0; x < xyWidth; ++x)
		{
			const uint index2d = x + y * xyWidth;

			// Compute the final x, y position
			const sint noiseInputX = (x + cubeMin.x) * data.m_lodScale;

			// Surface height
			const sint surface_z_int = data.m_hardFloorZMap[index2d];

			// Soil height
			const float soil_surface_z = data.m_softFloorZMap[index2d];
			const float soil_surface_z_floor = std::floor(soil_surface_z);
			const sint soil_surface_z_int = static_cast<sint>(soil_surface_z_floor);

			for (sint z = 0; z < zWidth; ++z)
			{
				const sint noiseInputZ = (z + cubeMin.z) * data.m_lodScale;

				if (noiseInputZ <= soil_surface_z_int + 1)
				{
					const float invSpread = g_invXYdisplacementSpread;
					const float fX = displacementX.get(noiseInputX * invSpread, noiseInputY * invSpread, noiseInputZ * invSpread) * xyMaxDisplacement;
					const float fY = displacementY.get(noiseInputX * invSpread, noiseInputY * invSpread, noiseInputZ * invSpread) * xyMaxDisplacement;

					const float floorX = std::floor(fX);
					const float floorY = std::floor(fY);
					const float ceilX = std::ceil(fX);
					const float ceilY = std::ceil(fY);

					const float remainingX = fX - floorX;
					const float remainingY = fY - floorY;

					const sint out1X = sint(floorX) + x;
					const sint out1Y = sint(floorY) + y;
					const float dx1 = 1.f - remainingX;
					const float dy1 = 1.f - remainingY;

					const sint out2X = sint(ceilX) + x;
					const sint out2Y = sint(ceilY) + y;
					const float dx2 = remainingX;
					const float dy2 = remainingY;

					const sint offsetX[2] = { out1X, out2X };
					const sint offsetY[2] = { out1Y, out2Y };
					const float coefX[2] = { dx1, dx2 };
					const float coefY[2] = { dy1, dy2 };

					for (sint i = 0; i <= out2X - out1X; ++i)
					{
						for (sint j = 0; j <= out2Y - out1Y; ++j)
						{
							const sint mx = (offsetX[i] - extraBorder) * data.m_invLodScale;
							const sint my = (offsetY[j] - extraBorder) * data.m_invLodScale;
							const sint mz = z * data.m_invLodScale;

							const float densityCoef = coefX[i] * coefY[j] * data.m_lodScale;

							//const uint index3D = PolyVox::morton256_x[mx] | PolyVox::morton256_y[my] | PolyVox::morton256_z[mz];
							const uint index3D = mz + my * xyWidth + mx * xyWidth * xyWidth;
							nlassert(index3D >= 0 && index3D < data.m_chunkDensities.size());
							const float initDensity = data.m_chunkDensities[index3D];

							if (noiseInputZ == soil_surface_z_int)
							{
								const float soil_surface_density = (soil_surface_z - soil_surface_z_floor) * g_maxSurfaceDensity;
								data.m_chunkDensities[index3D] = std::min(soil_surface_density * densityCoef + initDensity, float(Voxel::MaxDensity));
							}
							else if (noiseInputZ < surface_z_int)
							{
								data.m_chunkDensities[index3D] = std::min(float(g_groundDensity) * densityCoef + initDensity, float(Voxel::MaxDensity));
							}
							else if (noiseInputZ < soil_surface_z_int)
							{
								data.m_chunkDensities[index3D] = std::min(float(g_groundDensity) * densityCoef + initDensity, float(Voxel::MaxDensity));
							}
						}
					}
				}
			}
		}
	}
}

sint CMapGen::generateGround_v2(CBlockMakeData& data)
{
	sint hardGroundMaxZ = generateNoiseMaps(data);
	generateDensityCubeMap(data);

	return hardGroundMaxZ;
}

sint CMapGen::generateGround(CBlockMakeData& data)
{
	Voxel airVoxel;
	airVoxel.setType(Voxel::Air);

	Voxel groundVoxel;
	groundVoxel.setType(Voxel::Raw);
	groundVoxel.setMaterial(CMaterialLibrary::instance().getMaterialId("Wood"));

	Voxel soilVoxel;
	soilVoxel.setType(Voxel::Raw);
	soilVoxel.setMaterial(CMaterialLibrary::instance().getMaterialId("Humus"));

	sint stone_surface_max_z = std::numeric_limits<sint>::min();

	const sint minX = data.m_region.getLowerX(), maxX = data.m_region.getUpperX();
	const sint minY = data.m_region.getLowerY(), maxY = data.m_region.getUpperY();
	const sint minZ = data.m_region.getLowerZ(), maxZ = data.m_region.getUpperZ();

	const sint mapMinX = minX - g_AdditionnalBorder, mapMaxX = maxX + g_AdditionnalBorder;
	const sint mapMinY = minY - g_AdditionnalBorder, mapMaxY = maxY + g_AdditionnalBorder;
	const sint mapMinZ = minZ - g_AdditionnalBorder, mapMaxZ = maxZ + g_AdditionnalBorder;

	bool skipGroundGen = false;
	sint8 initHeight = std::numeric_limits<sint8>::min();
	if (maxZ < CMaterialLibrary::instance().getMinGroundHeight())
	{
		nlassert(CMaterialLibrary::instance().getMinGroundHeight() + 1 >= std::numeric_limits<sint8>::min());
		initHeight = CMaterialLibrary::instance().getMinGroundHeight() + 1;
		skipGroundGen = true;
	}
	else if (minZ > CMaterialLibrary::instance().getMaxGroundHeight())
	{
		nlassert(CMaterialLibrary::instance().getMinGroundHeight() + 1 <= std::numeric_limits<sint8>::max());
		initHeight = CMaterialLibrary::instance().getMaxGroundHeight() - 1;
		skipGroundGen = true;
	}

	const uint width = data.m_region.getWidthInVoxels();
	const float invLodScale = 1.f / data.m_lodScale;
	const uint chunkWidth = width * invLodScale;
	const uint mapWidth = data.m_region.getWidthInVoxels() + 2 * g_AdditionnalBorder;

	data.m_biomeMap.resize(mapWidth * mapWidth);
	data.m_hardFloorZMap.resize(mapWidth * mapWidth, initHeight);
	data.m_softFloorZMap.resize(mapWidth * mapWidth, initHeight);
	data.m_chunkDensities.resize(mapWidth * mapWidth * mapWidth, 0.f);

	const uint lodStep = data.m_lodScale;
	const float invLodStep = 1.f / lodStep;

	{
		//NL_PROF("ComputeNoiseMaps");
		NLMISC::CVectorI position(0, 0, 0);
		uint32 index = 0;
		for (sint y = mapMinY; y <= mapMaxY; y += lodStep)
		{
			for (sint x = mapMinX; x <= mapMaxX; x += lodStep, index++)
			{
				position.x = x; position.y = y;

				if (!skipGroundGen)
				{
					cacheTerrainLevel(data, position, stone_surface_max_z);
				}
				else
				{
					// if chunk is way below or above ground level, only initialize the biome map
					data.m_biomeMap[index] = getBiome(position.x, position.y);
				}
			}
		}
	}

	{
		//NL_PROF("ComputeXYDisplacement");
		anl::CImplicitBasisFunction displacementX(anl::VALUE, anl::LINEAR);
		displacementX.setSeed(0);
		anl::CImplicitBasisFunction displacementY(anl::VALUE, anl::LINEAR);
		displacementY.setSeed(1);

		const float xyMaxDisplacement = std::min(g_AdditionnalBorder, 5U);
		for (sint y = 0; y < mapWidth; y += lodStep)
		{
			const sint inputY = (y + mapMinY);

			for (sint x = 0; x < mapWidth; x += lodStep)
			{
				const uint index = x * invLodStep + y * mapWidth * invLodStep;

				// Compute the final x, y position
				const sint inputX = (x + mapMinX);

				// Surface height
				const sint surface_z_int = data.m_hardFloorZMap[index];

				// Soil height
				const float soil_surface_z = data.m_softFloorZMap[index];
				const float soil_surface_z_floor = std::floor(soil_surface_z);
				const float soil_surface_density = (soil_surface_z - soil_surface_z_floor) * g_maxSurfaceDensity;
				const sint soil_surface_z_int = static_cast<sint>(soil_surface_z_floor);

				for (sint z = 0; z < width; z += lodStep)
				{
					const sint inputZ = (z + minZ);
					if (inputZ <= soil_surface_z_int + 1)
					{
						const float invSpread = g_invXYdisplacementSpread;
						const float fX = displacementX.get(inputX * invSpread, inputY * invSpread, inputZ * invSpread) * xyMaxDisplacement;
						const float fY = displacementY.get(inputX * invSpread, inputY * invSpread, inputZ * invSpread) * xyMaxDisplacement;
						
						const float floorX = std::floor(fX);
						const float floorY = std::floor(fY);
						const float ceilX = std::ceil(fX);
						const float ceilY = std::ceil(fY);
						
						const float remainingX = fX - floorX;
						const float remainingY = fY - floorY;
						
						const sint out1X = sint(floorX) + x;
						const sint out1Y = sint(floorY) + y;
						const float dx1 = 1.f - remainingX;
						const float dy1 = 1.f - remainingY;
						
						const sint out2X = sint(ceilX) + x;
						const sint out2Y = sint(ceilY) + y;
						const float dx2 = remainingX;
						const float dy2 = remainingY;
						
						const sint offsetX[2] = { out1X, out2X };
						const sint offsetY[2] = { out1Y, out2Y };
						const float coefX[2] = { dx1, dx2 };
						const float coefY[2] = { dy1, dy2 };

						for (sint i = 0; i <= out2X - out1X; ++i)
						{
							for (sint j = 0; j <= out2Y - out1Y; ++j)
							{
								const sint mx = (offsetX[i] - g_AdditionnalBorder) * invLodScale;
								const sint my = (offsetY[j] - g_AdditionnalBorder) * invLodScale;
								const sint mz = z * invLodScale;

								if (mx < 0 ||
									mx >= width ||
									my < 0 ||
									my >= width)
									continue;

								const float densityCoef = coefX[i] * coefY[j] * data.m_lodScale;

								const uint index3D = PolyVox::morton256_x[mx] | PolyVox::morton256_y[my] | PolyVox::morton256_z[mz];
								const float initDensity = data.m_chunkDensities[index3D];

								if (inputZ == soil_surface_z_int)
								{
									data.m_chunkDensities[index3D] = std::min(soil_surface_density * densityCoef + initDensity, float(Voxel::MaxDensity));
								}
								else if (inputZ < surface_z_int)
								{
									data.m_chunkDensities[index3D] = std::min(float(g_groundDensity) * densityCoef + initDensity, float(Voxel::MaxDensity));
								}
								else if (inputZ < soil_surface_z_int)
								{
									data.m_chunkDensities[index3D] = std::min(float(g_groundDensity) * densityCoef + initDensity, float(Voxel::MaxDensity));
								}
							}
						}
					}
				}
			}
		}
	}

	{
		//NL_PROF("CopyDensitiesToChunk");
		for (sint y = 0; y < mapWidth; y += lodStep)
		{
			for (sint x = 0; x < mapWidth; x += lodStep)
			{
				const uint index = x * invLodStep + y * mapWidth * invLodStep;

				Voxel::BiomeType bt = data.m_biomeMap[index];
				const CEnvironmentDefinition* envDef = CMaterialLibrary::instance().getEnvironmentDef(bt);
				groundVoxel.setBiome(bt);
				soilVoxel.setBiome(bt);
				uint defaultSoildId = envDef->getDefaultSoilId();
				soilVoxel.setMaterial(defaultSoildId);
				nlassert(groundVoxel.getType() == Voxel::Raw);
				nlassert(soilVoxel.getType() == Voxel::Raw);

				// Surface height
				const sint surface_z_int = data.m_hardFloorZMap[index];

				// Soil height
				const float soil_surface_z = data.m_softFloorZMap[index];
				const float soil_surface_z_floor = std::floor(soil_surface_z);
				const float soil_surface_density = (soil_surface_z - soil_surface_z_floor) * g_maxSurfaceDensity;
				const sint soil_surface_z_int = static_cast<sint>(soil_surface_z_floor);

				for (sint z = 0; z < width; z += lodStep)
				{
					TVoxel* currentVoxel = &airVoxel;

					const sint mx = x * invLodScale;
					const sint my = y * invLodScale;
					const sint mz = z * invLodScale;

					if (mx < 0 ||
						mx >= chunkWidth ||
						my < 0 ||
						my >= chunkWidth)
						continue;

					const sint absoluteZ = (z + minZ);
					const uint index3D = PolyVox::morton256_x[mx] | PolyVox::morton256_y[my] | PolyVox::morton256_z[mz];

					if (absoluteZ == soil_surface_z_int)
					{
						soilVoxel.setDensity(data.m_chunkDensities[index3D]);

						if (soilVoxel.getDensity() > 0)
							currentVoxel = &soilVoxel;
						else
							currentVoxel = &airVoxel;
					}
					else if (absoluteZ < surface_z_int)
					{
						groundVoxel.setDensity(data.m_chunkDensities[index3D]);
						currentVoxel = &groundVoxel;
					}
					else if (absoluteZ < soil_surface_z_int)
					{
						soilVoxel.setDensity(data.m_chunkDensities[index3D]);
						currentVoxel = &soilVoxel;
					}

					if (currentVoxel == &airVoxel)
					{
						data.m_isFull = false;
					}
					else if (currentVoxel == &groundVoxel)
					{
						data.m_isEmpty = false;
					}

					data.m_chunk.setVoxel(mx, my, mz, *currentVoxel);
				}
			}
		}
	}

	return stone_surface_max_z;
}

float g_minFloraDensityMax = 0.f;
float g_maxFloraDensityMax = 5.f;
float g_minSmallFloraDensityMax = 5.f;
float g_maxSmallFloraDensityMax = 20.f;
uint g_bigFloraSafeRadius = 10; // in voxels
uint g_smallFloraSafeRadius = 3; // in voxels
float g_densityDomainInvScale = 2.f;
void CMapGen::generateTrees(CBlockMakeData& data)
{
	if (data.m_isEmpty || data.m_isFull)
		return; // no trees to generate

	Voxel treeVoxel;
	treeVoxel.setType(Voxel::Object);

	uint seed = data.m_region.getCentreX() + data.m_region.getCentreY(); // look for another seed
	
	anl::CImplicitBasisFunction densityNoise(anl::SIMPLEX, anl::CUBIC);
	densityNoise.setSeed(seed);

	PseudoRandom rand(seed);

	const uint width = data.m_region.getWidthInVoxels();
	const uint mapWidth = width + g_AdditionnalBorder * 2;
	float noiseX = data.m_region.getCentreX() / (g_densityDomainInvScale * width);
	float noiseY = data.m_region.getCentreY() / (g_densityDomainInvScale * width);

	float density = densityNoise.get(noiseX, noiseY) * (g_maxFloraDensityMax - g_minFloraDensityMax) + g_minFloraDensityMax;
	for (float i = 0; i < density; i += 1.f)
	{
		// Big flora
		uint posX = rand.range(0, width - 1);
		uint posY = rand.range(0, width - 1);

		const uint index = posX + g_AdditionnalBorder + (posY + g_AdditionnalBorder) * mapWidth;

		sint posZ = static_cast<sint>(std::floor(data.m_softFloorZMap[index])) + 1;
		if (posZ < data.m_region.getLowerZ() || posZ > data.m_region.getUpperZ())
			continue;

		Voxel::BiomeType bt = data.m_biomeMap[index];
		const CEnvironmentDefinition* envDef = CMaterialLibrary::instance().getEnvironmentDef(bt);
		const std::vector<uint32>& floraIds = envDef->getBigFloraSheetIds();
		if (floraIds.empty())
			continue;

		uint floraId = rand.range(0, floraIds.size() - 1);
		nlassert(floraId < floraIds.size());

		treeVoxel.setSheetId(floraIds[floraId]);

		data.m_chunk.setVoxel(posX, posY, posZ - data.m_region.getLowerZ(), treeVoxel);

		data.m_hasObjects = true;
	}

	densityNoise.setSeed(seed * 2);
	density = densityNoise.get(data.m_region.getCentreX(), data.m_region.getCentreY()) * (g_maxSmallFloraDensityMax - g_minSmallFloraDensityMax) + g_minSmallFloraDensityMax;
	for (float i = 0; i < density; i += 1.f)
	{
		// Small flora
		uint posX = rand.range(0, width - 1);
		uint posY = rand.range(0, width - 1);

		const uint index = posX + g_AdditionnalBorder + (posY + g_AdditionnalBorder) * mapWidth;

		sint posZ = static_cast<sint>(std::floor(data.m_softFloorZMap[index])) + 1;
		if (posZ < data.m_region.getLowerZ() || posZ > data.m_region.getUpperZ())
			continue;

		Voxel::BiomeType bt = data.m_biomeMap[index];
		const CEnvironmentDefinition* envDef = CMaterialLibrary::instance().getEnvironmentDef(bt);
		const std::vector<uint32>& floraIds = envDef->getSmallFloraSheetIds();
		if (floraIds.empty())
			continue;

		uint floraId = rand.range(0, floraIds.size() - 1);
		nlassert(floraId < floraIds.size());

		treeVoxel.setSheetId(floraIds[floraId]);

		data.m_chunk.setVoxel(posX, posY, posZ - data.m_region.getLowerZ(), treeVoxel);

		data.m_hasObjects = true;
	}
}

//////////////////////// Base terrain height functions

float CMapGen::baseTerrainLevel(float terrain_base, float terrain_higher, float steepness, float height_select)
{
	float base = 1 + terrain_base;
	float higher = 1 + terrain_higher;

	// Limit higher ground level to at least base
	if (higher < base)
		higher = base;

	// Steepness factor of cliffs
	float b = steepness;
	b = rangelim(b, 0.0, 1000.0);
	b = 5 * b * b * b * b * b * b * b;
	b = rangelim(b, 0.5, 1000.0);

	// Values 1.5...100 give quite horrible looking slopes
	if (b > 1.5 && b < 100.0)
		b = (b < 10.0) ? 1.5 : 100.0;

	float a_off = -0.20; // Offset to more low
	float a = 0.5 + b * (a_off + height_select);
	a = rangelim(a, 0.0, 1.0); // Limit

	return base * (1.0 - a) + higher * a;
}

NoiseSample CMapGen::baseTerrainLevel_derivatives(const NoiseSample& terrain_base, const NoiseSample& terrain_higher, const NoiseSample& steepness, float height_select)
{
	float base = 1 + terrain_base.value;
	float higher = 1 + terrain_higher.value;

	// Limit higher ground level to at least base
	if (higher < base)
		higher = base;

	// Steepness factor of cliffs
	float b = steepness.value;
	b = rangelim(b, 0.0, 1000.0);
	b = 5 * b * b * b * b * b * b * b;
	b = rangelim(b, 0.5, 1000.0);

	// Values 1.5...100 give quite horrible looking slopes
	if (b > 1.5 && b < 100.0)
		b = (b < 10.0) ? 1.5 : 100.0;

	float a_off = -0.20; // Offset to more low
	float a = 0.5 + b * (a_off + height_select);
	a = rangelim(a, 0.0, 1.0); // Limit

	NoiseSample res;
	res.value = base * (1.0 - a) + higher * a;
	res.derivative.x = 0.f;//???
	res.derivative.y = 0.f;//???
	res.derivative.z = 0.f;//???
	return res;
}

float CMapGen::baseTerrainLevelFromNoise(const CVectorI& p, const CMapgenParams* params)
{
	if (flags & CMapgenParams::Flat)
		return params->water_level;

	float terrain_base = NoisePerlin2DPosOffset(&params->np_terrain_base,
		p.x, 0.5, p.y, 0.5, params->seed);
	float terrain_higher = NoisePerlin2DPosOffset(&params->np_terrain_higher,
		p.x, 0.5, p.y, 0.5, params->seed);
	float steepness = NoisePerlin2DPosOffset(&params->np_steepness,
		p.x, 0.5, p.y, 0.5, params->seed);
	float height_select = NoisePerlin2DNoTxfmPosOffset(&params->np_height_select,
		p.x, 0.5, p.y, 0.5, params->seed);

	return baseTerrainLevel(terrain_base, terrain_higher,
		steepness, height_select);
}

NoiseSample CMapGen::baseTerrainLevelFromNoise_derivatives(const CVectorI& p, const CMapgenParams* params)
{
	if (flags & CMapgenParams::Flat)
	{
		NoiseSample res;
		res.value = params->water_level;
		res.derivative.x = 0.f;
		res.derivative.y = 0.f;
		res.derivative.z = 0.f;
		return res;
	}

	NoiseSample terrain_base = NoisePerlin2DPosOffset_derivatives(&params->np_terrain_base,
		p.x, 0.5, p.y, 0.5, params->seed);
	NoiseSample terrain_higher = NoisePerlin2DPosOffset_derivatives(&params->np_terrain_higher,
		p.x, 0.5, p.y, 0.5, params->seed);
	NoiseSample steepness = NoisePerlin2DPosOffset_derivatives(&params->np_steepness,
		p.x, 0.5, p.y, 0.5, params->seed);
	float height_select = NoisePerlin2DNoTxfmPosOffset(&params->np_height_select,
		p.x, 0.5, p.y, 0.5, params->seed);

	return baseTerrainLevel_derivatives(terrain_base, terrain_higher,
		steepness, height_select);
}

float CMapGen::getSoilAmount(const CVectorI& p, const CMapgenParams* params)
{
	if (flags & CMapgenParams::Flat)
		return 4.f;

	return NoisePerlin2DPosOffset(&params->np_soil,
		p.x, 0.5, p.y, 0.5, params->seed);
}

float g_caveAmountFactor = 100000.f;
void CMapGen::generateCaves(CBlockMakeData& data, sint max_stone_z)
{
	/*float cave_amount = NoisePerlin2D(np_cave, data.m_region.getLowerX(), data.m_region.getLowerZ(), seed);
	sint volume_nodes =
		(data.m_region.getUpperX() - data.m_region.getLowerX() + 1) *
		(data.m_region.getUpperY() - data.m_region.getLowerY() + 1) * 
		(data.m_region.getUpperZ() - data.m_region.getLowerZ() + 1);
	cave_amount = std::max(0.f, cave_amount);
	uint caves_count = cave_amount * volume_nodes / g_caveAmountFactor;
	uint bruises_count = 1;
	PseudoRandom ps(data.seed + 21343);
	PseudoRandom ps2(data.seed + 1032);

	if (ps.range(1, 6) == 1)
		bruises_count = ps.range(0, ps.range(0, 2));

// 	if (getBiome(v2s16(node_min.X, node_min.Z)) == BT_DESERT) {
// 		caves_count /= 3;
// 		bruises_count /= 3;
// 	}

	for (uint i = 0; i < caves_count + bruises_count; i++)
	{
		bool large_cave = (i >= caves_count);
		CCave cave(this, &ps, &ps2, large_cave);

		cave.makeCave(data, max_stone_z);
	}*/
}
