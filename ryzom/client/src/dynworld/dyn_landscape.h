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
#include "nel/3d/vegetable_def.h"

#include "game_share/dynworld/map_sector.h"

class IRegionMesh;

namespace NL3D
{
	class CVertexProgram;
	class CPixelProgram;
	class CVegetableManager;
	class CVegetableClipBlock;
	class CVegetableSortBlock;
	class CVegetableInstanceGroup;
	class CTileVegetableDesc;
	class CTileBank;
}

namespace DYNWORLD
{
	struct CVegetBucket;
}

class CRegionMesh;
class CSectorTransition;

typedef std::map<TGenericSectorId, NLMISC::CSmartPtr<IRegionMesh>> TRegionModelContainer;
typedef std::vector<NLMISC::CSmartPtr<CSectorTransition>> TTransitionContainer;

// ***************************************************************************
/**
 * P2 (issue #7): dynworld analogue of NL3D::CLandscapeVegetableBlock.
 *
 * One per (lod, sectorId) whose mesh hosts plantable surface. A stock
 * CVegetableClipBlock owns the sector vegetable geometry; one sort block and
 * per-DistType instance groups exist per plantable bucket (the 2 m clustering
 * cell of the placement engine — the analog of the stock 2x2 m tile). The
 * ring-band logic mirrors the stock CLandscapeVegetableBlock::update()
 * (camera distance band -> IG create/delete, sort block refresh), applied
 * per bucket entry so the per-frame commit budget can starve individual
 * entries without breaking their state.
 */
class	CDynVegetableBlock
{
public:
	struct TBucketEntry
	{
		NL3D::CVegetableSortBlock* SortBlock = nullptr;
		NL3D::CVegetableInstanceGroup* Igs[NL3D_VEGETABLE_BLOCK_NUMDIST] = { };
		/// Stock semantics: NL3D_VEGETABLE_BLOCK_NUMDIST means "no IG yet";
		/// band [0..NUMDIST) = IGs [0..CurDistType) exist.
		uint8 CurDistType = NL3D_VEGETABLE_BLOCK_NUMDIST;
		/// Planting data, owned by the sector mesh's placement set (the block
		/// is recreated whenever the mesh pointer changes).
		const DYNWORLD::CVegetBucket* Bucket = nullptr;
		const NL3D::CTileVegetableDesc* TileVegetDesc = nullptr;
	};

	CDynVegetableBlock() { }
	/// Releases everything that is still allocated.
	~CDynVegetableBlock() { nlassert(_Entries.empty() && _ClipBlock == nullptr); }

	/// Resolve the placement set into bucket entries (tile vegetable desc per
	/// (biome, material) group) and create the clip + sort blocks. Must run
	/// inside the vegetable manager lock window.
	void init(CRegionMesh& mesh, NL3D::CVegetableManager& vegetManager, const NLMISC::CVector& sectorCenter,
		const NL3D::CTileBank& tileBank);

	/// Delete IGs, sort blocks and the clip block. Must run inside the
	/// vegetable manager lock window.
	void release(NL3D::CVegetableManager& vegetManager);

	/// Stock ring-band update per entry. \\param budget remaining per-frame IG
	/// commit budget (decremented); an entry whose band change does not fit is
	/// left untouched and retried next frame.
	void update(const NLMISC::CVector& viewCenter, NL3D::CVegetableManager& vegetManager, sint& budget);

	CRegionMesh* getMesh() const { return _Mesh; }
	const NLMISC::CVector& getCenter() const { return _Center; }
	uint getEntryCount() const { return (uint)_Entries.size(); }
	uint getIgCount() const;

private:
	void createIgForDistType(TBucketEntry& entry, uint distType, NL3D::CVegetableManager& vegetManager);
	void deleteIg(TBucketEntry& entry, uint distType, NL3D::CVegetableManager& vegetManager);

	NLMISC::CVector _Center;
	NL3D::CVegetableClipBlock* _ClipBlock = nullptr;
	CRegionMesh* _Mesh = nullptr;
	std::vector<TBucketEntry> _Entries;
};

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

	/// P2 (issue #7 plan): per-frame vegetable block sync against
	/// m_regionsList (blocks keyed by (lod, sectorId), created on mesh
	/// insertion regardless of displayFactor — see §6.7 —, released on
	/// removal, recreated when the mesh pointer changed), then stock
	/// ring-band updates nearest-block-first under the per-frame
	/// g_maxVegetCommit IG budget. Main thread only; opens the vegetable
	/// manager lock window itself when there is any work to do.
	void updateVegetableBlocks(const NLMISC::CVector& viewCenter);

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

	/// P5 debug command support (issue #7 "dyn_veget"): enable/disable
	/// microvegetation. Disabling also releases the vegetable blocks
	/// (frees the vegetable VB space); re-enabling recreates them through
	/// the structural sync in updateVegetableBlocks.
	void setVegetableActive(bool enable);

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

	void clearVegetableBlocks();
	void createVegetableBlock(uint lod, TGenericSectorId sectorId, CRegionMesh& mesh);

	std::string m_VPSource;
	std::string m_PPSource;
	std::string m_VPGLSLBody;
	std::string m_PPGLSLBody;
	NL3D::CVertexProgram* m_VP;
	NL3D::CPixelProgram* m_PP;
	NL3D::CMaterial m_Mat;

	/// P2 vegetable blocks, keyed by (lod, sectorId). Blocks are created only
	/// for meshes with a non-empty placement set (a block with zero plantable
	/// entries stays in the map but costs only its empty structures).
	std::map<std::pair<uint, TGenericSectorId>, CDynVegetableBlock*> m_vegetableBlocks;
};


#endif // CL_DYN_LANDSCAPE_H
