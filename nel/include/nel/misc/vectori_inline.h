// NeL - MMORPG Framework <http://dev.ryzom.com/projects/nel/>
// Copyright (C) 2010  Winch Gate Property Limited
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Affero General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

#ifndef NL_VECTORI_INLINE_H
#define NL_VECTORI_INLINE_H

#include "types_nl.h"
#include "common.h"

namespace NLMISC
{

inline sint32 CVectorI::checkedComponent(double value)
{
    const double minValue = static_cast<double>(std::numeric_limits<sint32>::min());
    const double maxValue = static_cast<double>(std::numeric_limits<sint32>::max());
    nlassert(std::isfinite(value) && value >= minValue && value <= maxValue);

    if (std::isnan(value))
        return 0;
    if (value < minValue)
        return std::numeric_limits<sint32>::min();
    if (value > maxValue)
        return std::numeric_limits<sint32>::max();
    return static_cast<sint32>(value);
}

inline CVectorI::CVectorI(const CVector &v)
    : x(checkedComponent(v.x)), y(checkedComponent(v.y)), z(checkedComponent(v.z))
{
}

inline CVectorI::CVectorI(const CVectorD &v)
    : x(checkedComponent(v.x)), y(checkedComponent(v.y)), z(checkedComponent(v.z))
{
}

inline CVectorI &CVectorI::operator+=(const CVectorI &v)
{
    x += v.x;
    y += v.y;
    z += v.z;
    return *this;
}

inline CVectorI &CVectorI::operator-=(const CVectorI &v)
{
    x -= v.x;
    y -= v.y;
    z -= v.z;
    return *this;
}

inline CVectorI &CVectorI::operator*=(float f)
{
 x = checkedComponent(static_cast<double>(x) * f);
    y = checkedComponent(static_cast<double>(y) * f);
    z = checkedComponent(static_cast<double>(z) * f);
    return *this;
}

inline CVectorI CVectorI::operator+(const CVectorI &v) const
{
    return CVectorI(x + v.x, y + v.y, z + v.z);
}

inline CVectorI CVectorI::operator-(const CVectorI &v) const
{
    return CVectorI(x - v.x, y - v.y, z - v.z);
}

inline CVectorI CVectorI::operator-() const
{
    return CVectorI(-x, -y, -z);
}

inline CVectorI CVectorI::operator*(float f) const
{
   return CVectorI(checkedComponent(static_cast<double>(x) * f),
        checkedComponent(static_cast<double>(y) * f),
        checkedComponent(static_cast<double>(z) * f));
}

inline uint64 CVectorI::sqrnorm() const
{
    const sint64 sx = x;
    const sint64 sy = y;
    const sint64 sz = z;
    return static_cast<uint64>(sx * sx) + static_cast<uint64>(sy * sy) + static_cast<uint64>(sz * sz);
}

inline uint64 CVectorI::norm() const
{
    return static_cast<uint64>(std::sqrt(static_cast<long double>(sqrnorm())));
}

inline void CVectorI::set(sint32 _x, sint32 _y, sint32 _z)
{
    x = _x;
    y = _y;
    z = _z;
}

inline bool CVectorI::operator==(const CVectorI &v) const
{
    return x == v.x && y == v.y && z == v.z;
}

inline bool CVectorI::operator!=(const CVectorI &v) const
{
    return !(*this == v);
}

inline bool CVectorI::isNull() const
{
    return *this == CVectorI::Null;
}

inline bool CVectorI::operator<(const CVectorI &v) const
{
    if (x != v.x)
        return x < v.x;
    if (y != v.y)
        return y < v.y;
    return z < v.z;
}

inline void CVectorI::minof(const CVectorI &a, const CVectorI &b)
{
    x = std::min(a.x, b.x);
    y = std::min(a.y, b.y);
    z = std::min(a.z, b.z);
}

inline void CVectorI::maxof(const CVectorI &a, const CVectorI &b)
{
    x = std::max(a.x, b.x);
    y = std::max(a.y, b.y);
    z = std::max(a.z, b.z);
}

inline void CVectorI::serial(IStream &f)
{
    f.serial(x, y, z);
}

inline CVectorI &CVectorI::operator=(const CVector &v)
{
    x = checkedComponent(v.x);
    y = checkedComponent(v.y);
    z = checkedComponent(v.z);
    return *this;
}

inline CVectorI::operator CVector() const
{
    return CVector(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z));
}

inline void CVectorI::copyTo(CVector &dest) const
{
    dest.set(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z));
}

inline CVector CVectorI::asVector() const
{
    return CVector(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z));
}

}

#endif
