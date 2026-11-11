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

Validation: build + client session (no render call yet — count instances via
debug log/`dyn_vegetstats`).

### P3 — Render integration (first in-engine result)

- [ ] `CDynLandscape::render` honors `doVegetables` + `_VegetableActive` +
      `FilterVegetable`, calls the manager render with the tessblock pyramid
      and `textureDLM = nullptr`; fog behavior checked (stock fog setup is
      inside the manager).
- [ ] Milestone **M1**: in a client session, random microvegetation visibly
      covers soft-soil surfaces (grass on Humus forest floor), none on
      non-soil materials, none under water where water tops the ground.

Validation: in-engine, screenshots recorded in the system doc.

### P4 — Rings, edits, transitions

- [ ] Ring policy per D4 (incremental switching or plant-all), measured.
- [ ] Edit sync: `dyn_setvoxel`/`dyn_addvoxel` edits re-extract the mesh and
      the vegetation re-plants deterministically (no reshuffle) and stays
      consistent with the new surface (plants removed where voxels buried
      them, appear on new soil).
- [ ] LOD transitions: no popping regression from the existing
      `CSectorTransition` factor animation (vegetation is planted from the
      target mesh only, IG count transitioned or cut per D4).

Validation: in-engine edit + transition session walk; frame-time budget
recorded.

### P5 — Config, debug commands, performance sign-off

- [ ] Reuse `ClientCfg.MicroVeget`/`MicroVegetDensity` (already partially
      wired through `continent_manager.cpp`); live-toggle path
      (`main_loop_utilities.cpp:167-196`) verified for the dyn landscape.
- [ ] Debug commands in the `dyn_*` block (`commands.cpp:5338+`):
      `dyn_veget` (on/off), `dyn_vegetdensity <pct>`, `dyn_vegetstats`
      (per-lod instances/faces/IGs, `getNumVegetableFaceRendered`).
- [ ] Budget sign-off: VB allocator limits (stock
      `NL3D_LANDSCAPE_VEGETABLE_MAX_AGP_VERTEX_*` sized for the 50 m band),
      worker cost of placement, main-thread commit cost. Adjust ring policy
      (D4) and density defaults if needed.
- [ ] Create the **system doc** `docs/DynMicroVegetation.md` (CamelCase): what
      the system is, its data flow, invariants (lock discipline, seed
      determinism, soft-soil rule), the instrumentation/debug registry, and
      the P0–P5 findings; add it to the `AGENTS.md` ToC. Update the roadmap's
      plan-doc list to point here with status.

Validation: full dynworld exe set green; Release client build; session walk.

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
