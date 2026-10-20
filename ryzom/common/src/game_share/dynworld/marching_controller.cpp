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

#include "game_share/dynworld/marching_controller.h"


MarchingCubesController::DensityType MarchingCubesController::convertToDensity(Voxel voxel)
{
	DensityType density = static_cast<DensityType>(voxel.getDensity());
	return density;
}

uint g_interpolationMethod = 0;
float MarchingCubesController::interpolateDensity(Voxel a, Voxel b)
{
	MarchingCubesController::DensityType da = convertToDensity(a);
	MarchingCubesController::DensityType db = convertToDensity(b);
	float result = 0;
	switch (g_interpolationMethod)
	{
	case 0:
	{
		// regular method:
		float fThreshold = static_cast<float>(m_tThreshold);
		float fda = static_cast<float>(da);
		float fdb = static_cast<float>(db);
		result = (fThreshold - fda) / (fdb - fda);
	}
	break;

	case 1:
	{
		// custom method:
		float max = static_cast<float>(Voxel::MaxDensity);
		float coef = (max - .5f) / (max - 1.f);
		float cursor = static_cast<float>(db)-static_cast<float>(da);
		if (cursor > 0.f)
			result = (1.f - coef) * cursor + coef;
		else
			result = (1.f - coef) * cursor + (1.f - coef);
	}
	break;
	}
	return result;
}

Voxel MarchingCubesController::blendMaterials(Voxel a, Voxel b, float interp)
{
	const float blendThreshold = 1.f - ((float)m_tThreshold / ((float)getMaxDensity()));
	Voxel res = (interp < blendThreshold) ? a : b;
	if (res.getType() <= Voxel::LastNoDensity)
		res = (interp < blendThreshold) ? b : a;
	return res;
}
