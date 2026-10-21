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
#include <chrono>
#include <thread>

#include <nel/misc/reader_writer.h>

using NLMISC::CFastReaderWriter;

namespace {

// A tryEnterReader() that never leaks the lock on success,
// used to detect whether a writer has registered a wait.
bool nonLeakingTryEnterReader(CFastReaderWriter &lock)
{
	bool locked = lock.tryEnterReader();
	if (locked)
		lock.leaveReader();
	return locked;
}

} // anonymous namespace

// Test suite for NLMISC::CFastReaderWriter (the lock-free reader/writer lock
// ported from the legacy dynamic-world fork, used by polyvox_defs.h VolumeRWLock).
TEST(CUTMiscFastReaderWriterTest, LockAllowsConcurrentReaders)
{
	CFastReaderWriter lock;

	lock.enterReader();
	EXPECT_TRUE(lock.tryEnterReader());

	lock.leaveReader();
	lock.leaveReader();

	// Lock must be back to a clean state
	EXPECT_TRUE(lock.tryEnterWriter());
	lock.leaveWriter();
}

TEST(CUTMiscFastReaderWriterTest, TryEnterReaderFailsWhileWriterHoldsLock)
{
	CFastReaderWriter lock;

	lock.enterWriter();
	EXPECT_FALSE(lock.tryEnterReader());
	EXPECT_FALSE(lock.forceEnterReader());
	lock.leaveWriter();

	EXPECT_TRUE(lock.tryEnterReader());
	lock.leaveReader();
}

TEST(CUTMiscFastReaderWriterTest, TryEnterWriterFailsWhileWriterHoldsLock)
{
	CFastReaderWriter lock;

	lock.enterWriter();
	EXPECT_FALSE(lock.tryEnterWriter());
	lock.leaveWriter();

	EXPECT_TRUE(lock.tryEnterWriter());
	lock.leaveWriter();
}

TEST(CUTMiscFastReaderWriterTest, TryEnterWriterFailsWhileReadersHoldLock)
{
	CFastReaderWriter lock;

	lock.enterReader();
	EXPECT_FALSE(lock.tryEnterWriter());

	// A second reader is still allowed while the write attempt was refused
	EXPECT_TRUE(lock.tryEnterReader());
	lock.leaveReader();
	lock.leaveReader();

	EXPECT_TRUE(lock.tryEnterWriter());
	lock.leaveWriter();
}

TEST(CUTMiscFastReaderWriterTest, FailedTriesLeaveLockUsable)
{
	CFastReaderWriter lock;

	// Fail a write attempt while a writer holds the lock, then check the
	// lock state was not corrupted by the failed CAS attempts.
	lock.enterWriter();
	EXPECT_FALSE(lock.tryEnterWriter());
	EXPECT_FALSE(lock.tryEnterReader());
	lock.leaveWriter();

	EXPECT_TRUE(lock.tryEnterReader());
	lock.leaveReader();
	EXPECT_TRUE(lock.tryEnterWriter());
	lock.leaveWriter();
}

TEST(CUTMiscFastReaderWriterTest, WriterWaitingBlocksTryEnterReaderButForceEnterReaderSucceeds)
{
	CFastReaderWriter lock;

	// Occupy the lock as a reader so a would-be writer has to wait
	lock.enterReader();

	std::atomic<bool> writerEntered(false);
	std::thread writerThread([&lock, &writerEntered]()
	{
		lock.enterWriter();
		writerEntered.store(true, std::memory_order_release);
		lock.leaveWriter();
	});

	// Wait (bounded) until the writer thread registered its wait,
	// polling with a try that never leaks a read lock
	bool writerIsWaiting = false;
	for (int i = 0; i < 10000 && !writerIsWaiting; ++i)
	{
		writerIsWaiting = !nonLeakingTryEnterReader(lock);
		if (!writerIsWaiting)
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	ASSERT_TRUE(writerIsWaiting) << "writer thread never managed to register a wait";

	// While a writer waits, plain readers are refused...
	EXPECT_FALSE(lock.tryEnterReader());

	// ...but a reader that bypasses the writer fairness queue is allowed
	EXPECT_TRUE(lock.forceEnterReader());

	// Release the forced reader then the original one;
	// the writer must now acquire the lock
	lock.leaveReader();
	lock.leaveReader();

	writerThread.join();

	EXPECT_TRUE(writerEntered.load(std::memory_order_acquire));
	EXPECT_TRUE(lock.tryEnterReader());
	lock.leaveReader();
}

TEST(CUTMiscFastReaderWriterTest, TryUpgradeFailsWhileWriterHoldsLock)
{
	CFastReaderWriter lock;

	lock.enterWriter();
	EXPECT_FALSE(lock.tryUpgrade());
	EXPECT_FALSE(lock.tryEnterReader());
	lock.leaveWriter();

	EXPECT_TRUE(lock.tryEnterReader());
	lock.leaveReader();
}

TEST(CUTMiscFastReaderWriterTest, TryUpgradeConvertsReaderToWriter)
{
	CFastReaderWriter lock;

	lock.enterReader();
	ASSERT_TRUE(lock.tryUpgrade());
	// The reader lock is consumed by a successful upgrade: we now own the write lock
	EXPECT_FALSE(lock.tryEnterReader());
	EXPECT_FALSE(lock.tryEnterWriter());
	lock.leaveWriter();

	// Back to a clean state
	EXPECT_TRUE(lock.tryEnterWriter());
	lock.leaveWriter();
}

TEST(CUTMiscFastReaderWriterTest, UpgradeThenDowngradeReturnsToReaderHolder)
{
	CFastReaderWriter lock;

	lock.enterReader();
	ASSERT_TRUE(lock.tryUpgrade());
	lock.downgrade();
	// Downgrade restored the reader lock (and released the writer part)
	EXPECT_TRUE(lock.tryEnterReader());
	lock.leaveReader();
	lock.leaveReader();

	EXPECT_TRUE(lock.tryEnterWriter());
	lock.leaveWriter();
}
