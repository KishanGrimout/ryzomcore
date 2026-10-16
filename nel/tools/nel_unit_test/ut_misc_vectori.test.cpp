// NeL - MMORPG Framework <http://dev.ryzom.com/projects/nel/>
// Copyright (C) 2010  Winch Gate Property Limited
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

#include <gtest/gtest.h>

#include <limits>

#include <nel/misc/vectori.h>

TEST(CVectorITest, squaredLengthUsesWideArithmetic)
{
    const NLMISC::CVectorI vector(50000, 50000, 50000);
 EXPECT_EQ(vector.sqrnorm(), uint64(7500000000ULL));
}

TEST(CVectorITest, lengthSupportsFullSint32Range)
{
    const sint32 minimum = std::numeric_limits<sint32>::min();
    const NLMISC::CVectorI vector(minimum, 0, 0);
   EXPECT_EQ(vector.sqrnorm(), uint64(1) << 62);
    EXPECT_EQ(vector.norm(), uint64(1) << 31);

    const NLMISC::CVectorI maximumMagnitude(minimum, minimum, minimum);
    EXPECT_EQ(maximumMagnitude.sqrnorm(), uint64(3) * (uint64(1) << 62));
}

TEST(CVectorITest, floatingPointConversionsTruncateTowardZero)
{
    const NLMISC::CVectorI fromDouble(NLMISC::CVectorD(1.75, -2.75, 3.0));
    EXPECT_EQ(fromDouble.x, 1);
    EXPECT_EQ(fromDouble.y, -2);
    EXPECT_EQ(fromDouble.z, 3);

    const NLMISC::CVectorI scaled = NLMISC::CVectorI(2, -2, 3) * 1.5f;
    EXPECT_EQ(scaled.x, 3);
    EXPECT_EQ(scaled.y, -3);
    EXPECT_EQ(scaled.z, 4);
}

#if !defined(NL_DEBUG)
TEST(CVectorITest, invalidFloatingPointConversionsAreClamped)
{
    const sint32 minimum = std::numeric_limits<sint32>::min();
    const sint32 maximum = std::numeric_limits<sint32>::max();
    const NLMISC::CVectorI converted(NLMISC::CVectorD(
        static_cast<double>(maximum) + 1.0,
        static_cast<double>(minimum) - 1.0,
        std::numeric_limits<double>::quiet_NaN()));
    EXPECT_EQ(converted.x, maximum);
    EXPECT_EQ(converted.y, minimum);
    EXPECT_EQ(converted.z, 0);

    const NLMISC::CVectorI scaled = NLMISC::CVectorI(1, -1, 0) * std::numeric_limits<float>::infinity();
    EXPECT_EQ(scaled.x, maximum);
    EXPECT_EQ(scaled.y, minimum);
    EXPECT_EQ(scaled.z, 0);
}
#endif
