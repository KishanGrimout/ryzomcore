// DynWorld - based on 'Ryzom - MMORPG Framework <http://dev.ryzom.com/projects/ryzom/>'
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

#ifndef CL_DYN_CONTINENT_H
#define CL_DYN_CONTINENT_H

#include "continent.h"

class CMapClient;

class CDynContinent : public CContinent
{
public:
	static CMapClient* MAP;

	/// setup the continent from a sheet (use the SheetName that MUST be set)
	virtual void setup();

	/// Update global parameters like the texture for the micro veget.
	virtual void select(const NLMISC::CVectorD &pos, NLMISC::IProgressCallback &progress, bool complete, bool unhibernate, EGSPD::CSeason::TSeason season);

	/// This will remove extra rsc used by the continent (fog maps ..)
	//virtual void unselect();
};

#endif
