# Dynworld microvegetation on soft soil — plan for GitHub issue #7

Issue: <https://github.com/KishanGrimout/ryzomcore/issues/7> — "Ryzom has a
microvegetation system that would need to be reused to display it randomly. It
doesn't need to be stored inside the voxel data. It should just be used on top
of soft soil."

Status: **plan written 2026-11-10** (no implementation yet). Work goes on a
feature branch cut from `dev/dyn` (see `AGENTS.md` § Work methodology).
Integration roadmap: `docs/LEGACY_DYNWORLD_INTEGRATION.md`.

## Reference documentation

- `docs/DynClientRuntime.md` — client dynworld init/runtime (session routing,
  material library failure modes, `dyn_*` command catalog). The new feature
  extends the client landscape stack documented there.
- `docs/VoxelTerrainCollision.md` — the voxel terrain system doc (mesh
  extraction semantics in §8.1 are directly relevant: what per-vertex data the
  marching-cubes mesh carries).
- `docs/TestEnvironmentNotes.md` — validation environment quirks referenced by
  phase validations.
- `docs/LegacyAssetInventory.md` — legacy tree survey (data locations).

## 1. Audit findings (2026-11-10, verified — do not re-derive)

### 1.1 The stock microvegetation system (NeL 3D)

- Core files: `nel/src/3d/vegetable*.cpp`, `tile_vegetable_desc.cpp`,
  `landscape_vegetable_block.cpp`, `patch_vegetable.cpp` (headers in
  `nel/include/nel/3d/`). `CVegetableManager` is created and owned by
  `CLandscape` (`landscape.cpp:230`); blend-layer models are created by
  `CLandscapeModel::initModel` via
  `createVegetableBlendLayersModels(scene)`.
- Planting stock happens in `CPatch::generateTileVegetable()`
  (`patch_vegetable.cpp:45-265`): per tile → vegetable list from
  `CTileBank::getTileVegetableDesc(tileId)` → per-DistType
  `CVegetable::generateGroupBiLinear(pos, borderPos[4], normal, area, seed,
  instances)` → 2-step reserve protocol
  (`reserveIgAddInstances` → `reserveIgCompile`) → per instance
  `CVegetable::generateInstance(ig, matrix, ambient, diffuse, blendDistMax,
  waterState, dlmUV)` (which calls `CVegetableManager::addInstance`).
- `CLandscapeVegetableBlock` (`landscape_vegetable_block.cpp`) is the per-area
  lifecycle: 5 distance rings (10/20/30/40/50 m, `vegetable_def.h`), IGs
  created/deleted incrementally as the camera crosses ring boundaries.
- Rendering: `CVegetableManager::render(viewCenter, frontVector, pyramid,
  textureDLM, driver)` during the opaque pass; stock
  `CLandscapeModel::traverseRender` delays into `renderTRav`, then
  `CLandscape::render(..., doVegetables)` calls the manager. Per-frame time
  (`setVegetableTime`), sun (`setupVegetableLighting`), wind (`setWind`) and
  async relighting (`setUpdateLightingFrequency`) are all scene-driven through
  the landscape model.
- Instance generation API notes: `addInstance` is **main-thread only** (VB
  allocator); `lockBuffers()`/`unlockBuffers()` must enclose add/delete;
  `updateDriver(driver)` must precede them; `generateGroup(BiLinear)` uses
  `OptFastFloor` — wrap in `OptFastFloorBegin()/End()`. This tree additionally
  has the triple-buffering additions `setUnsynchronizedMode()` /
  `processDeferredFrees(swapBufferInFlight)` on the manager + VB allocator
  (stock calls the latter inside `CLandscape::updateGlobalsAndLockBuffers`,
  `landscape.cpp:889`).

### 1.2 What the dynworld path already has wired (and what it lacks)

- The dyn landscape embeds a real `CLandscape` (`CDynLandscape : CLandscape`,
  U-N4 chain), so a `CVegetableManager` already exists in dyn sessions.
- `CDynLandscapeModel::initModel` already creates the blend-layer models
  (`dyn_landscape_model.cpp:61`) and forwards time/relighting/sun
  (`setVegetableTime`/`setVegetableUpdateLightingTime` :199-203,
  `setupVegetableLighting` :215-217).
- `CDynLandscapeUser` forwards the whole `ULandscape` vegetable API to the
  embedded `CLandscape` (`dyn_landscape_user.cpp:1323-1354`, `:1393-1396`,
  `:1417-1420`), and the tile vegetable descs are already loaded/registered
  (`loadTileVegetableDescs` + `initTileVegetableDescs` at
  `dyn_landscape_user.cpp:261-262`; season postfix in
  `dyn_continent.cpp:394-396`; microveg texture per season
  `dyn_continent.cpp:215-218`).
- The generic client cfg knobs already work: `continent_manager.cpp:244-260`
  calls `Landscape->enableVegetable(true)`, `setVegetableWind(...)`,
  `setUpdateLightingFrequency(1/20)`, `setVegetableDensity(MicroVegetDensity/100)`
  when `ClientCfg.MicroVeget` — this path runs for the dyn landscape too.
- **What is missing** is exactly two things: (a) nobody *plants* — stock
  planting requires `CPatch`/TessBlock/tile infrastructure the dyn landscape
  never populates; (b) `CDynLandscape::render` discards the vegetable flag
  (`dyn_landscape.cpp:339-341`: `(void)doVegetables;`) and never calls the
  manager's render.
- The legacy dynworld fork did NOT integrate microvegetation either —
  `dynworldpatch.diff` only carries pass-through/comment churn of the existing
  API. This feature is net-new code.

### 1.3 Surface data available client-side for planting

- Visual meshes: per LOD sector (LOD0 = 32 m/64 voxels, LOD1 = 64 m, LOD2 =
  128 m) marching-cubes meshes extracted in `CGenerateMeshCommand::generateMesh`
  (`dyn_landscape_user.cpp:97-197`, worker-side deferred command) and
  decoded into `CRegionMesh`. **`TDecodedVertex::data` carries the full 32-bit
  `Voxel` (material 8-bit + biome 3-bit + density) per corner**
  (`polyvox_defs.h:83-85`), and `CRegionMesh::initMesh` already buckets each
  triangle into a (biome, material) submesh (`region_mesh.cpp:129-229`).
  Positions/normals are the smooth marching-cubes surface values.
- Mesh lifecycle hooks: a mesh "becomes visible" in
  `CDynLandscapeUser::updateGenerateMeshesCommands()` (`:1088`, commit at
  :1096-1116); a voxel edit re-extracts via `voxelTouched` → `updateMesh`
  (`:1517`, `:482`); sector removal goes through `removeMeshes` (`:446`).
- Voxel queries without the mesh (fallback): `CMapBase::getGroundNormal(sector,
  estimated, outNormal, voxelOut, searchMax)` returns the outward normal AND
  the ground `Voxel` in one call (`map.h:118`, `map.cpp:285`); `castRay` with
  collectors gives hit voxel + position (`map.h:122`).

### 1.4 "Soft soil" definition

There is no explicit soft/hard flag anywhere. Mapgen fills the soil band
between `m_softFloorZMap` and `m_hardFloorZMap` with the environment's default
soil material (`mapgen.cpp:634-649`, `envDef->getDefaultSoilId()`; e.g. Forest
→ `Humus`). Therefore: **a ground voxel is "soft soil" when it is `Raw` and its
material equals the environment's default soil material** (recoverable
client-side via `CMaterialLibrary::getEnvironmentDef(biome)->getDefaultSoilId()`).
Alternative filter: a per-material allow-list in the material `.cfg` files.
Decision D2 below.

