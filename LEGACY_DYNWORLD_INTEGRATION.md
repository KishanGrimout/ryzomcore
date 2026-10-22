# Legacy Dynworld Integration Roadmap

## Source and target

- Target repository: current Git repository, branch `integration/legacy-dynworld`.
- Integration base: `8ce92f49c` (`dev/dyn` at branch creation).
- Legacy source: `D:\Programmation\ryzomcore_old\ryzomcore_dev`, Mercurial tip `9514:da0dbd6457ca` (`Merge dynworld into develop`).
- Legacy exports describe 343 changed paths and 135 new files. Their source layout and build system predate the current repository; port features incrementally rather than applying the exports wholesale.

## Integration rules

- Preserve the current C++17 and target-based CMake conventions.
- Add dependency discovery only with the first code that consumes each dependency.
- Do not import legacy C++14 defaults, old MSVC workarounds, or optional PIX instrumentation without a current requirement.
- Integrate one cohesive unit at a time; build and test it before moving on.
- Review each completed unit before continuing into the next subsystem.

## Working context (for future sessions)

Paths and sources:

- Target repo: `D:\Programmation\ryzomcore\code`, branch `integration/legacy-dynworld` (based on `8ce92f49c`, `dev/dyn`).
- Legacy working copy (post-merge state at the dynworld tip): `D:\Programmation\ryzomcore_old\ryzomcore_dev`, Mercurial tip `9514:da0dbd6457ca` (`Merge dynworld into develop`). `hg` is not on PATH; invoke it explicitly: `& 'C:\Users\mynam\AppData\Local\Atlassian\SourceTree\hg_local\hg.exe' -R D:\Programmation\ryzomcore_old\ryzomcore_dev ...` (verified working; tip matches). Read the working-copy files directly — they ARE the post-merge content, so a "new" legacy file can be copied verbatim into the repo (then adapted to current conventions).
- Full legacy delta: `D:\Programmation\ryzomcore_old\ryzomcore_dev\dynworldpatch.diff` (~3 MB Mercurial unified diff; 343 changed paths, 135 new files; `dynworldpatch_nocode.diff` is the same without code bodies). Each file's delta starts at a `diff --git a/<path> b/<path>` marker line.

Recipes:

