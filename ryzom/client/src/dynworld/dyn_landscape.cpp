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

#include "stdpch.h"

#include "dyn_landscape.h"

#include "dynworld/region_mesh.h"
#include "dynworld/dyn_continent.h"
#include "dynworld/map_client.h"
#include "dynworld/dyn_landscape_user.h"

#include "nel/3d/vertex_program.h"
#include "nel/3d/pixel_program.h"
#include "nel/3d/uniform_buffer_format.h"
#include "nel/3d/uniform_buffer.h"
#include "nel/3d/vegetable_manager.h"
#include "nel/3d/vegetable_shape.h"
#include "nel/3d/tile_vegetable_desc.h"
#include "nel/misc/fast_floor.h"

#include "game_share/dynworld/material_library.h"
#include "game_share/dynworld/vegetation_placement.h"

using namespace std;
using namespace NLMISC;
using namespace NL3D;

static const char *DynLandscapeProgramName = "dyn_landscape";
static const char *DynLandscapeVertexProgramExt = ".vp1";
static const char *DynLandscapeFragmentProgramExt = ".arbfp1";

namespace
{
	// Hand-authored GLSL bodies loaded from data/shaders at runtime
	// (profile-neutral: see the comments in the .glsl files; the per-profile
	// #version + attribute/varying declarations below complete the sources,
	// and the GL3 driver injects the builtin/user uniform blocks).
	static const char *DynLandscapeVertexGLSLFile = "dyn_landscape_vp.glsl";
	static const char *DynLandscapeFragmentGLSLFile = "dyn_landscape_fp.glsl";

	// Shared per-draw params block, declared in BOTH GLSL stages
	// (x = tighten, y = scale, z = displayFactor, w = spare). The VP source
	// registers it at UBBindingVertexProgram and the PP source at
	// UBBindingPixelProgram; the same CUniformBuffer is bound to both binding
	// points around the landscape draws.
	struct CDynLandscapeUBOOffsets
	{
		sint Params;
	};
	CDynLandscapeUBOOffsets s_LandscapeUBOOffsets;
	NLMISC::CSmartPtr<CUniformBufferFormat> s_LandscapeUBFormat;
	NLMISC::CSmartPtr<CUniformBuffer> s_LandscapeUB;

	void initLandscapeUBO()
	{
		if (s_LandscapeUBFormat)
			return;
		CUniformBufferFormat *fmt = new CUniformBufferFormat();
		fmt->Name = "NlDynLandscape";
		s_LandscapeUBOOffsets.Params = fmt->push("landscapeParams", CUniformBufferFormat::FloatVec4);
		s_LandscapeUBFormat = fmt;
		s_LandscapeUB = new CUniformBuffer();
		s_LandscapeUB->Format = *fmt;
		s_LandscapeUB->UsageHint = CUniformBuffer::StreamDraw;
	}

	bool isGLSLProfile(NL3D::IProgram::TProfile profile)
	{
		return profile == NL3D::IProgram::glsl330v || profile == NL3D::IProgram::glsl330f
			|| profile == NL3D::IProgram::glsl300esv || profile == NL3D::IProgram::glsl300esf;
	}

	const char *programProfileName(NL3D::IProgram::TProfile profile)
	{
		switch (profile)
		{
		case NL3D::IProgram::nelvp: return "nelvp";
		case NL3D::IProgram::glsl330v: return "glsl330v";
		case NL3D::IProgram::glsl330f: return "glsl330f";
		case NL3D::IProgram::glsl300esv: return "glsl300esv";
		case NL3D::IProgram::glsl300esf: return "glsl300esf";
		case NL3D::IProgram::arbfp1: return "arbfp1";
		case NL3D::IProgram::arbvp1: return "arbvp1";
		default: return "?";
		}
	}

	// Per-profile GLSL prefixes: #version + attribute/varying declarations.
	// The driver (insertBuiltinHeaders) injects the builtin (NlModel/NlCamera/
	// NlLightTable) and user (NlDynLandscape) uniform blocks after the leading
	// #version/#extension/precision lines of these prefixes. The glsl300es
	// (linked) variant must not use layout() on varyings (GLSL ES 3.00
	// forbids it — the GL linker matches VP outputs to PP inputs by name);
	// the SSO glsl330 variant pins locations per the VaryingLocation* contract
	// (ecPos=0, texCoord0..2=8..10). Attribute locations follow the GL3
	// vertex buffer binding map (position=0, normal=2, Empty[alias Tangent]=7
	// — CVertexBuffer::Empty is a deprecated alias of Tangent, enum 15, NOT
	// the PaletteSkin slot at GL index 6; the legacy vp1 assembly's v[7].x is
	// exactly the Tangent GL index).
	const char *GLSLVPPrefixSSO =
		"#version 330\n"
		"#extension GL_ARB_separate_shader_objects : enable\n"
		"\n"
		"out gl_PerVertex { vec4 gl_Position; };\n"
		"\n"
		"layout(location = 0) in vec4 position;\n"
		"layout(location = 2) in vec4 normal;\n"
		// CVertexBuffer::Empty is a DEPRECATED ALIAS of Tangent (enum 15) —
		// the region VB per-vertex alpha therefore travels the Tangent slot,
		// GLVertexAttribIndex[Tangent] = 7 (= the legacy vp1 assembly's v[7]).
		"layout(location = 7) in vec4 empty;\n"
		"\n"
		"layout(location = 0) smooth out vec4 ecPos;\n"
		"layout(location = 8) smooth out vec4 texCoord0;\n"
		"layout(location = 9) smooth out vec4 texCoord1;\n"
		"layout(location = 10) smooth out vec4 texCoord2;\n"
		"\n";

	const char *GLSLVPPrefix300es =
		"#version 300 es\n"
		"precision highp float;\n"
		"precision highp int;\n"
		"\n"
		"layout(location = 0) in vec4 position;\n"
		"layout(location = 2) in vec4 normal;\n"
		// CVertexBuffer::Empty is a DEPRECATED ALIAS of Tangent (enum 15) —
		// the region VB per-vertex alpha therefore travels the Tangent slot,
		// GLVertexAttribIndex[Tangent] = 7 (= the legacy vp1 assembly's v[7]).
		"layout(location = 7) in vec4 empty;\n"
		"\n"
		"smooth out vec4 ecPos;\n"
		"smooth out vec4 texCoord0;\n"
		"smooth out vec4 texCoord1;\n"
		"smooth out vec4 texCoord2;\n"
		"\n";

	const char *GLSLFPPrefixSSO =
		"#version 330\n"
		"#extension GL_ARB_separate_shader_objects : enable\n"
		"\n"
		"layout(location = 0) smooth in vec4 ecPos;\n"
		"layout(location = 8) smooth in vec4 texCoord0;\n"
		"layout(location = 9) smooth in vec4 texCoord1;\n"
		"layout(location = 10) smooth in vec4 texCoord2;\n"
		"out vec4 fragColor;\n"
		// Sampler uniforms named after CProgramIndex::Sampler0/1 so
		// setupInitialUniforms binds material texture units 0/1 to them.
		"uniform sampler2D sampler0;\n"
		"uniform sampler2D sampler1;\n"
		"\n";

	const char *GLSLFPPrefix300es =
		"#version 300 es\n"
		"precision highp float;\n"
		"precision highp int;\n"
		"\n"
		"smooth in vec4 ecPos;\n"
		"smooth in vec4 texCoord0;\n"
		"smooth in vec4 texCoord1;\n"
		"smooth in vec4 texCoord2;\n"
		"out vec4 fragColor;\n"
		// Sampler uniforms named after CProgramIndex::Sampler0/1 so
		// setupInitialUniforms binds material texture units 0/1 to them.
		"uniform sampler2D sampler0;\n"
		"uniform sampler2D sampler1;\n"
		"\n";
}