## 2. Design direction

**Reuse the stock `CVegetableManager` stack end-to-end; only replace the
planting source and the render call.** Planting is client-side only — no voxel
data changes, no message chain, no server work. Placement is *derived* from
already-extracted visual meshes, keyed by the same (biome, material) data that
drives texture binding.

### Core pipeline (v1)

1. **Placement computation (worker, per mesh sector)** — inside
   `CGenerateMeshCommand`, after `extractMesh`/decode, walk the decoded mesh
   triangles: for each triangle whose submesh (biome, material) passes the
   soft-soil filter (D2), sample instance positions on the triangle with
   `CVegetable::generateGroup(posInWorld, triangleNormal, triangleArea, seed,
   instances)` — per DistType list from the tile-vegetable desc (D1). Seed is
   deterministic: hash of (sectorId, submeshId, distType) so re-plants after
   edits do not visibly reshuffle. Output: a pure-data placement list
   `(position, normal, biome, material, distType)` stored alongside the
   `CRegionMesh`. No NL3D manager calls on the worker.
2. **Vegetable blocks (client, per rendered sector)** — a small
   `CDynVegetableBlock` per (sector, lod): owns one `CVegetableClipBlock`
   (created from the manager) plus per-ring `CVegetableInstanceGroup`s.
   Creation/deletion of IGs follows the mesh lifecycle:
   - mesh becomes visible (`updateGenerateMeshesCommands` commit) → commit
     placements: `updateDriver(driver)` → `lockBuffers()` →
     `createClipBlock/createSortBlock/createIg` + `reserveIgAddInstances` →
     `reserveIgCompile` → `CVegetable::generateInstance` per placement →
     `unlockBuffers()` (wrapped with `OptFastFloorBegin/End`);
   - mesh replaced (edit or LOD transition) → delete the block's IGs (inside
     the same lock window) and re-commit from the new mesh;
   - sector removed (`removeMeshes`) → delete all IGs + clip block.
   Only LOD0/LOD1 meshes host vegetables; LOD2 (far 350 m box) never plants.
3. **Render** — `CDynLandscape::render` honors `doVegetables` and calls
   `_VegetableManager->render(refineCenter, frontVector, pyramid, nullptr,
   driver)` with the same tessblock pyramid, gated on the existing
   `_VegetableActive` (fed by `enableVegetable`/`FilterVegetable`).
   `textureDLM = nullptr` (no dynamic lightmap in dyn); lighting is ambient =
   sun ambient / diffuse = modulated per placement (cheap `normal·sun`
   shade instead of the stock lumelmap), no point lights and no DLM UV in v1.
4. **Ring policy** — stock 5 DistType rings (10..50 m) map onto per-sector
   blocks: v1 plants rings by camera distance to the sector center with
   incremental IG create/delete on ring crossings (mini
   `CLandscapeVegetableBlock::update`); a simpler "plant rings 0..N immediately,
   no switching" variant is the fallback if the incremental switching is not
   needed at 32/64 m sector granularity (D4).

### Alternatives considered (kept as fallbacks)

- **B — column-grid voxel sampling** (no mesh dependency): grid columns over
  the sector, `getGroundNormal(..., voxelOut)` per column, plant at the ground
  point. Pros: independent of mesh/LOD; cons: 0.5 m voxel-quantized surface
  (floats/buries relative to the smooth MC mesh), new sector walks competing
  with the volume read lock. Preferred only if mesh-triangle planting proves
  too costly (per-triangle overhead at 64³ sectors).
- **Stock tile desc vs environment cfg lists (D1)**: primary = reuse
  `CTileVegetableDesc` via the tile ids already resolved by
  `bindRegionMeshTexture` — zero new data pipeline, season postfix already
  works, per-material densities/angle rules for free. Fallback = new
  `MicroVegetables` list in environment `.cfg` (name → `.veget` shape +
  density), loaded by `CEnvironmentDefinition` — needed only if the sandbox
  client data ships no usable tile vegetable descs.

## 3. Open decisions

- **D1 — vegetable data source** (P0 gate): stock `CTileVegetableDesc` from the
  tile bank vs environment-cfg lists vs programmatic `CVegetableShapeBuild`
  shapes as emergency fallback. Recommendation: stock desc — **data confirmed
  present 2026-11-10**: the sandbox data dir (already in `client.cfg`
  `PreDataPath`, `D:/Programmation/ryzomcore_old/ryzom_core_client/data`)
  contains `jungle_vegetables.bnp` (35 `.veget` shapes, `Ju_*`) and
  `jungle_vegetable_sets.bnp` (50 `.vegetset` per-tileset descriptors incl.
  `Jungle_ForetSol.vegetset` + all four season variants — verified by BNP
  header parse; the client auto-mounts `.bnp`s from its data paths into
  `CPath`). Remaining P0 item is only the in-engine probe. Unpacked copies of
  all ecosystems (`desert_*`, `lacustre_*`, `primes_racines_*`,
  `jungle_*`) exist under `pipeline/install` should a per-biome data route ever
  be needed.
- **D2 — soft-soil filter** (P0/P1): (a) material == environment default soil
  id (derives from mapgen semantics, zero new data), or (b) explicit per-material
  flag in material `.cfg` (e.g. `MicroVeget = 1;`), more designer-controllable,
  costs a `CMaterialDefinition` field. Recommendation: (a) for v1, add (b) as
  an override later if art wants control.
- **D3 — per-triangle planting granularity**: triangle area (~voxel scale) is
  much smaller than the stock 16 m² tile; per-triangle `generateGroup` calls
  may over-run CPU or produce visible repetition patterns. If so, cluster
  triangles of a submesh into spatial buckets (e.g. 2 m cells) and plant per
  bucket. Decide with P1 numbers.
- **D4 — ring switching**: incremental per-ring IG lifecycle vs plant-all-rings
  per sector immediately (IGs beyond ring distance are clipped away cheaply by
  the pyramid; the cost is VB memory only). Decide with P4 measurements.

## 4. Phases

### P0 — Feasibility gate (no feature code)

- [x] Data availability (2026-11-10): the tile-bank vegetable data ships inside
      `jungle_vegetables.bnp` / `jungle_vegetable_sets.bnp` in the sandbox
      client data dir already listed in `PreDataPath` — `.veget`/`.vegetset`
      resolve via CPath's BNP auto-mounting without cfg changes. Recorded in
      D1 above. Other ecosystems: `desert_*`/`lacustre_*` BNPs exist in the
      same dir; `primes_racines_*` does NOT (unpacked copies under
      `pipeline/install/primes_racines_*` would need adding if/when the
      Roots biome needs its own bank).
- [x] Registration-order audit (2026-11-10): `CContinentManager::load()`
      enables vegetables on the (still empty) bank; the later
      `CDynContinent::select` → `flushTiles` loads the bank AND calls
      `initTileBanks()`, which itself calls
      `TileBank.initTileVegetableDescs(_VegetableManager)`
      (`landscape.cpp:2889`) — so vegetable shapes ARE registered after the
      bank loads, provided `ClientCfg.MicroVeget` was on at `load()` time.
      Probe `dyn_vegetdump` added (commands.cpp) to verify in-engine.
