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

#ifndef MATERIAL_LIBRARY_HEADER
#define MATERIAL_LIBRARY_HEADER

#include "environment_definition.h"
#include "material_definition.h"

#include "nel/misc/singleton.h"
#include "nel/misc/config_file.h"

namespace NL3D
{
	class CTileBank;
}

class CMaterialLibrary : public NLMISC::CSingleton<CMaterialLibrary>
{
public:
	void initialize(const char* libraryDef = "material_library.cfg");
	void bindMaterials(const NL3D::CTileBank& tileBank);

	uint getEnvironmentId(const char* envName) const;
	const char* getEnvironmentName(uint envId) const;

	uint getMaterialId(const char* matName) const;
	const char* getMaterialName(uint matId) const;

	const CEnvironmentDefinition* getEnvironmentDef(uint envId) const;
	const CEnvironmentDefinition* getEnvironmentDef(const char* envName) const;

	const CMaterialDefinition* getMaterialDef(uint matId) const;
	const CMaterialDefinition* getMaterialDef(const char* matName) const;

	bool isValidEnvironment(uint envId) const;
	bool isValidEnvironment(const char* envName) const;

	bool isValidMaterial(uint matId) const;
	bool isValidMaterial(const char* matName) const;

	uint getMaxEnvironmentId() const;
	uint getMaxMaterialId() const;

	float getMinGroundHeight() const { return m_minGroundHeight; }
	float getMaxGroundHeight() const { return m_maxGroundHeight; }

private:
	NLMISC::CConfigFile m_config;

	std::vector<CEnvironmentDefinition> m_environments;
	std::vector<CMaterialDefinition> m_materials;

	uint m_maxEnvironmentId;
	uint m_maxMaterialId;

	float m_minGroundHeight;
	float m_maxGroundHeight;
};

#endif
