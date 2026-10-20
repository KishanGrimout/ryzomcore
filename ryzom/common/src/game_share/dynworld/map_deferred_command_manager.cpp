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

#include "map_deferred_command_manager.h"
#include <limits>
#include <numeric>

const float IMapDeferredCommand::s_maxPriority = std::numeric_limits<float>::min();

CMapDeferredCommandManager::CMapDeferredCommandManager()
	: m_firstFreeSlot(ms_invalidSlotId)
{
	NLMISC::CParallelTaskManager::instance().registerTaskPriorityCallback(&m_changePriorityCallback);
}

CMapDeferredCommandManager::~CMapDeferredCommandManager()
{
	NLMISC::CParallelTaskManager::instance().unregisterTaskPriorityCallback(&m_changePriorityCallback);
}

void CMapDeferredCommandResult::run()
{
	bool executed = m_command->execute();

	if (executed)
	{
		m_isReady = true;
	}
	else
	{
		// Reschedule if not finished
		NLMISC::CParallelTaskManager::instance().addTask(this, m_command->getPriority());
	}
}

float CMapDeferredCommandManager::CChangeTaskPriority::getTaskPriority(const NLMISC::IRunnable &runable, float currentPriority)
{
	const CMapDeferredCommandResult* deferredCommand = dynamic_cast<const CMapDeferredCommandResult*>(&runable);
	if (deferredCommand != nullptr)
	{
		return deferredCommand->getCommand()->getPriority();
	}

	if (_Next != nullptr)
		return _Next->getTaskPriority(runable, currentPriority);

	return currentPriority;
}

void CMapDeferredCommandManager::updateFirstFreeSlot()
{
	for (uint i = m_firstFreeSlot + 1; i < m_commands.size(); ++i)
	{
		if (m_commands[i] == nullptr)
		{
			m_firstFreeSlot = i;
			return;
		}
	}
	m_firstFreeSlot = ms_invalidSlotId;
}

TMapDeferredCommandHandle CMapDeferredCommandManager::addCommand(IMapDeferredCommand* command)
{
	NLMISC::CAutoMutex<TMutex> lock(m_lock);
	CMapDeferredCommandResult* result = new CMapDeferredCommandResult(command);
	TMapDeferredCommandHandle commandIndex;
	if (m_firstFreeSlot != ms_invalidSlotId)
	{
		commandIndex = m_firstFreeSlot;
		m_commands[commandIndex] = result;
		updateFirstFreeSlot();
	}
	else
	{
		commandIndex = m_commands.size();
		m_commands.push_back(result);
	}

	NLMISC::CParallelTaskManager::instance().addTask(result, command->getPriority());

	return commandIndex;
}

void CMapDeferredCommandManager::removeCommand(TMapDeferredCommandHandle index)
{
	NLMISC::CAutoMutex<TMutex> lock(m_lock);
	if (index < m_commands.size())
	{
		//nlverify(deleteTask(m_commands[index]));
		delete m_commands[index];
		m_commands[index] = nullptr;

		if (index < m_firstFreeSlot)
		{
			m_firstFreeSlot = index;
		}
	}
}

const CMapDeferredCommandResult& CMapDeferredCommandManager::getCommand(TMapDeferredCommandHandle index) const
{
	NLMISC::CAutoMutex<TMutex> lock(m_lock);
	nlassert(index < m_commands.size());
	return *m_commands[index];
}

bool CMapDeferredCommandManager::isCommandValid(TMapDeferredCommandHandle index) const
{
	NLMISC::CAutoMutex<TMutex> lock(m_lock);
	nlassert(index < m_commands.size());
	return m_commands[index] != nullptr;
}