- [x] In a dyn client session, verify the stock desc load actually succeeds
      for the dyn-bound materials (2026-11-10 23:01:07, client.log one-shot
      DYNVEGET dump at `flushTiles`): 14 tilesets loaded; the dumped
      desc shape lists all resolve through the manager cache — every
      `Ju_*` shape appears as `...veget[ok]` — and the material→tileset
      bindings are OK for the ones the dyn map actually binds (e.g.
      `Humus env=0/1 -> tile 1573, vegetDesc OK`). Some materials
      (env=2/env=3 Wood/Bark/SawDust/...) bind tiles outside the loaded
      jungle bank → `EMPTY/MISSING`; benign until those biomes are used.
      Command caveat: **`dyn_vegettest` needs the full shape name with the
      `.veget` extension** — a `Ju_FleurA` (bare) run logged
      `getVegetableShape could not load shape file 'Ju_FleurA'` and
      early-returned without planting (23:05:19).
- [x] Verify stock vegetable rendering works under the GL3 driver in this
      tree (the vegetable VPs go through the standard NL3D VP path, not the
      dyn GLSL): repaint `dyn_renderveget` + `dyn_vegettest` in a dyn session
      and check for grass. **VALIDATED 2026-11-11 (two fixes + final run
      10:27:34):** (1) crash fix — `CDynLandscape::render` must release the
      VBs before the manager render (stock does it inside
      `CLandscape::render`, landscape.cpp:1231; the manager's `updateDriver`
      asserts `!isLocked`); (2) zero-draw fix — the probe (and any external
      block owner) must call `CVegetableSortBlock::updateSortBlock()` after
      planting (stock does it from `CLandscapeVegetableBlock::update()`
      after IG add/delete batches; `generateInstance` only marks
      `_Dirty`). Final stats: `facesRendered=240 ... visibleClipBlocks=1
      zsortRegistered=1 zsortFacesLast=240 zsortVBVerts=390` with a visible
      ~30-instance patch of `Ju_FleurA` at the player feet. The vegetable
      VP path (standard NL3D vertex programs, alpha-blend ZSort pass through
      the blend-layer models) works under the GL3 driver in dyn sessions;
      no GLSL migration prerequisite. P0 gate passed.
- [x] Zero-draw investigation (2026-11-11): with the reorder landed, faces
      stayed 0. First instrumented run (10:13:52):
      `visibleClipBlocks=1 zsortRegistered=0 zsortFacesLast=0` — the clip
      test passes but the manager registers nothing in the ZSort pass.
      Root cause: **the probe never calls `updateSortBlock`**. Stock keeps
      sort blocks fresh only from `CLandscapeVegetableBlock::update()`
      (landscape_vegetable_block.cpp:189/208) after IG add/delete batches;
      `generateInstance` merely marks the sortBlock `_Dirty`, and the
      manager's ZSort loop requires `sortBlock->_NTriangles != 0`
      (vegetable_manager.cpp:2269), a value computed only inside
      `CVegetableSortBlock::updateSortBlock()`. Fix: new public
      `CVegetableManager::updateSortBlockForIg(ig)` helper
      (`_SortOwner` is private) called by the probe after planting — the
      P2 `CDynVegetableBlock` commit path must do the same after every IG
      create/delete batch. Probe extended with `zsortVBVerts` (summed ZSort
      VB-allocator vertices, the outer ZSort gate) so a future zero stays
      attributable. **Confirmed fixed 2026-11-11 10:27:34:**
      `zsortRegistered=1 zsortFacesLast=240 zsortVBVerts=390` and the patch
      is visible in-engine.
- [x] Write down the decisions D1–D3 (status quo defaults if data is absent).
      DECIDED 2026-11-11 (P0 data confirmed): **D1 = stock `CTileVegetableDesc`**
      from the tile bank (data present and resolving in-engine, D1 audit);
      **D2 = filter (a)** — a ground voxel is plantable when `Raw` and its
      material equals `CMaterialLibrary::getEnvironmentDef(biome)
      ->getDefaultSoilId()`; no per-material `.cfg` flag in v1 (add later only
      if art wants control); **D3 = per-triangle `generateGroup` first**
      (measure with P1 numbers, cluster into spatial buckets only if it proves
      too costly/repetitive); **D4** stays open by design — decided with P4
      measurements.

Validation: notes appended to the new system doc (§6).

### P1 — Placement engine (pure data)

- [x] New module (2026-11-11):
      `ryzom/common/src/game_share/dynworld/vegetation_placement.{h,cpp}`
      (goes into `Ryzom::gameshare` — refinement over the plan's
      client/src/dynworld location: the placement engine is pure data with no
      client/NL3D dependency, so the established dynworld convention of
      shared dyn code in game_share applies and the unit test needs no client
      source). Contains:
      - `buildSoftSoilTable` (D2 filter (a)): per-biome table of the
        environment default-soil material ids from `CMaterialLibrary`
        (`VegetNoSoil=0xFF` disables a biome). MAIN THREAD ONLY (reads the
        immutable-after-init material library); worker commands capture it
        in their constructor.
      - `computeVegetPlacements`: walks the decoded mesh; a triangle is
        plantable iff **all three corners** share the same (biome, material)
        pair, are `Raw`, and the material equals the biome's soil entry
        (all-3-corners rule: mixed soil/rock/water boundary facets host no
        vegetation and can never be double-planted, which the per-corner
        CRegionSubMesh splitting would otherwise cause). Plantable triangles
        are clustered into **2 m XY cells** (`VegetBucketCellSize`) per
        (biome, material): bucket = { quantized cell center (noise seed),
        area-weighted normal, total area, member triangle indices + areas,
        mesh-index order }. Output ordering fully deterministic
        (std::map keyed groups/cells; plain `floorf` — never OptFastFloor on
        the worker, it fiddles a process-global FPU control word).
      - `vegetBucketResolvePosition`: commit-time UV→world mapping —
        deterministic hash of the generateGroup UV pair → area-CDF triangle
        pick, folded barycentric position inside it, face normal out.
        Design refinement: `CVegetable::generateGroup` itself runs at COMMIT
        time (P2, main thread), not on the worker — it reads
        `_Manager->getGlobalDensity()` and needs the OptFastFloor window,
        which would break the "no NL3D calls on the worker" invariant. The
        worker produces geometry only; commit calls generateGroup per bucket
        (Center, Normal, Area) exactly like stock calls it per tile.
- [x] Hook into mesh extraction (2026-11-11):
      `CMapClient::extractMesh` grew an optional `TDecodedMesh*` out-param;
      the placement set is computed right after extraction and stored on
      `CRegionMesh::m_vegetPlacements` — so it follows the mesh lifecycle
      automatically (recomputed on every re-extraction, freed with the
      mesh). Both generation paths wired:
      `CGenerateMeshCommand::generateMesh` (worker; soil table captured in
      the command ctor on the main thread) and
      `CDynLandscapeUser::generateMesh` (main-thread sync path). Same
      world transform as `CDynLandscape::renderRegions`
      (`sectorOrigin = idPos * sideSize`, `scale = voxelSize(lod) * 2^lod` —
      the decoded mesh carries sector-local resampled-voxel units). LOD2
      never plants (plan).
- [x] Unit test `ryzom_dynworld_vegetation_test`
      (`game_share_test/test_dynworld_vegetation.cpp`, links `NeL::3d` +
      `Ryzom::gameshare`, per plan note): 11 tests, all green in Release
      (2026-11-11) — empty mesh, soil-surface clustering (16 m² → 4 buckets,
      cell-quantized centers, up normals, triangle/area invariants),
      non-soil/no-soil-biome exclusion, **no instances on Liquid/Object
      tops** (mixed surface → only the soil group), mixed-material facet
      exclusion, bit-exact determinism, origin/scale transform, UV mapping
      lands on a member facet with its face normal + determinism, and
      **generateGroup instance-count scaling** (D3 numbers): 16 m² at
      `Density(1, 0 noise)` → exactly 16 instances, doubling density → 32 —
      validates the bucket-as-16m²-tile-analogue design. Whole dynworld exe
      set green (13 suites, 76 tests). Environment note: the test exe needs
      `freetype.dll` on PATH (pulled by `NeL::3d`) — used
      `redist\nel_tools_win_x64` (same loader-failure class as
      `docs/TestEnvironmentNotes.md` §1).

