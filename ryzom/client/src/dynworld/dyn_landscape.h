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

#ifndef CL_DYN_LANDSCAPE_H
#define CL_DYN_LANDSCAPE_H

#include "nel/misc/smart_ptr.h"
#include "nel/3d/landscape.h"

#include "game_share/dynworld/map_sector.h"

class IRegionMesh;

namespace NL3D
{
	class CVertexProgram;
	class CPixelProgram;
}

class CRegionMesh;
class CSectorTransition;

typedef std::map<TGenericSectorId, NLMISC::CSmartPtr<IRegionMesh>> TRegionModelContainer;
typedef std::vector<NLMISC::CSmartPtr<CSectorTransition>> TTransitionContainer;

// ***************************************************************************
/**
 * Dynamic version of CLandscape
 * \see CLandscape.
 */
class	CDynLandscape : public NL3D::CLandscape
{
public:
	/// Constructor
	CDynLandscape();
	/// Destructor. clear().
	virtual ~CDynLandscape();

	virtual void clear() override;
	virtual void render(const NLMISC::CVector &refineCenter, const NLMISC::CVector &frontVector, const NLMISC::CPlane pyramid[NL3D_TESSBLOCK_NUM_CLIP_PLANE], bool doTileAddPass = false, bool doVegetables = true) override;
	virtual void setDriver(NL3D::IDriver *drv);

	virtual float getCameraCollision(const NLMISC::CVector &start, const NLMISC::CVector &end, float radius, bool cone) override;
	virtual float getRayCollision(const NLMISC::CVector &start, const NLMISC::CVector &end) override;

	void clearGeneratedMeshes();

	/// Debug probe (P0, issue #7 plan): dump the tile-bank vegetable
	/// descriptors (tileset name, desc file, per-DistType vegetable shapes)
	/// and the material->tile bindings, to check the microvegetation data
	/// pipeline end to end. Run via the "dyn_vegetdump" command.
	void dumpVegetableData() const;

	/// Debug probe (P0, issue #7 plan): plant `count` instances of a .veget
	/// shape at a world position (surface-aligned on `normal`), in a
	/// dedicated probe IG. Pairs with the "dyn_renderveget" render toggle.
	/// Instances accumulate until the landscape is cleared (probe only).
	void vegetTest(const NLMISC::CVector& pos, const NLMISC::CVector& normal, const std::string& shapeName, uint count);

	/// Debug probe (P0, issue #7 plan): log the microvegetation render state
	/// (toggle, active flag, face counter of the last manager render).
	/// Run via the "dyn_vegetstats" command.
	void logVegetStats() const;

	TRegionModelContainer m_regionsList[LOD_COUNT];
	TTransitionContainer m_transitionsList;

private:
	void renderRegions(const NLMISC::CPlane* pyramid, sint nbPlanes);
	void renderRegion(const CRegionMesh* mesh, const NLMISC::CMatrix& matrix);
	void renderTransitions(const NLMISC::CPlane* pyramid, sint nbPlanes);
	void renderSelectedVoxel();
	bool setupPrograms();
	void addVPGLSLSources();
	void addPPGLSLSources();
	void reloadPrograms();
	void deletePrograms();
	void readShaderFiles();

	std::string m_VPSource;
	std::string m_PPSource;
	std::string m_VPGLSLBody;
	std::string m_PPGLSLBody;
	NL3D::CVertexProgram* m_VP;
	NL3D::CPixelProgram* m_PP;
	NL3D::CMaterial m_Mat;
};


#endif // CL_DYN_LANDSCAPE_H