/*class CVertexProgramDynLandscape : public CVertexProgram
{
public:
	struct CIdx
	{
		// 0-3 mvp
		//uint WorldToUV0; // 4
		//uint WorldToUV1; // 5
		//uint RefCamDist; // 6
		//uint DistScaleBias; // 7
		//uint Diffuse; // 8
		//// 9
		//// 10
		//uint BlendScale; // 11
	};
	CVertexProgramDynLandscape()
	{
		CIFile file(DynLandscapeProgramName + DynLandscapeVertexProgramExt, true);
		file.serial(m_source);

		// nelvp
		{
			CSource *source = new CSource();
			source->Profile = nelvp;
			source->DisplayName = "nelvp/";
			source->DisplayName += DynLandscapeProgramName;
			source->setSourcePtr(m_source.c_str());
			source->ParamIndices["modelViewProjection"] = 0;
			//source->ParamIndices["worldToUV0"] = 4;
			//source->ParamIndices["worldToUV1"] = 5;
			//source->ParamIndices["refCamDist"] = 6;
			//source->ParamIndices["distScaleBias"] = 7;
			//source->ParamIndices["diffuse"] = 8;
			//source->ParamIndices["blendScale"] = 11;
			addSource(source);
		}
		// TODO_VP_GLSL
	}
	~CVertexProgramDynLandscape()
	{

	}
	virtual void buildInfo()
	{
		CVertexProgram::buildInfo();
		//m_Idx.WorldToUV0 = getUniformIndex("worldToUV0");
		//nlassert(m_Idx.WorldToUV0 != ~0);
		//m_Idx.WorldToUV1 = getUniformIndex("worldToUV1");
		//nlassert(m_Idx.WorldToUV1 != ~0);
		//m_Idx.RefCamDist = getUniformIndex("refCamDist");
		//nlassert(m_Idx.RefCamDist != ~0);
		//m_Idx.DistScaleBias = getUniformIndex("distScaleBias");
		//nlassert(m_Idx.DistScaleBias != ~0);
		//m_Idx.Diffuse = getUniformIndex("diffuse");
		//nlassert(m_Idx.Diffuse != ~0);
		//m_Idx.BlendScale = getUniformIndex("blendScale");
		//nlassert(m_Idx.BlendScale != ~0);
	}
	inline const CIdx &idx() const { return m_Idx; }

private:
	CIdx m_Idx;
	std::string m_source;
};


class CPixelProgramDynLandscape : public CPixelProgram
{

};*/

CDynLandscape::CDynLandscape()
{
	m_VP = NULL;
	m_PP = NULL;

	readShaderFiles();

	m_Mat.initUnlit();
	m_Mat.setBlend(true);
	m_Mat.setBlendFunc(CMaterial::srcalpha, CMaterial::invsrcalpha);
}

CDynLandscape::~CDynLandscape()
{
	// Must run before the base destructor destroys _VegetableManager.
	clearVegetableBlocks();
}

namespace
{
	// Reads a shader source file from the search paths. Non-throwing: returns
	// false (and leaves dest empty) when the file is missing so optional files
	// (the GLSL bodies) may be absent — the assembly fallbacks still load.
	bool readProgramFile(const std::string &name, std::string &dest)
	{
		dest.clear();
		std::string path = CPath::lookup(name, false, false, true);
		if (path.empty())
		{
			nlwarning("3D: dynlandscape: shader file '%s' not found on search paths", name.c_str());
			return false;
		}
		CIFile file(path, true);
		uint fileSize = file.getFileSize();
		dest.resize(fileSize);
		file.serialBuffer((uint8*)dest.data(), fileSize);
		return true;
	}
}

void CDynLandscape::readShaderFiles()
{
	// Any of these may fail to load if the search paths miss the file;
	// setupPrograms() compiles only the sources that are actually present.
	readProgramFile(std::string(DynLandscapeProgramName) + DynLandscapeVertexProgramExt, m_VPSource);
	readProgramFile(std::string(DynLandscapeProgramName) + DynLandscapeFragmentProgramExt, m_PPSource);
	readProgramFile(DynLandscapeVertexGLSLFile, m_VPGLSLBody);
	readProgramFile(DynLandscapeFragmentGLSLFile, m_PPGLSLBody);
}

void CDynLandscape::clear()
{
	clearVegetableBlocks();
	clearGeneratedMeshes();
	CLandscape::clear();
}

void CDynLandscape::clearGeneratedMeshes()
{
	// Blocks reference mesh-owned placement data: drop them with the meshes.
	clearVegetableBlocks();
	for (uint i = 0; i < LOD_COUNT; ++i)
	{
		m_regionsList[i].clear();
	}
}

// ****************************************************************************
// P2 microvegetation blocks (issue #7). See CDynVegetableBlock in
// dyn_landscape.h; the ring-band logic mirrors stock
// CLandscapeVegetableBlock::update (landscape_vegetable_block.cpp).

// Per-frame budget of vegetable IG commits (initial plantings and ring band
// changes; each IG creation counts 1). Tuned via "dyn_vegetbudget".
uint g_maxVegetCommit = 256;

namespace
{
	// Distances type squared (stock CLVBSqrDistLUT: (i*ELTDIST)^2).
	struct CDynVBSqrDistLUT
	{
		float Array[NL3D_VEGETABLE_BLOCK_NUMDIST + 1];
		CDynVBSqrDistLUT()
		{
			for (uint i = 0; i < NL3D_VEGETABLE_BLOCK_NUMDIST + 1; ++i)
			{
				const float d = i * NL3D_VEGETABLE_BLOCK_ELTDIST;
				Array[i] = d * d;
			}
		}
	};
	const CDynVBSqrDistLUT DynVBSqrDistLUT;

	// (biome, material) -> first top-texture tile id, i.e. the same lookup
	// bindRegionMeshTexture uses for the diffuse binding. 0xFFFF = unresolvable.
	const uint16 VegetInvalidTileId = 0xFFFF;
	uint16 resolveTileIdForGroup(Voxel::BiomeType biome, Voxel::MaterialType material)
	{
		const CMaterialDefinition* matDef = CMaterialLibrary::instance().getMaterialDef(material);
		if (matDef == nullptr)
			return VegetInvalidTileId;
		const CMaterialTextureSet* textSet = matDef->getTextureSet(biome);
		if (textSet == nullptr || textSet->m_textures.empty() || textSet->m_textures[0].m_variations.empty())
			return VegetInvalidTileId;
		return textSet->m_textures[0].m_variations[0];
	}
}

// ****************************************************************************
void CDynVegetableBlock::init(CRegionMesh& mesh, CVegetableManager& vegetManager, const CVector& sectorCenter,
	const CTileBank& tileBank)
{
	nlassert(_ClipBlock == nullptr && _Entries.empty());
	_Center = sectorCenter;
	_Mesh = &mesh;
	_ClipBlock = vegetManager.createClipBlock();

	const DYNWORLD::CDynVegetPlacementSet& placements = mesh.m_vegetPlacements;
	for (const DYNWORLD::CVegetSurfaceGroup& group : placements.Groups)
	{
		const uint16 tileId = resolveTileIdForGroup(group.Biome, group.Material);
		if (tileId == VegetInvalidTileId)
			continue;
		const CTileVegetableDesc& tileVegetDesc = tileBank.getTileVegetableDesc(tileId);
		if (tileVegetDesc.empty())
			continue;

		for (const DYNWORLD::CVegetBucket& bucket : group.Buckets)
		{
			TBucketEntry entry;
			entry.Bucket = &bucket;
			entry.TileVegetDesc = &tileVegetDesc;
			// NB: same scale as the stock 2x2 m tile radius.
			entry.SortBlock = vegetManager.createSortBlock(_ClipBlock, bucket.Center, NL3D_PATCH_TILE_RADIUS);
			_Entries.push_back(entry);
		}
	}
}

