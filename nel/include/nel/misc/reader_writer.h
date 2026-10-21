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

#ifndef NL_READER_WRITER_H
#define NL_READER_WRITER_H

#include "types_nl.h"
#include "mutex.h"

#include <atomic>

#ifdef NL_CPP17
#include <shared_mutex>
#endif

namespace NLMISC {

#ifdef NL_CPP17

class CReaderWriter
{
private:
	std::shared_mutex m_Mutex;

public:
	void enterReader()
	{
		m_Mutex.lock_shared();
	}

	void leaveReader()
	{
		m_Mutex.unlock_shared();
	}

	bool tryEnterReader()
	{
		return m_Mutex.try_lock_shared();
	}

	void enterWriter()
	{
		m_Mutex.lock();
	}

	void leaveWriter()
	{
		m_Mutex.unlock();
	}
	
	bool tryEnterWriter()
	{
		return m_Mutex.try_lock();
	}
};
	
#else

/**
 * This class allows a reader/writer ressource usage policy.
 * \author Benjamin Legros
 * \author Nevrax France
 * \date 2001
 */
class CReaderWriter
{
private:

	CMutex	_Fairness;
	CMutex	_ReadersMutex;
	CMutex	_RWMutex;
	sint	_ReadersLevel;

public:

	CReaderWriter();
	~CReaderWriter();

	void			enterReader()
	{
		const_cast<CMutex&>(_Fairness).enter();
		const_cast<CMutex&>(_ReadersMutex).enter();
		++_ReadersLevel;
		if (_ReadersLevel == 1)
			const_cast<CMutex&>(_RWMutex).enter();
		const_cast<CMutex&>(_ReadersMutex).leave();
		const_cast<CMutex&>(_Fairness).leave();
	}

	void			leaveReader()
	{
		const_cast<CMutex&>(_ReadersMutex).enter();
		--_ReadersLevel;
		if (_ReadersLevel == 0)
			const_cast<CMutex&>(_RWMutex).leave();
		const_cast<CMutex&>(_ReadersMutex).leave();
	}

	void			enterWriter()
	{
		const_cast<CMutex&>(_Fairness).enter();
		const_cast<CMutex&>(_RWMutex).enter();
		const_cast<CMutex&>(_Fairness).leave();
	}

	void			leaveWriter()
	{
		const_cast<CMutex&>(_RWMutex).leave();
	}
	
	bool			tryEnterReader()
	{
		// Try to enter as reader - returns true if successful, false if a writer has the lock
		if (_RWMutex.tryEnter())
		{
			// Got the write lock, but we want read access, so we need to check if there are writers waiting
			// For simplicity, we'll just try to enter as reader properly
			_RWMutex.leave();
			
			// Proper read lock attempt
			const_cast<CMutex&>(_Fairness).enter();
			const_cast<CMutex&>(_ReadersMutex).enter();
			bool success = (_ReadersLevel == 0 && _RWMutex.tryEnter());
			if (success)
			{
				_ReadersLevel++;
			}
			const_cast<CMutex&>(_ReadersMutex).leave();
			const_cast<CMutex&>(_Fairness).leave();
			return success;
		}
		return false;
	}
	
	bool			tryEnterWriter()
	{
		// Try to enter as writer - returns true if successful
		const_cast<CMutex&>(_Fairness).enter();
		bool success = _RWMutex.tryEnter();
		if (success)
		{
			// Wait for readers to finish
			const_cast<CMutex&>(_ReadersMutex).enter();
			while (_ReadersLevel > 0)
			{
				const_cast<CMutex&>(_ReadersMutex).leave();
				const_cast<CMutex&>(_Fairness).leave();
				nlSleep(1);
				const_cast<CMutex&>(_Fairness).enter();
				const_cast<CMutex&>(_ReadersMutex).enter();
			}
			const_cast<CMutex&>(_ReadersMutex).leave();
		}
		else
		{
			const_cast<CMutex&>(_Fairness).leave();
		}
		return success;
	}
};

#endif

/**
 * This class uses a CReaderWriter object to implement a synchronized object (see mutex.h for standard CSynchronized.)
 * \author Benjamin Legros
 * \author Nevrax France
 * \date 2001
 */
template <class T>
class CRWSynchronized
{
public:

	class CReadAccessor
	{
		CRWSynchronized<T>		*_RWSynchronized;

	public:

		CReadAccessor(CRWSynchronized<T> *cs)
		{
			_RWSynchronized = cs;
			const_cast<CReaderWriter&>(_RWSynchronized->_RWSync).enterReader();
		}

		~CReadAccessor()
		{
			const_cast<CReaderWriter&>(_RWSynchronized->_RWSync).leaveReader();
		}

		const T		&value()
		{
			return const_cast<const T&>(_RWSynchronized->_Value);
		}
	};

	class CWriteAccessor
	{
		CRWSynchronized<T>		*_RWSynchronized;

	public:

		CWriteAccessor(CRWSynchronized<T> *cs)
		{
			_RWSynchronized = cs;
			const_cast<CReaderWriter&>(_RWSynchronized->_RWSync).enterWriter();
		}

		~CWriteAccessor()
		{
			const_cast<CReaderWriter&>(_RWSynchronized->_RWSync).leaveWriter();
		}

		T		&value()
		{
			return const_cast<T&>(_RWSynchronized->_Value);
		}
	};

private:
	friend class CRWSynchronized::CReadAccessor;
	friend class CRWSynchronized::CWriteAccessor;

	CReaderWriter		_RWSync;

	T					_Value;

};

/**
 * NOTE(dynworld): ported from the legacy dynamic-world fork (added there inside reader_writer.{h,cpp}).
 * Lock-free reader/writer lock with writer-fairness and reader->writer upgrade support.
 * Serves the voxel volume chunk locking (VolumeRWLock in polyvox_defs.h).
 */
class CFastReaderWriter
{
	static const uint RW_WAIT = 1;
	static const uint RW_WRITE = 2;
	static const uint RW_READ = 4;

	using TLockType = uint32;
	using TRWLock = std::atomic<TLockType>;
	TRWLock _RWLock;

public:
	CFastReaderWriter();

	void enterWriter();

	void leaveWriter();

	bool tryEnterWriter();

	void enterReader();

	void leaveReader();

	bool tryEnterReader();

	// Get a read lock, even if a writer is waiting
	bool forceEnterReader();

	// Try to upgrade from a read to a write lock atomically
	bool tryUpgrade();

	void upgrade();

	void downgrade();
};

} // NLMISC


#endif // NL_READER_WRITER_H

/* End of reader_writer.h */
