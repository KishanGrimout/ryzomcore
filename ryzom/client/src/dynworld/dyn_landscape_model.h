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

#ifndef CL_DYN_LANDSCAPE_MODEL_H
#define CL_DYN_LANDSCAPE_MODEL_H

#include "nel/3d/landscape_model.h"


// ***************************************************************************
const NLMISC::CClassId		DynLandscapeModelId=NLMISC::CClassId(0x5a573b56, 0x6b395830);


// ***************************************************************************
/**
 * Dynamic version of CLandscapeModel
 * \see CLandscapeModel.
 */
class	CDynLandscapeModel : public NL3D::CLandscapeModel
{
public:
	/// Call at the beginning of the program, to register the model
	static	void	registerBasic();

public:

	/** Override CTransform::initModel(), to create CLandscape's VegetableManager's BlendLayer models in the scene.
	 */
	virtual void	initModel();
	/// special traverseHRC.
	virtual void	traverseHrc();
	/// special clip(). NB: the real landscape clip is done in traverseRender()
	virtual void	traverseClip();
	virtual bool	clip() {return true;}
	/// traverseRender()
	virtual void	traverseRender();
	/// Some prof infos
	virtual	void	profileRender();

	/// Actual Clip and Render!! See Implementation for Why this scheme
	virtual void	clipAndRenderLandscape() override;


	/// \name ShadowMap Behavior. Receive only
	// @{
	virtual void		getReceiverBBox(NLMISC::CAABBox &bbox);
	virtual void		receiveShadowMap(NL3D::CShadowMap *shadowMap, const NLMISC::CVector &casterPos, const NL3D::CMaterial &shadowMat);
	virtual const NLMISC::CMatrix	&getReceiverRenderWorldMatrix() const {return _RenderWorldMatrix;}
	// @}

protected:
	CDynLandscapeModel();
	virtual ~CDynLandscapeModel() {}

private:
	static NL3D::CTransform	*creator() {return new CDynLandscapeModel;}
};


#endif // CL_DYN_LANDSCAPE_MODEL_H