// ****************************************************************************
void CDynVegetableBlock::release(CVegetableManager& vegetManager)
{
	// release all Igs, then all Sbs, then the clip block (stock order).
	for (TBucketEntry& entry : _Entries)
	{
		for (uint i = 0; i < NL3D_VEGETABLE_BLOCK_NUMDIST; ++i)
			deleteIg(entry, i, vegetManager);
		if (entry.SortBlock != nullptr)
		{
			vegetManager.deleteSortBlock(entry.SortBlock);
			entry.SortBlock = nullptr;
		}
	}
	_Entries.clear();

	if (_ClipBlock != nullptr)
	{
		vegetManager.deleteClipBlock(_ClipBlock);
		_ClipBlock = nullptr;
	}
	_Mesh = nullptr;
}

// ****************************************************************************
uint CDynVegetableBlock::getIgCount() const
{
	uint count = 0;
	for (const TBucketEntry& entry : _Entries)
		for (uint i = 0; i < NL3D_VEGETABLE_BLOCK_NUMDIST; ++i)
			if (entry.Igs[i] != nullptr)
				++count;
	return count;
}

// ****************************************************************************
void CDynVegetableBlock::deleteIg(TBucketEntry& entry, uint distType, CVegetableManager& vegetManager)
{
	if (entry.Igs[distType] != nullptr)
	{
		vegetManager.deleteIg(entry.Igs[distType]);
		entry.Igs[distType] = nullptr;
	}
}

// ****************************************************************************
void CDynVegetableBlock::createIgForDistType(TBucketEntry& entry, uint distType, CVegetableManager& vegetManager)
{
	nlassert(entry.Igs[distType] == nullptr);

	// get list of vegetables for this distType category (stock rule: each
	// distType hosts the vegetables declared for exactly that distance band)
	const std::vector<CVegetable>& vegetableList = entry.TileVegetDesc->getVegetableList(distType);
	if (vegetableList.empty())
		return; // nothing for this band: no IG (budget unaffected)

	CVegetableInstanceGroup* vegetIg = vegetManager.createIg(entry.SortBlock);
	entry.Igs[distType] = vegetIg;

	// First, for each vegetable, generate the number of instances to create
	// and their relative UV positions (bucket = the generating "patch"; runs
	// at commit time on the main thread: reads the manager global density and
	// needs the OptFastFloor window opened by the caller).
	const uint distAddSeed = entry.TileVegetDesc->getVegetableSeed(distType);
	CVegetableInstanceGroupReserve vegetIgReserve;
	std::vector<std::vector<CVector2f>> instanceUVArray(vegetableList.size());
	for (uint v = 0; v < vegetableList.size(); ++v)
	{
		const CVegetable& veget = vegetableList[v];
		veget.generateGroup(entry.Bucket->Center, entry.Bucket->Normal, entry.Bucket->Area,
			v + distAddSeed, instanceUVArray[v]);
		veget.reserveIgAddInstances(vegetIgReserve, CVegetable::AboveWater, (uint)instanceUVArray[v].size());
	}
	vegetManager.reserveIgCompile(vegetIg, vegetIgReserve);

	// Then generate the real instances. Lighting v1 (plan): white ambient,
	// diffuse shade from the landing facet slope; water state AboveWater.
	const CRGBAF ambientF(1.f, 1.f, 1.f, 1.f);
	const float blendDistMax = (distType + 1) * NL3D_VEGETABLE_BLOCK_ELTDIST;
	for (uint v = 0; v < vegetableList.size(); ++v)
	{
		const CVegetable& veget = vegetableList[v];
		const std::vector<CVector2f>& instanceUV = instanceUVArray[v];
		for (uint j = 0; j < instanceUV.size(); ++j)
		{
			CVector instancePos, instanceNormal;
			DYNWORLD::vegetBucketResolvePosition(*entry.Bucket, instanceUV[j], instancePos, instanceNormal);

			// surface-aligned basis (stock style: K up the facet normal,
			// I/J in the surface plane)
			CVector K = instanceNormal;
			CVector I = K ^ CVector(0.f, 0.f, 1.f);
			if (I.norm() < 0.01f)
				I = CVector(1.f, 0.f, 0.f);
			I.normalize();
			CVector J = K ^ I;
			J.normalize();

			CMatrix instanceMat;
			instanceMat.setRot(I, J, K);
			instanceMat.setPos(instancePos);

			const float shade = 0.35f + 0.65f * std::max(0.f, instanceNormal.z);
			const CRGBAF diffuseF(shade, shade, shade, 1.f);

			veget.generateInstance(vegetIg, instanceMat, ambientF, diffuseF, blendDistMax,
				CVegetable::AboveWater, CVegetableUV8());
		}
	}

	// If the ig is empty, delete him. This optimize rendering because no
	// useless ig are tested for rendering (stock rule).
	if (vegetIg->isEmpty())
	{
		vegetManager.deleteIg(vegetIg);
		entry.Igs[distType] = nullptr;
	}
}

// ****************************************************************************
void CDynVegetableBlock::update(const CVector& viewCenter, CVegetableManager& vegetManager, sint& budget)
{
	for (TBucketEntry& entry : _Entries)
	{
		if (entry.SortBlock == nullptr)
			continue;

		// compute new distance type from the bucket center (stock incremental
		// band walk over the squared-distance LUT)
		const float sqrDist = (viewCenter - entry.Bucket->Center).sqrnorm();
		uint newDistType = entry.CurDistType;
		while (newDistType > 0 && sqrDist < DynVBSqrDistLUT.Array[newDistType])
			--newDistType;
		while (newDistType < NL3D_VEGETABLE_BLOCK_NUMDIST && sqrDist > DynVBSqrDistLUT.Array[newDistType + 1])
			++newDistType;
		if (newDistType == entry.CurDistType)
			continue;

		if (newDistType > entry.CurDistType)
		{
			// receding: release IGs [CurDistType, newDistType)
			for (uint i = entry.CurDistType; i < newDistType; ++i)
				deleteIg(entry, i, vegetManager);
			entry.SortBlock->updateSortBlock(vegetManager);
			entry.CurDistType = (uint8)newDistType;
		}
		else
		{
			// approaching: create IGs [newDistType, CurDistType). All-or-
			// nothing per entry so the committed state stays coherent: when
			// the budget cannot cover the whole band change, the entry keeps
			// its current state and retries next frame.
			uint needed = 0;
			for (uint i = newDistType; i < entry.CurDistType; ++i)
			{
				if (!entry.TileVegetDesc->getVegetableList(i).empty())
					++needed;
			}
			if ((sint)needed > budget)
				continue;
			for (uint i = newDistType; i < entry.CurDistType; ++i)
				createIgForDistType(entry, i, vegetManager);
			entry.SortBlock->updateSortBlock(vegetManager);
			entry.CurDistType = (uint8)newDistType;
			budget -= (sint)needed;
		}
	}
}

