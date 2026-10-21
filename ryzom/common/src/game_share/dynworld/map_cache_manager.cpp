// DynWorld
// Copyright (C) 2016 Kishan Grimout <kishan.grimout@gmail.com>
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

#include "map_cache_manager.h"

#include "nel/misc/mutex.h"
#include "nel/misc/parallel_task_manager.h"

#include "game_share/dynworld/map_sector.h"
#include "game_share/dynworld/disk_file_db.h"

#include "seven_zip/seven_zip.h"

class CompressAndSaveRequest : public NLMISC::IRunnable
{
public:
	/// Data stream that will be saved
	NLMISC::CMemStream m_stream;
	size_t m_compressOffset;

	/// compressOffset:
	///		- 0: all the stream will be compressed,
	///		- N < stream.size: N bytes stored uncompressed, remaining of the stream is stored compressed
	///		- N >= stream.size: stream data won't be compressed and stored as is
	CompressAndSaveRequest(CMapCacheManagerImpl* owner, TLoadingSector::TId id, uint capacity, size_t compressOffset = size_t(-1))
		: m_owner(owner)
		, m_id(id)
		, m_stream(false, false, capacity)
		, m_compressOffset(compressOffset)
		, m_compressedSize(0)
		, m_state(WorkState::Compress)
	{}

private:
	enum class WorkState
	{
		Compress,
		Save
	};
	static const bool ms_singlePass = true; // set to false to use CUnqliteDataBase that needs db access single threaded

	CMapCacheManagerImpl* m_owner;
	TLoadingSector::TId m_id;
	std::unique_ptr<uint8[]> m_compressedBuffer;
	size_t m_compressedSize;
	WorkState m_state;

	// IRunnable implementation
	virtual void run() override;
	virtual void getName(std::string &result) const override;

	void compress();
	void save();
	void terminate();
};

class CMapCacheManagerImpl
{
	friend class CompressAndSaveRequest;
	static const uint8 ms_version = 0;

	// Database management
	CDiskFileDataBase m_db;
	
public:
	static const uint8 s_ChunkSaveVersion = 0;

	CMapCacheManagerImpl(const char* dbPath)
		: m_db(dbPath)
	{
		
	}

	void saveChunk(TLoadingSector::TId id, TVolume::Chunk* pChunk, const SectorMetadata& infos)
	{
		CompressAndSaveRequest* request = new CompressAndSaveRequest(this, id, pChunk->getDataSizeInBytes() + sizeof(SectorMetadata) + sizeof(s_ChunkSaveVersion));
		uint8 chunkSaveVersion = s_ChunkSaveVersion;
		request->m_stream.serial(chunkSaveVersion);
		const_cast<SectorMetadata&>(infos).serial(request->m_stream);
		request->m_compressOffset = request->m_stream.getPos();
		request->m_stream.serialBuffer((uint8*)pChunk->getData(), pChunk->getDataSizeInBytes());

		NLMISC::CParallelTaskManager::instance().addTask(request, -3.f);
	}

	bool readChunk(TLoadingSector::TId id, TVolume::Chunk* pChunk, SectorMetadata& outInfos)
	{
		if (m_db.isValid())
		{
			sint64 leastBufLen = pChunk->getDataSizeInBytes() + sizeof(SectorMetadata) + sizeof(s_ChunkSaveVersion);
			NLMISC::CMemStream stream(true, false);
			uint8* streamPtr = stream.bufferToFill(leastBufLen);

			if (!m_db.readData(id, streamPtr, leastBufLen))
				return false;

			uint8 chunkSaveVersion;
			stream.serial(chunkSaveVersion);
			if (chunkSaveVersion != s_ChunkSaveVersion)
			{
				nlwarning("Trashed data for chunk %u", id);
				return false;
			}

			outInfos.serial(stream);

			const uint8* lzmaBuf = stream.buffer() + stream.getPos();
			size_t lzmaBufLen = leastBufLen - stream.getPos();
			uint8* chunkData = (uint8*)pChunk->getData();
			size_t chunkDataLen = pChunk->getDataSizeInBytes();
			if (!unpackLZMA(lzmaBuf, lzmaBufLen, chunkData, chunkDataLen))
			{
				return false;
			}

			nlassert(chunkData == (uint8*)pChunk->getData() && chunkDataLen == pChunk->getDataSizeInBytes());

			return true;
		}

		return false;
	}
};

