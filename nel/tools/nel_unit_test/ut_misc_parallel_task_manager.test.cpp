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

#include <atomic>

#include <nel/misc/app_context.h>
#include <nel/misc/parallel_task_manager.h>

namespace {

class CCountingTask : public NLMISC::IRunnable
{
public:
    explicit CCountingTask(std::atomic<uint> &completed)
        : _Completed(completed)
    {
    }

    void run() NL_OVERRIDE
    {
        ++_Completed;
        delete this;
    }

private:
    std::atomic<uint> &_Completed;
};

class CRecursiveTask : public NLMISC::IRunnable
{
public:
    CRecursiveTask(NLMISC::CParallelTaskManager &manager, std::atomic<uint> &completed, uint depth)
        : _Manager(manager), _Completed(completed), _Depth(depth)
    {
    }

    void run() NL_OVERRIDE
    {
        ++_Completed;
        if (_Depth != 0)
            _Manager.addTask(new CRecursiveTask(_Manager, _Completed, _Depth - 1));
        delete this;
    }

private:
    NLMISC::CParallelTaskManager &_Manager;
    std::atomic<uint> &_Completed;
    uint _Depth;
};

class CWorkerContextTask : public NLMISC::IRunnable
{
public:
    CWorkerContextTask(NLMISC::CParallelTaskManager &manager, std::atomic<uint> &completed)
        : _Manager(manager), _Completed(completed)
    {
    }

    void run() NL_OVERRIDE
    {
        if (_Manager.getCurrentWorker())
            ++_Completed;
        delete this;
    }

private:
    NLMISC::CParallelTaskManager &_Manager;
    std::atomic<uint> &_Completed;
};

} // namespace

class CUTMiscParallelTaskManager : public testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE(NLMISC::INelContext::getInstance().isContextInitialised());
    }
};

TEST_F(CUTMiscParallelTaskManager, drainsQueuedAndNestedTasks)
{
    std::atomic<uint> completed{0};
    std::atomic<uint> workerContextTasks{0};
    {
        NLMISC::CParallelTaskManager manager(2);
        for (uint i = 0; i < 32; ++i)
            manager.addTask(new CCountingTask(completed));
        manager.addTask(new CRecursiveTask(manager, completed, 8));
        manager.addTask(new CWorkerContextTask(manager, workerContextTasks), 0, 1);
        manager.addTask(new CWorkerContextTask(manager, workerContextTasks), 0, 2);
    }

    EXPECT_EQ(43u, completed.load());
    EXPECT_EQ(2u, workerContextTasks.load());
}