// ****************************************************************************
void CDynLandscape::clearVegetableBlocks()
{
	if (!m_vegetableBlocks.empty() && _VegetableManager != nullptr)
	{
		OptFastFloorBegin();
		_VegetableManager->lockBuffers();
		for (auto& kv : m_vegetableBlocks)
			kv.second->release(*_VegetableManager);
		_VegetableManager->unlockBuffers();
		OptFastFloorEnd();
	}
	for (auto& kv : m_vegetableBlocks)
		delete kv.second;
	m_vegetableBlocks.clear();
}

// ****************************************************************************
void CDynLandscape::createVegetableBlock(uint lod, TGenericSectorId sectorId, CRegionMesh& mesh)
{
	// sector center (same origin/scale conventions as renderRegions)
	NLMISC::CVectorI sectorPosI;
	CSectorLod::idToPos(lod, sectorId, sectorPosI);
	const float sideSize = CSectorLod::getSideSize(lod);
	const CVector center(float(sectorPosI.x) * sideSize + sideSize * 0.5f,
		float(sectorPosI.y) * sideSize + sideSize * 0.5f,
		float(sectorPosI.z) * sideSize);

	CDynVegetableBlock* block = new CDynVegetableBlock;
	block->init(mesh, *_VegetableManager, center, TileBank);
	if (block->getEntryCount() == 0)
	{
		// nothing plantable in this mesh (no soil groups or empty vegetable
		// descs): don't keep an empty block around, the structural sync would
		// otherwise erase/recreate it every frame.
		block->release(*_VegetableManager);
		delete block;
		return;
	}
	m_vegetableBlocks[std::make_pair(lod, sectorId)] = block;
}

// ****************************************************************************
void CDynLandscape::updateVegetableBlocks(const CVector& viewCenter)
{
	if (_VegetableManager == nullptr || !isVegetableActive())
		return;

	// Structural sync (cheap pass, no NL3D calls): blocks exist for (lod 0/1)
	// sectors whose mesh is fully visible (displayFactor >= 1) and carries a
	// non-empty placement set. Recreate on mesh pointer change (re-extraction
	// = new placement data), release when the sector left the list.
	std::vector<std::pair<uint, TGenericSectorId>> toErase;
	std::vector<std::pair<uint, TGenericSectorId>> toCreate;
	for (uint lod = 0; lod < LOD_COUNT && lod <= 1; ++lod)
	{
		for (auto& kv : m_regionsList[lod])
		{
			CRegionMesh* mesh = static_cast<CRegionMesh*>(kv.second.getPtr());
			if (mesh == nullptr || mesh->m_displayFactor < 1.f || mesh->m_vegetPlacements.empty())
				continue;
			auto blockIt = m_vegetableBlocks.find(std::make_pair(lod, kv.first));
			if (blockIt == m_vegetableBlocks.end())
				toCreate.push_back(std::make_pair(lod, kv.first));
			else if (blockIt->second->getMesh() != mesh)
			{
				toErase.push_back(std::make_pair(lod, kv.first)); // replaced below
				toCreate.push_back(std::make_pair(lod, kv.first));
			}
		}
	}
	for (auto& kv : m_vegetableBlocks)
	{
		auto meshIt = m_regionsList[kv.first.first].find(kv.first.second);
		if (meshIt == m_regionsList[kv.first.first].end())
		{
			toErase.push_back(kv.first);
			continue;
		}
		CRegionMesh* mesh = static_cast<CRegionMesh*>(meshIt->second.getPtr());
		if (mesh == nullptr || mesh->m_displayFactor < 1.f || mesh->m_vegetPlacements.empty())
			toErase.push_back(kv.first);
	}
	// dedup (a replaced key appears in both passes)
	std::sort(toErase.begin(), toErase.end());
	toErase.erase(std::unique(toErase.begin(), toErase.end()), toErase.end());

	if (toErase.empty() && toCreate.empty() && m_vegetableBlocks.empty())
		return; // nothing to do this frame: no lock window at all

	// Commit window (stock discipline): updateDriver, lock, ops, unlock; the
	// OptFastFloor window is required by CVegetable::generateGroup.
	_VegetableManager->updateDriver(_Driver);
	OptFastFloorBegin();
	_VegetableManager->lockBuffers();

	// release stale blocks first (frees vegetable VB space before allocations)
	for (const auto& key : toErase)
	{
		auto blockIt = m_vegetableBlocks.find(key);
		if (blockIt != m_vegetableBlocks.end())
		{
			blockIt->second->release(*_VegetableManager);
			delete blockIt->second;
			m_vegetableBlocks.erase(blockIt);
		}
	}
	for (const auto& key : toCreate)
	{
		auto meshIt = m_regionsList[key.first].find(key.second);
		if (meshIt == m_regionsList[key.first].end())
			continue;
		CRegionMesh* mesh = static_cast<CRegionMesh*>(meshIt->second.getPtr());
		createVegetableBlock(key.first, key.second, *mesh);
	}

	// ring-band updates for all live blocks, under the per-frame IG budget
	sint budget = (sint)g_maxVegetCommit;
	for (auto& kv : m_vegetableBlocks)
		kv.second->update(viewCenter, *_VegetableManager, budget);

	_VegetableManager->unlockBuffers();
	OptFastFloorEnd();
}

// ****************************************************************************
// P0 probe for the microvegetation plan (docs/DYNWORLD_MICROVEGETATION_PLAN.md):
// dump everything the stock planting pipeline needs, so a client session can
// validate the data pipeline end to end (issue #7).
void CDynLandscape::dumpVegetableData() const
{
	nlinfo("DYNVEGET: dump begin (vegetableActive=%d, density=%.3f)", isVegetableActive(), getVegetableDensity());

	const CTileBank& tileBank = TileBank;
	const uint numTileSets = tileBank.getTileSetCount();
	nlinfo("DYNVEGET: %u tilesets in the bank", numTileSets);

	for (uint ts = 0; ts < numTileSets; ++ts)
	{
		const CTileSet* tileSet = tileBank.getTileSet(sint(ts));
		if (tileSet == nullptr)
			continue;

		const std::string& tileSetName = tileSet->getName();
		const std::string& descFileName = tileSet->getTileVegetableDescFileName();
		const CTileVegetableDesc& vegDesc = tileSet->getTileVegetableDesc();

		if (vegDesc.empty())
		{
			nlinfo("DYNVEGET: tileset '%s' vegetDesc='%s' EMPTY", tileSetName.c_str(), descFileName.c_str());
			continue;
		}

		for (uint distType = 0; distType < NL3D_VEGETABLE_BLOCK_NUMDIST; ++distType)
		{
			const std::vector<CVegetable>& vegetableList = vegDesc.getVegetableList(distType);
			if (vegetableList.empty())
				continue;

			std::string shapes;
			for (const CVegetable& veget : vegetableList)
			{
				// Shape availability through the manager cache. NB: shapes are
				// normally registered by initTileVegetableDescs (called from
				// initTileBanks at flushTiles time); getVegetableShape only
				// lazily loads what is missing.
				CVegetableShape* shape = _VegetableManager->getVegetableShape(veget.ShapeName);
				shapes += veget.ShapeName + (shape != nullptr ? "[ok]" : "[FAIL]") + " ";
			}
			nlinfo("DYNVEGET: tileset '%s' vegetDesc='%s' distType=%u seed=%u count=%u: %s",
				tileSetName.c_str(), descFileName.c_str(), distType,
				vegDesc.getVegetableSeed(distType), (uint)vegetableList.size(), shapes.c_str());
		}
	}

	// Material -> tile bindings (same lookup bindRegionMeshTexture uses), to
	// see which tileset (hence which vegetable desc) each bound material ends
	// up with.
	CMaterialLibrary& materialLibrary = CMaterialLibrary::instance();
	nlinfo("DYNVEGET: %u environments, %u materials", materialLibrary.getMaxEnvironmentId() + 1, materialLibrary.getMaxMaterialId() + 1);
	for (uint matId = 0; matId <= materialLibrary.getMaxMaterialId(); ++matId)
	{
		const CMaterialDefinition* matDef = materialLibrary.getMaterialDef(matId);
		if (matDef == nullptr)
			continue;

		for (uint envId = 0; envId <= materialLibrary.getMaxEnvironmentId(); ++envId)
		{
			const CMaterialTextureSet* textureSet = matDef->getTextureSet(envId);
			if (textureSet == nullptr || textureSet->m_textures.empty() || textureSet->m_textures[0].m_variations.empty())
				continue;

			uint16 tileId = textureSet->m_textures[0].m_variations[0];
			const CTileVegetableDesc& vegDesc = tileBank.getTileVegetableDesc(tileId);
			const CTileVegetableDesc* ownerTileSetDesc = nullptr;
			std::string ownerTileSetName = "<no tileset>";
			for (uint ts = 0; ts < numTileSets; ++ts)
			{
				const CTileSet* tileSet = tileBank.getTileSet(sint(ts));
				if (tileSet != nullptr && &tileSet->getTileVegetableDesc() == &vegDesc)
				{
					ownerTileSetDesc = &vegDesc;
					ownerTileSetName = tileSet->getName();
					break;
				}
			}
			nlinfo("DYNVEGET: material '%s' env=%u -> tile %u tileset '%s' vegetDesc %s",
				matDef->getName(), envId, (uint)tileId, ownerTileSetName.c_str(),
				(ownerTileSetDesc != nullptr && !ownerTileSetDesc->empty()) ? "OK" : "EMPTY/MISSING");
		}
	}

	nlinfo("DYNVEGET: dump end");
}

