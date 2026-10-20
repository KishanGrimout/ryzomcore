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

#ifndef CAST_COLLECTOR_HEADER
#define CAST_COLLECTOR_HEADER

#include "voxel.h"

struct CCastResult
{
	NLMISC::CVectorI m_voxelPosition;
	Voxel m_voxel;
};

class ICastCollector
{
public:
	virtual bool addHit(TVoxelIterator voxel) = 0;
	virtual uint getNumHits() const = 0;
	virtual Voxel getVoxel(uint i) const = 0;
	virtual TVoxelPos getVoxelPos(uint i) const = 0;
	virtual TVoxelIterator getVoxelIterator(uint i) const = 0;
};

class CCastCollector : public ICastCollector
{
	std::vector<TVoxelIterator> m_hitVoxel;

public:
	virtual bool addHit(TVoxelIterator voxel) override
	{
		m_hitVoxel.push_back(voxel);
		return true; // won't cast any further
	}
	virtual uint getNumHits() const override
	{
		return m_hitVoxel.size();
	}
	virtual Voxel getVoxel(uint i) const override
	{
		return m_hitVoxel[i].getVoxel();
	}
	virtual TVoxelPos getVoxelPos(uint i) const override
	{
		return ToNeLVectorI(m_hitVoxel[i].getPosition());
	}
	virtual TVoxelIterator getVoxelIterator(uint i) const override
	{
		return m_hitVoxel[i];
	}
};

class CCastCollectorSingleHit : public ICastCollector
{
	TVoxelIterator m_hitVoxel;
	bool m_hasHit;

public:
	CCastCollectorSingleHit()
		: m_hasHit(false)
		, m_hitVoxel(nullptr)
	{

	}
	virtual bool addHit(TVoxelIterator voxel) override
	{
		m_hasHit = true;
		m_hitVoxel = voxel;
		return false; // won't cast any further
	}
	virtual uint getNumHits() const override
	{
		return m_hasHit ? 1 : 0;
	}
	virtual Voxel getVoxel(uint i) const override
	{
		nlassert(m_hasHit);
		return m_hitVoxel.getVoxel();
	}
	virtual TVoxelPos getVoxelPos(uint i) const override
	{
		nlassert(m_hasHit);
		return ToNeLVectorI(m_hitVoxel.getPosition());
	}
	virtual TVoxelIterator getVoxelIterator(uint i) const override
	{
		nlassert(m_hasHit);
		return m_hitVoxel;
	}
};

#endif
