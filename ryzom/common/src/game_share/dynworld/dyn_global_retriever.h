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

#ifndef CL_DYN_GLOBAL_RETRIEVER_H
#define CL_DYN_GLOBAL_RETRIEVER_H

#include "nel/pacs/global_retriever.h"
#include "nel/pacs/u_global_position.h"
#include "dyn_retriever_bank.h"

class CMapBase;

/**
 * A class that allows to retrieve surface in a large amount of zones (referred as instances.) -> for dyn world
 * \author Kishan Grimout
 * \date 2014
 */
class CDynGlobalRetriever : public NLPACS::CGlobalRetriever
{
public:
	/**
	 * Constructor.
	 * Creates a global retriever with given width, height and retriever bank.
	 */
	CDynGlobalRetriever(const CDynRetrieverBank *bank=NULL, CMapBase* map=NULL);

	virtual ~CDynGlobalRetriever() {}

	void refreshLrAround(const NLMISC::CVector &position, float radius) override;
	void refreshLrAroundNow(const NLMISC::CVector &position, float radius) override;
	bool selectInstances(const NLMISC::CAABBox &bbox, NLPACS::CCollisionSurfaceTemp &cst, NLPACS::UGlobalPosition::TType type = NLPACS::UGlobalPosition::Unspecified) const override;

	NLPACS::UGlobalPosition	retrievePosition(const NLMISC::CVectorD &estimated, double /* threshold */= 0., NLPACS::UGlobalPosition::TType retrieveSpec = NLPACS::UGlobalPosition::Unspecified) const override;
	float getMeanHeight(const NLPACS::UGlobalPosition &pos) const override;
	NLMISC::CVector getGlobalPosition(const NLPACS::UGlobalPosition &global) const override;
	NLMISC::CVectorD getDoubleGlobalPosition(const NLPACS::UGlobalPosition &global) const override;

	const NLPACS::TCollisionSurfaceDescVector* testCylinderMove(const NLPACS::UGlobalPosition &start, const NLMISC::CVector &delta, float radius, NLPACS::CCollisionSurfaceTemp &cst) const override;
	NLPACS::UGlobalPosition doMove(const NLPACS::UGlobalPosition &startPos, const NLMISC::CVector &delta, float t, NLPACS::CCollisionSurfaceTemp &cst, bool rebuildChains) const override;

	bool isInterior(const NLPACS::UGlobalPosition &pos) const override;
	bool isWaterPosition(const NLPACS::UGlobalPosition &pos, float &waterHeight) const override;

	const std::string& getIdentifier(const NLPACS::UGlobalPosition &position) const override;

	void getBorders(const NLPACS::UGlobalPosition &pos, std::vector<std::pair<NLMISC::CLine, uint8> > &edges) override;

private:
	void refreshLrAroundMax(const NLMISC::CVector &position, float radius, uint maxEmerge = 0xFFFFFFFF);
	CMapBase* m_map;
};

#endif // CL_DYN_GLOBAL_RETRIEVER_H

/* End of dyn_global_retriever.h */
