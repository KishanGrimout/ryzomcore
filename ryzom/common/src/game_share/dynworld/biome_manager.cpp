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

#include "biome_manager.h"

#include "material_library.h"

#include <ANL/Noise/anl_noise.h>

class CBiomeManagerImpl
{
public:
	anl::CCellularGenerator m_cellularGenerator;
	anl::CImplicitVoronoi m_voronoi;
	anl::CImplicitBasisFunction m_borderVariationX;
	anl::CImplicitBasisFunction m_borderVariationY;

	CBiomeManagerImpl()
		: m_cellularGenerator(anl::EUCLIDSQR)
		, m_voronoi(&m_cellularGenerator)
		, m_borderVariationX(anl::SIMPLEX, anl::CUBIC)
		, m_borderVariationY(anl::SIMPLEX, anl::CUBIC)
	{
		m_borderVariationX.setSeed(0); // arbitrary value
		m_borderVariationY.setSeed(50); // arbitrary value
	}
};

CBiomeManager::CBiomeManager()
{
	
}

CBiomeManager::~CBiomeManager()
{
	CBiomeManagerImpl* impl = getImpl();
	delete impl;
	m_impl.setPointer(nullptr);
}

CBiomeManagerImpl* CBiomeManager::getImpl()
{
	CBiomeManagerImpl* impl = reinterpret_cast<CBiomeManagerImpl*>(m_impl.getPointer());
	if (impl == nullptr)
	{
		impl = new CBiomeManagerImpl;
		m_impl.setPointer(impl);

#if BIOME_MANAGER_DEBUG_INFO
		NLMISC::CAutoMutex<NLMISC::CMutex> lock(m_tlsArrayLock);
		m_tlsInternal.push_back(std::make_pair(NLMISC::getThreadId(), impl));
#endif
	}
	return impl;
}

float g_biomePrecision = 0.5f; // precision of the positions given is 0.5m, size of a voxel
float g_invBiomePrecision = 1.f / g_biomePrecision;
float g_biomeCoordScale = 1.f / 500.f; // scale of the coordinates. Here, total scale should be 5000 meters
float g_biomeBorderScale = 50.f; // scale of the border noise. Here, noise applied on a range of 50 meters
float g_invBiomeBorderScale = 1.f / g_biomeBorderScale;
Voxel::BiomeType CBiomeManager::getBiome(sint x, sint y)
{
	CBiomeManagerImpl* impl = getImpl();
	// Compute some noise for the input, based on this input
	float borderX = float(x) * g_invBiomeBorderScale;
	float borderY = float(y) * g_invBiomeBorderScale;

	float dx = impl->m_borderVariationX.get(borderX, borderY) * g_biomeBorderScale;
	float dy = impl->m_borderVariationY.get(borderX, borderY) * g_biomeBorderScale;

	// Apply this noise to the input and scale to the biomes' size
	float fx = std::floor((float(x) + dx) * g_invBiomePrecision) * g_biomePrecision * g_biomeCoordScale;
	float fy = std::floor((float(y) + dy) * g_invBiomePrecision) * g_biomePrecision * g_biomeCoordScale;
	float cell = impl->m_voronoi.get(fx, fy); // in range [-1, +1]
	cell += 1.f; // go positive
	cell *= .5f; // go to 0,1

	// Here, we have N biomes, but the Nth is under ground (prime roots),
	// and as we don't want it here, we use its value as a "count" to avoid using it
	uint envCount = CMaterialLibrary::instance().getMaxEnvironmentId();
	cell *= envCount;

	Voxel::BiomeType biome = static_cast<Voxel::BiomeType>(cell);
	nlassert(CMaterialLibrary::instance().isValidEnvironment(biome));

	return biome;
}
