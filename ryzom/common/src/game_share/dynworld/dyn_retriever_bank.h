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

#ifndef CL_DYN_RETRIEVER_BANK_H
#define CL_DYN_RETRIEVER_BANK_H

#include "nel/pacs/retriever_bank.h"


/**
 * A bank of retrievers, shared by several global retrievers -> for dyn world
 * \author Kishan Grimout
 * \date 2014
 */
class CDynRetrieverBank : public NLPACS::CRetrieverBank
{
	friend class NLPACS::URetrieverBank;

public:
	/// Constructor
	CDynRetrieverBank(bool allLoaded = true) : NLPACS::CRetrieverBank(allLoaded) {}
};

#endif // CL_DYN_RETRIEVER_BANK_H

/* End of dyn_retriever_bank.h */
