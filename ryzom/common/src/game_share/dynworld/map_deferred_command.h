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

#ifndef MAP_DEFERRED_COMMAND_HEADER
#define MAP_DEFERRED_COMMAND_HEADER

#include "polyvox_defs.h"
#include "nel/misc/thread.h"

#ifndef FINAL
#	define DYN_DEFERRED_COMMAND_DEBUG
#endif

class IMapDeferredCommand
{
protected:
#ifdef DYN_DEFERRED_COMMAND_DEBUG
	const IMapDeferredCommand* m_parent;
#endif

public:
	static const float s_maxPriority;

	IMapDeferredCommand(const IMapDeferredCommand* parent = nullptr) 
#ifdef DYN_DEFERRED_COMMAND_DEBUG
		: m_parent(parent)
#endif
	{}

	virtual ~IMapDeferredCommand() {}

	virtual float getPriority() const
	{
		return 0.f;
	}

	virtual void getName(std::string &result) const = 0;

	virtual bool execute() = 0;
};

#endif
