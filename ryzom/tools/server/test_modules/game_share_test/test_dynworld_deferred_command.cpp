#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include <game_share/dynworld/map_deferred_command_manager.h>

namespace {

	class CFakeCommand : public IMapDeferredCommand
	{
	public:
		explicit CFakeCommand(bool firstRunSucceeds, const std::string &name = "FakeCommand")
			: m_Name(name)
			, m_FirstRunSucceeds(firstRunSucceeds)
			, m_RunCount(0)
		{}

		float getPriority() const override { return 0.f; }

		void getName(std::string &result) const override { result = m_Name; }

		// execute() runs on a parallel task manager worker thread
		bool execute() override
		{
			uint32 previousRunCount = m_RunCount.fetch_add(1);
			return m_FirstRunSucceeds || previousRunCount > 0;
		}

		uint32 getRunCount() const { return m_RunCount.load(); }

	private:
		std::string m_Name;
		bool m_FirstRunSucceeds;
		std::atomic<uint32> m_RunCount;
	};

	// Polls until the deferred command ran to completion, with a timeout
	bool waitReady(const CMapDeferredCommandResult &result, int timeoutMs = 10000)
	{
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
		while (!result.isReady())
		{
			if (std::chrono::steady_clock::now() >= deadline)
				return false;
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		return true;
	}

} // anonymous namespace

// Test suite for CMapDeferredCommandManager (the async dynamic-world command
// scheduler running commands through NLMISC::CParallelTaskManager) and the
// CMapDeferredCommandResult wrapper.
class CUTDynworldDeferredCommandTest : public ::testing::Test
{
protected:
	void SetUp() override
	{
		// A fresh manager instance per test keeps handle numbering deterministic
		CMapDeferredCommandManager::releaseInstance();
		CMapDeferredCommandManager::instance();
	}

	void TearDown() override
	{
		// The manager only deletes its own result wrapper in removeCommand():
		// user commands have to be deleted here once each task is done.
		// Never delete either part while a task is still pending on the
		// parallel task manager, so poll readiness first.
		CMapDeferredCommandManager &manager = CMapDeferredCommandManager::instance();
		for (SDeferredCommand &deferredCommand : m_DeferredCommands)
		{
			// Handles may have been freed during the test body (freed slots are
			// reused by later addCommand() calls), so check validity first:
			// getCommand() would dereference an empty slot otherwise.
			if (manager.isCommandValid(deferredCommand.m_Handle))
			{
				if (!waitReady(manager.getCommand(deferredCommand.m_Handle)))
				{
					ADD_FAILURE() << "a deferred command stayed unready on teardown:"
					              << " leaking to avoid use-after-free in the task queue";
					continue;
				}
				manager.removeCommand(deferredCommand.m_Handle);
			}
			delete deferredCommand.m_Command;
		}
		m_DeferredCommands.clear();
		CMapDeferredCommandManager::releaseInstance();
	}

	TMapDeferredCommandHandle addCommand(IMapDeferredCommand *command)
	{
		TMapDeferredCommandHandle handle = CMapDeferredCommandManager::instance().addCommand(command);
		SDeferredCommand deferredCommand = { command, handle };
		m_DeferredCommands.push_back(deferredCommand);
		return handle;
	}

private:
	struct SDeferredCommand
	{
		IMapDeferredCommand *m_Command;
		TMapDeferredCommandHandle m_Handle;
	};

	std::vector<SDeferredCommand> m_DeferredCommands;
};

TEST_F(CUTDynworldDeferredCommandTest, ExecutesCommandAndMarksResultReady)
{
	CFakeCommand *command = new CFakeCommand(true);
	TMapDeferredCommandHandle handle = addCommand(command);

	const CMapDeferredCommandResult &result = CMapDeferredCommandManager::instance().getCommand(handle);
	EXPECT_EQ(command, result.getCommand());
	ASSERT_TRUE(waitReady(result));

	EXPECT_EQ(1u, command->getRunCount());
	EXPECT_TRUE(CMapDeferredCommandManager::instance().isCommandValid(handle));
}

TEST_F(CUTDynworldDeferredCommandTest, FreshManagerAssignsSequentialHandles)
{
	TMapDeferredCommandHandle handle0 = addCommand(new CFakeCommand(true));
	TMapDeferredCommandHandle handle1 = addCommand(new CFakeCommand(true));
	TMapDeferredCommandHandle handle2 = addCommand(new CFakeCommand(true));

	EXPECT_EQ(0u, handle0);
	EXPECT_EQ(1u, handle1);
	EXPECT_EQ(2u, handle2);

	for (uint handle : { handle0, handle1, handle2 })
	{
		ASSERT_TRUE(waitReady(CMapDeferredCommandManager::instance().getCommand(handle)));
		EXPECT_TRUE(CMapDeferredCommandManager::instance().isCommandValid(handle));
	}
}

TEST_F(CUTDynworldDeferredCommandTest, ReschedulesUntilExecuteReturnsTrue)
{
	// A command refusing its first execution gets rescheduled on the
	// parallel task manager and only completes on the second run
	CFakeCommand *command = new CFakeCommand(false);
	TMapDeferredCommandHandle handle = addCommand(command);

	const CMapDeferredCommandResult &result = CMapDeferredCommandManager::instance().getCommand(handle);
	ASSERT_TRUE(waitReady(result));

	EXPECT_EQ(2u, command->getRunCount());
}

TEST_F(CUTDynworldDeferredCommandTest, RemoveCommandInvalidatesHandleAndFreesSlot)
{
	TMapDeferredCommandHandle handle0 = addCommand(new CFakeCommand(true));
	TMapDeferredCommandHandle handle1 = addCommand(new CFakeCommand(true));
	TMapDeferredCommandHandle handle2 = addCommand(new CFakeCommand(true));
	for (TMapDeferredCommandHandle handle : { handle0, handle1, handle2 })
		ASSERT_TRUE(waitReady(CMapDeferredCommandManager::instance().getCommand(handle)));

	CMapDeferredCommandManager &manager = CMapDeferredCommandManager::instance();
	manager.removeCommand(handle1);

	EXPECT_FALSE(manager.isCommandValid(handle1));
	EXPECT_TRUE(manager.isCommandValid(handle0));
	EXPECT_TRUE(manager.isCommandValid(handle2));

	// The freed slot is reused by the next added command
	EXPECT_EQ(handle1, addCommand(new CFakeCommand(true)));
}

TEST_F(CUTDynworldDeferredCommandTest, RemoveCommandIgnoresOutOfRangeHandle)
{
	TMapDeferredCommandHandle handle = addCommand(new CFakeCommand(true));
	ASSERT_TRUE(waitReady(CMapDeferredCommandManager::instance().getCommand(handle)));

	// Guarded: out-of-range handles are ignored without touching valid slots
	CMapDeferredCommandManager::instance().removeCommand(12345u);
	EXPECT_TRUE(CMapDeferredCommandManager::instance().isCommandValid(handle));
}
