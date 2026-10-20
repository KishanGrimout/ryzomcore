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

#ifndef REGION_MESH_INTERFACE_HEADER
#define REGION_MESH_INTERFACE_HEADER

#include "game_share/dynworld/polyvox_defs.h"
#include "nel/misc/smart_ptr.h"

class IRegionMesh : public NLMISC::CRefCount
{
public:
	virtual ~IRegionMesh() {}
	virtual void initMesh(const TDecodedMesh& mesh) = 0;
};

#endif
