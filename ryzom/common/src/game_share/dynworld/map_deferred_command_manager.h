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

#ifndef MAP_DEFERRED_COMMAND_MANAGER_HEADER
#define MAP_DEFERRED_COMMAND_MANAGER_HEADER

#include "nel/misc/singleton.h"
#include "nel/misc/mutex.h"
#include "nel/misc/parallel_task_manager.h"
#include "nel/misc/time_nl.h"

#include "map_deferred_command.h"


class CMapDeferredCommandResult : public NLMISC::IRunnable
{
public:
	CMapDeferredCommandResult(IMapDeferredCommand* command)
		: m_command(command)
		, m_isReady(0)
	{}

	IMapDeferredCommand* getCommand() const
	{
		return m_command;
	}

	bool isReady() const
	{
		return m_isReady != 0;
	}

	virtual void run() override;

	virtual void getName(std::string &result) const override
	{
		m_command->getName(result);
	}

private:
	IMapDeferredCommand* m_command;
	sint32 m_isReady;
};

typedef uint TMapDeferredCommandHandle;

class CMapDeferredCommandManager : public NLMISC::CSingleton<CMapDeferredCommandManager>
{
	typedef NLMISC::CFairMutex TMutex;
	mutable TMutex m_lock;
	std::vector<CMapDeferredCommandResult*> m_commands;
	uint32 m_firstFreeSlot;
	static const sint ms_invalidSlotId = 0xFFFFFFFF;

	class CChangeTaskPriority : public NLMISC::CTaskManager::IChangeTaskPriority
	{
		virtual float getTaskPriority(const NLMISC::IRunnable &runable, float currentPriority) override;
	} m_changePriorityCallback;

	void updateFirstFreeSlot();

public:
	CMapDeferredCommandManager();
	~CMapDeferredCommandManager();

	TMapDeferredCommandHandle addCommand(IMapDeferredCommand* command);
	void removeCommand(TMapDeferredCommandHandle index);
	const CMapDeferredCommandResult& getCommand(TMapDeferredCommandHandle index) const;
	bool isCommandValid(TMapDeferredCommandHandle index) const;
};

#endif
