# DynClientRuntime — client-side dynworld init & runtime

> **System doc.** How a client session enters a dynamic world and what the client needs
> at runtime: session routing, data search path, material library, landscape init,
> debug commands, config knobs.
> Deployment/server side: `docs/ShardRuntimeOps.md` (§7 dyn deltas).
> Voxel apply commands & message chain: `docs/VoxelMessageChain.md`.
> Client sandbox details: `pipeline/client_dev/local/README_SANDBOX.md`.
> Client boot smoke history / sandbox gotcha list: roadmap Phase 5 in
> `docs/LEGACY_DYNWORLD_INTEGRATION.md`.

## 1. Session routing into a dyn world

- EGS mirrors `isInDynWorldSession` to the client via `CUserCharMsg` bit 3 (write side
  `character.cpp:7703` = `IsDynamicContinent.get()`; client read `net_manager.cpp:430` →
  `bool IsInDynWorldSession`).
- `CContinentManager::select()` routes `select("dynamic")` (`continent_manager.cpp:407`)
  → `CDynContinent` + `CDynLandscape` instead of the legacy continent path.
- Verify live **by behavior, not by config echo**: NeL `CVariable`s don't log. Proof
  markers: legacy `Looking into %s` continent logs absent + dyn landscape init lines + (a
  successful entry) `3D: dynlandscape vertex program available` +
  `pixel program available` (`dyn_landscape.cpp:427/457`).

## 2. Client data search path (two `PreDataPath` entries, commented `[DYN_WORLD]`)

1. `code/ryzom/client/data/shaders` — required before any dyn landscape init:
   `CDynLandscape` init does `CIFile(CPath::lookup("dyn_landscape.vp1"), true)`
   (`dyn_landscape.cpp:124/129`); strict-`CIFile` means a missing shader source =
   unhandled `EPathNotFound` crash class during world entry.
2. `code/ryzom/common/data_leveldesign/dynworld` — needed by
   `CMaterialLibrary::initialize("material_library.cfg")`, called at in-game init
   (`init_main_loop.cpp:549`). Without it the load throws inside a `catch(...)` that only
   logs `Error when loading MaterialLibrary infos` (that warning is filtered out of the
   log entirely) and the library stays **empty**, which leads to the assert flood below.
   The dir is registered recursively so `environments/*.cfg` (5 biomes) and
   `materials/*.cfg` (9 materials) resolve. This dir is also what the **server** needs in
   its `Paths` (discoverable with zero extra config there, Phase 5 verified).

Both entries exist as commented `[DYN_WORLD]` blocks in the client cfg sandbox; enable
together with the dyn session routing, never partially.

## 3. Failure mode catalog (client init)

| Symptom in log | Root cause | Effect |
|---|---|---|
| crash/EPathNotFound at world entry | `data/shaders` missing from `PreDataPath` | strict `CIFile` construction throws on missing `dyn_landscape.vp1` |
| repeated `nlassert` flood → client Not Responding | `data_leveldesign/dynworld` missing → empty `CMaterialLibrary` → `getMaxEnvironmentId()==0` → `CBiomeManager::getBiome` computes `biome=0` → `nlassert(CMaterialLibrary::instance().isValidEnvironment(biome))` **every frame** (`biome_manager.cpp:98`) | unusable in-world state, log flood; looks like a hang |
| `Error when loading MaterialLibrary infos` (single line, easy to miss) | throw inside `initialize()` caught by `catch(...)`; the warning may be filtered out by the log system | missing or malformed env/material cfg set |
| per-frame `EUnknownVar` entries during material-library init | optional-var `try`/`catch` lookups (e.g. `BigFlora` absent in `prime_roots.cfg`, `TexturesTop`/`TexturesSides` absent in some material cfgs) | benign — same code+data as legacy |
| `Failed to open file './cache/map/....sector'` at first dyn entry | empty local `.sector` cache | benign |

## 4. Runtime config knobs

- `ChunksMemoryCache` — client cfg `uint32` added by the integration (U-C1): ctor default
  `1024U` (≈1 GB), legacy dev value `3072`; read via `READ_INT_FV` in `client_cfg.cpp`.
- `AllowDebugCommands = 1` — needed for the `dyn_*` command set below (legacy client.cfg
  already ships it).
- `StartupHost 127.0.0.1:40916` — the local shard's FE web port (see
  `docs/ShardRuntimeOps.md` web/DB section).
