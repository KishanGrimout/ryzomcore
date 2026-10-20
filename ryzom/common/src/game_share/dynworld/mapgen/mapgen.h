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

#ifndef MAPGEN_HEADER
#define MAPGEN_HEADER

#include "nel/misc/vectori.h"
#include "game_share/dynworld/mapgen/noise.h"
#include "game_share/dynworld/polyvox_defs.h"

class CMapgenParams;

struct CBlockMakeData
{
	// Input
	const TRegion& m_region;
	const uint m_lodScale;
	const float m_invLodScale;

	// Work buffers
	std::vector<Voxel::BiomeType> m_biomeMap;
	std::vector<sint8> m_hardFloorZMap;
	std::vector<float> m_softFloorZMap;
	std::vector<float> m_chunkDensities;

	// Output
	TVolume::Chunk& m_chunk;
	bool m_isFull;
	bool m_isEmpty;
	bool m_hasObjects;

	CBlockMakeData(const TRegion& region, TVolume::Chunk& pChunk, uint lodScale = 1);
};

class CMapGen
{
	friend class CCave;

	uint32 flags;
	NLMISC::CVectorI csize;


	float baseTerrainLevel(float terrain_base, float terrain_higher, float steepness, float height_select);
	NoiseSample baseTerrainLevel_derivatives(const NoiseSample& terrain_base, const NoiseSample& terrain_higher, const NoiseSample& steepness, float height_select);
	float baseTerrainLevelFromNoise(const NLMISC::CVectorI& p, const CMapgenParams* params);
	NoiseSample baseTerrainLevelFromNoise_derivatives(const NLMISC::CVectorI& p, const CMapgenParams* params);
	float baseTerrainLevelFromNoiseAndCache(CBlockMakeData& data, const NLMISC::CVectorI& position, sint& stone_surface_max_z);
	float cacheTerrainLevel(CBlockMakeData& data, const NLMISC::CVectorI& position, sint& stone_surface_max_z);
	float getSoilAmount(const NLMISC::CVectorI& p, const CMapgenParams* params);
	Voxel::BiomeType getBiome(sint x, sint y);
	int generateGround(CBlockMakeData& data);
	void generateTrees(CBlockMakeData& data);
	void generateCaves(CBlockMakeData& data, sint max_stone_y);

	// Ground generation V2
	sint generateGround_v2(CBlockMakeData& data);
	sint generateNoiseMaps(CBlockMakeData& data);
	void generateDensityCubeMap(CBlockMakeData& data);


	// OLD, for reference
	int getGroundLevelAtPoint(const NLMISC::CVectorI& p);
	sint16 find_stone_level(const NLMISC::CVectorI& p2d);
	bool block_is_underground(uint64 seed, const NLMISC::CVectorI& blockpos);
	sint16 find_ground_level_from_noise(uint64 seed, const NLMISC::CVectorI& p2d, sint16 precision);

	float getHumidity(const NLMISC::CVectorI& p);
	float getTreeAmount(const NLMISC::CVectorI& p);
	bool getHaveAppleTree(const NLMISC::CVectorI& p);
	
	bool getHaveBeach(const NLMISC::CVectorI& p);
	bool getHaveBeach(int index);

	uint32 getBlockSeed(uint64 seed, const NLMISC::CVectorI& p);

	void calculateNoise(CBlockMakeData& data);
	
	void addMud();
	void flowMud(sint16 &mudflow_minpos, sint16 &mudflow_maxpos);
	void addDirtGravelBlobs();
	void growGrass();
	void placeTreesAndJungleGrass();
	void generateExperimental() {}

public:
	CMapGen();
	~CMapGen();

	void makeChunk(CBlockMakeData& data);
};

#endif
