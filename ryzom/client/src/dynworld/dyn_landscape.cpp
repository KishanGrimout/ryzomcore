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

using namespace std;
using namespace NLMISC;
using namespace NL3D;

static const char *DynLandscapeProgramName = "dyn_landscape";
static const char *DynLandscapeVertexProgramExt = ".vp1";
static const char *DynLandscapeFragmentProgramExt = ".arbfp1";

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
	
}

void CDynLandscape::readShaderFiles()
{
	CIFile VPFile(CPath::lookup(std::string(DynLandscapeProgramName) + std::string(DynLandscapeVertexProgramExt)), true);
	uint fileSize = VPFile.getFileSize();
	m_VPSource.resize(fileSize);
	VPFile.serialBuffer((uint8*)m_VPSource.data(), fileSize);

	CIFile PPFile(CPath::lookup(std::string(DynLandscapeProgramName) + std::string(DynLandscapeFragmentProgramExt)), true);
	fileSize = PPFile.getFileSize();
	m_PPSource.resize(fileSize);
	PPFile.serialBuffer((uint8*)m_PPSource.data(), fileSize);
}

void CDynLandscape::clear()
{
	clearGeneratedMeshes();
	CLandscape::clear();
}

void CDynLandscape::clearGeneratedMeshes()
{
	for (uint i = 0; i < LOD_COUNT; ++i)
	{
		m_regionsList[i].clear();
	}
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
Voxel::MaterialType g_discardMaterial = 0xFF;
bool g_clipBV = true;
void CDynLandscape::render(const CVector &refineCenter, const CVector &frontVector, const CPlane pyramid[NL3D_TESSBLOCK_NUM_CLIP_PLANE], bool doTileAddPass /*= false*/, bool doVegetables /*= true*/)
{
	(void)doVegetables;
	if (g_rebuildShaders)
	{
		bool succeed = false;
		while (!succeed)
		{
			deletePrograms();
			readShaderFiles();
			succeed = setupPrograms();
		}
		g_rebuildShaders = false;
	}

	_Driver->setupFog(_Driver->getFogStart(), _Driver->getFogEnd(), _Driver->getFogColor());

	if (g_useVP)
	{
		bool vpok = _Driver->activeVertexProgram(m_VP);
		nlassert(vpok);
	}
	if (g_usePP)
	{
		bool ppok = _Driver->activePixelProgram(m_PP);
		nlassert(ppok);
	}

	if (!g_usePP)
	{
		m_Mat.setTexCoordGen(0, true);
		m_Mat.setTexCoordGenMode(0, g_texCoordGenMode);
	}

	renderRegions(pyramid, NL3D_TESSBLOCK_NUM_CLIP_PLANE);
	renderTransitions(pyramid, NL3D_TESSBLOCK_NUM_CLIP_PLANE);

	// reset texture
	m_Mat.setTexture(0, NULL);
	m_Mat.setTexture(1, NULL);

	// deactivate program
	_Driver->activeVertexProgram(NULL);
	_Driver->activePixelProgram(NULL);
	_Driver->setupModelMatrix(CMatrix::Identity);

	if (g_renderLegacyLandscape)
		CLandscape::render(refineCenter, frontVector, pyramid, doTileAddPass);
	else
		unlockBuffers();

	renderSelectedVoxel();
}

void CDynLandscape::renderRegion(const CRegionMesh* mesh, const CMatrix& matrix)
{
	_Driver->setupModelMatrix(matrix);

	float scale = matrix.getScaleUniform();

	if (g_useVP && m_VP != NULL)
	{
		_Driver->setUniformMatrix(IDriver::VertexProgram, m_VP->getUniformIndex(CProgramIndex::ModelViewProjection), IDriver::ModelViewProjection, IDriver::Identity);
		_Driver->setUniform1f(IDriver::VertexProgram, 4, g_tighten);
		_Driver->setUniform1f(IDriver::VertexProgram, 5, scale);
		_Driver->setUniformFog(IDriver::VertexProgram, 10);
	}

	if (g_usePP && m_PP != NULL)
	{
		// xy: fog factor, z:blend factor
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

bool CDynLandscape::setupPrograms()
{
	m_VP = new CVertexProgram();
	// nelvp
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
		nldebug("3D: dynlandscape vertex program available");
	}

	m_PP = new CPixelProgram();
	// arbfp1
	{
		IProgram::CSource *source = new IProgram::CSource();
		source->Features.MaterialFlags = CProgramFeatures::TextureStages;
		source->Profile = IProgram::arbfp1;
		source->setSourcePtr(m_PPSource.c_str());
		m_PP->addSource(source);
	}
	// ps_2_0
		//{
		//	IProgram::CSource *source = new IProgram::CSource();
		//	source->Features.MaterialFlags = CProgramFeatures::TextureStages;
		//	source->Profile = IProgram::ps_2_0;
		//	source->setSourcePtr(a_ps_2_0);
		//	m_PP->addSource(source);
		//}
	if (!_Driver->compilePixelProgram(m_PP))
	{
		nlwarning("3D: No supported pixel program for dynlandscape");

		delete m_PP;
		m_PP = NULL;
		return false;
	}
	else
	{
		nldebug("3D: dynlandscape pixel program available");
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