// ****************************************************************************
// P0 probe (issue #7): exercise the full stock reserve/add commit path and the
// dyn_renderveget render hook with hand-planted instances.
void CDynLandscape::vegetTest(const CVector& pos, const CVector& normal, const std::string& shapeName, uint count)
{
	if (_VegetableManager == nullptr)
	{
		nlwarning("DYNVEGET: no vegetable manager (vegetables not enabled?)");
		return;
	}

	CVegetableShape* shape = _VegetableManager->getVegetableShape(shapeName);
	if (shape == nullptr)
	{
		nlwarning("DYNVEGET: shape '%s' not found (CPath lookup/BNP problem)", shapeName.c_str());
		return;
	}

	CVegetable veget;
	veget.ShapeName = shapeName;
	veget.registerToManager(_VegetableManager);

	_VegetableManager->updateDriver(_Driver);

	OptFastFloorBegin();
	_VegetableManager->lockBuffers();

	CVegetableClipBlock* clipBlock = _VegetableManager->createClipBlock();
	CVegetableSortBlock* sortBlock = _VegetableManager->createSortBlock(clipBlock, pos, 50.f);
	CVegetableInstanceGroup* ig = _VegetableManager->createIg(sortBlock);

	CVegetableInstanceGroupReserve reserve;
	veget.reserveIgAddInstances(reserve, CVegetable::AboveWater, count);
	_VegetableManager->reserveIgCompile(ig, reserve);

	// Surface-aligned basis (stock patch style: I/J in the surface plane, K up)
	CVector K = normal.normed();
	CVector I = K ^ CVector(0.f, 0.f, 1.f);
	if (I.norm() < 0.01f)
		I = CVector(1.f, 0.f, 0.f);
	I.normalize();
	CVector J = K ^ I;
	J.normalize();

	CMatrix instanceMatrix;
	instanceMatrix.setRot(I, J, K);

	const CRGBAF white(1.f, 1.f, 1.f, 1.f);
	for (uint i = 0; i < count; ++i)
	{
		// spread the instances in a small grid around pos so a bunch is visible
		CMatrix mat = instanceMatrix;
		float dx = (float)(i % 5 - 2) * 0.5f;
		float dy = (float)(i / 5 - 2) * 0.5f;
		mat.setPos(pos + I * dx + J * dy + K * 0.05f);
		veget.generateInstance(ig, mat, white, white, 50.f, CVegetable::AboveWater, CVegetableUV8());
	}

	// Stock parity: CLandscapeVegetableBlock::update() calls updateSortBlock()
	// right after creating/deleting IGs (landscape_vegetable_block.cpp:189/208).
	// generateInstance only marks the sortBlock _Dirty; _NTriangles is computed
	// by updateSortBlock. Skipping it leaves _NTriangles==0, and the manager's
	// ZSort pass registers nothing (observed: zsortRegistered=0 with the whole
	// IG in the ZSort pass).
	_VegetableManager->updateSortBlockForIg(ig);

	_VegetableManager->unlockBuffers();
	OptFastFloorEnd();

	nlinfo("DYNVEGET: vegetTest planted %u x '%s' at (%.2f,%.2f,%.2f)",
		count, shapeName.c_str(), pos.x, pos.y, pos.z);
}

bool clipBV(const CAABBox& bv, const CMatrix& transform, const CPlane* planes, sint nbPlanes)
{
	bool clipped = false;
	CAABBox globalBv = CAABBox::transformAABBox(transform, bv);
	for (sint i = 0; i < nbPlanes; i++)
	{
		// If entirely out.
		if (!globalBv.clipBack(planes[i]))
		{
			clipped = true;
			break;
		}
	}
	return clipped;
}

