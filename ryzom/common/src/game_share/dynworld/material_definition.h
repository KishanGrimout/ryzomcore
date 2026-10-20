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

#ifndef MATERIAL_DEFINITION_HEADER
#define MATERIAL_DEFINITION_HEADER

#include "nel/misc/config_file.h"
#include "nel/3d/texture.h"

namespace NL3D
{
	class CTileBank;
	class CTile;
}

class CMaterialTextureDef
{
public:
	CMaterialTextureDef(const std::string& baseName)
		: m_baseName(baseName) {}
	std::string m_baseName;
	std::vector<uint16> m_variations; // TileBank tile indices
};

class CMaterialTextureSet
{
	void bindSet(std::vector<CMaterialTextureDef>& matSet, const NL3D::CTileBank& tileBank);

public:	
	std::vector<CMaterialTextureDef> m_textures;
	std::vector<CMaterialTextureDef> m_texturesSides;
	std::vector<CMaterialTextureDef> m_texturesTop;

	uint m_biomeId;

	void bindMaterials(const NL3D::CTileBank& tileBank);
};

class CMaterialDefinition
{
public:
	void initialize(const char* defFilename);
	void bindMaterials(const NL3D::CTileBank& tileBank);

	const char* getName() const { return m_name.c_str(); }
	uint getId() const { return m_id; }
	CMaterialTextureSet* getTextureSet(uint biomeId);
	const CMaterialTextureSet* getTextureSet(uint biomeId) const;

private:
	NLMISC::CConfigFile m_config;

	std::string m_name;
	uint m_id;

	uint8 m_densityMin;
	uint8 m_densityMax;

	std::vector<CMaterialTextureSet> m_textures;
};

// Forward declaration to break circular dependency
class CMaterialLibrary;

#endif