- `PreDataPath = { "user", "patch", "data", "examples" }` (client_default.cfg) bakes
  recursively — a directory inside `data/` is automatically on `CPath` (this is why the
  shaders dir needs no extra registration once copied under `data/`).

## 5. Debug command catalog (`#if !FINAL_VERSION` block, `commands.cpp`)

19 `NLMISC_COMMAND(dyn_*)` commands (added U-C7, all resolve live):

- Terrain/voxel: `dyn_setvoxel`, `dyn_addvoxel`, `dyn_voxelselection` (usage + the
  selection-cube default-off caveat → `docs/VoxelMessageChain.md` §4).
- Mesh/regen: `dyn_regenvoxels`, `dyn_regenmesh`, `dyn_cubicgen`, `dyn_rebuildshaders`.
- Position/camera: `dyn_refreshpos`, `dyn_igdist`, `dyn_prefetchextents`.
- Lod/render switches: `dyn_baselod`, `dyn_maxlod`, `dyn_fardist`, `dyn_useVP`,
  `dyn_usePP`, `dyn_reloadenvconfig`, `dyn_renderoldlandscape`.
- Movement: `dyn_snap` — toggles `g_noSnap` (`entity_cl.cpp`: client PACS ground-snap is
  hard-disabled for dyn worlds via `bool g_noSnap = true;` + early return feeding
  `if (g_noSnap) needSnap = false;`).
- Query: `dyn_floorheight`, `dyn_voxelselection` readouts (`CMaterialLibrary` name +
  density readout of the voxel under cursor).
- Reload: `dyn_reloadenvconfig`.

ShowInfos debug cycle extended by one level (`%6` → `%7` in `debug_client.cpp` +
`interface_v3/action_handler_debug.cpp`); `main_loop.cpp` calls
`displayDebugGeneration()` on level 6 (`main_loop_debug.cpp`). The runnable-stats
`CTasksStats` section was stripped (U-M2 decision) — that debug page has no
thread-stats section.

## 6. Landscape render toggles / Phase-5 render smoke material

- Globals exercised by the upcoming render validation: `g_rebuildShaders`,
  `g_useVP`, `g_usePP`, `g_renderLegacyLandscape` (contrast: new VP/PP path vs legacy
  landscape render).
- `CDynLandscape` shaders: `dyn_landscape.vp1` (44 lines, NV vertex program 1.0) +
  `dyn_landscape.arbfp1` (~70 lines, ARB fragment program), loaded via
  `CPath::lookup("dyn_landscape")` + `CIFile::serialBuffer` (readShaderFiles).
  The `.cg` pair (`dyn_landscape_vp.cg` / `dyn_landscape_fp.cg`) is authoring source only
  — never loaded at runtime.
- `CDynLandscape::render()` replaces the **whole** render path including vegetables
  (trailing `bool doVegetables = true` param accepted and unused in the impl).

## 7. Interaction notes

- Contextual cursor: the "VOXEL" context (uim texture `uimGcmExtractRM`) + voxel
  under-cursor selection branch with the `CMaterialLibrary` name + density readout;
  `checkUnderCursor` resets via `CDynContinent::MAP->unselectVoxel()`.
- Click-parameter change (integration U-C6): contextual cursor actions now receive
  `NLMISC::TMouseButton` (left/right/middle) instead of a `bool rightClick` — the middle
  button = carve voxel (below fill: left = fill, middle = carve to `Air`), fill speed
  `g_voxelFillSpeed = 255`.
- Known gaps to watch: `contextSetVoxel` density operations currently write straight to
  `CDynContinent::MAP` without going through the deferred-command path (TODO markers in
  the source, same as legacy).

## 8. Sandbox/runtime environment notes (client)

- Client import set: `libxml2/lua/libcurl/zlib/libssl-1_1/libcrypto-1_1/freetype/
  vorbis/vorbisfile/jpeg62/libpng16` + fresh `nel_drv_*` DLLs from
  `build\bin\Release` — the shipped `redist\ryzom_client_win_x64` bulk copy also carries
  **stale 2022 `nel_drv_*` driver DLLs** whose exported `NL3D_interfaceVersion()` predates
  the current `IDriver::InterfaceVersion` (0x74) → driver-version gate rejects them ⇒
  fresh driver DLLs must be copied in AFTER the redist bulk copy (see
  `pipeline/client_dev/local/README_SANDBOX.md` for the live copy recipe).
- Legacy asset run dir (`ryzom_core_client`): chosen client runtime bed; details in
  `docs/LegacyAssetInventory.md`.
