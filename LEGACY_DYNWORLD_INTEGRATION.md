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

## External dependency version notes

- ANL checkout: `D:\Programmation\ANL`, branch `master`, HEAD `28d40d67f6f3e25149f5580da0e6655cc7ee8467` (2015-11-20, `optimize voronoi noise`). License: Permissive (BSD/MIT-like) - verified at https://github.com/JTippetts/accidental-noise-library/blob/master/LICENSE. Compatible with project.
- PolyVox checkout: `D:\Programmation\polyvox`, branch `kishan-dynworld`, commit `c5c63c22db5194029e54dea770d5b10799dbfdd3` (2018-05-01); describes as `v0.2.1-1068-gc5c63c22` and has a clean worktree. `include/PolyVox/PagedVolume.h` identifies the MIT license.

## Remaining integration order

### 1. Shared dynworld foundation — complete

Test harness note: ryzom-side gtest home is `ryzom/tools/server/test_modules/<x>_test/` (convention established upstream by PR #826 "enable ryzom tests"; siblings `egs_test`, `session_browser_test`, `shard_unifier_test`). The Oct-16 dynworld foundation commits added four `add_test_executable` targets there under `game_share_test/` — `ryzom_dynworld_voxel_test` (links `NeL::misc` only; voxel is header-only), `ryzom_dynworld_noise_test`, `ryzom_dynworld_mapgen_params_test`, `ryzom_dynworld_environment_definition_test` (those three link `Ryzom::gameshare`). All four build + pass in Release (10 tests). The Oct-21 test unit added four more: `ryzom_dynworld_map_sector_test` (10 tests; uses the public `s_posToId`/`s_idToPos` — `s_packId`/`s_unpackId` are private, only reachable via the friend `mapsectorUnittest`; `CSectorLod::convertId` only converts mesh-lod → loading; the z packed field range is only ±524287), `ryzom_dynworld_deferred_command_test` (5 tests; teardown must check `isCommandValid` BEFORE `getCommand` — freed slots are reused and `getCommand` dereferences empty slots without any assert in Release), and `ryzom_dynworld_disk_file_db_test` (5 tests; run from the `build/` dir — `CDiskFileDataBase` writes to hardcoded CWD-relative `./cache/map/`, whose parent `CFile::createDirectory` does not create — tests use `createDirectoryTree`). All seven build + pass in Release. New dynworld tests belong in the same directory as their own `add_test_executable` subtarget (do NOT create a separate dynworld test tree); NeL-misc tests stay in `nel/tools/nel_unit_test/ut_*.test.cpp`. Beware: `build/` contains a stale `ryzom_dynworld_polyvox_defs_test.vcxproj` from a never-committed past-CMake definition — harmless build residue, not a real target. `test_dynworld_map_cache.cpp` (CMapCacheManager round-trip) is written and parked on the side, deliberately unregistered: it needs the vendored 7zip SDK port first (see the seven_zip audit correction above); `game_share_test/CMakeLists.txt` currently has no `ryzom_dynworld_map_cache_test` target.

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

- [ ] Port.

- `ryzom/common/src/game_share/msg_client_server.h`: `CUserCharMsg::write/read` gains `isInDynWorldSession` (bit 3; stream width 3→4 bits). Wire-visible: any unpatched client keeps working (extra bit inert).
- `ryzom/common/data_common/msg.xml`: append the `CLIENT:DYN_WORLD:SET_VOXEL` branch (format `s32 s32 s32 u8 u8 u8 u8`, targets EGS). Do NOT port the adjacent `DUMMY` → `DISABLE_LOGOUT_BUTTON` rename (legacy noise).
- `ryzom/common/src/game_share/ring_session_manager_itf.{xml,h}`: add `st_dynworld` to `TSessionType` (insert BEFORE `st_mainland` — the numeric values are DB/wire-visible), extend index/string tables, add the `JDW` web invoke `joinDynamicWorld(charId, sessionId, clientApplication)`. The `.h` is checked into `game_share` (hand-managed, not generated for server_share modules) — hand-port XML + header together in this unit.

Validation: build `Ryzom::servershare` consumers: `ryzom_shard_unifier_service`, `ryzom_session_browser_server`, plus a `ryzom_gpm_service` regression build.

#### U-S2 — server_share continent containers

- [ ] Port.

- `ryzom/server/src/server_share/r2_variables.{h,cpp}`: new `isDynamicContinent`-style global `CVariable<bool> IsDynamicContinent` (+ maybe some lines in the .cpp — follow the legacy pattern).
- `ryzom/server/src/server_share/continent_container.{h,cpp}`: add `CMapServer* Map` on each continent entry + `getDynMap()` accessor; move `findContinent` to the `.cpp` (optional — the current inline version works). Port only the delta: preserve the current repo's modernization (public `CSheet` used by `sheets_dump_shard`, `loadContinent` by-value signature).
- New `ryzom/server/src/server_share/dynworld/dyn_continent_container.{h,cpp}`: `CContinentContainer` subclass that builds `CDynRetrieverBank` + `CMapServer` + `CDynGlobalRetriever` + `CDynMoveContainer` per continent instead of loading `.rbank`/`.gr` files. Keep the `../stdpch.h` include (current server_share still has stdpch semantics).
- `ryzom/server/src/server_share/CMakeLists.txt`: add `FILE(GLOB DYNWORLD dynworld/*.cpp dynworld/*.h)` + `SOURCE_GROUP("dynworld")` + append to the target's sources (the current glob only sees top-level files; PolyVox include dir is already set in that file).

Dependencies: U-N3. Validation: build `Ryzom::servershare`.

#### U-S3 — gpm2_service (fork of the CURRENT gpm_service, not of legacy)

- [ ] Create and wire.

Procedure (the legacy service is 95% identical to the current `gpm_service`, so build it by copying the current one and applying only the functional deltas):

1. Copy `ryzom/server/src/gpm_service` → `ryzom/server/src/gpm2_service` (do not modify `gpm_service`; the two coexist in the build). Registration is `NLNET_SERVICE_MAIN(..., "GPMS", "gpm2_service", ...)` — a drop-in runtime replacement for gpms on dynworld shards, chosen via the shard start/stop scripts.
2. Deltas to apply (from the legacy fork): use `CDynContinentContainer` as `_ContinentContainer` (+ includes of `voxel.h`/`map_server.h`/`dyn_continent_container.h`); stub out static-landscape patat logic (`initPacsPrim()`, `initPatatManager()`, `loadPatatsInFile()`, `_PatatSubscribeManager.getNewEntryIndex()` call paths — commented out in legacy); add `CMaterialLibrary::instance().initialize("material_library.cfg")` in `init()`; add `CWorldPositionManager::setVoxel(TVoxelPos, Voxel)` (continent lookup → `CMapServer::setVoxelAt()`); add `cbSetVoxel` in `messages.cpp` + the `SET_VOXEL` routing-table entry (+8 lines).
3. Do NOT port the legacy bug in `world_entity.cpp`: gpm2 changed the bot check from `(id.getType() >= bot_ai_begin) && (id.getType() <= bot_ai_end)` to `... || ...` (always true). Keep the `&&`.
4. CMake: follow the current `gpm_service` conventions, not the legacy glob — target-namespace links (`Ryzom::adminmodules`, `Ryzom::gpmsheets`, `Ryzom::gameshare`, `Ryzom::servershare`, `NeL::misc`, `NeL::net`, `NeL::pacs`, `NeL::georges`, `NeL::ligo`), `target_precompile_headers`, `sheets.cpp` provided by the shared `Ryzom::gpmsheets` library (exclude it from the service sources). Add `ADD_SUBDIRECTORY(gpm2_service)` to `ryzom/server/src/CMakeLists.txt`. (Legacy additionally had `TARGET_INCLUDE_DIRECTORIES` with `${POLYVOX_INCLUDE_DIR}` — already available via `Ryzom::gameshare`/`servershare` includes; verify.)

Dependencies: U-S2, ported `material_library`. Validation: build `ryzom_gpm2_service`; runtime smoke: launch with `gpm2_service.cfg` + `used_continents.cfg`, confirm mirror sync and `CDynContinentContainer` continent-load logs (requires U-S5 config).

#### U-S4 — dynworld session flow in services

- [ ] Port.

- EGS `ryzom/server/src/entities_game_service/client_messages.cpp`: add the `CLIENT:DYN_WORLD:SET_VOXEL` client callback → mirror/impulse relay `SET_VOXEL` to GPMS.
- EGS `ryzom/server/src/entities_game_service/player_manager/character.cpp`: pass `IsDynamicContinent.get()` into `CUserCharMsg::write`.
- SU `ryzom/server/src/shard_unifier_service/ring_session_manager.cpp`: restructure `joinSessionIntoSession` into `switch(sessionType)`; new `case st_dynworld` behaves like a mainland join (`_joinSessionCommon`); add `on_joinDynamicWorld`.
- SBS `ryzom/server/src/session_browser_server/session_browser_server.cpp`: `on_joinDynamicWorld` web callback → forwards `joinDynamicWorld` to SU.
- `ryzom/server/shard_start_cmake.bat` / `shard_stop_cmake.bat`: pick `ryzom_gpm2_service` instead of `ryzom_gpm_service`. Keep the `session_browser_server` binary name in the stop script (matches the current target name; the legacy rename to `session_browser_service` is stale).

Dependencies: U-S1, U-S3. Validation: build EGS/SU/SBS; integration test of a shard join onto a dss-less dynamic-continent session.

#### U-S5 — server configuration

- [ ] Port.

- New `ryzom/server/gpm2_service.cfg` + `gpm2_service_default.cfg` (`#include used_continents.cfg`, mirrors gpm's structure; per-continent `IsDynamicContinent = 1` line here or in used_continents).
- `ryzom/server/used_continents.cfg`: `IsDynamicContinent = 1` for the dynamic continent entry.
- `ryzom/server/gpm_service.cfg`: `LoadPacsCol = 0` (dynamic continents load no `.rbank`/`.gr`).

Environment noise to skip: `ryzomcore-data`/`../../leveldesign` relative-path additions in ai/backup/dss/egs/fs/ios/logger/mirror/monitor/tick cfgs, `DBHost`/`FSListenHost` → `127.0.0.1`, `ReadWorkOnly = 1` (author's local dev environment).

Validation: with U-S3 smoke test.

### 4. Client rendering and runtime — planned as units U-C1 … U-C8

#### U-C1 — client scaffolding

- [ ] Port.

- `ryzom/client/src/client_cfg.{h,cpp}`: `uint32 ChunksMemoryCache` (Mb) + `READ_INT_FV` entry.
- `ryzom/client/src/net_manager.cpp`: define/extern the `IsInDynWorldSession` global consumed at the flag-bit read in `CUserCharMsg` handling (~line 427 area).
- `ryzom/client/src/CMakeLists.txt`: `FILE(GLOB SRC_DYNWORLD dynworld/*.cpp dynworld/*.h)` + `SOURCE_GROUP("dynworld")` + `target_sources(ryzom_client PRIVATE ${SRC_DYNWORLD})` (target-based convention; legacy glob-era patch had an `ADD_EXECUTABLE` append). CRITICAL payoff-order issue: `map_client.cpp` includes `PolyVox/MarchingCubesSurfaceExtractor.h` directly; PolyVox/ANL are currently discovered only inside `ryzom/common/src/game_share/CMakeLists.txt` with directory-scope `INCLUDE_DIRECTORIES` that do not reach the client target. Fix by promoting PolyVox headers to `Ryzom::gameshare` PUBLIC interface, or re-running `FIND_PACKAGE(PolyVox)` in the client + `target_include_directories(ryzom_client ...)`.

Dependencies: U-S1 (for the flag definition). Validation: client builds with an (empty) `src/dynworld` glob.

#### U-C2 — core client dyn modules

- [ ] Port `map_client.{h,cpp}`, `region_mesh.{h,cpp}`, `dyn_continent.{h,cpp}` (legacy `code/ryzom/client/src/dynworld/`, verbatim + include-path fixes).

- `map_client` (~11 KB): `CMapClient : CMapBase` — LOD sector selection via `selectSectorsNoOrder<TMeshSectorLod<N>>`, PolyVox resampling + `MarchingCubesSurfaceExtractor`, voxel ray-pick using `CView`/`MainCam.getRayWithPoint`, honors `ClientCfg.ChunksMemoryCache`/`SelectionDist`, `CPrefetchSectorCommand`, `IChangeTaskPriority`-cast ray priority.
- `region_mesh` (~7 KB): `CRegionMesh : IRegionMesh`, per (biome×material) `CRegionSubMesh` (NeL VB with `Empty`-alias alpha + IB), textures from `CMaterialTextureSet`/`CMaterialLibrary`.
- `dyn_continent` (~16 KB): `CDynContinent : CContinent`, static `MAP` pointer, owns a `CMapClient`, stock `select()` with legacy-IG blocks under `if(0)`; priority via `CAsyncFileManager`.

Dependencies: U-C1; U-N3 indirectly (its globals). Validation: build `ryzom_client`; sanity of voxel picking once U-C7 debug commands land.

#### U-C3 — landscape stack (new client modules)

- [ ] Port in order: `dyn_landscape_model.{h,cpp}` → `dyn_landscape.{h,cpp}` → `dyn_landscape_user.{h,cpp}` → `dyn_landscapeig_manager.{h,cpp}` → `dyn_instance_group_user.{h,cpp}`.

- `dyn_landscape_model` (~10 KB): `CDynLandscapeModel : CLandscapeModel` passing `new CDynLandscape()` to the param ctor; `DynLandscapeModelId = CClassId(0x5a573b56, 0x6b395830)`; overrides virtual `clipAndRenderLandscape()` (requires U-N4).
- `dyn_landscape` (~15 KB): `CDynLandscape : CLandscape` — virtual `render()`/`mayRayCollision` overrides → mesh `castRay`; loads `dyn_landscape.vp1/.arbfp1` via base-name `CPath::lookup` + `compileVertex/PixelProgram`, `setUniform*`, triplanar; renders `CRegionMesh` + voxel highlight box; tunables `g_useVP`, `g_usePP`, `g_rebuildShaders`, `g_renderLegacyLandscape`, `g_clipBV`, `g_tighten`, `g_discardMaterial`, `g_texCoordGenMode` (module-local).
- `dyn_landscape_user` (~55 KB / 1635 lines — the big one): `CDynLandscapeUser : CLandscapeUser` + `IVoxelListener`; 3-LOD mesh generation with `CSectorTransition`, deferred `CGenerateMeshCommand`/`CPrefetchSectorCommand`, `getPriorityToPlayer(CVectorI)`, `CMaterialLibrary::initialize("material_library.cfg")` + `bindMaterials`, texture set → `CTileBank` (uses `findTileTexture`, now public via U-N4 chain).
- `dyn_landscapeig_manager` (~16 KB): per-sector plant IG loader (`CSector<6>`, `CGenerateIgCommand`, shape-bank preload/refcount, `flushTextures`, `IGCallbacks->addIG`).
- `dyn_instance_group_user` (~19 KB): `CDynInstanceGroupUser : NL3D::UInstanceGroup` building IGs at runtime from PLANT-type `Voxel::Object` sheets (requires `CPlantSheet::getShapeName` and the U-N5 init fix).

Dependencies: U-N4, U-N5, U-C2. Validation: build `ryzom_client`; headless init smoke test if feasible.

#### U-C4 — session/flow integration

- [ ] Port.

`continent.{h,cpp}` (virtual `setup`/`select()`/`unselect()`, `unregisterTaskPriorityCallback` in dtor); `continent_manager.cpp` (`preloadDynContinent()` silent "dynamic" continent from the newbieland sheet; route `select()` when `IsInDynWorldSession`); `init_main_loop.cpp` (`CMaterialLibrary::instance().initialize("material_library.cfg")`; in session: `registerBasic()` + `newLand = new CDynLandscapeUser(Scene)`, `Landscape = Scene->createLandscape(newLand)` — new signature from U-N4); `far_tp.{h,cpp}` (`joinDynamicWorld` path superseding the SHARD join in `JoinDynWorld` mode); `sheet_manager.h` (expose plant sheet data for PLANT voxel use); `entities.cpp`/`entity_cl.cpp` (`g_noSnap` minor hooks).

Dependencies: U-C3, U-S1, U-S4. Validation: build `ryzom_client`; Phase 5 shard-join flow.

#### U-C5 — PACS hookup

- [ ] Port.

`pacs_client.cpp`: in a dynworld session, construct `CDynRetrieverBank(false)`, `CDynGlobalRetriever(RB, CDynContinent::MAP)`, `CDynMoveContainer(...)` instead of legacy retriever setup. `ig_callback.cpp`: `!IsInDynWorldSession` on the last flag of `addCollisionnablePrimitiveBlock`.

Dependencies: U-N3, U-C2. Validation: build + in-game collision walk test.

#### U-C6 — interaction/UI

- [ ] Port.

Cross-cutting signature change applied to all 5 files together: `contextual_cursor.{h,cpp}`, `cursor_functions.cpp`, `motion/user_controls.{h,cpp}`, `motion/modes/ai_mode.cpp`, `r2/editor.cpp` — click parameter `bool rightClick` → `NLMISC::TMouseButton` (adds middle-button support). Then `cursor_functions.cpp` VOXEL context menu via `CDynContinent::MAP->selectVoxelUnderPos/getVoxelAt/setVoxelAt`; `contextual_cursor.cpp` (CMaterialLibrary-based context detection).

Dependencies: U-C4 (for `CDynContinent::MAP`); `decal.cpp` accessor change should already be done inside U-N4's same-commit migration. Validation: build + manual UI test.

#### U-C7 — debug tooling

- [ ] Port.

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
2. **`st_dynworld` enum ordering** — inserting before `st_mainland` shifts its numeric value; wire/DB-visible. Both ends must change in the same unit (U-S1), and nothing in between may rely on old values.
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