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

#ifndef POLYVOX_DEFS_HEADER
#define POLYVOX_DEFS_HEADER

#include <PolyVox/PagedVolume.h>
#include <PolyVox/RawVolume.h>
#include <PolyVox/Vertex.h>
#include <PolyVox/Mesh.h>
#include <PolyVox/MarchingCubesSurfaceExtractor.h>

#include "voxel.h"
#include "nel/misc/vectori.h"
#include "nel/misc/tds.h"
#include "nel/misc/reader_writer.h"

class VolumeRWLock
{
	mutable NLMISC::CReaderWriter m_lock;
	template <typename T> static std::atomic<T>& as_atomic(T& value)
	{
		static_assert(sizeof(T) == sizeof(std::atomic<T>), "");
		//static_assert(alignof(T) == alignof(std::atomic<T>), ""); // TODO: need VS2015
		return reinterpret_cast<std::atomic<T>&>(value);
	}
public:
	void readLock()	const 		{ m_lock.enterReader(); }
	void readUnlock() const 	{ m_lock.leaveReader(); }
	void writeLock() const 		{ m_lock.enterWriter(); }
	void writeUnlock() const 	{ m_lock.leaveWriter(); }
	/*void upgradeLock() const { m_lock.upgrade(); }
	void downgradeLock() const 	{ m_lock.downgrade(); }
	bool isLocked() const
	{
		bool lockSucceed = m_lock.tryEnterReader();
		if (lockSucceed)
			m_lock.leaveReader();
		return !lockSucceed;
	}*/

	template <typename T> static T postIncr(T& value) { return as_atomic(value)++; }
	template <typename T> static T preIncr(T& value)  { return ++as_atomic(value); }
	template <typename T> static T postDecr(T& value) { return as_atomic(value)--; }
	template <typename T> static T preDecr(T& value)  { return --as_atomic(value); }
};

typedef Voxel TVoxel;

typedef PolyVox::PagedVolume<TVoxel, VolumeRWLock> TVolume;

typedef PolyVox::RawVolume<TVoxel> TSampleVolume;

typedef TVolume::Sampler TVoxelIterator;

typedef PolyVox::Region TRegion;

typedef TVolume::Pager TPager;

typedef PolyVox::Mesh<PolyVox::MarchingCubesVertex<TVoxel>, uint32> TEncodedMesh;
typedef PolyVox::Mesh<PolyVox::Vertex<TVoxel>, uint32> TDecodedMesh;
typedef PolyVox::Mesh<PolyVox::Vertex<TVoxel>, uint32>::VertexType TDecodedVertex;

class MultiThreadChunkCache : public TVolume::IChunkCache
{
public:
	virtual std::weak_ptr<TVolume::Chunk> getChunkCached(const TVolume* pVolume, int32_t iChunkX, int32_t iChunkY, int32_t iChunkZ) const;
	virtual void setChunkCached(const TVolume* pVolume, std::weak_ptr<TVolume::Chunk> pChunk, int32_t iChunkX, int32_t iChunkY, int32_t iChunkZ);
	virtual void dirtyCache();

private:
	// Storing these properties individually has proved to be faster than keeping
	// them in a Vector3DInt32 as it avoids constructions and comparison overheads.
	// They are also at the start of the class in the hope that they will be pulled
	// into cache - I've got no idea if this actually makes a difference.
	thread_local static struct CacheData
	{
		int32_t m_v3dLastAccessedChunkX{ 0 };
		int32_t m_v3dLastAccessedChunkY{ 0 };
		int32_t m_v3dLastAccessedChunkZ{ 0 };
		std::weak_ptr<TVolume::Chunk> m_pLastAccessedChunk;
		const void* m_pVolume{ nullptr };
	} *mtls_cacheData;

	CacheData& getCacheData();
	const CacheData& getCacheData() const;
};

inline const PolyVox::Vector3DInt32& ToPolyVoxVectorI(const NLMISC::CVectorI& in)
{
	return *(PolyVox::Vector3DInt32*)&in;
}

inline const NLMISC::CVectorI& ToNeLVectorI(const PolyVox::Vector3DInt32& in)
{
	return *(NLMISC::CVectorI*)&in;
}

inline const PolyVox::Vector3DFloat& ToPolyVoxVectorF(const NLMISC::CVector& in)
{
	return *(PolyVox::Vector3DFloat*)&in;
}

inline const NLMISC::CVector& ToNeLVectorF(const PolyVox::Vector3DFloat& in)
{
	return *(NLMISC::CVector*)&in;
}

#endif
