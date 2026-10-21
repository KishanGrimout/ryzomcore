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

#include "stdpch.h"

#include "map_server.h"
#include "master_pager.h"

CMapServer::CMapServer()
{
	m_pager = new CMasterPager(*this);
	static const uint64 memoryLimit = 3U * 1024U * 1024U * 1024U;
	initMap(m_pager, memoryLimit);
}

CMapServer::~CMapServer()
{
	delete m_pager;
	m_pager = NULL;
}
