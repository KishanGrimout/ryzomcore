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

#ifndef MARCHING_CONTROLLER_HEADER
#define MARCHING_CONTROLLER_HEADER

#include "game_share/dynworld/voxel.h"

class MarchingCubesController
{
public:
	/// Used to inform the MarchingCubesSurfaceExtractor about which type it should use for representing densities.
	typedef float DensityType;
	/// Used to inform the MarchingCubesSurfaceExtractor about which type it should use for representing materials. We're using a float here
	/// because this implementation always returns a constant value off 1.0f. PolyVox also uses floats to store the materials in the mesh vertices
	/// but this is not really desirable on modern hardware. We'll probably come back to material representation in the future.
	typedef uint8 MaterialType;

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	/// Constructor
	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	/// This version of the constructor takes no parameters and sets the threshold to the middle of the representable range of the underlying type.
	/// For example, if the voxel type is 'uint8_t' then the representable range is 0-255, and the threshold will be set to 127. On the other hand,
	/// if the voxel type is 'float' then the representable range is -FLT_MAX to FLT_MAX and the threshold will be set to zero.
	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	MarchingCubesController(void)
	{
		m_tThreshold = ((std::numeric_limits<DensityType>::min)() + (std::numeric_limits<DensityType>::max)()) / 2;
	}

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	/// Constructor
	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	/// This version of the constructor allows you to set a custom threshold.
	/// \param tThreshold The threshold to use.
	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	MarchingCubesController(DensityType tThreshold)
	{
		m_tThreshold = tThreshold;
	}

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	/// Converts the underlying voxel type into a density value.
	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	/// The default implementation of this function just returns the voxel type directly and is suitable for primitives types. Specializations of
	/// this class can modify this behaviour.
	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	DensityType convertToDensity(Voxel voxel);

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	/// Converts the underlying voxel type into a material value.
	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	/// The default implementation of this function just returns the constant '1'. There's not much else it can do, as it needs to work with primitive
	/// types and the actual value of the type is already being considered to be the density. Specializations of this class can modify this behaviour.
	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	MaterialType convertToMaterial(Voxel voxel)
	{
		return voxel.getType();
	}

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	/// Returns the density value which was passed to the constructor.
	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	/// As mentioned in the class description, the extracted surface will pass through the density value specified by the threshold, and so you
	/// should make sure that the threshold value you choose is between the minimum and maximum values found in your volume data. By default it
	///is in the middle of the representable range of the underlying type.
	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	DensityType getThreshold(void)
	{
		return m_tThreshold;
	}

	DensityType getMaxDensity()
	{
		return Voxel::MaxDensity;
	}

	float interpolateDensity(Voxel a, Voxel b);

	Voxel blendMaterials(Voxel a, Voxel b, float interp);

private:
	DensityType m_tThreshold;
};

#endif
