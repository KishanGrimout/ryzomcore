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

#include "material_library.h"

using namespace NLMISC;
using namespace NL3D;

void CMaterialLibrary::initialize(const char* libraryDef)
{
	m_maxEnvironmentId = 0;
	m_maxMaterialId = 0;
	m_minGroundHeight = std::numeric_limits<float>::max();
	m_maxGroundHeight = std::numeric_limits<float>::min();

	try
	{
		m_config.load(libraryDef, true);

		std::string tempVal;

		{
			CConfigFile::CVar& envs = m_config.getVar("Environments");
			m_environments.resize(envs.size());
			for (uint i = 0; i < envs.size(); ++i)
			{
				CEnvironmentDefinition& env = m_environments[i];

				tempVal = envs.asString(i);
				env.initialize(tempVal.c_str());

				if (env.getId() > m_maxEnvironmentId)
				{
					m_maxEnvironmentId = env.getId();
				}

				if (env.getMinGroundHeight() < m_minGroundHeight)
				{
					m_minGroundHeight = env.getMinGroundHeight();
				}

				if (env.getMaxGroundHeight() > m_maxGroundHeight)
				{
					m_maxGroundHeight = env.getMaxGroundHeight();
				}
			}
		}

		{
			CConfigFile::CVar& mats = m_config.getVar("Materials");
			m_materials.resize(mats.size());
			for (uint i = 0; i < mats.size(); ++i)
			{
				tempVal = mats.asString(i);
				m_materials[i].initialize(tempVal.c_str());
				if (m_materials[i].getId() > m_maxMaterialId)
				{
					m_maxMaterialId = m_materials[i].getId();
				}
			}
		}
	}
	catch (...)
	{
		nlwarning("Error when loading MaterialLibrary infos");
	}

	// Resolve default environment soil material IDs
	for (uint i = 0; i < m_environments.size(); ++i)
	{
		CEnvironmentDefinition& env = m_environments[i];
		uint soilId = getMaterialId(env.getDefaultSoilName());
		env.setDefaultSoilId(soilId);
	}
}

void CMaterialLibrary::bindMaterials(const CTileBank& tileBank)
{
	for (uint i = 0; i < m_materials.size(); ++i)
	{
		m_materials[i].bindMaterials(tileBank);
	}
}

uint CMaterialLibrary::getEnvironmentId(const char* envName) const
{
	const CEnvironmentDefinition* envDef = getEnvironmentDef(envName);
	return envDef != NULL ? envDef->getId() : (uint)-1;
}

const char* CMaterialLibrary::getEnvironmentName(uint envId) const
{
	const CEnvironmentDefinition* envDef = getEnvironmentDef(envId);
	return envDef != NULL ? envDef->getName() : "";
}

uint CMaterialLibrary::getMaterialId(const char* matName) const
{
	const CMaterialDefinition* matDef = getMaterialDef(matName);
	return matDef != NULL ? matDef->getId() : (uint)-1;
}

const char* CMaterialLibrary::getMaterialName(uint matId) const
{
	const CMaterialDefinition* matDef = getMaterialDef(matId);
	return matDef != NULL ? matDef->getName() : "";
}

const CEnvironmentDefinition* CMaterialLibrary::getEnvironmentDef(uint envId) const
{
	for (uint i = 0; i < m_environments.size(); ++i)
	{
		if (m_environments[i].getId() == envId)
			return &m_environments[i];
	}
	return NULL;
}

const CEnvironmentDefinition* CMaterialLibrary::getEnvironmentDef(const char* envName) const
{
	for (uint i = 0; i < m_environments.size(); ++i)
	{
		if (strcmp(m_environments[i].getName(), envName) == 0)
			return &m_environments[i];
	}
	return NULL;
}

const CMaterialDefinition* CMaterialLibrary::getMaterialDef(uint matId) const
{
	for (uint i = 0; i < m_materials.size(); ++i)
	{
		if (m_materials[i].getId() == matId)
			return &m_materials[i];
	}
	return NULL;
}

const CMaterialDefinition* CMaterialLibrary::getMaterialDef(const char* matName) const
{
	for (uint i = 0; i < m_materials.size(); ++i)
	{
		if (strcmp(m_materials[i].getName(), matName) == 0)
			return &m_materials[i];
	}
	return NULL;
}

bool CMaterialLibrary::isValidEnvironment(uint envId) const
{
	return getEnvironmentDef(envId) != NULL;
}

bool CMaterialLibrary::isValidEnvironment(const char* envName) const
{
	return getEnvironmentDef(envName) != NULL;
}

bool CMaterialLibrary::isValidMaterial(uint matId) const
{
	return getMaterialDef(matId) != NULL;
}

bool CMaterialLibrary::isValidMaterial(const char* matName) const
{
	return getMaterialDef(matName) != NULL;
}

uint CMaterialLibrary::getMaxEnvironmentId() const
{
	return m_maxEnvironmentId;
}

uint CMaterialLibrary::getMaxMaterialId() const
{
	return m_maxMaterialId;
}