bool g_renderLegacyLandscape = false;
CMaterial::TTexCoordGenMode g_texCoordGenMode = CMaterial::TexCoordGenObjectSpace;
bool g_useVP = true;
bool g_usePP = true;
bool g_rebuildShaders = false;
float g_tighten = 0.4679f;
// Fragment-stage debug mode selector, staged through the unused x channel of
// the NlDynLandscape. w component (dyn_shaderdebug handler in commands.cpp).
// 0 = production path; 1..6 keep the production VP untouched and bisect the
// fragment stage (see dyn_landscape_fp.glsl header).
float g_shaderDebugMode = 0.f;
Voxel::MaterialType g_discardMaterial = 0xFF;
bool g_clipBV = true;
// P0 probe (issue #7): render planted vegetable IGs with the stock manager,
// gated behind the dyn_renderveget debug toggle until the planting pipeline
// lands (P3 honors doVegetables for real).
bool g_renderVegetables = false;
void CDynLandscape::logVegetStats() const
{
	if (_VegetableManager == nullptr)
	{
		nlinfo("DYNVEGET: stats: no vegetable manager (vegetables not enabled?)");
		return;
	}
	uint numClipBlocks, numEmptyClipBlocks, numIgs, numTris, numVerts;
	uint numVisibleClipBlocks, numZSortRegistered, zSortFacesLast, zSortVBVerts;
	_VegetableManager->getVegetProbeState(numClipBlocks, numEmptyClipBlocks, numIgs, numTris, numVerts,
		numVisibleClipBlocks, numZSortRegistered, zSortFacesLast, zSortVBVerts);
	// P2 block-level state (const map: the pointer is const, the block isn't)
	uint vegetBlocks = 0, vegetEntries = 0, vegetBlockIgs = 0;
	for (auto& kv : m_vegetableBlocks)
	{
		++vegetBlocks;
		vegetEntries += kv.second->getEntryCount();
		vegetBlockIgs += kv.second->getIgCount();
	}
	nlinfo("DYNVEGET: stats: renderVegetables=%d active=%d facesRendered=%u clipBlocks=%u emptyClipBlocks=%u igs=%u tris=%u verts=%u visibleClipBlocks=%u zsortRegistered=%u zsortFacesLast=%u zsortVBVerts=%u vegetBlocks=%u vegetEntries=%u vegetBlockIgs=%u",
		(int)g_renderVegetables, (int)isVegetableActive(),
		(uint)_VegetableManager->getNumVegetableFaceRendered(),
		numClipBlocks, numEmptyClipBlocks, numIgs, numTris, numVerts,
		numVisibleClipBlocks, numZSortRegistered, zSortFacesLast, zSortVBVerts,
		vegetBlocks, vegetEntries, vegetBlockIgs);
}
void CDynLandscape::render(const CVector &refineCenter, const CVector &frontVector, const CPlane pyramid[NL3D_TESSBLOCK_NUM_CLIP_PLANE], bool doTileAddPass /*= false*/, bool doVegetables /*= true*/)
{
	if (g_rebuildShaders)
	{
		g_rebuildShaders = false;
		reloadPrograms();
	}

	_Driver->setupFog(_Driver->getFogStart(), _Driver->getFogEnd(), _Driver->getFogColor());

	if (g_useVP && m_VP != NULL)
	{
		bool vpok = _Driver->activeVertexProgram(m_VP);
		nlassert(vpok);
	}
	if (g_usePP && m_PP != NULL)
	{
		bool ppok = _Driver->activePixelProgram(m_PP);
		nlassert(ppok);
	}

	// No user pixel program available (compile failed or disabled): leave the
	// material in the builtin texgen path so the terrain still renders
	// through the driver's builtin programs.
	if (!g_usePP || m_PP == NULL)
	{
		m_Mat.setTexCoordGen(0, true);
		m_Mat.setTexCoordGenMode(0, g_texCoordGenMode);
	}

	bool glslVP = (g_useVP && m_VP != NULL && isGLSLProfile(m_VP->profile()));
	bool glslPP = (g_usePP && m_PP != NULL && isGLSLProfile(m_PP->profile()));
	// Shared params UBO: the same buffer feeds both stages (block
	// NlDynLandscape declared in both GLSL sources).
	if (glslVP)
		_Driver->bindUniformBuffer(NL3D::UBBindingVertexProgram, s_LandscapeUB);
	if (glslPP)
		_Driver->bindUniformBuffer(NL3D::UBBindingPixelProgram, s_LandscapeUB);

	renderRegions(pyramid, NL3D_TESSBLOCK_NUM_CLIP_PLANE);
	renderTransitions(pyramid, NL3D_TESSBLOCK_NUM_CLIP_PLANE);

	// unbind user UBOs
	if (glslVP)
		_Driver->bindUniformBuffer(NL3D::UBBindingVertexProgram, nullptr);
	if (glslPP)
		_Driver->bindUniformBuffer(NL3D::UBBindingPixelProgram, nullptr);

	// reset texture
	m_Mat.setTexture(0, NULL);
	m_Mat.setTexture(1, NULL);

	// deactivate program
	_Driver->activeVertexProgram(NULL);
	_Driver->activePixelProgram(NULL);
	_Driver->setupModelMatrix(CMatrix::Identity);

	// P0 probe (issue #7): stock microvegetation render through the embedded
	// CVegetableManager. No dynamic lightmap in dyn sessions (NULL), sun
	// ambient/diffuse already fed by CDynLandscapeModel::setupVegetableLighting.
	// NB: placed AFTER the buffer unlock below — the stock CLandscape::render
	// releases the VBs at the end of the far/tile passes and only then renders
	// vegetables (landscape.cpp:1231 vs the vegetable section); the manager's
	// updateDriver asserts !isLocked, so the manager render must run outside
	// the frame's VB lock window.
	if (g_renderLegacyLandscape)
		CLandscape::render(refineCenter, frontVector, pyramid, doTileAddPass);
	else
		unlockBuffers();

	if (doVegetables && g_renderVegetables && isVegetableActive() && _VegetableManager != nullptr)
	{
		// The manager's render takes a vector pyramid (stock CLandscape::render
		// builds the same from the tessblock array).
		std::vector<CPlane> vegetPyramid(pyramid, pyramid + NL3D_TESSBLOCK_NUM_CLIP_PLANE);
		_VegetableManager->render(refineCenter, frontVector, vegetPyramid, NULL, _Driver);
		// P0 probe: log only on change so the log stays readable. The counter
		// covers this frame (the manager resets it when it renders).
		uint32 faces = _VegetableManager->getNumVegetableFaceRendered();
		static sint32 lastFaces = -1;
		if ((sint32)faces != lastFaces)
		{
			nlinfo("DYNVEGET: render: facesRendered=%u", (uint)faces);
			lastFaces = (sint32)faces;
		}
	}

	renderSelectedVoxel();
}

void CDynLandscape::renderRegion(const CRegionMesh* mesh, const CMatrix& matrix)
{
	_Driver->setupModelMatrix(matrix);

	float scale = matrix.getScaleUniform();

	if (g_useVP && m_VP != NULL)
	{
		if (isGLSLProfile(m_VP->profile()))
		{
			// params into the shared user UBO (NlDynLandscape): x = tighten,
			// y = scale, z = displayFactor. MVP/camera state comes from the
			// driver-uploaded builtin UBOs (NlModel/NlCamera); per-vertex
			// register writes are not used in the GLSL path.
			s_LandscapeUB->lock();
			s_LandscapeUB->set(s_LandscapeUBOOffsets.Params, g_tighten, scale, mesh->m_displayFactor, g_shaderDebugMode);
			s_LandscapeUB->unlock();
		}
		else // nelvp fallback (GL1 driver and converted GL3 path): fixed register ids
		{
			_Driver->setUniformMatrix(IDriver::VertexProgram, m_VP->getUniformIndex(CProgramIndex::ModelViewProjection), IDriver::ModelViewProjection, IDriver::Identity);
			_Driver->setUniform1f(IDriver::VertexProgram, 4, g_tighten);
			_Driver->setUniform1f(IDriver::VertexProgram, 5, scale);
			_Driver->setUniformFog(IDriver::VertexProgram, 10);
		}
	}

	if (g_usePP && m_PP != NULL && !isGLSLProfile(m_PP->profile()))
	{
		// xy: fog factor, z:blend factor (arbfp1 fallback only — the GLSL
		// fragment stage takes displayFactor from the UBO and fog state from
		// the NlCamera builtin UBO)
		_Driver->setUniform4f(IDriver::PixelProgram, 2, -1.f / (_Driver->getFogEnd() - _Driver->getFogStart()), _Driver->getFogEnd() / (_Driver->getFogEnd() - _Driver->getFogStart()), mesh->m_displayFactor, 0.f);
	}

	uint submeshesCount = mesh->m_submeshes.size();
	for (uint subIdx = 0; subIdx < submeshesCount; ++subIdx)
	{
		CRegionSubMesh* submesh = mesh->m_submeshes[subIdx];
		if (submesh != nullptr && submesh->m_materialType != g_discardMaterial)
		{
			ITexture* diffuse0 = submesh->DiffuseTop;
			ITexture* diffuse1 = submesh->DiffuseSide;

			if (diffuse0 == nullptr)
				diffuse0 = diffuse1;

			if (diffuse1 == nullptr)
				diffuse1 = diffuse0;

			if (diffuse0 == nullptr)
				continue;

			// Setup material.
			// Setup Diffuse texture of the tile.
			m_Mat.setTexture(0, diffuse0);
			m_Mat.setTexture(1, diffuse1);

			_Driver->activeVertexBuffer(submesh->m_vb);
			_Driver->activeIndexBuffer(submesh->m_ib);

			//Render of all triangles of the cube
			_Driver->renderTriangles(m_Mat, 0, submesh->m_ib.getNumIndexes() / 3);
		}
	}
}

