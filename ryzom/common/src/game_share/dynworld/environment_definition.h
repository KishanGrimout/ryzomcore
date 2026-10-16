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

#ifndef ENVIRONMENT_DEFINITION_HEADER
#define ENVIRONMENT_DEFINITION_HEADER

#include <string>
#include <vector>

#include "nel/misc/config_file.h"

#include "game_share/dynworld/mapgen/mapgen_params.h"

class CEnvironmentDefinition
{
public:
	void initialize(const char* defFilename);

	const char* getName() const { return m_name.c_str(); }
	uint getId() const { return m_id; }

	const char* getDefaultSoilName() const { return m_defaultSoilName.c_str(); }
	uint getDefaultSoilId() const { return m_defaultSoilId; }
	void setDefaultSoilId(uint id) { m_defaultSoilId = id; }

	const CMapgenParams& getMapgenParams() const { return m_mapgenParams; }

	float getMinGroundHeight() const;
	float getMaxGroundHeight() const;

	const std::vector<uint32>& getBigFloraSheetIds() const { return m_bigFloraSheetIds; }
	const std::vector<uint32>& getSmallFloraSheetIds() const { return m_smallFloraSheetIds; }

private:
	NLMISC::CConfigFile m_config;

	std::string m_name;
	uint m_id;
	std::string m_defaultSoilName;
	uint m_defaultSoilId;

	CMapgenParams m_mapgenParams;

	std::vector<uint32> m_bigFloraSheetIds;
	std::vector<uint32> m_smallFloraSheetIds;
};

#endif
