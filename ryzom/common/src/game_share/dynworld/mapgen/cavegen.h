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

#ifndef CAVEGEN_HEADER
#define CAVEGEN_HEADER

#include "nel/misc/vector.h"
#include "nel/misc/vectori.h"
#include "game_share/dynworld/mapgen/noise.h"
#include "game_share/dynworld/polyvox_defs.h"

class CMapGen;
class CBlockMakeData;

class CCave
{
public:
	CMapGen *mg;
	CBlockMakeData* block_data;
// 	ManualMapVoxelManipulator *vm;
// 	INodeDefManager *ndef;

	sint min_tunnel_diameter;
	sint max_tunnel_diameter;
	uint tunnel_routepoints;
	sint dswitchint;
	sint part_max_length_rs;

	bool large_cave;
	bool large_cave_is_flat;
	bool flooded;

	sint max_stone_z;
	NLMISC::CVectorI node_min;
	NLMISC::CVectorI node_max;

	NLMISC::CVector orp;  // starting point, relative to caved space
	NLMISC::CVectorI of; // absolute coordinates of caved space
	NLMISC::CVectorI ar; // allowed route area
	sint rs;   // tunnel radius size
	NLMISC::CVector main_direction;

	sint route_z_min;
	sint route_z_max;

	PseudoRandom *ps;
	PseudoRandom *ps2;

// 	content_t c_water_source;
// 	content_t c_lava_source;

	sint water_level;

	CCave() {}
	CCave(CMapGen *mg, PseudoRandom *ps, PseudoRandom *ps2, bool large_cave);
	void makeCave(CBlockMakeData& data, sint max_stone_height);
	void makeTunnel(bool dirswitch);
	void carveRoute(NLMISC::CVector vec, float f, bool randomize_xz);
};

#endif
