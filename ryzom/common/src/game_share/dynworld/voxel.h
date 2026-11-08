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

#ifndef VOXEL_HEADER
#define VOXEL_HEADER

#include "nel/misc/debug.h"
#include "nel/misc/rgba.h"
#include "nel/misc/stream.h"
#include "nel/misc/types_nl.h"
#include "nel/misc/vectori.h"

/**
 * A voxel is stored in a 32-bit value. Its type determines how the remaining
 * bits are interpreted:
 *
 * - Air: stores RGB color in three 8-bit values (256 values per channel;
 *   alpha is always returned as 0).
 * - Object: stores a 24-bit sheet ID (16,777,216 possible IDs) and a
 *   2-bit object subtype.
 * - Liquid: stores a 2-bit liquid subtype and an 8-bit density
 *   (256 possible density values).
 * - Raw: stores an 8-bit material, a 3-bit biome, and an 8-bit density.
 *
 * The type field is 3 bits wide, allowing 8 encodings; four types are
 * currently named below. The biome field is 3 bits wide (8 encodings).
 * Subtype fields are 2 bits wide (4 encodings).
 */
class Voxel
{
public:
	typedef uint8 BiomeType;
	typedef uint8 MaterialType;
	typedef uint8 DensityType;

	enum Biome : BiomeType
	{
		// Stored in 3 bits: 8 possible encodings. Biome values are defined
		// by data; GenericBiome is the fallback/max value currently declared.
		GenericBiome = 7,
		MaxBiome = GenericBiome,
	};

	enum Type : MaterialType
	{
		// Stored in 3 bits: 8 possible encodings; these four are currently named.
		Air = 0, // Stores RGB color in the three parameter bytes.
		Object = 1, // Stores a 24-bit sheet ID and an object subtype.
		LastNoDensity = Object,
		Liquid = 2, // Stores a liquid subtype and an 8-bit density.
		Raw = 3, // Stores an 8-bit material and an 8-bit density.
	};

	enum LiquidType : MaterialType
	{
		// Stored in 2 bits: 4 possible encodings; Water and Sap are named.
		Water = 0,
		Sap = 1,
	};

	enum ObjectType : MaterialType
	{
		// Stored in 2 bits: 4 possible encodings; Static and Dynamic are named.
		Static = 0,
		Dynamic = 1
	};

	enum Constants
	{
		// Density is an unsigned 8-bit value, so its range is 0–255 (256 values).
		MaxDensity = 0xFF,
	};

    explicit Voxel(uint32 value = 0) : m_Value(value) {}

    Type getType() const
    {
        return static_cast<Type>((m_Value >> TypeShift) & FieldMask);
    }

    void setType(Type type)
    {
        m_Value = (m_Value & ~(FieldMask << TypeShift)) | ((static_cast<uint32>(type) & FieldMask) << TypeShift);
    }

    BiomeType getBiome() const
    {
        return static_cast<BiomeType>((m_Value >> BiomeShift) & FieldMask);
    }

    void setBiome(BiomeType biome)
    {
        nlassert(biome <= MaxBiome);
        m_Value = (m_Value & ~(FieldMask << BiomeShift)) | ((static_cast<uint32>(biome) & FieldMask) << BiomeShift);
    }

    MaterialType getMaterial() const
    {
        nlassert(getType() == Raw);
        return getParameter(8);
    }

    void setMaterial(MaterialType material)
    {
        nlassert(getType() == Raw);
        setParameter(8, material);
    }

    uint16 getMaterialAndBiome() const
    {
        nlassert(getType() == Raw);
        return getParameter(8) | (getBiome() << 8);
    }

    LiquidType getLiquidType() const
    {
        nlassert(getType() == Liquid);
        return static_cast<LiquidType>((m_Value >> SubtypeShift) & SubtypeMask);
    }

    void setLiquidType(LiquidType type)
    {
        nlassert(getType() == Liquid);
        m_Value = (m_Value & ~(SubtypeMask << SubtypeShift)) | ((static_cast<uint32>(type) & SubtypeMask) << SubtypeShift);
    }

    DensityType getDensity() const
    {
        return getType() > Voxel::LastNoDensity ? getParameter(0) : 0;
    }

    void setDensity(DensityType density)
    {
        nlassert(getType() == Liquid || getType() == Raw);
        setParameter(0, density);
    }

    void getColor(NLMISC::CRGBA& colorOut) const
    {
        nlassert(getType() == Air);
        colorOut.R = getParameter(0);
        colorOut.G = getParameter(8);
        colorOut.B = getParameter(16);
        colorOut.A = 0;
    }

    void setColor(const NLMISC::CRGBA& colorOut)
    {
        nlassert(getType() == Air);
        setParameter(0, colorOut.R);
        setParameter(8, colorOut.G);
        setParameter(16, colorOut.B);
    }

    uint32 getSheetId() const
    {
        nlassert(getType() == Object);
        return getParameter(0) | (getParameter(8) << 8) | (getParameter(16) << 16);
    }

    void setSheetId(uint32 sheetId)
    {
        nlassert(getType() == Object);
        nlassert((sheetId >> 24) == 0);
        setParameter(0, (sheetId >> 0) & 0xFF);
        setParameter(8, (sheetId >> 8) & 0xFF);
        setParameter(16, (sheetId >> 16) & 0xFF);
    }

    ObjectType getObjectType() const
    {
        nlassert(getType() == Object);
        return static_cast<ObjectType>((m_Value >> SubtypeShift) & SubtypeMask);
    }

    void setObjectType(ObjectType type)
    {
        nlassert(getType() == Object);
        m_Value = (m_Value & ~(SubtypeMask << SubtypeShift)) | ((static_cast<uint32>(type) & SubtypeMask) << SubtypeShift);
    }

    uint32 getRawValue() const
    {
        return m_Value;
    }

    void getRawParams(uint8* params) const
    {
        params[0] = getParameter(0);
        params[1] = getParameter(8);
        params[2] = getParameter(16);
    }

    bool operator == (Voxel other) const
    {
        return m_Value == other.m_Value;
    }

    bool operator != (Voxel other) const
    {
        return m_Value != other.m_Value;
    }

    bool operator > (Voxel other) const
    {
        return m_Value > other.m_Value;
    }

    void serial(NLMISC::IStream& f)
    {
        uint8 packedType = static_cast<uint8>(m_Value >> 24);
        uint8 param0 = getParameter(0);
        uint8 param1 = getParameter(8);
        uint8 param2 = getParameter(16);
        f.serial(packedType, param0, param1, param2);

        if (f.isReading())
        {
            m_Value = (static_cast<uint32>(packedType) << 24)
                | (static_cast<uint32>(param2) << 16)
                | (static_cast<uint32>(param1) << 8)
                | param0;
        }
    }

private:
    static constexpr uint32 BiomeShift = 24;
    static constexpr uint32 TypeShift = 27;
    static constexpr uint32 SubtypeShift = 30;
    static constexpr uint32 FieldMask = 0x7;
    static constexpr uint32 SubtypeMask = 0x3;

    uint8 getParameter(uint32 shift) const
    {
        return static_cast<uint8>((m_Value >> shift) & 0xFF);
    }

    void setParameter(uint32 shift, uint8 value)
    {
        const uint32 mask = 0xFFu << shift;
        m_Value = (m_Value & ~mask) | (static_cast<uint32>(value) << shift);
    }

    uint32 m_Value;
};

typedef NLMISC::CVectorI TVoxelPos;

#endif
