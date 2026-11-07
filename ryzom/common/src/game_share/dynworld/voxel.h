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

class Voxel
{
public:
    typedef uint8 BiomeType;
    typedef uint8 MaterialType;
    typedef uint8 DensityType;

    enum Biome : BiomeType
    {
        // Defined on 3 bits
        // All first values are defined in data
        GenericBiome = 7,
        MaxBiome = GenericBiome,
    };

    enum Type : MaterialType
    {
        // Defined on 3 bits
        Air = 0,
        Object = 1,
        LastNoDensity = Object,
        Liquid = 2,
        Raw = 3,
    };

    enum LiquidType : MaterialType
    {
        // Defined on 2 bits for m_subtype
        Water = 0,
        Sap = 1,
    };

    enum ObjectType : MaterialType
    {
        Static = 0,
        Dynamic = 1
    };

    enum Constants
    {
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