- Extract one file's legacy delta: locate its marker with `Select-String -Path <patch> -Pattern 'diff --git a/code/<path>'`, then read that section up to the next marker (a small PowerShell script writing the section to `C:\Users\mynam\AppData\Local\Temp\opencode` works well).
- Compare legacy working copy vs current repo (more reliable for *modified* files): `git -C D:\Programmation\ryzomcore\code diff --no-index --stat D:\Programmation\ryzomcore_old\ryzomcore_dev\code\<path> D:\Programmation\ryzomcore\code\<path>` (the current repo root corresponds to legacy `code/`).
- If the legacy-vs-current diff is much bigger than the patch section for that file, the excess is repository modernization drift, not dynworld work; port only the patch delta.
- Build & test: configured VS solution at `build\RyzomCore.sln` (or `cmake --build build --config Release --target <target>`). **Validation config is Release, on user instruction (2026-10-21)**: `nelmisc_r.lib`, `ryzom_gameshare_r.lib`, test binary `build\bin\Release\nel_misc_test.exe` (Release-only test note: `ut_misc_vectori.test.cpp::invalidFloatingPointConversionsAreClamped` is `#if !defined(NL_DEBUG)`, so Release runs 71 tests vs 70 in Debug after the reader-writer additions — expected; Release run passes 71/71). Tests via `ctest --test-dir build` or run the `unit_test` binaries under `build\bin`; `build_test\` holds a second configure used previously. CMake source-list changes require re-running configure before building (`cmake --build build --config Release --target ZERO_CHECK`). All Phase-2 validations recorded in Debug were re-verified in Release (2026-10-21): `nelmisc_r`, `nelpacs_r`, `nel3d_r`, `ryzom_gameshare_r`, `nel_misc_test`, plus consumer canaries `zone_lighter.exe`/`ig_lighter.exe`/`unbuild_land.exe` (build via per-project MSBuild, since `cmake --build --target <tool>` hits MSB1009 for tool targets).
- Test targets that include `polyvox_defs.h` (via `map_sector.h`, `map_cache_manager.h`, etc.) need `${POLYVOX_INCLUDE_DIR}` in their own `target_include_directories`: gameshare gets PolyVox through the old-style directory `INCLUDE_DIRECTORIES(${POLYVOX_INCLUDE_DIR})` in `game_share/CMakeLists.txt`, which does NOT propagate to test targets. Similarly `map_cache_manager.h` expects `nel/misc/singleton.h` to be pre-included by its consumer (inside gameshare it comes from the `stdmisc.h` precompiled header), and `NLMISC::CFile` statics live in `nel/misc/path.h` (include path.h, not file.h, for `createDirectoryTree`/`deleteFile`).

Audit conclusions already verified (do NOT re-derive):

- Remaining unported `game_share/dynworld` files: `dyn_retriever_bank.{h,cpp}`, `dyn_global_retriever.{h,cpp}`, `dyn_move_container.{h,cpp}` (thin PACS adapters; used by future `server_share/dynworld/dyn_continent_container` and client `pacs_client`), and `unqlite_db.{h,cpp}` (referenced only by the disabled multi-pass path of legacy `map_cache_manager.cpp`; defer as optional backend — would require the `code/ryzom/common/src/unqlite` C library), plus `voxel_material_defs.h` (zero references anywhere in the legacy tree — orphan, skip).
- Ported `map.{h,cpp,hpp}`, `map_server.h`, `master_pager.h`, `slave_pager.h` are byte-identical to legacy (`map.hpp` differs by one blank line). All client modules compile against the ported APIs with no adaptation.
- Legacy `gpm2_service` vs legacy `gpm_service`: 24 of 28 files byte-identical; real deltas are `world_position_manager.{h,cpp}` (+131/−77), `gpm_service.cpp` (−21/+5), `messages.cpp` (+8 lines: `cbSetVoxel` + routing entry), `client_messages.cpp`/`cell.h`/`world_entity.h` whitespace only.
- `parallel_task_manager.cpp` is picked up by `nel/src/misc/CMakeLists.txt`'s `FILE(GLOB SRC *.cpp ...)` — no registration action needed.
- Modern `CGlobalRetriever` already derives `UGlobalRetriever` with most dynworld-needed methods pure virtual / `NL_OVERRIDE`; only `selectInstances`, `testCylinderMove`, `doMove` still need `virtual`, plus the 3-arg `retrievePosition` in `UGlobalRetriever` must become pure virtual. `_Instances` is already protected (`getInstance()`/`getInstanceFullAccess()`); modern `getInternalCST()` can replace the legacy private `_InternalCST` exposure.
- `seven_zip` was already centralized at `ryzom/common/src/seven_zip` with `NeL::sevenzip` discovery; the client must link that, not a client-local copy. **CORRECTED 2026-10-21, then completed**: the port had only brought 4 files (`7zCrc.h`, `seven_zip.{h,cpp}`, `CMakeLists.txt`); the legacy dir **vendored the entire 7zip SDK (~90 files: `7z.h`, `LzmaEnc.cpp`, ...), which the wrapper is written against (old API: `CLookToRead`, `SzArEx_Open`)**, and legacy `common/src/CMakeLists.txt` had `ADD_SUBDIRECTORY(seven_zip)`. The wrapper cannot compile against `nel/3rdparty/seven_zip` headers (API drift) — it needs its vendored SDK. The vendored SDK has since been ported (72 files copied verbatim, legacy `seven_zip/CMakeLists.txt` adapted: `NL_TARGET_LIB(ryzom_sevenzip)`, `Ryzom::sevenzip` alias, `UNICODE/_UNICODE/_7ZIP_PPMD_SUPPPORT` target-scoped, message-PCH `Precomp.h` when `WITH_PCH`, legacy `7zDec` tool under `WITH_RYZOM_TOOLS`, link `NeL::misc` instead of legacy `nelmisc`); `common/src/CMakeLists.txt` registers it (`ADD_SUBDIRECTORY(seven_zip)` **before** `game_share` so the alias exists at configure time) and gameshare links `Ryzom::sevenzip` (replacing the unfaithful `NeL::sevenzip` substitution — legacy linked `ryzom_sevenzip`). Verified in Release: `ryzom_sevenzip_r.lib`, `ryzom_gameshare_r` relink, `zone_lighter.exe` canary, and a full gameshare-consuming test run (with `WITH_RYZOM_TOOLS=OFF` in the current configure, `7zDec` is not generated — same as legacy).
- Shaders: only `dyn_landscape.vp1` (44 lines, NV vertex program 1.0) and `dyn_landscape.arbfp1` (~70 lines, ARB fragment program) exist at the legacy tip, and they are the only ones loaded (`CPath::lookup("dyn_landscape")` + `CIFile::serialBuffer`). The patch also lists `dyn_landscape_{vp,fp}.cg` Cg prototypes but they are absent at the merge tip — do not fabricate them.

## Completed

- [x] Create the isolated integration branch.
- [x] Audit the legacy CMake/dependency changes. ANL and PolyVox are not in the current dependency manifest/ports; defer wiring until their consumers are integrated.
- [x] Add `CVectorI` to NeL and cover wide length arithmetic and checked float conversions with tests.
- [x] Add `CParallelTaskManager`, adapt task-priority callback chaining, and test scheduling, nested work, worker context, and shutdown draining.
- [x] Add `requestShutdown()` for worker-requested shutdown; require external-thread destruction and log a NeL fatal error on invalid worker-thread destruction.
- [x] Build the workspace and run `nel_misc_test` successfully.
- [x] Port the shared dynworld `Voxel` type, register its header for game-share installation, and add focused tests.
- [x] Port the NeL-only `mapgen/noise` unit, correct derivative arithmetic, and add focused tests.
- [x] Port `mapgen_params`, assign valid bitmask values to `Flat` and `Caves`, initialize the unused biome parameters safely, and test configuration loading.
- [x] Port `environment_definition`, register only the selected source/header, and test config loading and derived bounds.
- [x] Integrate PolyVox wrapper and definitions (FindPolyVox.cmake, polyvox_defs.h/cpp).
- [x] Port and test `map_sector` with sector ID/region coordinate system.
- [x] Port `material_definition` for material texture definitions.
- [x] Port `material_library` for material and environment management.
- [x] Verify ANL license (permissive BSD/MIT-like at https://github.com/JTippetts/accidental-noise-library/blob/master/LICENSE).
- [x] Create FindANL.cmake for ANL discovery.
- [x] Port `biome_manager` with ANL noise integration.
- [x] Port `mapgen/cavegen` for cave generation.
- [x] Port `mapgen/mapgen` for main map generation.
- [x] Port `cast_collector.h` for raycast hit collection.
- [x] Port `marching_controller.h/cpp` for marching cubes surface extraction.
- [x] Port `region_mesh_interface.h` for region mesh initialization.
- [x] Port `map_deferred_command.h` for deferred map command interface.
- [x] Port `map_deferred_command_manager.h/cpp` for async command management.
- [x] Port `map.h/cpp` and `map.hpp` for core map management.
- [x] Port `disk_file_db.h/cpp` for disk-based sector cache backend.
- [x] Port and integrate seven_zip module (seven_zip.h/cpp) for LZMA compression.
- [x] Port `map_cache_manager.h/cpp` for sector cache management with compression.
- [x] Port `map_server.h/cpp` for server-side map interface.
- [x] Port `master_pager.h/cpp` for master pager coordinator.
- [x] Port `slave_pager.h/cpp` for slave pager with sector I/O and caching.
- [x] Add a `CFastReaderWriter` unit test (`ut_misc_reader_writer.test.cpp`, 9 tests: try/force/upgrade/downgrade semantics, writer-wait blocks new readers but `forceEnterReader` wins through) — `nel_misc_test` passes 71/71 in Release.
- [x] Complete the sevenzip vendor port: copy the vendored 7zip SDK (72 files) into `ryzom/common/src/seven_zip`, register `ryzom_sevenzip` via `ADD_SUBDIRECTORY`, link `Ryzom::sevenzip` from gameshare (replacing `NeL::sevenzip`), and validate Release (`ryzom_sevenzip_r.lib`, gameshare relink, `zone_lighter.exe` canary). This makes `map_cache_manager.obj` (and any pager consumer) linkable again.
- [x] Register and run the `CMapCacheManager` round-trip test (`ryzom_dynworld_map_cache_test`, 2 tests: full save → async LZMA compress → `tryFillRegion` read-back with bit-exact voxels + metadata; sector-id record-name pinning). `Voxel::setBiome` only accepts values <= 7 (`GenericBiome`) — fill patterns must respect the field limits. All builds + passes in Release.

## External dependency version notes

- ANL checkout: `D:\Programmation\ANL`, branch `master`, HEAD `28d40d67f6f3e25149f5580da0e6655cc7ee8467` (2015-11-20, `optimize voronoi noise`). License: Permissive (BSD/MIT-like) - verified at https://github.com/JTippetts/accidental-noise-library/blob/master/LICENSE. Compatible with project.
- PolyVox checkout: `D:\Programmation\polyvox`, branch `kishan-dynworld`, commit `c5c63c22db5194029e54dea770d5b10799dbfdd3` (2018-05-01); describes as `v0.2.1-1068-gc5c63c22` and has a clean worktree. `include/PolyVox/PagedVolume.h` identifies the MIT license.

## Remaining integration order

### 1. Shared dynworld foundation — complete

Test harness note: ryzom-side gtest home is `ryzom/tools/server/test_modules/<x>_test/` (convention established upstream by PR #826 "enable ryzom tests"; siblings `egs_test`, `session_browser_test`, `shard_unifier_test`). The Oct-16 dynworld foundation commits added four `add_test_executable` targets there under `game_share_test/` — `ryzom_dynworld_voxel_test` (links `NeL::misc` only; voxel is header-only), `ryzom_dynworld_noise_test`, `ryzom_dynworld_mapgen_params_test`, `ryzom_dynworld_environment_definition_test` (those three link `Ryzom::gameshare`). All four build + pass in Release (10 tests). The Oct-21 test unit added four more: `ryzom_dynworld_map_sector_test` (10 tests; uses the public `s_posToId`/`s_idToPos` — `s_packId`/`s_unpackId` are private, only reachable via the friend `mapsectorUnittest`; `CSectorLod::convertId` only converts mesh-lod → loading; the z packed field range is only ±524287), `ryzom_dynworld_deferred_command_test` (5 tests; teardown must check `isCommandValid` BEFORE `getCommand` — freed slots are reused and `getCommand` dereferences empty slots without any assert in Release), and `ryzom_dynworld_disk_file_db_test` (5 tests; run from the `build/` dir — `CDiskFileDataBase` writes to hardcoded CWD-relative `./cache/map/`, whose parent `CFile::createDirectory` does not create — tests use `createDirectoryTree`). All seven build + pass in Release. New dynworld tests belong in the same directory as their own `add_test_executable` subtarget (do NOT create a separate dynworld test tree); NeL-misc tests stay in `nel/tools/nel_unit_test/ut_*.test.cpp`. Beware: `build/` contains a stale `ryzom_dynworld_polyvox_defs_test.vcxproj` from a never-committed past-CMake definition — harmless build residue, not a real target. `ryzom_dynworld_map_cache_test` (CMapCacheManager round-trip) became runnable once the vendored 7zip SDK landed (see the seven_zip audit correction above); it links `Ryzom::gameshare` like the others, needs `${POLYVOX_INCLUDE_DIR}` and `nel/misc/singleton.h` pre-included, writes to CWD-relative `./cache/map/` (run it with CWD = `build/`), and passes 2/2 in Release.

- [x] Inventory `ryzom/common/src/game_share/dynworld` and map external dependency consumers.
- [x] Port and test the self-contained `Voxel` type without server or client runtime dependencies.
- [x] Port and test `mapgen/noise` using existing NeL dependencies; keep ANL isolated to higher-level map generation.
- [x] Port and test `mapgen_params` using the existing NeL config and noise APIs.
- [x] Port and test `environment_definition` using existing NeL configuration APIs.
- [x] Integrate the clean, MIT-licensed PolyVox wrapper and map support modules first.
- [x] Port `map_sector` for sector ID/region coordinate system.
- [x] Port `material_definition` for material texture definitions.
- [x] Port `material_library` for material and environment management.
- [x] Verify ANL license and integrate ANL discovery.
- [x] Port `biome_manager` for biome generation with ANL.
- [x] Port `mapgen/cavegen` for cave generation.
- [x] Port `mapgen/mapgen` for main map generation.
- [x] Port `cast_collector`, `marching_controller`, and `region_mesh_interface` for mesh/raycast support.
- [x] Port `map_deferred_command` and `map_deferred_command_manager` for async operations.
- [x] Port `map.h/cpp` for core map management.
- [x] Port `disk_file_db.h/cpp` for disk-based cache backend.
- [x] Integrate seven_zip wrapper for LZMA compression support.
- [x] Port `map_cache_manager.h/cpp` for sector cache management.
- [x] Port `map_server.h/cpp` for server-side map interface.
- [x] Port `master_pager.h/cpp` and `slave_pager.h/cpp` for sector paging coordination and I/O.

Checkpoint review of the pager components against current NeL APIs: clean — every `nel/misc` API used by the ported dynworld code (`mutex`, `singleton`, `tds`, `reader_writer`, `parallel_task_manager`, `vectori`, `config_file`, `time_nl`, `smart_ptr`, `hierarchical_timer`, `debug`, `stream`, `rgba`, `types_nl`, `vectord`, `aabbox`) exists in the current repo, with no stale legacy API usage. Notes carried into later units: `master_pager.cpp pageIn()` has its cache-fill path commented out (always regenerates, never saves — revisit in U-S2/S3 wiring); `g_saveSectors` is a bare global in `slave_pager.cpp` (candidate for a config variable during U-S3); `CrcGenerateTable()` is called in both pagers but is only needed where cache I/O happens (master's call is redundant, harmless).

### 2. NeL 3D and PACS extensions — planned as units U-N1 … U-N5 (plus optionals)

Units are small and independently buildable; do them in the listed order (N1 → N2 → N3 is a dependency chain; N4/N5 are independent of it).

#### U-N1 — PACS retriever widening (prerequisite for U-N3)

- [x] Done. `collision_surface_temp.h` (`CollisionInstances` → `std::vector<uint64>`), `u_global_position.h` (`InstanceId` → `sint64` + note), `u_global_retriever.h` (3-arg `retrievePosition` now pure `virtual ... =0`, comment added), `global_retriever.h` (`virtual` on `selectInstances`/`testCylinderMove`/`doMove`, `NL_OVERRIDE` on the 3-arg implementation). Validation: `nelpacs` builds clean; `nel_misc_test` 61/61 pass (exit 0). NB: the run initially failed on `CUTMiscParallelTaskManager.drainsQueuedAndNestedTasks` — pre-existing arithmetic bug in the test itself (43 expected, but `CWorkerContextTask` never increments that counter, so max = 32 counting + 9 recursive = 41). Fixed the constant to 41 with a comment; manager behavior was correct (both worker-context tasks did run, `2u` expectation passed).

Files and exact changes:

- `nel/include/nel/pacs/collision_surface_temp.h`: `std::vector<sint32> CollisionInstances` → `std::vector<uint64>` (matches `TSectorsList` = `std::vector<uint64>` already in ported `map_sector.h`; it is a per-call temp, no serialization impact).
- `nel/include/nel/pacs/u_global_position.h`: `UGlobalPosition::InstanceId` `sint32` → `sint64` (dyn positions index sectors whose IDs are `TGenericSectorId` = `uint64`).
- `nel/include/nel/pacs/u_global_retriever.h`: make the 3-arg `retrievePosition(const CVectorD&, double threshold, UGlobalPosition::TType)` pure virtual (`= 0`); `CGlobalRetriever` already implements it. Without this, `CDynGlobalRetriever`'s `override` does not bind.
- `nel/include/nel/pacs/global_retriever.h`: add `virtual` to `selectInstances`, `testCylinderMove`, `doMove` (the other dynworld-needed overrides are already virtual via the modern `UGlobalRetriever` base).

Watch for narrowing assignments such as `uint32 id = cst.CollisionInstances[i]` in `nel/src/pacs/global_retriever.cpp` (compile warnings, harmless; no changes required there).

Validation: build `nelpacs`; run the existing NeL PACS unit tests.

#### U-N2 — `CMoveContainer` subclass exposure

- [x] Done. `move_container.h`: first `private:` block (members incl. `-_CellWidth`/`_CellHeight`) is now `protected:` (commented); the `clear()` block stays private as it has no dyn consumer. Validation: `nelpacs` rebuilds clean (header change recompiled the pacs cpps via PCH).

`nel/include/nel/pacs/move_container.h`: make the `private:` block containing `_CellWidth`/`_CellHeight` `protected:` so `CDynMoveContainer`'s ctor can set them to 16. Legacy also protected the `clear()`/`_TestTime` blocks — only open what has a consumer.

Validation: build `nelpacs` + PACS unit tests.

#### U-N3 — game_share dyn-PACS adapters

- [x] Done. Copied `dyn_retriever_bank.{h,cpp}`, `dyn_move_container.{h,cpp}`, `dyn_global_retriever.{h,cpp}` from the legacy working copy into `ryzom/common/src/game_share/dynworld/`, with adaptations: dropped legacy `#include "stdpch.h"` (game_share target has no PCH; ported dynworld files include directly) and added explicit includes (`nel/misc/hierarchical_timer.h` for the H_AUTO macros, `<limits>`); replaced all compiled-path `_InternalCST` accesses with the modern `getInternalCST()` accessor (the `#if 0` reference blocks keep the legacy code untouched, commented or excluded). Registered the 3 cpps in `SRC_DYNWORLD` and the 3 headers in `PRIV_DYNWORLD_H` of `game_share/CMakeLists.txt`. Note for future sessions: files copied out of the Mercurial working copy are read-only — clear `IsReadOnly` before editing. Validation: `ryzom_gameshare` builds clean (exit 0).

