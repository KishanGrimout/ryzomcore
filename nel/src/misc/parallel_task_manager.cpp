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

#include "stdmisc.h"

#include "nel/misc/parallel_task_manager.h"

#include <algorithm>
#include <atomic>
#include <limits>
#include <thread>

namespace NLMISC {
namespace {

struct CWorkerContext
{
    CParallelTaskManager *Manager = nullptr;
    CTaskManager *Worker = nullptr;
};

thread_local CWorkerContext CurrentWorkerContext;

class CSetWorkerContextTask : public IRunnable
{
public:
    CSetWorkerContextTask(CParallelTaskManager *manager, CTaskManager *worker)
        : _Manager(manager), _Worker(worker), _Ready(false)
    {
    }

    void run() NL_OVERRIDE
    {
        CurrentWorkerContext.Manager = _Manager;
        CurrentWorkerContext.Worker = _Worker;
        _Ready.store(true, std::memory_order_release);
    }

    bool isReady() const
    {
        return _Ready.load(std::memory_order_acquire);
    }

private:
    CParallelTaskManager *_Manager;
    CTaskManager *_Worker;
    std::atomic<bool> _Ready;
};

} // namespace

CParallelTaskManager::CParallelTaskManager(uint workerCount)
    : _ShuttingDown(false)
{
    if (workerCount == 0)
    {
        const uint hardwareThreadCount = std::thread::hardware_concurrency();
        workerCount = std::max<uint>(1, hardwareThreadCount / 2);
    }
    workerCount = std::min<uint>(workerCount, std::numeric_limits<uint>::digits);
    _ThreadsPool.reserve(workerCount);

    for (uint i = 0; i < workerCount; ++i)
    {
        std::unique_ptr<CTaskManager> worker = std::make_unique<CTaskManager>();
        CTaskManager *workerPointer = worker.get();
        _ThreadsPool.push_back(std::move(worker));

        CSetWorkerContextTask setContextTask(this, workerPointer);
        workerPointer->addTask(&setContextTask);
        while (!setContextTask.isReady() || workerPointer->isTaskRunning())
            nlSleep(1);
    }
}

CParallelTaskManager::~CParallelTaskManager()
{
    if (CurrentWorkerContext.Manager == this)
    {
        nlerror("CParallelTaskManager must be destroyed outside its worker threads");
    }

    for (;;)
    {
        std::unique_lock<std::mutex> lock(_SchedulingMutex);
        if (workersIdle())
        {
            _ShuttingDown = true;
            break;
        }
        lock.unlock();
        nlSleep(1);
    }

    _ThreadsPool.clear();
}

void CParallelTaskManager::requestShutdown()
{
    std::lock_guard<std::mutex> lock(_SchedulingMutex);
    _ShuttingDown = true;
}

bool CParallelTaskManager::workersIdle()
{
    for (const std::unique_ptr<CTaskManager> &worker : _ThreadsPool)
    {
        if (worker->taskListSize() != 0 || worker->isTaskRunning())
            return false;
    }
    return true;
}

void CParallelTaskManager::addTask(IRunnable *task, float priority, uint affinity)
{
    nlassert(task != nullptr);
    if (!task)
        return;

    std::lock_guard<std::mutex> lock(_SchedulingMutex);
    nlassert(!_ShuttingDown);
    if (_ShuttingDown)
        return;

    CTaskManager *currentWorker = getCurrentWorker();
    if (currentWorker)
    {
        for (uint i = 0; i < _ThreadsPool.size(); ++i)
        {
            if (_ThreadsPool[i].get() == currentWorker && (affinity & (uint(1) << i)) != 0)
            {
                currentWorker->addTask(task, priority);
                return;
            }
        }
    }

    uint bestWorker = static_cast<uint>(_ThreadsPool.size());
    uint bestLoad = std::numeric_limits<uint>::max();
    for (uint i = 0; i < _ThreadsPool.size(); ++i)
    {
        if ((affinity & (uint(1) << i)) == 0)
            continue;

        CTaskManager *worker = _ThreadsPool[i].get();
        const uint load = worker->taskListSize() + (worker->isTaskRunning() ? 1 : 0);
        if (load < bestLoad)
        {
            bestLoad = load;
            bestWorker = i;
        }
    }

    if (bestWorker == _ThreadsPool.size())
    {
        nlassert(false);
        bestWorker = 0;
        bestLoad = _ThreadsPool[0]->taskListSize() + (_ThreadsPool[0]->isTaskRunning() ? 1 : 0);
        for (uint i = 1; i < _ThreadsPool.size(); ++i)
        {
            CTaskManager *worker = _ThreadsPool[i].get();
            const uint load = worker->taskListSize() + (worker->isTaskRunning() ? 1 : 0);
            if (load < bestLoad)
            {
                bestLoad = load;
                bestWorker = i;
            }
        }
    }

    _ThreadsPool[bestWorker]->addTask(task, priority);
}

uint CParallelTaskManager::getNumWaitingTasks()
{
    uint waitingTasks = 0;
    for (const std::unique_ptr<CTaskManager> &worker : _ThreadsPool)
        waitingTasks += worker->getNumWaitingTasks();
    return waitingTasks;
}

void CParallelTaskManager::registerTaskPriorityCallback(CTaskManager::IChangeTaskPriority *callback)
{
    if (!callback)
        return;

    std::lock_guard<std::mutex> lock(_SchedulingMutex);
    nlassert(!_ShuttingDown);
    if (_ShuttingDown)
        return;

    for (const std::unique_ptr<CTaskManager> &worker : _ThreadsPool)
        worker->registerTaskPriorityCallback(callback);
}

void CParallelTaskManager::unregisterTaskPriorityCallback(CTaskManager::IChangeTaskPriority *callback)
{
    std::lock_guard<std::mutex> lock(_SchedulingMutex);
    for (const std::unique_ptr<CTaskManager> &worker : _ThreadsPool)
        worker->unregisterTaskPriorityCallback(callback);
}

CTaskManager *CParallelTaskManager::getCurrentWorker() const
{
    return CurrentWorkerContext.Manager == this ? CurrentWorkerContext.Worker : nullptr;
}

} // namespace NLMISC
