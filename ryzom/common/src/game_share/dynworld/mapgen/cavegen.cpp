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

#include "cavegen.h"

#include "nel/misc/vector.h"

#include "game_share/utils.h"
#include "game_share/dynworld/material_library.h"
#include "game_share/dynworld/biome_manager.h"
#include "game_share/dynworld/mapgen/mapgen.h"

#define rangelim(d, min, max) ((d) < (min) ? (min) : ((d)>(max)?(max):(d)))

using namespace NLMISC;

NoiseParams nparams_caveliquids(0, 1, CVector(150.0, 150.0, 150.0), 776, 3, 0.6);


///////////////////////////////////////////////////////////////////////////////


CCave::CCave(CMapGen *mg, PseudoRandom *ps, PseudoRandom *ps2, bool is_large_cave)
{
	this->mg = mg;
	//this->vm = mg->vm;
	//this->ndef = mg->ndef;
	//this->water_level = mg->water_level;
	this->large_cave = is_large_cave;
	this->ps = ps;
	this->ps2 = ps2;
// 	this->c_water_source = mg->c_water_source;
// 	this->c_lava_source = mg->c_lava_source;

	min_tunnel_diameter = 2;
	max_tunnel_diameter = ps->range(2, 6);
	dswitchint = ps->range(1, 14);
	flooded = true;

	if (large_cave)
	{
		part_max_length_rs = ps->range(2, 4);
		tunnel_routepoints = ps->range(5, ps->range(15, 30));
		min_tunnel_diameter = 5;
		max_tunnel_diameter = ps->range(7, ps->range(8, 24));
	}
	else
	{
		part_max_length_rs = ps->range(2, 9);
		tunnel_routepoints = ps->range(10, ps->range(15, 30));
	}

	large_cave_is_flat = (ps->range(0, 1) == 0);
}


void CCave::makeCave(CBlockMakeData& data, int max_stone_height)
{
	block_data = &data;
	node_min = ToNeLVectorI(data.m_region.getLowerCorner());
	node_max = ToNeLVectorI(data.m_region.getUpperCorner());
	max_stone_z = max_stone_height;
	main_direction = CVector(0, 0, 0);

	// Allowed route area size in nodes
	ar = node_max - node_min + CVectorI(1, 1, 1);
	// Area starting point in nodes
	of = node_min;

	// Allow a bit more
	//(this should be more than the maximum radius of the tunnel)
	const sint max_spread_amount = 64/*MAP_BLOCKSIZE*/;
	sint insure = 10;
	sint more = std::max(max_spread_amount - max_tunnel_diameter / 2 - insure, 1);
	ar += CVectorI(1, 1, 0) * more * 2;
	of -= CVectorI(1, 1, 0) * more;

	route_z_min = 0;
	// Allow half a diameter + 7 over stone surface
	route_z_max = -of.z + max_stone_z + max_tunnel_diameter / 2 + 7;

	// Limit maximum to area
	route_z_max = rangelim(route_z_max, 0, ar.z - 1);

	if (large_cave)
	{
		sint min = 0;
		/*if (node_min.z < water_level && node_max.z > water_level)
		{
			min = water_level - max_tunnel_diameter / 3 - of.z;
			route_z_max = water_level + max_tunnel_diameter / 3 - of.z;
		}*/
		route_z_min = ps->range(min, min + max_tunnel_diameter);
		route_z_min = rangelim(route_z_min, 0, route_z_max);
	}

	sint route_start_z_min = route_z_min;
	sint route_start_z_max = route_z_max;

	route_start_z_min = rangelim(route_start_z_min, 0, ar.z - 1);
	route_start_z_max = rangelim(route_start_z_max, route_start_z_min, ar.z - 1);

	// Randomize starting position
	orp = CVector(
		(float)(ps->next() % ar.x) + 0.5,
		(float)(ps->next() % ar.y) + 0.5,
		(float)(ps->range(route_start_z_min, route_start_z_max)) + 0.5
		);

// 	int notifytype = large_cave ? GENNOTIFY_LARGECAVE_BEGIN : GENNOTIFY_CAVE_BEGIN;
// 	if (mg->gennotify & (1 << notifytype)) {
// 		std::vector <CVectorI> *nvec = mg->gen_notifications[notifytype];
// 		nvec->push_back(CVectorI(of.x + orp.x, of.y + orp.y, of.z + orp.z));
// 	}

	// Generate some tunnel starting from orp
	for (uint j = 0; j < tunnel_routepoints; j++)
		makeTunnel(j % dswitchint == 0);

// 	notifytype = large_cave ? GENNOTIFY_LARGECAVE_END : GENNOTIFY_CAVE_END;
// 	if (mg->gennotify & (1 << notifytype)) {
// 		std::vector <CVectorI> *nvec = mg->gen_notifications[notifytype];
// 		nvec->push_back(CVectorI(of.x + orp.x, of.y + orp.y, of.z + orp.z));
// 	}
}


