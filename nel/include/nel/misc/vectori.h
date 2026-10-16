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

#ifndef NL_VECTORI_H
#define NL_VECTORI_H

#include "types_nl.h"
#include "vector.h"
#include "vectord.h"

#include <cmath>
#include <limits>
#include <string>

#include "stream.h"

namespace NLMISC
{

class IStream;

class CVectorI
{
public:
    /**
     * A 3D vector of integers.
     * \author Kishan Grimout
     * \date 2014
     */
    sint32 x, y, z;

    static const CVectorI Null;
    static const CVectorI I;
    static const CVectorI J;
    static const CVectorI K;

    CVectorI() {}
    CVectorI(sint32 _x, sint32 _y, sint32 _z) : x(_x), y(_y), z(_z) {}
    CVectorI(const CVectorI &v) : x(v.x), y(v.y), z(v.z) {}
    CVectorI(const CVector &v);
    CVectorI(const CVectorD &v);

    CVectorI &operator+=(const CVectorI &v);
    CVectorI &operator-=(const CVectorI &v);
    CVectorI &operator*=(float f);
    CVectorI operator+(const CVectorI &v) const;
    CVectorI operator-(const CVectorI &v) const;
    CVectorI operator-() const;
    CVectorI operator*(float f) const;

    uint64 norm() const;
    uint64 sqrnorm() const;

    void set(sint32 _x, sint32 _y, sint32 _z);
    bool operator==(const CVectorI &v) const;
    bool operator!=(const CVectorI &v) const;
    bool isNull() const;
    bool operator<(const CVectorI &v) const;

    void minof(const CVectorI &a, const CVectorI &b);
    void maxof(const CVectorI &a, const CVectorI &b);
    void serial(IStream &f);
    CVectorI &operator=(const CVector &v);
    operator CVector() const;
    void copyTo(CVector &dest) const;
    CVector asVector() const;

    std::string asString() const { return toString(); }
    std::string toString() const;

private:
    static sint32 checkedComponent(double value);
};

}

#include "vectori_inline.h"

#endif // NL_VECTORI_H