CMapCacheManager::CMapCacheManager()
	: m_impl(std::make_unique<CMapCacheManagerImpl>("./cache/map.cache"))
{
	
}

CMapCacheManager::~CMapCacheManager()
{
	
}

bool CMapCacheManager::tryFillRegion(const TRegion& region, TVolume::Chunk* pChunk, SectorMetadata& outInfos)
{
	TLoadingSector::TId sectorIndex = TLoadingSector::getRegionId(region);
	return m_impl->readChunk(sectorIndex, pChunk, outInfos);
}

void CMapCacheManager::saveRegionToCache(const TRegion& region, TVolume::Chunk* pChunk, const SectorMetadata& infos)
{
	TLoadingSector::TId sectorIndex = TLoadingSector::getRegionId(region);
	m_impl->saveChunk(sectorIndex, pChunk, infos);
}

void CompressAndSaveRequest::run()
{
	if (ms_singlePass)
	{
		compress();
		save();
	}
	else
	{
		switch (m_state)
		{
		case WorkState::Compress:
			compress();
			break;

		case WorkState::Save:
			save();
			break;
		}
	}
}

void CompressAndSaveRequest::getName(std::string &result) const
{
	if (ms_singlePass)
	{
		result = "CompressAndSaveChunkRequest";
	}
	else
	{
		switch (m_state)
		{
		case WorkState::Compress:
			result = "CompressAndSaveChunkRequest/Compress";
			break;

		case WorkState::Save:
			result = "CompressAndSaveChunkRequest/Save";
			break;

		default:
			result = "CompressAndSaveChunkRequest";
			break;
		}
	}
}

void CompressAndSaveRequest::compress()
{
	// Do we have something to compress?
	if (m_stream.getPos() > m_compressOffset)
	{
		size_t sizeToCompress = m_stream.getPos() - m_compressOffset;
		size_t maxPackSize = maxPackLZMASize(sizeToCompress);
		m_compressedBuffer = std::unique_ptr<uint8[]>(new uint8[maxPackSize]);
		uint8* outBuffer = m_compressedBuffer.get();
		size_t outSize = maxPackSize;

		// Compress the buffer
		if (packLZMA(m_stream.buffer() + m_compressOffset, sizeToCompress, outBuffer, outSize, 0))
		{
			nlassert(outBuffer == m_compressedBuffer.get()); // check it has not been reallocated
			m_compressedSize = outSize;
		}
		else
		{
			nlerror("Failed to compress data %u.", m_id);
			terminate();
		}
	}

	// Reschedule on thread 0 to save the compressed data (only one allowed to access the DB)
	m_state = WorkState::Save;
	if (!ms_singlePass)
		NLMISC::CParallelTaskManager::instance().addTask(this, -3.f);
}

bool g_flushDb = false;
void CompressAndSaveRequest::save()
{
	if (m_compressedSize > 0)
	{
		// Is there a non-compressed header?
		if (m_compressOffset > 0)
		{
			// Store the header
			if (m_owner->m_db.storeData(m_id, m_stream.buffer(), m_compressOffset))
			{
				// Append the compressed buffer
				if (!m_owner->m_db.appendData(m_id, m_compressedBuffer.get(), m_compressedSize))
					nlwarning("Failed to store data %u.", m_id);
			}
			else
			{
				nlwarning("Failed to store header %u.", m_id);
			}
		}
		else
		{
			// Store the whole compressed buffer
			if (!m_owner->m_db.storeData(m_id, m_compressedBuffer.get(), m_compressedSize))
				nlwarning("Failed to store data %u.", m_id);
		}
	}
	else
	{
		// Store the stream
		if (!m_owner->m_db.storeData(m_id, m_stream.buffer(), m_stream.getPos()))
			nlwarning("Failed to store data %u.", m_id);
	}

	if (g_flushDb)
	{
		m_owner->m_db.flush();
		g_flushDb = false;
	}

	terminate();
}

void CompressAndSaveRequest::terminate()
{
	delete this;
}
