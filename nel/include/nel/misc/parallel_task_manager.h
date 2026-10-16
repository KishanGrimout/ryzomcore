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

#ifndef NL_PARALLEL_TASK_MANAGER_H
#define NL_PARALLEL_TASK_MANAGER_H

#include "nel/misc/singleton.h"
#include "nel/misc/task_manager.h"

#include <memory>
#include <mutex>
#include <vector>

namespace NLMISC {

class CParallelTaskManager : public CSingleton<CParallelTaskManager>
{
public:
    explicit CParallelTaskManager(uint workerCount = 0);
    ~CParallelTaskManager() NL_OVERRIDE;

    /// Stop accepting tasks. Already queued tasks continue to run until destruction.
    void requestShutdown();

    void addTask(IRunnable *task, float priority = 0, uint affinity = 0xFFFFFFFF);
    uint getNumWaitingTasks();
    uint getNumWorkers() const { return static_cast<uint>(_ThreadsPool.size()); }

    void registerTaskPriorityCallback(CTaskManager::IChangeTaskPriority *callback);
    void unregisterTaskPriorityCallback(CTaskManager::IChangeTaskPriority *callback);

    CTaskManager *getCurrentWorker() const;

private:
    bool workersIdle();

    std::vector<std::unique_ptr<CTaskManager>> _ThreadsPool;
    std::mutex _SchedulingMutex;
    bool _ShuttingDown;
};

} // NLMISC

#endif // NL_PARALLEL_TASK_MANAGER_H