Validation: unit tests green in Release. **P1 DONE 2026-11-11.**

### P2 — Vegetable blocks + main-thread commit

- [x] `CDynVegetableBlock` per (sector, lod) (2026-11-11, `dyn_landscape.h/.cpp`):
      stock clip block per sector + one sort block and per-DistType instance
      groups per plantable **bucket** (the 2 m clustering cell of the placement
      engine — the analog of the stock 2x2 m tile; stock scale: one vegetable
      block per 4 m tessblock with 4 sort blocks, ours: one block per 32/64 m
      sector with up to ~256 sort blocks — only for buckets whose group's tile
      vegetable desc is non-empty). Ring-band logic mirrors stock
      `CLandscapeVegetableBlock::update` (squared-distance LUT band walk over
      `(i·10 m)²`, receding deletes IGs `[cur,new)`, approaching creates
      `[new,cur)`), but applied **per bucket entry** so the per-frame budget
      can starve individual entries without breaking committed state (an
      entry's band change is all-or-nothing; left untouched and retried next
      frame when out of budget). Planting mirrors
      `CPatch::generateTileVegetable` per bucket: `getVegetableList(distType)`
      + `getVegetableSeed`, `CVegetable::generateGroup` (bucket Center as the
      noise seed, bucket Area as the density driver — the D3 clustering
      contract), `vegetBucketResolvePosition` for the UV→facet landing
      (surface-aligned I/J/K basis), empty IGs freed immediately (stock rule),
      `updateSortBlock` after each batch, `blendDistMax=(distType+1)·10 m`,
      lighting v1 = white ambient + diffuse shade `0.35+0.65·max(0,normal.z)`,
      water state `AboveWater`, LOD0/LOD1 only. Commit path respects the
      stock lock discipline (`updateDriver → lockBuffers → ops →
      unlockBuffers`, `OptFastFloorBegin/End` around `generateGroup`;
      triple-buffer `processDeferredFrees` happens inside the manager's
      `lockBuffers`). **P1 refinement required by this**: buckets now carry
      world-space triangle corners (`CVegetBucket::TriCorners`) — the commit
      phase runs after the decoded mesh is destroyed (only `CRegionMesh`
      survives extraction), so `vegetBucketResolvePosition` no longer needs
      the mesh/origin/scale context; placement test updated and green.
- [x] Wire the mesh lifecycle (2026-11-11): per-frame structural sync
      `CDynLandscape::updateVegetableBlocks(viewCenter)` called from
      `CDynLandscapeUser::refreshLodSectors` (right after
      `updateGenerateMeshesCommands`), keyed by (lod, sectorId) against
      `m_regionsList`: create on fully visible meshes (`displayFactor ≥ 1`,
      placement set non-empty; blocks with zero plantable entries are not
      kept, to avoid erase/recreate churn), release when the sector left the
      list or the mesh pointer changed (re-extraction = new placement data),
      release-all in `clearGeneratedMeshes`/`clear`/destructor (before the
      base dtor destroys `_VegetableManager`). Budget knob
      `g_maxVegetCommit` (default 256 IG commits/frame; counts each IG
      creation) with the `dyn_vegetbudget <n>` command; blocks/entries beyond
      the budget simply stay pending for later frames. Tile→desc resolution
      at commit: `(biome, material)` →
      `CMaterialLibrary::getMaterialDef(material)->getTextureSet(biome)
      ->m_textures[0].m_variations[0]` (tile id, same lookup
      `bindRegionMeshTexture` uses) → `TileBank.getTileVegetableDesc(tileId)`;
      empty descs/unresolvable materials are skipped.
      Validation: stats-only in-engine check pending (`dyn_vegetstats` now
      also reports `vegetBlocks`/`vegetEntries`/`vegetBlockIgs`); client
      build green, whole dynworld test set green (13 suites).

