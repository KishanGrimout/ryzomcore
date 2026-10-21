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

#include "nel/misc/reader_writer.h"

#include <thread>

#ifdef DEBUG_NEW
	#define new DEBUG_NEW
#endif

namespace NLMISC {

#ifndef NL_CPP17

CReaderWriter::CReaderWriter()
{
	_ReadersLevel = 0;
}

CReaderWriter::~CReaderWriter()
{
	// here some checks to avoid a reader/writer still working while we flush the mutexes...
}

#endif

void reader_writer_dummy_cpp__()
{
	CReaderWriter readerWriter;
	CRWSynchronized<int> num;
	{
		CRWSynchronized<int>::CReadAccessor access0(&num);
		CRWSynchronized<int>::CReadAccessor access1(&num);
	}
	{
		CRWSynchronized<int>::CWriteAccessor accessW(&num);
	}
}

// NOTE(dynworld): ported from the legacy dynamic-world fork. Compared to the legacy code:
// - the NL_PROF_F()/PIX scoped-profiling calls were removed (project rule: no PIX instrumentation);
// - the legacy spin-backoff nlNSleep(100) uses std::this_thread::yield() instead (the legacy
//   nlNSleep implementation passes nanoseconds directly to SetWaitableTimer, which counts
//   100-ns units, so it was platform-ambiguous there; a scheduler yield is portable and safe).

CFastReaderWriter::CFastReaderWriter()
	: _RWLock(0)
{

}

void CFastReaderWriter::enterWriter()
{
	while (1)
	{
		TLockType state = _RWLock;

		/* No readers or writers? */
		if (state < RW_WRITE)
		{
			/* Turn off RW_WAIT, and turn on RW_WRITE */
			if (_RWLock.compare_exchange_weak(state, RW_WRITE))
				return;

			/* Someone else got there... time to wait */
			state = _RWLock;
		}

		/* Turn on writer wait bit */
		if (!(state & RW_WAIT))
			_RWLock |= RW_WAIT;

		/* Wait until can try to take the lock */
		while (_RWLock > RW_WAIT)
			std::this_thread::yield();
	}
}

void CFastReaderWriter::leaveWriter()
{
	_RWLock -= RW_WRITE;
}

bool CFastReaderWriter::tryEnterWriter()
{
	TLockType state = _RWLock;

	return ((state < RW_WRITE) && (_RWLock.compare_exchange_strong(state, state + RW_WRITE)));
}

void CFastReaderWriter::enterReader()
{
	while (1)
	{
		/* A writer exists? */
		while (_RWLock & (RW_WAIT | RW_WRITE))
			std::this_thread::yield();

		/* Try to get read lock */
		if (!(_RWLock.fetch_add(RW_READ) & (RW_WAIT | RW_WRITE)))
			return;

		/* Undo */
		_RWLock -= RW_READ;
	}
}

void CFastReaderWriter::leaveReader()
{
	_RWLock -= RW_READ;
}

bool CFastReaderWriter::tryEnterReader()
{
	/* Try to get read lock */
	TLockType state = _RWLock.fetch_add(RW_READ);

	if (!(state & (RW_WAIT | RW_WRITE)))
		return true;

	/* Undo */
	_RWLock -= RW_READ;

	return false;
}

// Get a read lock, even if a writer is waiting
bool CFastReaderWriter::forceEnterReader()
{
	/* Try to get read lock */
	TLockType state = _RWLock.fetch_add(RW_READ);

	/* We succeed even if a writer is waiting */
	if (!(state & RW_WRITE))
		return true;

	/* Undo */
	_RWLock -= RW_READ;

	return false;
}

// Try to upgrade from a read to a write lock atomically
bool CFastReaderWriter::tryUpgrade()
{
	/* Someone else is trying (and will succeed) to upgrade to a write lock? */
	if (_RWLock.fetch_or(RW_WRITE) & RW_WRITE)
		return false;

	/* Don't count myself any more */
	_RWLock -= RW_READ;

	/* Wait until there are no more readers */
	while (_RWLock > (RW_WAIT | RW_WRITE))
		std::this_thread::yield();

	return true;
}

void CFastReaderWriter::upgrade()
{
	while (!tryUpgrade()) {}
}

void CFastReaderWriter::downgrade()
{
	_RWLock += RW_READ;
	leaveWriter();
}

} // NLMISC

/* end of file */