#### U-N4 — Landscape subclassing chain (central 3D unit)

- [x] Done. Applied to: `landscape_model.h` (public `getLandscape()`, `virtual clipAndRenderLandscape()`, `CLandscapeModel(CLandscape* = NULL)` protected ctor, declared dtor, `creator()` + all members protected incl. new `CLandscape* Landscape`); `landscape_model.cpp` (pointer ctor + `new CLandscape()` default, dtor `delete Landscape`, all `Landscape.` member accesses → `Landscape->`; NOTE the modern water-reflection clip sites that the legacy patch predates needed the same treatment); `landscape.h` (`virtual` on `clear`/`setDriver`/`render`/`getCameraCollision`/`getRayCollision` + `findTileTexture` placed in a public slice between `protected` blocks, exactly like legacy); `landscape_user.h` (`protected` members, ctor `CLandscapeUser(CScene*, NLMISC::CClassId = LandscapeModelId)`); `landscape_user.cpp` (55 × `_Landscape->Landscape.` → `_Landscape->getLandscape().`); `u_scene.h` + `scene_user.{h,cpp}` + `scene.cpp` (`createLandscape(ULandscape* land = NULL)`; `pLM->getLandscape().initAnimatedLightIndex`); `visual_collision_manager_user.h` (`getLandscape()->getLandscape()`).
- Same-commit call-site migration — the audit's "13 sites" undercounted. Full sweep found stale `.Landscape`/`->Landscape` member accesses also in: `nel/tools/3d/ligo/plugin_max/script.cpp`, `nel/tools/3d/object_viewer/object_viewer.cpp`, `nel/tools/3d/zone_painter/{board_session,save_ops,script_and_ui,viewer}.cpp`, `nel/tools/3d/zviewer/zviewer.cpp`, `studio/src/plugins/object_viewer/vegetable/vegetable_editor.cpp`, plus `nel_patch_paint/paint.cpp` (audited) and `ryzom/client/src/decal.cpp` (audited). All migrated to `getLandscape()` (pointer and `&`-taken cases both). The only remaining pattern is a comment in `zone_painter/main.cpp`.
- Validation status: `nel3d` builds clean; real consumer canaries `zone_lighter`, `ig_lighter`, `unbuild_land` build clean. `object_viewer` and the Max plugins (`nel_patch_paint`, `ligo/plugin_max`) are NOT in the current build configuration — their migrated sources are text-verified only; `ryzom/client/src/decal.cpp` gets compile validation when the client target builds (U-C1).

#### U-N5 — `CInstanceGroup::CInstance` init fix

- [x] Done. `nel/src/3d/scene_group.cpp` ctor now deterministically sets `nParent = -1`, `SunContribution = 0`, `Light[0..NumStaticLightPerInstance)= 0xFF` (comment added). Validation: `nel3d` rebuild clean.


#### Optional NeL units (explicit decisions, not blockers)