void CDynLandscape::renderRegions(const CPlane* pyramid, sint nbPlanes)
{
	for (uint i = 0; i < LOD_COUNT; ++i)
	{
		float lodFactor = pow(2.f, float(i));
		float scale = CSectorLod::getVoxelSize(i) * lodFactor;
		float sideSize = CSectorLod::getSideSize(i);
		TRegionModelContainer::const_iterator it = m_regionsList[i].begin();
		TRegionModelContainer::const_iterator end = m_regionsList[i].end();
		while (it != end)
		{
			CRegionMesh* mesh = static_cast<CRegionMesh*>(it->second.getPtr());
			if (mesh->m_displayFactor < 1.f)
			{
				++it;
				continue;
			}

			NLMISC::CVectorI posI;
			CSectorLod::idToPos(i, it->first, posI);
			NLMISC::CVector posF(posI.x * sideSize, posI.y * sideSize, posI.z * sideSize);

			CMatrix mtx;
			mtx.identity();
			mtx.translate(posF);
			mtx.setScale(scale);

			if (g_clipBV && clipBV(mesh->m_bv, mtx, pyramid, nbPlanes))
			{
				++it;
				continue;
			}

			renderRegion(mesh, mtx);

			++it;
		}
	}
}

void CDynLandscape::renderTransitions(const CPlane* pyramid, sint nbPlanes)
{
	for (auto& transition : m_transitionsList)
	{
		if (transition->m_previousFactor < 1.f)
		{
			uint lod = transition->m_previousLod;
			float lodFactor = pow(2.f, float(lod));
			float scale = CSectorLod::getVoxelSize(lod) * lodFactor;
			float sideSize = CSectorLod::getSideSize(lod);

			for (uint i = 0; i < transition->m_previousMeshes.size(); ++i)
			{
				CRegionMesh* mesh = static_cast<CRegionMesh*>(transition->m_previousMeshes[i].getPtr());
				NLMISC::CVectorI posI;
				CSectorLod::idToPos(lod, transition->m_previousSectorIds[i], posI);
				NLMISC::CVector posF(posI.x * sideSize, posI.y * sideSize, posI.z * sideSize);

				CMatrix mtx;
				mtx.identity();
				mtx.translate(posF);
				mtx.setScale(scale);

				if (g_clipBV && clipBV(mesh->m_bv, mtx, pyramid, nbPlanes))
				{
					continue;
				}

				renderRegion(mesh, mtx);
			}
		}
		else if(transition->m_nextFactor < 1.f)
		{
			uint lod = transition->m_nextLod;
			float lodFactor = pow(2.f, float(lod));
			float scale = CSectorLod::getVoxelSize(lod) * lodFactor;
			float sideSize = CSectorLod::getSideSize(lod);

			for (uint i = 0; i < transition->m_nextMeshes.size(); ++i)
			{
				CRegionMesh* mesh = static_cast<CRegionMesh*>(transition->m_nextMeshes[i].getPtr());
				NLMISC::CVectorI posI;
				CSectorLod::idToPos(lod, transition->m_nextSectorIds[i], posI);
				NLMISC::CVector posF(posI.x * sideSize, posI.y * sideSize, posI.z * sideSize);

				CMatrix mtx;
				mtx.identity();
				mtx.translate(posF);
				mtx.setScale(scale);

				if (g_clipBV && clipBV(mesh->m_bv, mtx, pyramid, nbPlanes))
				{
					continue;
				}

				renderRegion(mesh, mtx);
			}
		}
	}
}

void CDynLandscape::setDriver(NL3D::IDriver *drv)
{
	if (_Driver != drv)
	{
		CLandscape::setDriver(drv);
		deletePrograms();
		setupPrograms();
	}
}

void CDynLandscape::deletePrograms()
{
	delete m_VP;
	m_VP = NULL;
	delete m_PP;
	m_PP = NULL;
}

void CDynLandscape::reloadPrograms()
{
	// Snapshot the currently-loaded sources; restored below if the freshly
	// read files fail to compile. Single attempt per set (no infinite retry
	// loop in render()): on double failure the terrain falls back to the
	// builtin programs until a later dyn_rebuildshaders succeeds.
	std::string lastVPSource, lastPPSource, lastVPGLSL, lastPPGLSL;
	lastVPSource.swap(m_VPSource);
	lastPPSource.swap(m_PPSource);
	lastVPGLSL.swap(m_VPGLSLBody);
	lastPPGLSL.swap(m_PPGLSLBody);

	readShaderFiles();
	deletePrograms();
	if (setupPrograms())
	{
		nlinfo("3D: dynlandscape shaders reloaded (profile delta: VP %s, PP %s)",
			programProfileName(m_VP ? m_VP->profile() : NL3D::IProgram::nelvp),
			programProfileName(m_PP ? m_PP->profile() : NL3D::IProgram::arbfp1));
		return;
	}
	if (!lastVPSource.empty() || !lastPPSource.empty() || !lastVPGLSL.empty() || !lastPPGLSL.empty())
	{
		// new sources unusable: restore the previous snapshot and rebuild once
		m_VPSource.swap(lastVPSource);
		m_PPSource.swap(lastPPSource);
		m_VPGLSLBody.swap(lastVPGLSL);
		m_PPGLSLBody.swap(lastPPGLSL);
		if (setupPrograms())
		{
			nlwarning("3D: dynlandscape new shaders failed to compile; previous programs restored");
			return;
		}
	}
	m_VP = NULL;
	m_PP = NULL;
	nlwarning("3D: dynlandscape shaders unavailable after reload attempt; using builtin programs until the next rebuild");
}

void CDynLandscape::addVPGLSLSources()
{
	if (m_VPGLSLBody.empty())
		return;

	// glsl300esv — linked single-stage source (primary with the GL3 driver,
	// which forces m_LinkedMegaShaders=true on desktop; also the GLES3 path).
	// OnlyUBOs: the linked path rejects default-block uniforms —
	// all params go through the "NlDynLandscape" user UBO block. The NlModel
	// object UBO provides modelViewProjection and implies the camera UBO
	// (inverseProjectionBasis used by the synthesized ecPos) and the light
	// table.
	{
		IProgram::CSource *source = new IProgram::CSource();
		source->Features.MaterialFlags = CProgramFeatures::TextureStages;
		source->Features.OnlyUBOs = true;
		source->Features.UsesObjectUBO = true;
		source->UniformBufferFormats[NL3D::UBBindingVertexProgram] = s_LandscapeUBFormat;
		source->Profile = IProgram::glsl300esv;
		source->DisplayName = "glsl300esv/dyn_landscape";
		source->setSource(std::string(GLSLVPPrefix300es) + m_VPGLSLBody);
		m_VP->addSource(source);
	}
	// glsl330v — SSO fallback (desktop separable programs)
	{
		IProgram::CSource *source = new IProgram::CSource();
		source->Features.MaterialFlags = CProgramFeatures::TextureStages;
		source->Features.OnlyUBOs = true;
		source->Features.UsesObjectUBO = true;
		source->UniformBufferFormats[NL3D::UBBindingVertexProgram] = s_LandscapeUBFormat;
		source->Profile = IProgram::glsl330v;
		source->DisplayName = "glsl330v/dyn_landscape";
		source->setSource(std::string(GLSLVPPrefixSSO) + m_VPGLSLBody);
		m_VP->addSource(source);
	}
}

