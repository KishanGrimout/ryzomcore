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

#include "environment_definition.h"

using namespace NLMISC;

void CEnvironmentDefinition::initialize(const char* defFilename)
{
	try
	{
		m_config.load(defFilename, true);

		{
			CConfigFile::CVar& nameVar = m_config.getVar("Name");
			m_name = nameVar.asString();
		}

		{
			CConfigFile::CVar& idVar = m_config.getVar("Id");
			m_id = idVar.asInt();
		}

		{
			CConfigFile::CVar& idVar = m_config.getVar("DefaultSoil");
			m_defaultSoilName = idVar.asString();
			m_defaultSoilId = (uint)-1;
		}

		m_mapgenParams.load(m_config);

		{
			CConfigFile::CVar& var = m_config.getVar("BigFlora");
			for (sint i = 0; i < var.size(); ++i)
			{
				uint32 sheetId = var.asInt(i);
				m_bigFloraSheetIds.push_back(sheetId);
			}
		}

		{
			CConfigFile::CVar& var = m_config.getVar("SmallFlora");
			for (sint i = 0; i < var.size(); ++i)
			{
				uint32 sheetId = var.asInt(i);
				m_smallFloraSheetIds.push_back(sheetId);
			}
		}
	}
	catch (...)
	{ }
}

float CEnvironmentDefinition::getMinGroundHeight() const
{
	return m_mapgenParams.np_terrain_base.offset - m_mapgenParams.np_terrain_base.scale + m_mapgenParams.np_soil.offset - m_mapgenParams.np_soil.scale;
}

float CEnvironmentDefinition::getMaxGroundHeight() const
{
	return m_mapgenParams.np_terrain_higher.offset + m_mapgenParams.np_terrain_higher.scale + m_mapgenParams.np_soil.offset + m_mapgenParams.np_soil.scale;
}