- **U-M1 — `nlNSleep` + task-poll retiming** (depends on nothing): `nel/include/nel/misc/common.h` + `nel/src/misc/common.cpp` gain `void nlNSleep(uint32 ns)` (legacy impl: waitable timer / nanosleep); `CTaskManager::sleepTask()` switches `nlSleep(10)` (10 ms, verified in current repo at `task_manager.h:71`) → `nlNSleep(500000)` (intended 0.5 ms — CAREFUL: the legacy Windows `nlNSleep` passes nanoseconds directly to `SetWaitableTimer`, which counts 100-ns units, so it actually sleeps 50 ms there; only the Unix branch is correct. If ported, fix the units and consider `CREATE_WAITABLE_TIMER_HIGH_RESOLUTION`). This changes polling throughput globally for all `CTaskManager` users (notably `CAsyncFileManager`) — decide deliberately. The ported `CParallelTaskManager` uses its own `nlSleep(1)` and is unaffected.
- **U-M2 — runnable-stats profiling stack** (defer to U-C7): `nel/include/nel/misc/thread.h` (`NL_RUNNABLE_STATS`, `IRunnable::TStats`) + `nel/include/nel/misc/task_manager.h`/`nel/src/misc/task_manager.cpp` (`CTasksStats` singleton + stats hooks), all behind `#ifndef FINAL`. Only consumer is legacy `main_loop_debug.cpp` `displayDebugGeneration()` (`CTasksStats::getInstance().dump()`).
- **U-M3 — `CFastReaderWriter` port** (done on user request after session review):
  - [x] Done. The lock-free reader/writer lock the legacy fork appended inside `reader_writer.{h,cpp}` (it was never stock/upstream) is now ported: declaration after `CRWSynchronized` in `nel/include/nel/misc/reader_writer.h` (needed `#include <atomic>`; legacy's `#include "tds.h"` dropped — nothing in the class references it), full implementation appended in `nel/src/misc/reader_writer.cpp`. Two documented adaptations: the legacy `NL_PROF_F()`/PIX scoped-profiling calls were removed (project rule), and the legacy spin-backoff `nlNSleep(100)` became `std::this_thread::yield()` (the legacy `nlNSleep` implementation passes nanoseconds directly to `SetWaitableTimer`, which counts 100-ns units — platform-ambiguous on Windows; the scheduler yield is portable). `ryzom/common/src/game_share/dynworld/polyvox_defs.h` `VolumeRWLock` restored to `mutable NLMISC::CFastReaderWriter m_lock;` with `upgradeLock()`/`downgradeLock()` re-enabled — matching the legacy fork exactly. Validation: `nelmisc` and `ryzom_gameshare` build clean; `nel_misc_test` 61/61. Context: `VolumeRWLock` is the lock type of the live voxel volume (`TVolume = PolyVox::PagedVolume<TVoxel, VolumeRWLock>` in `map.cpp` `new TVolume(pager, …, MultiThreadChunkCache)`, used by `master_pager`/`map_cache_manager`/`mapgen`). NOTE: the lock interacts thread-wise with U-M1 (legacy spin design assumed a low-granularity sleep kernel); if U-M1 is accepted later it is a fairer backoff, but the yield-based port is not dependent on it.
- **PIX tooling — excluded** by project rule: `CMakeModules/FindPix.cmake`, `NL_PROF*`/`NL_USE_PIX` in `hierarchical_timer.h`, `WITH_PIX` option, PIX includes/libs in `nel/src/misc/CMakeLists.txt`.

### 3. Server integration — planned as units U-S1 … U-S5

#### U-S1 — shared protocol and message interfaces (independent of Phase 2)

- [x] Done (2026-10-22). `CUserCharMsg`: `write`/`read` gain `bool isInDynWorldSession` (bit 3; the userRole/flags field serial width 3→4 bits) in `msg_client_server.h`. Adapters: EGS `character.cpp` passes `false` for now (real value `IsDynamicContinent.get()` wired in U-S4); client `net_manager.cpp` defines the legacy `bool IsInDynWorldSession = false;` global next to `IsInRingSession` and reads into it (U-C1's step — moved forward because the signature change forces the call site edit; extern declarations still to add at each client consumer); `client_drone/simulated_client.cpp` reads into an unused local (tool absent at the legacy tip, adapted minimally). `tools/client/client_multi/net_manager.cpp` uses a legacy object-style `CUserCharMsg` (no write/read) — untouched, tool not in the current build.
- `msg.xml`: appended the `DYN_WORLD` branch (`SET_VOXEL`, `sendto="EGS"`, format `s32 s32 s32 u8 u8 u8 u8`) before `</client_messages_description>`, restyled to the current file's modernized formatting (2-space, one-line leaves). DECISION: the legacy patch's stray trailing `<leaf name="DUMMY" description="stupid server....."/>` inside the new branch was omitted — copy-paste noise, same spirit as the excluded `DUMMY`→`DISABLE_LOGOUT_BUTTON` rename. Verified `CGenericXmlMsgHeaderManager::init` tolerates partial-attribute leaves, and session types persist as strings, not ints.
- `ring_session_manager_itf.{xml,h}`: hand-ported together — `TSessionType` gains `st_dynworld` inserted BEFORE `st_mainland` (enum + `nb_enum_items` 4→5 + index table 3/4 + string table; st_mainland's numeric value shifts 3→4 as the roadmap cross-cutting risk 2 intends; SU DB stores session types as strings (`_SessionType.toString()`), so existing DB rows are unaffected; wire serialization is only between programs from this branch); `JDW` web invoke `joinDynamicWorld(charId, sessionId, clientApplication)` added to the `.xml` (after the `joinSessionResultExt` return block) and mirrored in the hand-managed `.h` (callback table entry before `JML`, `cb_joinDynamicWorld` skeleton before `cb_joinMainland` with legacy `NULL`→`nullptr` per current file convention, pure virtual `on_joinDynamicWorld`, and the `CRingSessionManagerWebClient::joinDynamicWorld` wrapper). XSLT skip-item verified skippable: the current `generate_module_interface.xslt` already emits the AGPL banner (with `encoding="UTF-8"` vs legacy `ascii` — current repo modernization wins) and the generated/header files already carry it.
- Required companions (U-S4 slivers pulled forward): the new pure virtual made the two concrete web-itf implementers abstract, so their legacy `on_joinDynamicWorld` implementations had to land in this unit — SU `ring_session_manager.cpp` (verbatim legacy: a single `_joinSessionCommon(from, charId, sessionId, clientApplication, false)` call placed after `on_joinMainland`) and SBS `session_browser_server.cpp` (verbatim legacy: auth-check + forward to SU via `CRingSessionManagerWebClientItf::joinDynamicWorld`). No other subclasses exist (`simulation_service` has only comments). Remaining U-S4 SU scope: the `joinSessionIntoSession` restructure into `switch(sessionType)` with the `st_dynworld` case.

Validation: Release builds all clean — `ryzom_shard_unifier_service`, `ryzom_session_browser_service` (current target name; exe `ryzom_session_browser_service.exe`), `ryzom_gpm_service` regression, plus EGS (`ryzom_entities_game_service`) and the full `ryzom_client` as canaries for the msg header changes. `client_drone` is not in the current configure — `simulated_client.cpp` is text-verified until the client tools build catches up.

#### U-S2 — server_share continent containers

- [x] Done (2026-10-22). `r2_variables.{h,cpp}`: added the legacy `CVariable<bool> IsDynamicContinent` (extern + definition) right after `IsRingShard`. `continent_container.h`: only the dynworld delta — re-added the `class CMapServer;` forward declaration, `CMapServer *Map` member + ctor init on `CContinentMoveContainer`, and the `getDynMap(sint index)` accessor (legacy style/comment). The "move `findContinent` to the `.cpp`" option was NOT taken: the current repo's inline version works and the decision flags it optional. Everything else of the legacy diff is repo modernization drift (public `CSheet`, by-value `loadContinent`, `toLowerAscii`, inlined `findContinent`) and was preserved as-is. `continent_container.cpp` needs NO change. New `server_share/dynworld/dyn_continent_container.{h,cpp}` copied verbatim from the legacy working copy (read-only flag cleared; kept `../stdpch.h`, kept the wrong trailing `/* End of continent_container.h */` comment for fidelity, same style as the earlier dyn-adapter ports). `server_share/CMakeLists.txt`: added `FILE(GLOB DYNWORLD dynworld/*.cpp dynworld/*.h)` + `SOURCE_GROUP("dynworld")` + `${DYNWORLD}` appended to `NL_TARGET_LIB(ryzom_servershare ...)`. Verified before copy: ported `CDynRetrieverBank(bool)`, `CDynGlobalRetriever(CDynRetrieverBank*, CMapBase*)`, `CDynMoveContainer(GR, 16, 16, ...)` signatures match, and `CContinentContainer`'s protected `_GridWidth/_GridHeight/_CellSize/_PrimitiveMaxSize/_NbWorldImages` members exist in the current header. NOTE (kept faithful, flagged here): base `removeContinent()` deletes rbank/gr/move-container but not `Map` — a `CDyn` continent removed at runtime leaks its `CMapServer` (legacy had the same hole; the legacy dyn path does not override `removeContinent`). POLYVOX include dir is already directory-scoped into the target (`INCLUDE_DIRECTORIES(${POLYVOX_INCLUDE_DIR})`), so no addition was needed.

Dependencies: U-N3. Validation: `ryzom_servershare` builds clean (including `dyn_continent_container.cpp`); `ryzom_gpm_service` consumer regression builds clean.

#### U-S3 — gpm2_service (fork of the CURRENT gpm_service, not of legacy)

- [x] Done (2026-10-22). Copied the current `ryzom/server/src/gpm_service` → `ryzom/server/src/gpm2_service` (read-only cleared; nothing changed in gpm_service), then applied the legacy fork deltas (legacy `gpm_service` vs legacy `gpm2_service`; here expressed against the current clone, which carries its own modernization — the only intersection needed re-judging where current text differs from legacy-gpm text, and none of the deltas line collided): (1) `gpm_service.cpp` — removed the `cbEVSConnection`/`cbEVSDisconnection` callbacks and their `EVS` up/down callback registrations and dropped `extern bool EVSUp;` / its definition (`EVSUp` has no remaining references in gpm2); added `#include "game_share/dynworld/material_library.h"` and `CMaterialLibrary::instance().initialize("material_library.cfg")` right before `} // init //` after the RingVisionDeltaManager/MoveChecker block; `NLNET_SERVICE_MAIN` now names `gpm2_service` (service id string stays `"GPMS"`). (2) `world_position_manager.{h,cpp}` — `#include "server_share/dynworld/dyn_continent_container.h"` replacing `continent_container.h`, `#include "game_share/dynworld/voxel.h"` (+ `map_server.h` in the .cpp), static `CDynContinentContainer _ContinentContainer` (with `// DYN WORLD` comments), patat stubs exactly as legacy: `_ContinentContainer.initPacsPrim()` commented inside both `initPacsPrim()` bodies, `_PatatSubscribeManager.init()` commented in `initPatatManager()`, the whole `loadPatatsInFile` body commented (legacy `// ` style), and ONLY the `updateEntityPosition(CWorldEntity*)` overload's `getNewEntryIndex` line commented (the `(entity, cell)` overload keeps its call — legacy kept it too, don't "fix" later); new `static void setVoxel(const TVoxelPos&, Voxel)` declared (header, after `displayVisionCells`) and implemented at the .cpp end (continent lookup via `findContinent(worldPos)` then `CMapServer::setVoxelAt` — `worldPos *= .5f` kept). (3) `messages.cpp` — `#include "game_share/dynworld/voxel.h"`, `void cbSetVoxel(NLNET::CMessage&, ...)` after `cbR2ForceVisionReset` (serial `TVoxelPos` + `Voxel`, then `CWorldPositionManager::setVoxel`), and `{ "SET_VOXEL", cbSetVoxel }` placed behind `R2_VISION_REFRESH` in `CbGPMSArray` with the `// DYN_WORLD stuff` comment. (4) `world_entity.cpp` `&&`→`||` bot-check bug NOT ported (skip list). Legacy whitespace-only churn in `client_messages.cpp`/`cell.h`/`world_entity.h` and the stray `world_position_manager.cpp.rej` in the legacy tree were NOT ported. (5) `world_position_manager.h`'s `computeCellVision` whitespace churn skipped.
- CMake: `gpm2_service/CMakeLists.txt` rewritten from the CURRENT gpm_service conventions (target-based `ryzom_gpm2_service WIN32`, `Ryzom::gpmsheets` exclusion of `sheets.cpp/.h`, same `target_link_libraries` set, `target_precompile_headers(stdpch.h)`, install sbin/services) + `ADD_SUBDIRECTORY(gpm2_service)` in `ryzom/server/src/CMakeLists.txt`. **PolyVox was needed after all** (`map_server.h` → `map.h` → `polyvox_defs.h` are compiled into `world_position_manager.obj`; servershare's `INCLUDE_DIRECTORIES(${POLYVOX_INCLUDE_DIR})` is directory-scoped and does NOT propagate): added `${POLYVOX_INCLUDE_DIR}` to the target's `TARGET_INCLUDE_DIRECTORIES`, exactly like legacy.
- **ANL dependency made linkable (first linker consumption)**: linking gpm2 pulled gameshare's `mapgen.obj`/`biome_manager.obj` through the `CMapServer`/pager chain, surfacing that gameshare never linked ANL (legacy gameshare linked `${ANL_LIBRARIES}` PUBLIC-wide). `CMakeModules/FindANL.cmake` extended to discover `ANL_LIBRARY`: find_library over the ANL checkout's in-tree build layouts, `Build_2017` preferred over the stale/partial `Build` tree (the `Build` `Noise.lib` lacks `implicitbasisfunction.obj` outright), names `Noise` tried before `anl` (the Module target's `anl.lib` does NOT contain the noise classes), a plain fallback `find_library(ANL_LIBRARY anl)` for installed ANLs; CRT verified compatible (MultiThreadedDLL/MultiThreadedDebugDLL both sides). `${ANL_LIBRARY}` is now linked `PUBLIC` from `ryzom_gameshare` (mirrors legacy propagation); expect benign LNK4099 PDB warnings. NOTE if ANL discovery misbehaves later: purge `ANL_LIBRARY:*` lines from `build\CMakeCache.txt` — cached values survive module edits.

Dependencies: U-S2, ported `material_library`. Validation: `ryzom_gpm2_service` builds AND LINKS clean in Release (`build\bin\Release\ryzom_gpm2_service.exe`); `ryzom_gpm_service` and `ryzom_shard_unifier_service` relink clean after the gameshare/ANL interface change. Runtime smoke (gpm2_service.cfg + used_continents.cfg mirror sync/continent logs) deferred to U-S5 validation as planned.

#### U-S4 — dynworld session flow in services

- [x] Done (2026-10-22).

- EGS `client_messages.cpp`: added `#include "game_share/dynworld/voxel.h"` after `server_share/r2_vision.h` (legacy patch position verbatim), the `CLIENT:DYN_WORLD` section with `void cbClientSetVoxel(NLNET::CMessage&, const std::string&, NLNET::TServiceId)` before the `// CbClientArray` marker (serials `CEntityId`/`TVoxelPos`/`Voxel`; `CEntityBaseManager::getEntityBasePtr` — the current file's own convention at lines ~124/149; `canEntityUseAction()` guard; the legacy typo `"<EntityCallbacks::cbDefaultAttack>"` nlwarning label kept verbatim for fidelity; relays `CMessage("SET_VOXEL")` via `sendMessageViaMirror("GPMS", ...)`), and the `{ "CLIENT:DYN_WORLD:SET_VOXEL", cbClientSetVoxel }` routing entry after `CLIENT:NPC_ICON:GET_DESC` (with the legacy blank line). No rename drift collision: current EGS also names it `CEntityBaseManager`.
- EGS `character.cpp`: the U-S1 placeholder `false /* isInDynWorldSession */` in the `CUserCharMsg::write(...)` call replaced with `IsDynamicContinent.get()` — matches legacy patch line 82873 exactly. `IsDynamicContinent` resolves through character.cpp's transitive include of `server_share/r2_variables.h` via `r2_vision.h` (no new include added).
- SU `ring_session_manager.cpp`: `_joinSessionCommon`'s `if (session->getSessionType() != TSessionType::st_mainland) {...ring...} else {...mainland...}` restructured into `switch (session->getSessionType().getValue())` with `default:` (ring session block), `case TSessionType::st_mainland:` (mainland block) and `case TSessionType::st_dynworld:` (verbatim copy of the mainland case incl. its `// join a mainland session` comment), each case braced + `break;`, per legacy. Body texts preserved from the CURRENT file — in particular the current repo's split "Shard is down"/"Shard is restricted to %s access" access checks (merge-into-"Welcome service closed" is legacy-base text the later mainline already replaced). The legacy patch's concurrent `againstu`→`against` typo fix in `on_joinMainland` and its EOF blank-line removal were skipped as unrelated churn. Applied via scripted re-indentation on exact anchors; nothing else in the file touched.
- SBS: `on_joinDynamicWorld` already landed in U-S1 — nothing to do.
- `shard_start_cmake.bat`/`shard_stop_cmake.bat`: **SKIPPED — the files do not exist in the current repo** (`ryzom/server/` contains only `CMakeLists.txt`; those legacy launch scripts were already removed upstream, and the legacy patch's BUILD/TARGET parameterization block is the fork author's local-env tooling). The gpm_service→gpm2_service binary switch, if a local launcher is recreated later, is one line (`ryzom_gpm2_service.exe`); the SBS binary name today is `ryzom_session_browser_service`.

Dependencies: U-S1, U-S3. Validation: `ryzom_shard_unifier_service` (with the restructure) and `ryzom_entities_game_service` (with the callback + flag wiring) build clean in Release; SBS untouched. Runtime integration test of a shard join onto a dss-less dynamic-continent session deferred to Phase 5 validation.

#### U-S5 — server configuration

- [x] Done (2026-10-22, adapted): **the current repo ships no Ryzom server cfg set** (`ryzom/server/` contains only `CMakeLists.txt`; `used_continents.cfg`, `gpm_service.cfg`, `common.cfg`, etc. are assembled at deployment time and were already absent upstream — verified, no `*.cfg` matches anywhere from the repo root). Importing the legacy tree's 40-file cfg set wholesale would violate the "delta only" rule, so no cfg files were added. The three dynworld cfg deltas are recorded here verbatim for any future deployment/runtime validation (they apply to the legacy cfg set in `D:\Programmation\ryzomcore_old\ryzomcore_dev\code\ryzom\server\`, which carries `gpm2_service.cfg`/`gpm2_service_default.cfg` ready-made):

  - `used_continents.cfg` — appended after the `"newbieland", "20"` block:
    ```cfg
    IsDynamicContinent = 1;
    ```
  - `gpm_service.cfg` — appended after `VerboseMIRROR = 0;`:
    ```cfg
    LoadPacsCol = 0;
    ```
  - `gpm2_service.cfg`/`gpm2_service_default.cfg` — new files (legacy working copy, minus env noise): default = `#include "common.cfg"`, `CheckPlayerSpeed = 0;`, `SecuritySpeedFactor = 1.5;`, `LoadPacsPrims = 0;`, `LoadPacsCol = 1;`; main cfg = comment header `// Use with commandline: gpm_service -C. -L. --nobreak --writepid`, `#include "gpm2_service_default.cfg"`, `AESAliasName = "gpms"` (same mirror id as gpm), the five `GraphVars += { ... }` mirror lines, `Paths = { ".", "../common/data_common", "../common/data_leveldesign" }` (legacy's extra `"../../../../ryzomcore-data"` line = author-local env noise, skipped per skip list), `#include "used_continents.cfg"`, mirror limits `DatasetSizefe_temp = 600000;` / `DatasetSizefame = 26000;`, `IsRingShard = 0;`, `VerboseMIRROR = 0;`, `LoadPacsCol = 1;`.
  - Remaining legacy cfg diffs to NOT port (skip list): the env-noise `ryzomcore-data`/`../../leveldesign` relative-path additions in `ai`/`backup`/`dss`/`egs`/`fs`/`ios`/`logger`/`mirror`/`monitor`/`tick` cfgs, `DBHost`/`FSListenHost` → `127.0.0.1`, `ReadWorkOnly = 1`.

Dependencies: U-S3. Validation: n/a for building (no code touched); the Phase 5 runtime smoke needs these three deltas applied to a deployed cfg set.

Validation: with U-S3 smoke test.

### 4. Client rendering and runtime — planned as units U-C1 … U-C8

#### U-C1 — client scaffolding

- [x] Done (2026-10-22). `client_cfg.h`: added the `ChunksMemoryCache` `uint32` member in a `// DYNWORLD //` block after `BackgroundDownloader` (legacy position); `client_cfg.cpp`: ctor default `ChunksMemoryCache = 1024U; // 1 Gb by default` and `READ_INT_FV(ChunksMemoryCache)` after the commented-out `READ_BOOL_FV(BackgroundDownloader)` (macro exists, already used for `SelectedSlot`). Skipped legacy whitespace noise (blank line after `LoginSupportURL`). `net_manager.cpp` needed nothing more — `IsInDynWorldSession` global + `CUserCharMsg::read` feed landed with U-S1; client consumers will `extern` it as they appear in U-C2+. Client `CMakeLists.txt`: `FILE(GLOB SRC_DYNWORLD dynworld/*.cpp dynworld/*.h)` + `SOURCE_GROUP("dynworld" ...)` + `target_sources(ryzom_client PRIVATE ${SRC_DYNWORLD})` (target-based, matching this CMakeLists' idiom; empty glob OK).
- **PolyVox/ANL interface fix**: the roadmap-flagged payoff order was real — `${POLYVOX_INCLUDE_DIR}`/`${ANL_INCLUDE_DIR}` were directory-scoped includes inside `game_share/CMakeLists.txt`, invisible to consumers (`map_client.cpp` in U-C2 will include PolyVox directly). Promoted to `TARGET_INCLUDE_DIRECTORIES(ryzom_gameshare PUBLIC ${POLYVOX_INCLUDE_DIR} ${ANL_INCLUDE_DIR})`; removed the now-redundant explicit `${POLYVOX_INCLUDE_DIR}` from `gpm2_service`'s target include dirs (comment left in the file explaining the propagation).

Dependencies: U-S1 (for the flag definition). Validation: client builds with an (empty) `src/dynworld` glob; `ryzom_gpm2_service` relink clean with the include propagation change.

#### U-C2 — core client dyn modules

- [x] Done (2026-10-22). Ported `map_client.{h,cpp}`, `region_mesh.{h,cpp}`, `dyn_continent.{h,cpp}` verbatim from legacy `code/ryzom/client/src/dynworld/` into `ryzom/client/src/dynworld/` (incl. the legacy `#include "stdpch.h"` pattern — compiles through the client's `target_precompile_headers(stdpch.h)` + `src` include dir). Adaptations:
  - `dyn_continent.cpp` `CChangeTaskPriority`: stripped the `if (_Next) return _Next->getTaskPriority(...)` forwarding — the current NeL `CTaskManager::IChangeTaskPriority` interface dropped the `_Next` chain member (legacy dead code: `_Next` was never linked); registration call site (`CAsyncFileManager::getInstance().registerTaskPriorityCallback`) untouched.
  - `dyn_continent.cpp` `pauseBGDownloader()` call wrapped in `#ifdef RYZOM_BG_DOWNLOADER` (legacy declared it unconditionally; current mainline guards the decl in `bg_downloader_access.h` behind that define, which is set nowhere in the tree — the bg downloader is dead code here); did NOT include `bg_downloader_access.h` (its `game_share/bnp_patch.h` chain drags sevenzip references into every TU; the guarded call compiles out anyway).
  - Build issue encountered after adding the files: the client links gameshare's dynworld objects for the first time (`map_cache_manager`/`master_pager`/`slave_pager` reference the sevenzip wrapper), colliding with `nel_sevenzip_r.lib` — see the sevenzip unification below — plus `login_patch.cpp` already pinned the NeL SDK side. Also removed the client's direct `NeL::sevenzip` link entry (it arrives through `NeL::misc`'s private static-forward anyway); consumers now resolve a single SDK.
- **Sevenzip SDK unification (`ryzom/common/src/seven_zip`)**: the U-N port had built the fork's vendored 7z SDK copy alongside mainline's `nel/3rdparty/seven_zip` (`NeL::sevenzip`, used by `NLMISC::unpackLZMA`/`unpack7Zip` via `login_patch.cpp`). Both define the identical upstream C symbols (`7zStream.obj` et al.), so any binary pulling both wrappers hit LNK2005. Legacy never had this clash (legacy NeL shipped no SDK at all — the wrapper dir WAS the only one). Fix: `ryzom_sevenzip` now compiles just the legacy wrapper (`seven_zip.{cpp,h}`) on top of `NeL::sevenzip` (PUBLIC link); the vendored SDK sources (~60 files) deleted; wrapper adapted to the 18.05 SDK API (`CLookToRead`→`CLookToRead2` with caller-provided look buffer, `lookStream.s`→`.vt`, `const ISeekInStream*` callback signatures); consumers resolve `seven_zip/7zCrc.h` etc. through `ryzom_sevenzip`'s PUBLIC `nel/3rdparty` include dir (`seven_zip/seven_zip.h` still resolves via gameshare's `ryzom/common/src` private dir). Single SDK per binary from now on. `game_share/CMakeLists.txt` still carries its older `FIND_PACKAGE(NeL REQUIRED COMPONENTS sevenzip)` include-dir lines — stale but harmless, left alone.

Dependencies: U-C1; U-N3 indirectly (its globals). Validation: `ryzom_client`, `ryzom_gpm2_service`, `ryzom_entities_game_service`, `ryzom_shard_unifier_service`, `ryzom_session_browser_service` all relink clean in Release; `dyn_continent.obj`/`map_client.obj`/`region_mesh.obj` present in the client build. Sanity of voxel picking once U-C7 debug commands land.

#### U-C3 — landscape stack (new client modules)

- [x] Done (2026-10-22). Ported all five modules verbatim from legacy `code/ryzom/client/src/dynworld/` (in the dependency order of the unit, but as one cohesive build: `dyn_landscape_model` + `dyn_landscape` first, then `dyn_landscape_user` (1635 lines), `dyn_landscapeig_manager`, `dyn_instance_group_user`; `dyn_landscape.cpp` in fact includes `dynworld/dyn_landscape_user.h`, so the set is atomic). Adaptations:
  - `CDynLandscape::render()` signature extended with the mainline-added trailing `bool doVegetables = true` param (current `CLandscape::render` overload; `(void)doVegetables;` in the impl — the dynworld renderer replaces the whole render path, vegetables included).
  - `nel/include/nel/3d/landscape.h`: the `_Driver`/VB-allocator data section changed `private:` → `protected:` with a NOTE(dynworld) comment — legacy base had it protected and `CDynLandscape` manipulates `_Driver`/`_Far0VB`/...directly; U-N4 already made `clear()`/`setDriver()` virtual in this header, so this continues that pattern.
  - PIX profiling: the fork's `NL_PROF`/`NL_PROF_F` macros are PIX tooling (skip list) and don't exist in the current `hierarchical_timer.h`; the 6 call sites in `dyn_landscapeig_manager.cpp`/`dyn_landscape_user.cpp` commented out with `//NLMISC-PIX-STRIPPED` (same treatment as the U-N-tagged `NL_PROF_F` in `map.cpp`).

Dependencies: U-N4, U-N5, U-C2. Validation: full `ryzom_client` build in Release — all six `dyn_*.obj` compiled and linked (`dyn_landscape_model`, `dyn_landscape`, `dyn_landscape_user`, `dyn_landscapeig_manager`, `dyn_instance_group_user`, plus U-C2's). Shader-asset loading (`dyn_landscape.vp1/.arbfp1`) is deferred to U-C8 code paths; headless init smoke test if feasible later.

#### U-C4 — session/flow integration

- [x] Done (2026-10-22). Files and deltas:
  - `continent.{h,cpp}`: `setup`/`select`/`unselect` → `virtual` (for `CDynContinent` to override; U-C2 base); dtor `registerTaskPriorityCallback(nullptr)` → `unregisterTaskPriorityCallback(&ChangeTaskPriority)` (both methods live on `CAsyncFileManager : CTaskManager`). `CChangeTaskPriority::getTaskPriority` already returned `currentPriority` in mainline (legacy `_Next` chain forwarding is dead code there) — no edit needed.
  - `continent_manager.{h,cpp}`: `CDynContinent` fwd decl + `preloadDynContinent()` decl + `_Dynamic` member; in cpp: `dynworld/dyn_continent.h` include, `extern bool IsInDynWorldSession;`, call `preloadDynContinent()` at the end of `preloadSheets()` (the "TODO DYN: temp" `dynamic` continent cloned from the *newbieland* sheet selection name), and the dynworld routing at the top of `select(const CVectorD &pos, ...)`.
  - `net_manager.cpp`: `bool IsInDynWorldSession = false;` (the read-side was already wired through `CUserCharMsg` incl. the game_share bit-3 field and the EGS `IsDynamicContinent` write side from U-N/U-S units).
  - `init_main_loop.cpp`: `nel/3d/scene_user.h` + dynworld + `game_share/dynworld/material_library.h` includes; `extern bool IsInDynWorldSession;`; `CMaterialLibrary::instance().initialize("material_library.cfg")` at the end of the XML-db init block; landscape creation goes through `ULandscape* newLand = NULL; if (IsInDynWorldSession) { CDynLandscapeModel::registerBasic(); newLand = new CDynLandscapeUser(Scene); } Landscape = Scene->createLandscape(newLand);` — U-N4 `createLandscape(ULandscape* = NULL)` signature, mainline call sites unchanged.
  - `far_tp.{h,cpp}`: `JoinDynWorld` mode (enum after `JoinMainland`, `_URLTable` keeps 3 initializers like legacy); shadowing `TSessionId sessionId(0)` removed from `JoinMainland` case (function param used by the new case); `JoinDynWorld` case calls `sb.joinDynamicWorld(charId, sessionId, ClientApp)` — SU/`ring_session_manager_itf` side already in place from U-S2/S3 (`st_dynworld` session type, JDW proxy + `on_joinDynamicWorld`).
  - `entity_cl.cpp`: `bool g_noSnap = true;` before `snapToGround()` + `if (g_noSnap) needSnap = false;` (no PACS ground-snap inside dynamic worlds; toggled by a U-C7 command).
- Adaptations/skips: the patch's comment-spelling hunks (`persitent`→`persistent`, `poiner`→`pointer`, `sheet_manager.h` grammar) and three pure whitespace/reindent hunks in `init_main_loop.cpp` skipped per the skip list. Legacy `sheet_manager.h` "plant sheet data exposure" turned out to be that grammar-only hunk — the plant data itself was already public from U-N5 groundwork.

Dependencies: U-C3, U-S1, U-S4. Validation: `ryzom_client` Release build clean. Runtime shard-join flow validation deferred to Phase 5.

#### U-C5 — PACS hookup

- [x] Done (2026-10-22). `pacs_client.cpp`: dyn PACS includes (`dynworld/dyn_continent.h`, `dynworld/map_client.h`, `game_share/dynworld/dyn_{retriever_bank,global_retriever,move_container}.h` + `nel/pacs` C-level headers); `extern bool IsInDynWorldSession;`; early branch in `initPACS()` — `RB = new CDynRetrieverBank(false)`, `GR = new CDynGlobalRetriever((CDynRetrieverBank*)RB, CDynContinent::MAP)` + `initAll(false)` ("don't init instances as we serialized them"), `PACS = new CDynMoveContainer(((CDynGlobalRetriever*)GR), 16, 16, RYZOM_ENTITY_SIZE_MAX, 2, NELPACS_DEFAULT_MAX_TEST_ITERATION, NELPACS_DEFAULT_OT_SIZE)`, `return`. The classes ported with U-S3 match the legacy ctor/param shapes exactly (`initAll` is public `CGlobalRetriever` in current NeL); interface upcasts (`URetrieverBank*`/`UGlobalRetriever*`/`UMoveContainer*`) stay valid. `ig_callback.cpp`: `extern bool IsInDynWorldSession;` + last-arg `!IsInDynWorldSession` in the `addCollisionnablePrimitiveBlock` call (dynworld IG prims are inserted but not marked collisionnable).

Dependencies: U-N3, U-C4. Validation: `ryzom_client` Release build clean. Runtime collision behavior deferred to Phase 5.

#### U-C6 — interaction/UI

- [x] Done (2026-10-22). Cross-cutting click-parameter change (`bool rightClick` → `NLMISC::TMouseButton`, adds middle-button support) applied to: `contextual_cursor.{h,cpp}` (`TFunc2` typedef + `execute()`), `cursor_functions.cpp` (all `contextXxx` decls/defs + `r2`-editor routing), `motion/user_controls.{h,cpp}` (`execActionCursorPos` + `_MiddleClickStart/_MiddleClickEnd` members + middle dbl-click tracking + middle-release action hook), `motion/modes/ai_mode.cpp` (same middle-click tracking + call sites), `r2/editor.{h,cpp}` (`CEditor::mouseClick` → `button == leftButton` / `else if (button == rightButton)`.).
- `cursor_functions.cpp`: dynworld includes, "VOXEL" context (`uimGcmExtractRM` cursor texture), `CDynContinent::MAP->unselectVoxel()` reset in `checkUnderCursor`, and the voxel-under-cursor selection branch (r2-instance else path) with a `CMaterialLibrary` name + density readout; new `contextSetVoxel` + module-local `Voxel::DensityType g_voxelFillSpeed = 255` (left = fill/raise density, middle = carve to Air; density ops on `CDynContinent::MAP`, TODO markers for deferred commands as in legacy).
- Adaptations: legacy `ucstring` cursor name → `std::string` (mainline `CContextualCursor::context` exposes `const std::string &cursName`); mainline's `CContextualCursor::execute` needed the same treatment in user_controls' `dblClickLeft` reuse for the middle click (legacy had renamed nothing there). `contextARKitect` doesn't exist in current (mainline removed it; signature change N/A). Patch comment-typo hunks (`poiner`, `persitent`, `nex`) skipped per skip list.

Dependencies: U-C4 (for `CDynContinent::MAP`). Validation: `ryzom_client` Release build clean. Manual UI test deferred (needs a dynworld session).

#### U-C7 — debug tooling

- [x] Done (2026-10-22). `commands.cpp`: dynworld + material_library includes and the `#if !FINAL_VERSION` block of 19 `NLMISC_COMMAND(dyn_*)` debug commands (`dyn_regenvoxels`, `dyn_regenmesh`, `dyn_prefetchextents`, `dyn_floorheight`, `dyn_cubicgen`, `dyn_renderoldlandscape`, `dyn_refreshpos`, `dyn_snap` → U-C4 `g_noSnap`, `dyn_baselod`/`dyn_maxlod`/`dyn_fardist`, `dyn_setvoxel`/`dyn_addvoxel` (with the `DYN_WORLD:SET_VOXEL` impulse, server handler already in place from U-S4), `dyn_voxelselection`, `dyn_rebuildshaders`, `dyn_useVP`/`dyn_usePP`/`dyn_reloadenvconfig`, `dyn_igdist`); all `extern` globals resolve (map_client/dyn_landscape* client modules from U-C3 + `dyn_global_retriever.cpp`/`mapgen.cpp` from the U-S ports). Legacy `#pragma optimize("", off)` skipped per roadmap note.
- ShowInfos cycle `%6` → `%7` + updated extern-comment in `debug_client.cpp`; `main_loop.cpp` calls the new `displayDebugGeneration()` on level 6.
- `main_loop_debug.{h,cpp}`: `displayDebugGeneration()` added — **U-M2 decision: strip**. The `#ifdef NL_RUNNABLE_STATS` / `CTasksStats` runnable-stats block is not ported (the define is set nowhere in the current tree; sole consumer was this dump block; same dead-code reasoning as `RYZOM_BG_DOWNLOADER` in U-C2) — replaced by a `NLMISC-PIX-STRIPPED` note; the `<nel/misc/task_manager.h>` include wasn't needed. U-M2 port remains available on request if runnable-stats profiling ever becomes useful.

Dependencies: U-C2/U-C4/U-C6 (commands touch live map state). Validation: `ryzom_client` Release build clean; run the dyn_* commands locally deferred to Phase 5.

`commands.cpp`: ~20 `NLMISC_COMMAND(dyn_*)` debug commands including `dyn_setvoxel`/`dyn_addvoxel` → `DYN_WORLD:SET_VOXEL` impulse (skip the legacy `#pragma optimize("", off)` suggestion — incidental). `debug_client.cpp` + `interface_v3/action_handler_debug.cpp`: ShowInfos cycle `%6` → `%7`. `main_loop_debug.{h,cpp}`: `displayDebugGeneration()` — decide: port U-M2 stats stack, or strip the `CTasksStats` block from this function.

Dependencies: U-C2/U-C4 (commands touch live map state). Validation: build; run the dyn_* commands locally.

#### U-C8 — shaders and dynworld data assets

- [ ] Copy.

- `ryzom/client/data/shaders/dyn_landscape.vp1` + `dyn_landscape.arbfp1` ONLY (see Working-context note: the `.cg` pair is absent at the legacy tip). Loaded via `CPath::lookup("dyn_landscape")` + `CIFile::serialBuffer` — verify the client search path already covers `data/shaders` per the existing convention.
- `ryzom/common/data_leveldesign/dynworld/`: `material_library.cfg` (Environments = forest/jungle/desert/lakes/prime_roots + 9 material cfgs), `environments/*.cfg` (`Name`/`Id`/`DefaultSoil`/`Seed`/`WaterLevel`/`Flags`/`globalOffset`/`NP_*` + flora plant sheet-ID lists), `materials/*.cfg` (`Type` `Raw|Refined`, `DensityMin/Max`, `Textures`/"Env:X"-prefixed tile names per environment). Resolution uses `CConfigFile::load(name, true)`/`CPath::lookup` → make `data_leveldesign/dynworld` discoverable for both client and gpm2 service. IMPORTANT: flora plant sheets must be resident before `CMaterialLibrary::initialize()` runs.

Dependencies: none strictly (safe after U-C3/U-C4 for validation). Validation: client/gpm2 startup without missing-file errors; env config load logs.

### 5. Assets, configuration, and end-to-end validation

- [ ] CI-relevant builds + tests: `nel misc/3d/pacs` (+ unit tests), `Ryzom::gameshare`, `Ryzom::servershare`, `ryzom_gpm2_service`, EGS, SU, SBS, `ryzom_client`.
- [ ] Local shard runtime: FS on `127.0.0.1`, `gpm2_service` + `used_continents.cfg` (`IsDynamicContinent = 1`), join a dynamic-continent session via the SU/SBS `joinDynamicWorld` web path.
- [ ] Client round-trip: console `dyn_setvoxel` → EGS (`CLIENT:DYN_WORLD:SET_VOXEL`) → GPMS (`SET_VOXEL`) → `CMapServer::setVoxelAt()`; verify persistence through `map_cache_manager`/`slave_pager`.
- [ ] Render smoke test: `g_rebuildShaders = 1`, `g_useVP = 1`, `g_usePP = 1` vs `g_renderLegacyLandscape = 1` contrast; ShowInfos 6 for generation stats (if U-M2 ported).
- [ ] Regression: legacy (non-dyn) landscape sessions unchanged after U-N4's landscape pointer rewrite.

## Cross-cutting risks and decisions

1. **U-N4 pointer rewrite ripple** — public API break in a hot 3D path (~50 deref rewrites + the virtual render call graph); the 13 stale call sites (`paint.cpp` ×12, `decal.cpp` ×1) are the trap. Same-commit migration mandatory.
2. **`st_dynworld` enum ordering** — inserting before `st_mainland` shifts its numeric value; wire/DB-visible. Both ends must change in the same unit (U-S1), and nothing in between may rely on old values. U-S1 landed it: session types persist in the ring DB as strings (`_SessionType.toString()`), so existing rows survive; the raw enum only crosses the wire between programs from this branch.
3. **`sleepTask` retiming (U-M1)** — global throughput change for every `CTaskManager` user, not just dynworld paging.
4. **PolyVox header visibility in the client target** — the first thing to break when U-C1/U-C2 land; fix the interface explicitly rather than with include-dir leaks.
5. **Master pager cache path commented out** — decide whether master-side regeneration is intentional when U-S2/S3 connect `CMapServer` cluster-wide.
6. **InstanceId widening** — watch narrowing warnings; no serialization format change (serial commented out; `CollisionInstances` is a per-call temp).
7. **Flora plant sheet availability** — configs hold numeric plant sheet IDs; sheets must exist in data before `CMaterialLibrary::initialize()` (U-C8 + U-S3 init order).

## Skip list (legacy noise — do not port)

- **PIX (project rule):** `FindPix.cmake`, all `NL_PROF*`/`NL_USE_PIX` code, `WITH_PIX` option, PIX links in `nel/src/misc/CMakeLists.txt`.
- **nel/misc already-present or obsolete:** `system_info` `getNumHWThreads()` (ported parallel_task_manager calls `std::thread::hardware_concurrency()` directly); `CFastReaderWriter` was SKIPPED first and then ported as U-M3 on user request (see that section); `string_common.h`/`variable.h`/`algo.cpp` cosmetic churn; legacy ms-flags in `nel.cmake` (MSVC12 flags, `-std=c++14`, `/Zm` removals — modern target-based C++17 config is authoritative); `FindExternal.cmake` blank-line no-op.
- **NeL 3D comment-only:** `material.h`, `u_instance_group.h`, `vegetable_manager.h`, `zone_search.h`, `landscape.cpp`, `zone.cpp`, dead `friend class CTransformUser` in `instance_group_user.h`.
- **NeL PACS obsolete:** `global_retriever.cpp`, `local_retriever.{h,cpp}` (`__Tips` → `_Tips` hygiene; the `_Tips` refs in current .cpp sit in dead commented blocks), `move_container.cpp`, `retriever_instance.{h,cpp}` (typo + dead-code deletion), `u_move_container.h`, and the `primitive_world_image.{h,cpp}` 3D-bbox addition (defined but never read anywhere in the legacy tree).
- **server:** `frontend_service` `nlSleep(0)`→`nlSleep(1)`; `ai_service` whitespace/comment/`<functional>`; `gpm_service` comment/typo cleanups + `WPM_VISION_CELL_CHECK_ON_UPDATE` guard + `PLAYER_POSITION_HISTORY_MAX_SIZE` constant; EGS `guild_inv.cpp`/`outpost_squad.cpp`/`shard_unifier_client.cpp`/`entity_callbacks.cpp`/`character_inventory_manipulation.cpp` include-shuffles and typos; EGS/IOS PCH gutting; `/bigobj` (already present in current CMake); `msg.xml` `DISABLE_LOGOUT_BUTTON` rename; `generate_module_interface.xslt` banner (verify once — skip if current generated files already carry it); cfg path/SQL/listen-host local-environment lines; the gpm2 `world_entity.cpp` `&&`→`||` bot-check bug (active bug — deliberately excluded).
- **client:** legacy client-local `seven_zip` copy (already centralized in `ryzom/common/src/seven_zip` with `NeL::sevenzip`); `landscape_poly_drawer.h` (already present); `commands.cpp` `#pragma optimize("", off)` suggestion.
- **game_share:** `voxel_material_defs.h` (orphan header, zero references in the entire legacy tree); `unqlite_db.{h,cpp}` + `code/ryzom/common/src/unqlite` (defer — only the disabled multi-pass map_cache_manager path references it).