**Validation 2026-11-11 (in-engine, PASS — P2 DONE):** session stats
      `vegetBlocks=209 vegetEntries=22660 vegetBlockIgs=335 tris=5746
      verts=10711` with render still off (only the ≤50 m band entries hold
      IGs — the band logic keeps the 22k far entries dormant), then after
      `.dyn_renderveget`: `facesRendered=3780 visibleClipBlocks=14
      zsortRegistered=205 zsortVBVerts=10847`, vegetation visibly rendered.
      Consistency checks: `clipBlocks+emptyClipBlocks == vegetBlocks`
      (27+182=209, then 28+220=248 — the manager moves empty clip blocks to
      `_EmptyClipBlockList`, so the probe's split lists reconcile exactly);
      `igs == vegetBlockIgs`; no vegetable-related warnings or asserts in
      `client.log`. Note for P4/P5: 209 blocks/22.7k entries are created for
      every visible plantable sector even far beyond the 50 m band (cheap
      dormant structures: one clip block + up to ~100 sort blocks each);
      if profiling shows this matters, lazy sort-block creation for far
      blocks is the candidate fix (D4 territory).

Validation: build + client session (no render call yet — count instances via
debug log/`dyn_vegetstats`).

### P3 — Render integration (first in-engine result)

- [x] `CDynLandscape::render` honors `doVegetables` + `_VegetableActive` +
      `FilterVegetable`, calls the manager render with the tessblock pyramid
      and `textureDLM = nullptr`; fog behavior checked (stock fog setup is
      inside the manager). (2026-11-13, user sign-off: "except for P5, I
      think everything is OK".)
- [x] Milestone **M1**: in a client session, random microvegetation visibly
      covers soft-soil surfaces (grass on Humus forest floor), none on
      non-soil materials, none under water where water tops the ground.
      (Validated in-engine, user sign-off 2026-11-13.)

Validation: in-engine, screenshots recorded in the system doc.

### P4 — Rings, edits, transitions

- [x] Ring policy per D4 (incremental switching or plant-all), measured.
      (Shipped: incremental switching with near-first ordering + §6.7/§6.8/§6.9
      budget pools; user sign-off 2026-11-13.)
- [x] Edit sync: `dyn_setvoxel`/`dyn_addvoxel` edits re-extract the mesh and
      the vegetation re-plants deterministically (no reshuffle) and stays
      consistent with the new surface (plants removed where voxels buried
      them, appear on new soil). (User: "planting is deterministic", validated
      in-engine 2026-11-13; blink fixed by the §6.9 near budget.)
- [x] LOD transitions: no popping regression from the existing
      `CSectorTransition` factor animation (vegetation is planted from the
      target mesh only, IG count transitioned or cut per D4).

Validation: in-engine edit + transition session walk; frame-time budget
recorded.

### P5 — Config, debug commands, performance sign-off

- [x] Reuse `ClientCfg.MicroVeget`/`MicroVegetDensity` (already partially
      wired through `continent_manager.cpp`); live-toggle path
      (`main_loop_utilities.cpp:167-196`) verified for the dyn landscape.
      (2026-11-13: both commands forward through `CDynLandscapeUser` unchanged.)
- [x] Debug commands in the `dyn_*` block (`commands.cpp:5338+`):
      `dyn_veget` (on/off), `dyn_vegetdensity <pct>`, `dyn_vegetstats`
      (per-lod instances/faces/IGs, `getNumVegetableFaceRendered`).
      (2026-11-13: `dyn_veget`/`dyn_vegetdensity` added via
      `CDynLandscape::setVegetableActive` + `CDynLandscapeUser::
      isVegetableActive` wrapper; §6.11.)
- [x] Budget sign-off: VB allocator limits (stock
      `NL3D_LANDSCAPE_VEGETABLE_MAX_AGP_VERTEX_*` sized for the 50 m band),
      worker cost of placement, main-thread commit cost. Adjust ring policy
      (D4) and density defaults if needed. (2026-11-13, §6.11: measured
      ~8k AGP verts vs Unlit quota 50000 — ~6x headroom, no density change.)
- [x] Create the **system doc** `docs/DynMicroVegetation.md` (CamelCase): what
      the system is, its data flow, invariants (lock discipline, seed
      determinism, soft-soil rule), the instrumentation/debug registry, and
      the P0–P5 findings; add it to the `AGENTS.md` ToC. Update the roadmap's
      plan-doc list to point here with status. (Done 2026-11-13.)

Validation: full dynworld exe set green (13 suites, 2026-11-13); Release
client build; session walk (user sign-off: everything validated except P5
deliverables, which are now complete).

## 5. Risks / notes

- **GL3 vegetable VP** (P0 gate): vegetables use their own vertex programs; if
  the GL3 driver path fails in a dyn session, this becomes a GLSL migration
  prerequisite — do not paper over it.
- **Data location (resolved 2026-11-10)**: `.veget`/`.vegetset` data ships
  inside BNPs in the sandbox data dir already on `PreDataPath`; no cfg changes
  needed (see D1). Caveat noted there: the loaded tile bank is the jungle one
  (dyn continent reuses the newbieland sheet), so planted flora follows the
  tileset of the bound material tile — forest-floor materials get
  `Jungle_ForetSol`'s rich set; other biomes may inherit jungle-flavored lists
  unless the environment-cfg route (D1 fallback) is taken later.
- **Main-thread commit cost**: stock planting is incremental per ring; our
  sector commit can spike (a 64 m LOD1 sector ≈ 16 stock tiles). Cap per-frame
  commits and rely on the deferred-command pattern already used for meshes.
- **Determinism**: same voxel data + deterministic seeds ⇒ two clients render
  the same vegetation without any sync; no protocol work exists or is needed.
- **`Object`-voxel flora is a different system**: mapgen-planted trees/bushes
  are `Voxel::Object` sheet ids rendered via `CDynInstanceGroupUser`
  (`dyn_instance_group_user.cpp:113-162`); microvegetation is a pure client
  visual on top and must not collide with that pipeline (soft-soil filter keys
  on `Raw` soil materials only).

## 6. Close-pop investigation (issue #7 follow-up, started 2026-11-12)

Symptom report: microvegetation pops near the player when the camera is put
close to the ground; toggling `/dyn_refreshpos` stops it. (Reference: that
command toggles `g_refresh`, which gates the whole
`CDynLandscapeUser::refreshZonesAround` → `refreshLodSectors` path — and
`CDynLandscape::updateVegetableBlocks` is the only vegetation-driving call
inside it, `dyn_landscape_user.cpp:828`. So the pops are proven to come from
the per-frame band/block updates, not from the render pass.)

### 6.1 Correction of the record (previous session summary is void)

A prior session's closing summary described a fix path built on identifiers
that **do not exist in this tree** — `VegetationEventProbe`, `PopLogProbe`,
`PlantGridSideLand.json`, `FreshActionCache`, `PendingVegetationList`,
`PerFrameApplyTag`, `MosaicTileKey`, `applyPopPendingApply`, `TActionPopTask`,
`CDynVegetableAction::updateVegetableActionLoop`, `CoarseUpdate`,
`PlantGridEdgeBudget` (verified by tree-wide grep 2026-11-12). Do **not**
implement that path; it was confabulated. The real pop-relevant machinery is
only: `CDynVegetableBlock` (per (sector, lod) block: 1 clip block + 1 sort
block + per-band IGs per 2 m bucket entry), its stock-mirroring band walk in
`CDynVegetableBlock::update` (`dyn_landscape.cpp:513`), and the per-frame IG
commit budget `g_maxVegetCommit` (256, `dyn_vegetbudget`). The existing
TEMP-PROBE `nlinfo` lines (`probe band cell=…`, `probe block
create/erase center=…`) are from that investigation and stay until it closes.

### 6.2 Ground truth: how the stock band system masks its own swaps

Stock `CLandscapeVegetableBlock::update`
(`nel/src/3d/landscape_vegetable_block.cpp:149`) invariant (re-derived
2026-11-12): an entry holds IGs for **all bands `[CurDistType, NUMDIST)`** —
approach (`new < cur`) *creates* `[new, cur)` without deleting anything;
recede (`new > cur`) *deletes* `[cur, new)`. Combined with the per-instance
ZSort-pass alpha computed per frame in the vegetable VP
(`vegetable_manager.cpp:1494`: `alpha = (blendDistMax - dist) /
NL3D_VEGETABLE_BLOCK_BLEND_TRANSITION_DIST`, with
`blendDistMax = (distType+1)·10 m`, transition width 10 m), every create and
delete lands where the affected IG's alpha ≈ 0:

- create happens only when *entering* a nearer band → the new band's IG
  starts at the band's outer edge, alpha ≈ 0, and fades in across the whole
  10 m band width while approaching;
- delete happens only when *leaving* a band at its outer edge, where that
  IG's alpha has already faded to ≈ 0.

So band-boundary swaps are invisible **by construction** — provided the
accumulation invariant holds every frame. Our port replicates create/delete
directions, `blendDistMax`, the LUT walk and the sort-block refresh exactly.

Data note from the 2026-11-12 20:56 dump: **band 0 (0–10 m) lists are empty
for all 14 loaded tilesets** (the dump skips empty lists, hence zero
`distType=0` lines). Benign for masking (empty band ⇒ no-op create/delete,
other bands keep rendering: a band-1 IG stays full-alpha at 5 m), but it
means nothing new is planted inside 10 m of the camera.

### 6.3 Log analysis of the 2026-11-12 20:56 session (`client.log`)

- 3350 band transitions in ~34 s over 909 unique cells, all within ~80×72 m
  around the player area; approach=1359 / recede=1991. Mix:
  `5→4` 671, `2↔3` 1050, `3↔4` 899, `5→3` 337, `4↔5`, `5→2` 84.
- **Zero transitions involving bands 0/1** ⇒ the camera was ≥ 20 m from every
  cell the whole capture — the "camera close to the ground" repro is *not* in
  this window (or the camera never went low). Cells churn only at the 20–50 m
  rings.
- **Zero block erases, 116 block creates** spread through the whole session
  (31 at start, then a trickle): LOD transitions near the camera keep
  creating new blocks mid-walk — each first update of a new block plants its
  entries 5→current-band **in one step**, i.e. all bands `[cur, 5)` appear at
  whatever their current alpha is (mid/far bands at **full alpha** — the
  fade-in they would have had was skipped). Candidate pop source #1.
- Budget starvation is currently **silent** (the `needed > budget` branch
  just `continue`s) — unknown whether it fires. Candidate pop source #2:
  a deferred approach-create commits later, when the camera is already
  deeper in the band ⇒ the fade-in starts mid-band at partial alpha = visible
  pop-in.
- Candidate pop source #3: camera moves ≥ 10 m in one frame (dragging the
  camera down fast, teleports) produce multi-band jumps `5→2/3/4` (present in
  the log); the created mid bands then start at their *current* alpha, not at
  ≈ 0.

### 6.4 Probes added 2026-11-12 (same TEMP-PROBE class, strip together)

- `DYNVEGET: probe budget starve cell=… need=%d budget=%d` — in
  `CDynVegetableBlock::update`'s deferred-approach branch (source #2).
- `DYNVEGET: probe cam pos=…` every 30 frames in `updateVegetableBlocks` —
  correlates churn with camera position/height (source: the bands-0/1
  question). Rebuilt `ryzom_client_dev_r.exe` (build green).

### 6.5 In-engine repro 2026-11-12 21:10 (probes live) → ROOT CAUSE FOUND

Session with the two added probes (camera low → lift → low; ground at
z≈24.5, camera z 26.9↔30.8). Rotated logs `client000.log` (4217 band events,
202 block creates) + `client001.log` (875 band events, 11 block creates):

- **0 budget starves, 0 block erases** — sources #2/#3 from §6.4 eliminated;
  the deferred-approach and erase/recreate hypotheses are dead.
- The lift (z 26.7→30.8, one second) fired 2→3×84, 3→4×34, 4→5×16 recedes
  plus a 5→2/5→3/5→4 create wave; descending back inverted it. 875
  transitions with a *nearly static camera* (xy fixed, z bobbing 4 m) ⇒ the
  classification distance is hypersensitive to camera z.
- **Every logged cell z printed 0.00** across 909+ cells while the terrain
  surface is at z≈24.5.

Root cause: `CVegetBucket::Center` is the *quantized 2 m cell center with
z = 0* (`vegetation_placement.h` — designed as the generateGroup noise seed),
but `CDynVegetableBlock::update` used it as the **band-distance reference
point**. With terrain at 24.5 m, every classification distance is inflated by
≈24.5 m ⇒ cells 2+ bands too far; a band's create/delete then fires when the
camera crosses a sphere around the WRONG center — mid-band in real instance
distance, where the per-frame ZSort alpha (`(blendDistMax-dist)/10`) is at
full alpha ⇒ unmasked hard pops. Lifting the camera 4 m pushes all near cells
across the fake 30 m boundary (their visible band-2 grasses deleted at full
alpha, replaced by different band-3 content) = "unpop"; descending back =
"pop again". Also confirmed benign-but-real: band 0 (0–10 m) lists are empty
for all 14 loaded tilesets (stock data), and the same z=0 center fed
`createSortBlock` (sort blocks 24 m under the terrain: wrong pyramid-clip
geometry and ZSort layer assignment).

### 6.6 Fix (2026-11-12, code landed, awaiting in-engine confirmation)

- `CVegetBucket` gained `SurfaceCenter`: area-weighted centroid of the
  member triangle corners (real surface position). `Center` is unchanged
  (quantized z=0) and remains the generateGroup noise seed — plant layouts
  are bit-identical to before.
- `CDynVegetableBlock::update` classifies bands from `SurfaceCenter`;
  `createSortBlock` is centered on it too.
- Probes print `SurfaceCenter` now (real z visible in future logs).
- Unit test: `SurfaceCenterCarriesElevation` regression (flat surface at
  z=24.5 ⇒ `SurfaceCenter.z == 24.5`, `Center.z == 0`) + centroid re-derivation
  in the clustering test. **Whole dynworld exe set green: 13 suites, 77
  tests** (Release, direct-exe run from `build\bin\Release` with
  `pipeline/client_dev/local` + `redist\nel_tools_win_x64` on PATH — same
  loader class as `docs/TestEnvironmentNotes.md` §1). Client rebuilt.
- Expected effect in-engine: band transitions only at real 10/20/30/40/50 m
  boundaries around the true surface ⇒ the fade masking holds again (swaps
  at alpha≈0), and the near-field churn should stop entirely. Cells within
  10 m real distance classify band 0, whose lists are empty ⇒ nothing plants
  inside 10 m of the camera; the nearest content is band-1 grasses
  (10–20 m), full alpha inside 10 m.
- Retest protocol (clean signal): spawn, wait for the probe log to go quiet
  (chunk gen / LOD transitions settled — `probe band`/`probe block` lines
  stop), then do the low → lift → low steps with a few seconds between each;
  the pops should be gone and `probe band` events should only fire when the
  camera actually crosses real ring boundaries.
- Note: with centers fixed, the first session after the fix reclassifies
  every entry once (one-time reshuffle of when plants appear) — expected,
  not a regression.
- In-engine confirmation (2026-11-12): "no more poping, good job!" — the
  close-pop bug is closed. Second report the same session: "still it looks
  like some fading is missing" → §6.7.

### 6.7 "Some fading is missing" (2026-11-12, second pass)

Symptom (user): moving forward through loaded terrain, plants pop in at
~15 m and ~40 m; moving backward, plants disappear abruptly (consistently).

**How the stock fade actually works** (verified in NeL source): there is no
temporal fade. The alpha is spatial, computed per-frame in the vegetable
vertex program: `alpha = (blendDistMax − distToVertex) / BTD` with
`blendDistMax = (distType+1) × 10` (`NL3D_VEGETABLE_BLOCK_ELTDIST`,
`BLEND_TRANSITION_DIST` = 10; VP `MAD o[COL0].w, dist, c[11].x, v[9].w`,
c[11] = negInvTransDist). A band's IG is created exactly when the camera
crosses the band's outer ring (alpha 0 there) and reaches full alpha at the
inner ring — so on-time creates/deletes are invisible. The formula only
applies to the `UNLIT_2SIDED_ZSORT` pass; opaque passes pop.

**Shape data check**: unpacked `jungle_vegetables.bnp` (the "veget" files
there are CVegetableShape binaries — the per-plant `.veget` names are shape
files, the `*_Sp.vegetset` files hold the descriptors). All 35 jungle shapes
are `Lighted=1, DoubleSided=1, PreComputeLighting=1` → `destLighted = 0`
(because PreComputeLighting) → with `AlphaBlend=1` they take
`UNLIT_2SIDED_ZSORT`, i.e. the fade pass. Only `Ju_DebrisA/B`
(`AlphaBlend=0`, desert plateau tileset) are opaque. So the fade pipeline is
live for the vegetation in the test area — the pops are NOT unfaded shapes.

**Log evidence** (probe build still running, session 21:32): ~4,500 band
transitions in 20 s during a fly-around (balanced create/delete pairs around
ring boundaries = normal), 121 block creates / 54 erases with the same
sectors erase+create cycled 4–5×, and budget-starve bursts (125/177 entries
skipped in single seconds) coinciding with the churn storms.

**Root cause — the structural sync is displayFactor-gated**
(`CDynLandscape::updateVegetableBlocks`): blocks exist only while
`m_displayFactor ≥ 1`. But `CSectorTransition::updateFactors` stages the
transition: the NEXT mesh fades 0→1 first, THEN the PREVIOUS mesh fades
1→0. Therefore, for every LOD change / mesh re-extraction:

1. next mesh at factor 0 → block NOT created (gate) → vegetation missing
   while the terrain fades in;
2. next mesh reaches 1 → block created, but its IGs are filled by the
   budget-gated band walk; during churn bursts (15–50 blocks/frame) entries
   starve for seconds → IGs appear mid-band at partial/full alpha = the
   15 m / 40 m pop-ins (a starved band-1 create lands at ~15 m instead of
   the 20 m ring edge, alpha ≈ 0.5);
3. previous mesh starts fading out → its block is erased at fade-out START,
   at full alpha → abrupt disappearances (visible when moving backward).

**Fix (landed, same session):**
- Block lifecycle decoupled from `m_displayFactor`: created as soon as the
  mesh enters `m_regionsList` (factor 0 — hidden under the still-visible
  previous mesh, which shows the same ground) and erased only when the mesh
  leaves the list (transition end, by which time the replacement block is
  populated). Vegetation becomes continuous across LOD transitions and
  re-extractions; during the transition both LODs' vegetation is briefly
  alive (placements derive from the same voxels → near-identical plants,
  negligible ghosting, documented trade-off vs. gap+flash+vanish).
- Near-first band walk: blocks are sorted by distance to the viewCenter and
  updated closest-first, so the per-frame IG budget (`g_maxVegetCommit`,
  tunable via `dyn_vegetbudget`) can no longer be consumed by far churn
  refills before the visible near-field creates. A starved near entry is
  what pops mid-band; far entries tolerate deferral (their creates happen at
  alpha ≈ 0 anyway).

Awaiting in-engine confirmation of §6.7's fix before stripping the probes
(still present in the running exe; already stripped from source).

**Second report (same session): "it still pops in the same manner"** with the
§6.7 fix live. User's reading: pops at *constant distances* while moving,
independent of ground mesh/LOD — i.e. the band rings themselves, pointing at
the alpha path rather than the structural sync. User also notes LOD1 is far
and likely vegetation-free.

Added two debug commands to settle it (built, in client since this rebuild):

- `dyn_vegfade <scale>` — scales the slope of the stock vegetable VP alpha
  formula (`NegInvTransDist` uniform, `CVegetableManager::setupVertexProgram`
  Constants`, stock `NL3D_VegetFadeSlopeScale = 1` in
  `nel/src/3d/vegetable_manager.cpp`). Semantics:
  `alpha = blendDistMax/10 − dist·scale/10`, i.e. alpha hits 0 at
  `blendDistMax/scale`. **scale 1 = stock**; **scale 0 = no fade at all**
  (constant alpha ≥ 1, every plant full, pops maximal); **scale 0.1 = 10×
  longer fade** (plants fade in over 100–500 m, an obvious gradient).
  Verdict rule: if the rendering does NOT change between 0 and 0.1, the VP
  alpha path is dead in the GL3 driver and the band rings render at full
  alpha (constant-distance pops explained); if it changes smoothly, the fade
  is live and the pops are timing (budget/structural).
- `dyn_wireframe` — toggles whole-scene wireframe (`IDriver::setPolygonMode`)
  to see which terrain LOD meshes exist where (lod boundary = triangle size
  change) and compare with the plant rings. Vegetation blocks are created
  for lod 0 AND lod 1 meshes by the sync (`lod <= 1`); whether a lod1 sector
  actually hosts plants depends on its placement set.
  **REMOVED after the A/B**: driver-level polygon-mode switching corrupts the
  GL3 render state after one frame (terrain back to filled, big flora rendered
  without fog). Do not re-add. LOD coverage will be answered by a per-block
  dump in `dyn_vegetdump` instead. The command, `toggleWireframe()` and the
  `CDynLandscape::getDriver()` getter were all removed.

Static review of the GL3 alpha path (no bug found): nelvp→GLSL converter
maps `o[COL0]` → `diffuseColor` varying (alpha included); the synthesized
fragment program does `fragColor = diffuseColor` then standard texenv
modulation (alpha multiplies), alpha test via `discard`, material blend
state translated (`convBlend` + `IDRV_TOUCHED_BLENDFUNC`). So the fade
should be live — the A/B run decides.

**A/B outcome (user):** `dyn_vegfade 0` displays opaque vegetation,
`dyn_vegfade 1` shows some alpha → the VP alpha path IS live. The pops are
therefore *timing* (creates happening mid-band at partial/full alpha), not a
dead alpha path. `dyn_wireframe` confirmed broken (GL3 state corruption) and
was removed.

**Fade-timing probe pass (probes re-added, source + rebuilt exe):** to find
the remaining mid-band creates, TEMP-PROBE instrumentation in
`dyn_landscape.cpp`:

- `probe band cell=... A->B dist=... alphaCreate=... fresh=...` — per band
  transition: |viewCenter − SurfaceCenter| and the alpha the created band
  carries at the transition moment (approach: `((newDistType+1)·10 − dist)/10`,
  the innermost created band; recede: `((CurDistType+1)·10 − dist)/10`).
  `fresh=1` = the entry had no IG yet (block-creation burst) vs a ring
  crossing. On-time ring-edge creates must show alphaCreate ≈ 0; alpha > 0.2
  at create = the visible pop.
- `probe budget starve ... needed=... budget=...` — entries deferred by the
  IG budget (they create late, mid-band).
- `probe block create/erase center=... lod=...` — structural churn timing.
- `probe cam pos=...` every 30 frames — correlates pop reports with position.

`dyn_vegetdump` extended with a per-block inventory line
(`block lod=... sector=(...) center=... entries=... igs=...`) to settle
whether LOD1 meshes carry vegetation blocks.

Repro protocol: walk forward through loaded terrain until pops occur, note
the moments; then the log analysis checks, around the pop seconds, whether
creates happen with alpha > 0.2 and whether they are fresh-entry or
ring-crossing creates. Probe strip + test run + commit after validation.

### 6.8 Reference-point mismatch: band walk vs VP alpha (2026-11-13, root cause found)

**User observation (decisive):** "on first person view, there is no pop";
"the closer the camera is to the player, the less pop I see and blend works
fine". Third person pops at constant distances (~15 m, ~40 m), disappearing
consistently when moving backward.

**Log analysis (18:39 session, 36 s of walking, 5236 band transitions):**

- 4917 logged creates: only 502 (~10%) at alpha ≈ 0 (on-time); 2139 at alpha
  0.5–1.0 and 1425 at 0.2–0.5 → **72% of creates mid-band at visible alpha**.
- 4716 of them `fresh=1` (whole-block burst: the block did not exist yet);
  their dist distribution: 3459 at 30–50 m, 839 at 20–30 m, 287 at 10–20 m,
  93 at <10 m from the view center.
- 3306 budget starves, but ALL with `needed=0` → not real starvation; the
  create path is not budget-limited in this session (near-first ordering
  working). Fresh bursts at 20–50 m are the dominant artifact.

**Root cause — the band walk and the VP alpha use different reference
points.** The vegetable VP alpha is computed against `_ViewCenter`
(`CVegetableManager::render(viewCenter...)` ← `CLandscape::render(refineCenter,...)`
← `CDynLandscapeModel::clipAndRenderLandscape`: `refineCenter =
_RefineCenterUser` ← `main_loop.cpp: "Landscape->setRefineCenterUser(View.refinePos())"`
= **the player position in 3rd person** (`user_controls.cpp: View.refinePos(UserEntity->pos())`,
fly mode: view pos)). Stock runs the band walk on the SAME point
(`CLandscape::refine(refineCenter)` → `vegetBlock->update(refineCenter,...)`,
landscape.cpp:739). Our P2 port passed `MainCam.getPos()` — the **camera** —
to `updateVegetableBlocks`.

Consequence in 3rd person (camera trails the player by D):

1. Ring-edge creates fire when the *camera* crosses the ring; the player is
   still D inside the band → alpha (player-based) = D/10 > 0 → pop-in while
   moving forward (15 m ≈ band-1 ring 20 − D; 40 m ≈ band-4 ring 50 − D).
2. Ring-edge deletes fire when the *camera* exits the band; the player is
   still D inside → plants deleted at alpha = D/10 (visible) → abrupt
   disappearance when moving backward.
3. 1st person: D ≈ 0 → creates/deletes at alpha ≈ 0 → no artifact.
4. Pop alpha = D/10 → bigger camera offset = bigger pops.

**Secondary artifact — fresh blocks materializing at 20–50 m:** dynworld
terrain sectors stream in around the player (unlike stock patches, loaded at
800 m vision), so a new block's whole band set is created at once at the
current distance; every band whose outer ring is beyond that distance is
instantly visible (stock alpha ≥ 0) → pop even with a consistent reference.

**Fixes (landed, built):**

1. `updateVegetableBlocks(pos)` at the call site — pass the same `pos`
   (`View.refinePos()`) that sector streaming and the render refine center
   use, restoring the stock single-reference invariant.
2. Late-create fade in `createIgForDistType`: when the IG would be visible
   at creation (dist < blendDistMax), shift its blend window to start at the
   current distance (`blendDist = max(dist, ELTDIST)`): alpha = 0 at
   creation, ramps to full over the next 10 m of approach. Kills the
   fresh-burst pops at 20–50 m; stock behavior unchanged for on-time
   creates (dist ≈ ring edge → shift is a no-op).
   **DISABLED after in-engine test (same day)**: user confirmed fix 1 alone
   is enough ("OK it works" — no pops while walking), and the shift has a
   visible downside on near-voxel edits: a block recreated under the view
   center gets `blendDist = dist` → its plants start at alpha (dist)/10, so
   vegetation visibly fades/dims when touching a nearby voxel. Stock blend
   formula restored (comment tombstone in `createIgForDistType`); the fresh
   blocks materializing at 20–50 m keep their stock full-alpha appearance,
   accepted for now. A true temporal fade remains a P4 ring-policy option.
3. Probes extended with `late=N` (number of created bands that got the
   shifted window) to validate fix 2 from the log. (Removed again when fix 2
   was disabled.)

Awaiting in-engine validation; probes still in the exe.

### 6.9 Voxel-edit vegetation blink (2026-11-13)

User report (after §6.8 fix validated: "OK it works"): touching a nearby
voxel sometimes shows a blink of vegetation. Question asked: async update?
Can we keep the old vegetation until the new one is ready?

**Flow analysis (voxel edit, same-lod mesh update):** `updateMesh`
(dyn_landscape_user.cpp:665) registers a `CSectorTransition` and posts an
async `CGenerateMeshCommand`; the OLD mesh stays in `m_regionsList` until
the transition completes, so the old vegetation block stays alive during
extraction — asyncness is NOT the gap. When the extraction completes,
`updateTransitionMeshes` (line 1142-1148) inserts the new mesh into
`m_regionsList` only once `m_nextFactor >= 1` (same-lod update path). The
next vegetable sync then sees the pointer change and does erase-old +
create-new in ONE commit window — but the new block's initial IG fill goes
through the shared per-frame budget (`g_maxVegetCommit`): a full 16 m lod0
sector needs up to 64 buckets × 4 bands = 256 IGs, so the all-or-nothing
per-entry fill can spill over 1–3 frames → the renderer shows bare terrain
where plants just were → the blink. Matches "sometimes" (depends on the
other blocks competing for the same frame's budget).

**Fix (landed, build pending client close):** dedicated budget pool for
near blocks. Blocks with center within the vegetation render range
(`NL3D_VEGETABLE_DEFAULT_DIST_MAX` = 60 m) draw from `nearBudget =
2 × g_maxVegetCommit` instead of the shared budget, so a freshly
(re)created near block fills completely in its creation frame; erase +
create + fill become one atomic pre-render operation — the rendered frame
never shows the gap. Far blocks keep the shared budget (their creates land
at alpha ≈ 0; timing irrelevant).

Fallback if blinks persist (fills larger than the pool): retire-and-swap —
keep the replaced block alive in a retiring list (receding-only band
updates) until the replacement block is settled (no pending band creates),
then release it in one window. Deferred until needed.

### 6.10 §6.5–§6.9 validation complete, probes stripped (2026-11-13)

User confirmed in-engine: close-pop fixed ("no more poping"), fading
fixed ("OK it works" after §6.8 fix 1; late-create fade disabled same
session), voxel-edit blink fixed ("no more blink, that's perfect"). All
TEMP-PROBE instrumentation (band/budget/block/cam, §6.4 class) stripped
from `dyn_landscape.cpp` per convention; the permanent debug commands
(`dyn_vegetdump`, `dyn_vegettest`, `dyn_renderveget`, `dyn_vegetbudget`,
`dyn_vegetstats`, `dyn_vegfade`) stay. Remaining before commit: rebuild
(client exe must be closed), full dynworld unit test set green, then commit
the whole feature branch.

### 6.11 P5 completion (2026-11-13)

- **`_VegetableManager` origin resolved**: created once in the `CLandscape`
  ctor (`nel/src/3d/landscape.cpp:230`) with the stock VB quotas; `CDynLandscape`
  only inherits it and its dtor destroys it (blocks released in the
  `CDynLandscape` dtor first, `dyn_landscape.cpp:268`). No dangling risk from
  enable/disable toggling.
- **New debug commands** (`dyn_landscape_user.cpp`): `dyn_veget [0|1]` —
  toggle; disable goes through the new `CDynLandscape::setVegetableActive`
  which releases all vegetable blocks (`clearVegetableBlocks`) before the
  stock `enableVegetable(false)`, so the vegetable VB space is freed; the next
  structural sync recreates them on re-enable. `dyn_vegetdensity <pct>` —
  forwards to `setVegetableDensity(pct/100)`, no-arg prints the current value.
  `ULandscape` has no active-state getter, so `CDynLandscapeUser::
  isVegetableActive()` was added (forwards to the model), following the
  `dumpVegetableData`/`logVegetStats` wrapper pattern.
- **Live-toggle verified**: `ClientCfg.MicroVeget`/`MicroVegetDensity`
  (`main_loop_utilities.cpp:167-196` → `enableVegetable`/`setVegetableDensity`)
  works unchanged for the dyn landscape — both forward through
  `CDynLandscapeUser`.
- **Budget sign-off** (no density change needed):
  - VB quotas: the manager is the stock one — `Unlit 50000` / `Lighted 5000`
    AGP vertices (`landscape_def.h:57-58`); `CVegetableVBAllocator::
    exceedMaxVertexInBufferHard` drops allocations beyond the quota (the
    stock degradation mode, not a crash).
  - Measured steady state (§4 P2/M1 record, `dyn_renderveget` at default
    density): 14 visible clip blocks ≈ 3780 rendered faces ≈ ~8k AGP
    vertices — under 16% of the Unlit quota, so the vegetation window
    (60 m) has ~6x headroom; density increases stay viable.
  - Main-thread commit cost: capped at `g_maxVegetCommit = 256` IG
    creates/frame shared budget + `nearBudget = 2×256` pool for near blocks
    (§6.9); worst case a full 16 m sector fill = 256 IGs fits one near-pool
    frame. Plant generation is amortized in the worker (async mesh
    extraction); band-walk cost is a per-frame sort of the block map
    (reused buffer, ~O(n log n), n ≈ tens).
- **Test set**: 13 dynworld suites green after the strip + P5 additions
  (direct-exe Release run, 2026-11-13).