void CDynLandscape::addPPGLSLSources()
{
	if (m_PPGLSLBody.empty())
		return;

	// glsl300esf — linked single-stage source (primary, same as VP). Fog
	// state comes from the camera UBO, the sun from light table entry 0
	// (nlLights[0]); no PPL uniforms are used (SupportPPL stays off).
	{
		IProgram::CSource *source = new IProgram::CSource();
		source->Features.MaterialFlags = CProgramFeatures::TextureStages;
		source->Features.OnlyUBOs = true;
		source->Features.UsesLightTableUBO = true;
		source->Features.UsesCameraUBO = true;
		source->UniformBufferFormats[NL3D::UBBindingPixelProgram] = s_LandscapeUBFormat;
		source->Profile = IProgram::glsl300esf;
		source->DisplayName = "glsl300esf/dyn_landscape";
		source->setSource(std::string(GLSLFPPrefix300es) + m_PPGLSLBody);
		m_PP->addSource(source);
	}
	// glsl330f — SSO fallback (desktop separable programs)
	{
		IProgram::CSource *source = new IProgram::CSource();
		source->Features.MaterialFlags = CProgramFeatures::TextureStages;
		source->Features.OnlyUBOs = true;
		source->Features.UsesLightTableUBO = true;
		source->Features.UsesCameraUBO = true;
		source->UniformBufferFormats[NL3D::UBBindingPixelProgram] = s_LandscapeUBFormat;
		source->Profile = IProgram::glsl330f;
		source->DisplayName = "glsl330f/dyn_landscape";
		source->setSource(std::string(GLSLFPPrefixSSO) + m_PPGLSLBody);
		m_PP->addSource(source);
	}
}

bool CDynLandscape::setupPrograms()
{
	initLandscapeUBO();

	m_VP = new CVertexProgram();
	// nelvp — GL1 fallback (all drivers; note: m_VP != NULL even if the
	// GLSL sources below are absent so the legacy path always works)
	{
		IProgram::CSource *source = new IProgram::CSource();
		//source->Features.MaterialFlags = CProgramFeatures::TextureStages;
		source->Profile = IProgram::nelvp;
		source->setSourcePtr(m_VPSource.c_str());
		source->ParamIndices["modelViewProjection"] = 0;
		source->ParamIndices["tighten"] = 4;
 		source->ParamIndices["scale"] = 5;
		source->ParamIndices["fog"] = 10;
// 		source->ParamIndices["tileOffset"] = 5;

		m_VP->addSource(source);
	}
	// hand-authored GLSL (data/shaders/dyn_landscape_vp.glsl) — GL3 primary
	addVPGLSLSources();
	if (!_Driver->compileVertexProgram(m_VP))
	{
		nlwarning("3D: No supported vertex program for dynlandscape");

		delete m_VP;
		m_VP = NULL;
		delete m_PP;
		m_PP = NULL;
		return false;
	}
	else
	{
		nldebug("3D: dynlandscape vertex program available (%s)", programProfileName(m_VP->profile()));
	}

	m_PP = new CPixelProgram();
	// arbfp1 — GL1 fallback
	{
		IProgram::CSource *source = new IProgram::CSource();
		source->Features.MaterialFlags = CProgramFeatures::TextureStages;
		source->Profile = IProgram::arbfp1;
		source->setSourcePtr(m_PPSource.c_str());
		m_PP->addSource(source);
	}
	// hand-authored GLSL (data/shaders/dyn_landscape_fp.glsl) — GL3 primary
	addPPGLSLSources();
	if (!_Driver->compilePixelProgram(m_PP))
	{
		nlwarning("3D: No supported pixel program for dynlandscape");

		delete m_PP;
		m_PP = NULL;
		return false;
	}
	else
	{
		nldebug("3D: dynlandscape pixel program available (%s)", programProfileName(m_PP->profile()));
	}
	return true;
}

void CDynLandscape::renderSelectedVoxel()
{
	if (!CDynContinent::MAP->isVoxelSelected())
		return;

	CVector pos(CDynContinent::MAP->voxelUnderCursor());
	pos *= TLoadingSector::VoxelSize; // back to worlds coordinates
	pos -= (CVector(TLoadingSector::VoxelSize*.5f, TLoadingSector::VoxelSize*.5f, TLoadingSector::VoxelSize*.5f));
	CMatrix mtx;
	mtx.identity();
	mtx.translate(pos);
	_Driver->setupModelMatrix(mtx);

	//Material needed by the fonction of render triangles
	CMaterial mat;
	mat.initUnlit();
	mat.setSrcBlend(CMaterial::srcalpha);
	mat.setDstBlend(CMaterial::invsrcalpha);
	mat.setZFunc(CMaterial::always);
	mat.setColor(CRGBA(50, 255, 255, 150)); //Set a color to the material

	//Must determinate the format for vertex
	//Later we'll see that we could add other informations as the color for exemple
	CVertexBuffer vb;
	vb.setVertexFormat(CVertexBuffer::PositionFlag);
	vb.setNumVertices(8);
	{
		CVertexBufferReadWrite vba;
		vb.lock(vba);
		vba.setVertexCoord(0, CVector(0.0f, 0.0f, 0.0f));
		vba.setVertexCoord(1, CVector(TLoadingSector::VoxelSize, 0.0f, 0.0f));
		vba.setVertexCoord(2, CVector(TLoadingSector::VoxelSize, 0.0f, TLoadingSector::VoxelSize));
		vba.setVertexCoord(3, CVector(0.0f, 0.0f, TLoadingSector::VoxelSize));
		vba.setVertexCoord(4, CVector(0.0f, TLoadingSector::VoxelSize, 0.0f));
		vba.setVertexCoord(5, CVector(TLoadingSector::VoxelSize, TLoadingSector::VoxelSize, 0.0f));
		vba.setVertexCoord(6, CVector(TLoadingSector::VoxelSize, TLoadingSector::VoxelSize, TLoadingSector::VoxelSize));
		vba.setVertexCoord(7, CVector(0.0f, TLoadingSector::VoxelSize, TLoadingSector::VoxelSize));
	}
	//Now we can set to active our vertex buffer
	_Driver->activeVertexBuffer(vb);

	CIndexBuffer pbQuad1;
	pbQuad1.setNumIndexes(24);
	{
		CIndexBufferReadWrite iba1;
		pbQuad1.lock(iba1);
		//With vertex we can now make some lines for cube
		iba1.setLine(0, 0, 1); //front
		iba1.setLine(2, 1, 2); //front
		iba1.setLine(4, 2, 3); //right
		iba1.setLine(6, 3, 0); //right
		iba1.setLine(8, 4, 0); //top
		iba1.setLine(10, 5, 1); //top
		iba1.setLine(12, 6, 2); //bottom
		iba1.setLine(14, 7, 3); //bottom
		iba1.setLine(16, 4, 5); //left
		iba1.setLine(18, 5, 6); //left
		iba1.setLine(20, 6, 7); //behind
		iba1.setLine(22, 7, 4); //behind
	}

	//Active index buffer
	_Driver->activeIndexBuffer(pbQuad1);
	//Render of all triangles of the cube
	_Driver->renderLines(mat, 0, pbQuad1.getNumIndexes() / 2);
}

float CDynLandscape::getCameraCollision(const CVector &start, const CVector &end, float radius, bool cone)
{
	// TODO: use map deferred command
	return CDynContinent::MAP->castRay(start, end);
}

// ***************************************************************************
float CDynLandscape::getRayCollision(const CVector &start, const CVector &end)
{
	// TODO: use map deferred command
	return CDynContinent::MAP->castRay(start, end);
}
