// DynWorld - based on 'Ryzom - MMORPG Framework <http://dev.ryzom.com/projects/ryzom/>'
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

#ifndef CL_DYN_MOVE_CONTAINER_H
#define CL_DYN_MOVE_CONTAINER_H

#include "nel/pacs/move_container.h"
#include "dyn_global_retriever.h"

/**
 * A container for movable objects -> for dyn world
 * Some constraints:
 * * The move bounding box must be lower than the cell size
 *
 * \author Kishan Grimout
 * \date 2014
 */

class CDynMoveContainer: public NLPACS::CMoveContainer
{
	friend class CMovePrimitive;
	friend class CPrimitiveWorldImage;
public:
	/// Constructor
	CDynMoveContainer (double xmin, double ymin, double xmax, double ymax, uint widthCellCount, uint heightCellCount, double primitiveMaxSize,
		uint8 numWorldImage, uint maxIteration, uint otSize)
		: NLPACS::CMoveContainer (xmin, ymin, xmax, ymax, widthCellCount, heightCellCount, primitiveMaxSize, numWorldImage, maxIteration, otSize)
	{
		// Cells size override
		_CellWidth = 16.;
		_CellHeight = 16.;
	}

	/// Init the container with a global retriever
	CDynMoveContainer (CDynGlobalRetriever* retriever, uint widthCellCount, uint heightCellCount, double primitiveMaxSize,
		uint8 numWorldImage, uint maxIteration, uint otSize)
		: NLPACS::CMoveContainer(retriever, widthCellCount, heightCellCount, primitiveMaxSize, numWorldImage, maxIteration, otSize)
	{
		// Cells size override
		_CellWidth = 16.;
		_CellHeight = 16.;
	}

	/// Destructor
	virtual ~CDynMoveContainer () {}

};


#endif // CL_DYN_MOVE_CONTAINER_H

/* End of move_container.h */