void CCave::makeTunnel(bool dirswitch)
{
	if (dirswitch && !large_cave)
	{
		main_direction = CVector(
			((float)(ps->next() % 20) - (float)10) / 10,
			((float)(ps->next() % 20) - (float)10) / 10,
			((float)(ps->next() % 20) - (float)10) / 30
			);
		main_direction *= (float)ps->range(0, 10) / 10;
	}

	// Randomize size
	sint min_d = min_tunnel_diameter;
	sint max_d = max_tunnel_diameter;
	rs = ps->range(min_d, max_d);

	CVectorI maxlen;
	if (large_cave)
	{
		maxlen = CVectorI(
			rs * part_max_length_rs,
			rs * part_max_length_rs,
			rs * part_max_length_rs / 2
			);
	}
	else
	{
		maxlen = CVectorI(
			rs * part_max_length_rs,
			rs * part_max_length_rs,
			ps->range(1, rs * part_max_length_rs)
			);
	}

	CVector vec(
		(float)(ps->next() % (maxlen.x * 1)) - (float)maxlen.x / 2,
		(float)(ps->next() % (maxlen.y * 1)) - (float)maxlen.y / 2,
		(float)(ps->next() % (maxlen.z * 1)) - (float)maxlen.z / 2
		);

	// Jump downward sometimes
	if (!large_cave && ps->range(0, 12) == 0)
	{
		vec = CVector(
			(float)(ps->next() % (maxlen.x * 1)) - (float)maxlen.x / 2,
			(float)(ps->next() % (maxlen.y * 1)) - (float)maxlen.y / 2,
			(float)(ps->next() % (maxlen.z * 2)) - (float)maxlen.z
			);
	}

	vec += main_direction;

	CVector rp = orp + vec;
	if (rp.x < 0)
		rp.x = 0;
	else if (rp.x >= ar.x)
		rp.x = ar.x - 1;

	if (rp.y < 0)
		rp.y = 0;
	else if (rp.y >= ar.y)
		rp.y = ar.y - 1;

	if (rp.z < route_z_min)
		rp.z = route_z_min;
	else if (rp.z >= route_z_max)
		rp.z = route_z_max - 1;

	vec = rp - orp;

	float veclen = vec.norm();
	// As odd as it sounds, veclen is *exactly* 0.0 sometimes, causing a FPE
	if (veclen < 0.05)
		veclen = 1.0;

	// Every second section is rough
	bool randomize_xz = (ps2->range(1, 2) == 1);

	// Carve routes
	for (float f = 0; f < 1.0; f += 1.0 / veclen)
		carveRoute(vec, f, randomize_xz);

	orp = rp;
}


void CCave::carveRoute(CVector vec, float f, bool randomize_xy)
{
	Voxel airnode; airnode.setType(Voxel::Air);
// 	MapNode waternode(c_water_source);
// 	MapNode lavanode(c_lava_source);

	CVectorI startp(orp.x, orp.y, orp.z);
	startp += of;

	CVector fp = orp + vec * f;
	fp.x += 0.1 * ps->range(-10, 10);
	fp.y += 0.1 * ps->range(-10, 10);
	CVectorI cp(fp.x, fp.y, fp.z);

	sint d0 = -rs / 2;
	sint d1 = d0 + rs;
	if (randomize_xy)
	{
		d0 += ps->range(-1, 1);
		d1 += ps->range(-1, 1);
	}

	for (sint y0 = d0; y0 <= d1; y0++)
	{
		sint si = rs / 2 - std::max(0, abs(y0) - rs / 7 - 1);
		for (sint x0 = -si - ps->range(0, 1); x0 <= si - 1 + ps->range(0, 1); x0++)
		{
			sint maxabsxy = std::max(abs(x0), abs(y0));
			sint si2 = rs / 2 - std::max(0, maxabsxy - rs / 7 - 1);
			for (sint z0 = -si2; z0 <= si2; z0++)
			{
				if (large_cave_is_flat)
				{
					// Make large caves not so tall
					if (rs > 7 && abs(z0) >= rs / 3)
						continue;
				}

				CVectorI p(cp.x + x0, cp.y + z0, cp.z + y0);
				p += of;

				//if (vm->m_area.contains(p) == false)
				if (!block_data->m_region.containsPoint(ToPolyVoxVectorI(p)))
					continue;

				//uint i = vm->m_area.index(p);
				Voxel c = block_data->m_chunk.getVoxel(p.x - block_data->m_region.getLowerX(), p.y - block_data->m_region.getLowerY(), p.z - block_data->m_region.getLowerZ());
				//content_t c = vm->m_data[i].getContent();
				//if (!ndef->get(c).is_ground_content)
				if (c.getType() != Voxel::Raw)
					continue;

				if (large_cave)
				{
					int full_zmin = node_min.z - 64/*MAP_BLOCKSIZE*/;
					int full_zmax = node_max.z + 64/*MAP_BLOCKSIZE*/;
// 
// 					if (flooded && full_zmin < water_level && full_zmax > water_level)
// 					{
// 						vm->m_data[i] = (p.z <= water_level) ? waternode : airnode;
// 					}
// 					else if (flooded && full_zmax < water_level) {
// 						vm->m_data[i] = (p.z < startp.z - 2) ? lavanode : airnode;
// 					}
// 					else
					{
						//vm->m_data[i] = airnode;
						block_data->m_chunk.setVoxel(p.x - block_data->m_region.getLowerX(), p.y - block_data->m_region.getLowerY(), p.z - block_data->m_region.getLowerZ(), airnode);
					}
				}
				else
				{
					// Don't replace air or water or lava or ignore
// 					if (c == CONTENT_IGNORE || c == CONTENT_AIR ||
// 						c == c_water_source || c == c_lava_source)
					if (c.getType() == Voxel::Air || c.getType() == Voxel::Liquid)
						continue;

					block_data->m_chunk.setVoxel(p.x - block_data->m_region.getLowerX(), p.y - block_data->m_region.getLowerY(), p.z - block_data->m_region.getLowerZ(), airnode);
// 					vm->m_data[i] = airnode;
// 					vm->m_flags[i] |= VMANIP_FLAG_CAVE;
				}
			}
		}
	}
}
