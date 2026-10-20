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

#include "material_definition.h"
#include "nel/3d/tile_bank.h"

using namespace NLMISC;
using namespace NL3D;

// Include material_library.h here to access CMaterialLibrary methods
#include "material_library.h"

void CMaterialDefinition::initialize(const char* defFilename)
{
	try
	{
		std::string temp;
		m_config.load(defFilename, true);

		{
			CConfigFile::CVar& var = m_config.getVar("Name");
			m_name = var.asString();
		}

		{
			CConfigFile::CVar& var = m_config.getVar("Id");
			m_id = var.asInt();
		}

		{
			CConfigFile::CVar& var = m_config.getVar("DensityMin");
			m_densityMin = var.asInt();
		}

		{
			CConfigFile::CVar& var = m_config.getVar("DensityMax");
			m_densityMax = var.asInt();
		}

		CMaterialTextureSet* currentTextureSet = nullptr;
		m_textures.clear();
		try
		{
			CConfigFile::CVar& var = m_config.getVar("Textures");
			for (uint i = 0; i < var.size(); ++i)
			{
				temp = var.asString(i);
				if (temp.find("Env:") == 0)
				{
					uint biomeId = CMaterialLibrary::getInstance().getEnvironmentId(temp.c_str() + sizeof("Env:") - 1);
					currentTextureSet = getTextureSet(biomeId);
					if (currentTextureSet == nullptr)
					{
						CMaterialTextureSet newTextureSet;
						newTextureSet.m_biomeId = biomeId;
						m_textures.push_back(newTextureSet);
						currentTextureSet = &m_textures.back();
					}
				}
				else
				{
					nlassert(currentTextureSet != nullptr);
					currentTextureSet->m_textures.push_back(CMaterialTextureDef(var.asString(i)));
				}
			}
		}
		catch (...)
		{

		}

		try
		{
			CConfigFile::CVar& var = m_config.getVar("TexturesSides");
			for (uint i = 0; i < var.size(); ++i)
			{
				temp = var.asString(i);
				if (temp.find("Env:") == 0)
				{
					uint biomeId = CMaterialLibrary::getInstance().getEnvironmentId(temp.c_str() + sizeof("Env:") - 1);
					currentTextureSet = getTextureSet(biomeId);
					if (currentTextureSet == nullptr)
					{
						CMaterialTextureSet newTextureSet;
						newTextureSet.m_biomeId = biomeId;
						m_textures.push_back(newTextureSet);
						currentTextureSet = &m_textures.back();
					}
				}
				else
				{
					nlassert(currentTextureSet != nullptr);
					currentTextureSet->m_texturesSides.push_back(CMaterialTextureDef(var.asString(i)));
				}
			}
		}
		catch (...)
		{

		}

		try
		{
			CConfigFile::CVar& var = m_config.getVar("TexturesTop");
			for (uint i = 0; i < var.size(); ++i)
			{
				temp = var.asString(i);
				if (temp.find("Env:") == 0)
				{
					uint biomeId = CMaterialLibrary::getInstance().getEnvironmentId(temp.c_str() + sizeof("Env:") - 1);
					currentTextureSet = getTextureSet(biomeId);
					if (currentTextureSet == nullptr)
					{
						CMaterialTextureSet newTextureSet;
						newTextureSet.m_biomeId = biomeId;
						m_textures.push_back(newTextureSet);
						currentTextureSet = &m_textures.back();
					}
				}
				else
				{
					nlassert(currentTextureSet != nullptr);
					currentTextureSet->m_texturesTop.push_back(CMaterialTextureDef(var.asString(i)));
				}
			}
		}
		catch (...)
		{

		}
	}
	catch (...)
	{
		nlassert(0);
	}
}

void CMaterialDefinition::bindMaterials(const CTileBank& tileBank)
{
	for (uint i = 0; i < m_textures.size(); ++i)
	{
		m_textures[i].bindMaterials(tileBank);
	}
}

CMaterialTextureSet* CMaterialDefinition::getTextureSet(uint biomeId)
{
	for (uint i = 0; i < m_textures.size(); ++i)
	{
		if (m_textures[i].m_biomeId == biomeId)
		{
			return &m_textures[i];
		}
	}
	return nullptr;
}

const CMaterialTextureSet* CMaterialDefinition::getTextureSet(uint biomeId) const
{
	for (uint i = 0; i < m_textures.size(); ++i)
	{
		if (m_textures[i].m_biomeId == biomeId)
		{
			return &m_textures[i];
		}
	}
	return nullptr;
}

void CMaterialTextureSet::bindSet(std::vector<CMaterialTextureDef>& matSet, const CTileBank& tileBank)
{
	for (uint i = 0; i < matSet.size(); ++i)
	{
		CMaterialTextureDef& matDef = matSet[i];

		for (uint ts = 0; ts < tileBank.getTileCount(); ts++)
		{
			const CTile* tile = tileBank.getTile(ts);
			const std::string& tileFilename = tile->getRelativeFileName(CTile::diffuse);
			if (tileFilename.find(matDef.m_baseName) == 0)
			{
				matDef.m_variations.push_back(ts);
			}
		}
	}
}

void CMaterialTextureSet::bindMaterials(const CTileBank& tileBank)
{
	bindSet(m_textures, tileBank);
	bindSet(m_texturesSides, tileBank);
	bindSet(m_texturesTop, tileBank);
}
