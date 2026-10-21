// DynWorld
// Copyright (C) 2016 Kishan Grimout <kishan.grimout@gmail.com>
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
// along with this program;  If not, see <http://www.gnu.org/licenses/>.

#include "disk_file_db.h"

const char* CDiskFileDataBase::ms_defaultPath = "./cache/map/";

CDiskFileDataBase::CDiskFileDataBase(const char* dbPath)
{
	init(dbPath);
}

void CDiskFileDataBase::init(const char* dbPath)
{
	NLMISC::CFile::createDirectory(ms_defaultPath);
}

void CDiskFileDataBase::close()
{
	
}

bool CDiskFileDataBase::isValid()
{
	return true;
}

void CDiskFileDataBase::flush()
{
	
}

void CDiskFileDataBase::handleError()
{
	
}
