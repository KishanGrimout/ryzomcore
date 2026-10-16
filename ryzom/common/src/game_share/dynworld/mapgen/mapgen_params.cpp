// DynWorld
// Copyright (C) 2016 Kishan Grimout <kishan.grimout@gmail.com>
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

#include "mapgen_params.h"

#include "nel/misc/config_file.h"

using namespace NLMISC;

CMapgenParams::CMapgenParams()
	: seed(0)
	, water_level(1)
	, flags(Caves)
	, np_terrain_base(-4, 20.0*2.f, NLMISC::CVector(250.0, 250.0, 250.0)*4.f, 82341, 5, 0.6)
	, np_terrain_higher(20, 16.0*2.f, NLMISC::CVector(500.0, 500.0, 500.0)*2.f, 85039, 5, 0.6)
	, np_steepness(0.85, 0.5*2.f, NLMISC::CVector(125.0, 125.0, 125.0)*2.f, -932, 5, 0.7)
	, np_height_select(0.5, 1.0*2.f, NLMISC::CVector(250.0, 250.0, 250.0)*2.f, 4213, 5, 0.69)
	, np_soil(4, 2.0, NLMISC::CVector(200.0, 200.0, 200.0), 91013, 3, 0.55)
	, np_beach(0, 1.0, NLMISC::CVector(250.0, 250.0, 250.0), 59420, 3, 0.50)
    , np_biome(0, 0, NLMISC::CVector(1.0, 1.0, 1.0), 0, 0, 0)
	, np_cave(6, 6.0*2.f, NLMISC::CVector(250.0, 250.0, 250.0)*2.f, 34329, 3, 0.50)
	, np_humidity(0.5, 0.5, NLMISC::CVector(500.0, 500.0, 500.0), 72384, 4, 0.66)
	, np_trees(0, 1.0, NLMISC::CVector(125.0, 125.0, 125.0), 2, 4, 0.66)
	, np_apple_trees(0, 1.0, NLMISC::CVector(100.0, 100.0, 100.0), 342902, 3, 0.45)
{

}

void CMapgenParams::load(NLMISC::CConfigFile& cfg)
{
	try
	{
		{
			CConfigFile::CVar& var = cfg.getVar("Seed");
			seed = var.asInt();
		}

		{
			CConfigFile::CVar& var = cfg.getVar("WaterLevel");
			water_level = var.asInt();
		}

		{
			CConfigFile::CVar& var = cfg.getVar("Flags");
			flags = 0;
			for (sint i = 0; i < var.size(); ++i)
			{
				const std::string& flagStr = var.asString(i);
				if (flagStr == "Caves")
				{
					flags |= Caves;
				}
				else if (flagStr == "Flat")
				{
					flags |= Flat;
				}
			}
		}

		{
			CConfigFile::CVar& var = cfg.getVar("NP_terrain_base");
			np_terrain_base.load(var);
		}

		{
			CConfigFile::CVar& var = cfg.getVar("NP_terrain_higher");
			np_terrain_higher.load(var);
		}

		{
			CConfigFile::CVar& var = cfg.getVar("NP_steepness");
			np_steepness.load(var);
		}

		{
			CConfigFile::CVar& var = cfg.getVar("NP_height_select");
			np_height_select.load(var);
		}

		{
			CConfigFile::CVar& var = cfg.getVar("NP_soil");
			np_soil.load(var);
		}

		{
			CConfigFile::CVar& var = cfg.getVar("NP_beach");
			np_beach.load(var);
		}

		{
			CConfigFile::CVar& var = cfg.getVar("NP_cave");
			np_cave.load(var);
		}

		{
			CConfigFile::CVar& var = cfg.getVar("NP_humidity");
			np_humidity.load(var);
		}

		{
			CConfigFile::CVar& var = cfg.getVar("NP_trees");
			np_trees.load(var);
		}

		{
			CConfigFile::CVar& var = cfg.getVar("NP_apple_trees");
			np_apple_trees.load(var);
		}
	}
	catch (...)
	{
	}
}
