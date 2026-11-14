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
class CDynLandscape;

typedef std::map<TGenericSectorId, NLMISC::CSmartPtr<IRegionMesh>> TRegionModelContainer;
typedef std::vector<NLMISC::CSmartPtr<CSectorTransition>> TTransitionContainer;

// ***************************************************************************
/**
 * P2: dynworld analogue of NL3D::CLandscapeVegetableBlock.
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
		/// Biome of the surface group.
		Voxel::BiomeType Biome = 1;
	};

	CDynVegetableBlock() { }
	/// Releases everything that is still allocated.
	~CDynVegetableBlock() { nlassert(_Entries.empty() && _ClipBlock == nullptr); }

	/// Resolve the placement set into bucket entries (tile vegetable desc per
	/// (biome, material) group) and create the clip + sort blocks. Must run
	/// inside the vegetable manager lock window.
	void init(CRegionMesh& mesh, NL3D::CVegetableManager& vegetManager, const NLMISC::CVector& sectorCenter,
		const CDynLandscape& landscape);

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
	/// Landscape owner (merged-atlas shape remap). Set in init().
	const CDynLandscape* _Landscape = nullptr;
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

	/// P2: per-frame vegetable block sync against
	/// m_regionsList (blocks keyed by (lod, sectorId), created on mesh
	/// insertion regardless of displayFactor — see §6.7 —, released on
	/// removal, recreated when the mesh pointer changed), then stock
	/// ring-band updates nearest-block-first under the per-frame
	/// g_maxVegetCommit IG budget. Main thread only; opens the vegetable
	/// manager lock window itself when there is any work to do.
	void updateVegetableBlocks(const NLMISC::CVector& viewCenter);

	/// Debug probe: dump the tile-bank vegetable
	/// descriptors (tileset name, desc file, per-DistType vegetable shapes)
	/// and the material->tile bindings, to check the microvegetation data
	/// pipeline end to end. Run via the "dyn_vegetdump" command.
	void dumpVegetableData() const;

	/// Debug probe: plant `count` instances of a .veget
	/// shape at a world position (surface-aligned on `normal`), in a
	/// dedicated probe IG. Pairs with the "dyn_renderveget" render toggle.
	/// Instances accumulate until the landscape is cleared (probe only).
	void vegetTest(const NLMISC::CVector& pos, const NLMISC::CVector& normal, const std::string& shapeName, uint count,
		Voxel::BiomeType biome = 1);

	/// Debug probe: log the microvegetation render state
	/// (toggle, active flag, face counter of the last manager render).
	/// Run via the "dyn_vegetstats" command.
	void logVegetStats() const;

	/// P5 debug command support: enable/disable
	/// microvegetation. Disabling also releases the vegetable blocks
	/// (frees the vegetable VB space); re-enabling recreates them through
	/// the structural sync in updateVegetableBlocks.
	void setVegetableActive(bool enable);

	/// Per-biome vegetable side banks: load
	/// the ecosystem tile banks the main TileBank does not contain (the dyn
	/// continent reuses the newbieland sheet, whose ecosystem is jungle, so
	/// the main bank holds only jungle tilesets). Call once after flushTiles.
	/// Re-calling releases the vegetable blocks first (they hold desc
	/// pointers into these banks) and rebuilds.
	void loadVegetSideBanks();

	/// Resolve the tile vegetable desc for a (biome, material) surface group:
	/// main TileBank first (bindMaterials tile id — the stock path), then the
	/// biome's side bank (name-prefix tile match, same rule as
	/// CMaterialTextureSet::bindSet). Returns nullptr when the group has no
	/// plantable data (the caller skips it).
	const NL3D::CTileVegetableDesc* resolveVegetDescForGroup(Voxel::BiomeType biome, Voxel::MaterialType material) const;

	/// Merged microvegetation atlas: install the 2x2
	/// composition of the four ecosystem atlases (jungle, fyros, tryker,
	/// prime roots) as the manager's stage-1 texture, using the season-
	/// postfixed source variants for `season` (0..3 = Sp/Su/Au/Wi, 0xFF =
	/// base, season >= 4 falls back to base). Returns false when a source
	/// atlas is unavailable (caller falls back to the stock
	/// single-ecosystem loadVegetableTexture path).
	bool loadDynVegetableAtlas(uint8 season);

	/// Remap a .veget shape's Tex0 UVs (its ecosystem atlas coordinates) into
	/// the merged atlas quarter of `biome`. Must run before the first IG of
	/// the shape compiles (Tex0 is copied into the vegetable VBs at compile
	/// time); idempotent per shape name, warns on cross-quarter reuse.
	/// (const: the remap record is a cache — the manager-owned shape data
	/// mutation is the actual effect.)
	bool ensureVegetShapeAtlasRemap(const std::string& shapeName, Voxel::BiomeType biome) const;

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
	void clearVegetSideBanks();

	/// Per-biome side tile banks for vegetable descs, keyed by biome id
	/// (desert=2, lakes=3, roots=4). Owned; must outlive all vegetable
	/// blocks (they store desc pointers into these banks).
	std::map<uint8, NL3D::CTileBank*> m_vegetSideBanks;

	/// Merged-atlas remap record: shape name -> atlas quarter it was remapped
	/// to. Mutable: ensureVegetShapeAtlasRemap is const
	/// (the actual mutation is the manager-owned shape data). The manager
	/// caches shapes across reloads, so the record must survive
	/// CDynLandscape::clear() as well.
	mutable std::map<std::string, uint8> m_vegetShapeAtlasQuarter;

	/// True once loadDynVegetableAtlas() installed the merged 2x2 atlas.
	/// ensureVegetShapeAtlasRemap must not compress UVs into quarters while
	/// the bound texture is a single full-range ecosystem atlas (stock
	/// fallback): remapping against it would show a quarter-sized crop.
	bool m_vegetMergedAtlasInstalled;

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
