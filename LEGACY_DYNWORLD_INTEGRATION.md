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

## External dependency version notes

- ANL checkout: `D:\Programmation\ANL`, branch `master`, HEAD `28d40d67f6f3e25149f5580da0e6655cc7ee8467` (2015-11-20, `optimize voronoi noise`). `git describe --tags --always --dirty` reports `28d40d6-dirty`; although status reports `Noise/noise_gen.h` modified, its content hash matches HEAD. The checkout also has untracked build directories, no release tag, and no identified license file. Verify the license before wiring ANL.
- PolyVox checkout: `D:\Programmation\polyvox`, branch `kishan-dynworld`, commit `c5c63c22db5194029e54dea770d5b10799dbfdd3` (2018-05-01); describes as `v0.2.1-1068-gc5c63c22` and has a clean worktree. `include/PolyVox/PagedVolume.h` identifies the MIT license.

## Remaining integration order

### 1. Shared dynworld foundation — in progress

- [x] Inventory `ryzom/common/src/game_share/dynworld` and map external dependency consumers.
- [x] Port and test the self-contained `Voxel` type without server or client runtime dependencies.
- [x] Port and test `mapgen/noise` using existing NeL dependencies; keep ANL isolated to higher-level map generation.
- [x] Port and test `mapgen_params` using the existing NeL config and noise APIs.
- [x] Port and test `environment_definition` using existing NeL configuration APIs.
- [x] Integrate the clean, MIT-licensed PolyVox wrapper and map support modules first.
- [x] Port `map_sector` for sector ID/region coordinate system.
- `mapgen.cpp` also includes `material_library`, `biome_manager`, and `cavegen`; `mapgen.h` uses PolyVox-backed definitions. Do not port the generator as a standalone unit.
- Defer ANL wiring until its license is verified; UnQLite remains unversioned and unwired.

### 2. NeL 3D and PACS extensions

- Review the legacy changes to landscape, scene, instance-group, material, and PACS APIs against current implementations.
- Port compatible API changes in small units, with focused tests/builds.

### 3. Server integration

- Port shared server-side dynworld state, persistence, map/continent management, and service hooks.
- Reconcile old CMake source registration with current server targets.

### 4. Client rendering and runtime

- Port the dynamic landscape renderer, mesh integration, client lifecycle hooks, and shaders.
- Add target-scoped dependency/link configuration and build the client target.

### 5. Assets, configuration, and end-to-end validation

- Port environment/material configuration and runtime assets.
- Validate server/client configurations, packaging, and CI-relevant builds.

## Current checkpoint

The `Voxel` header, NeL-only `mapgen/noise`, `mapgen_params`, `environment_definition`, PolyVox wrapper, and `map_sector` are integrated. All focused dynworld tests and the full workspace build pass. The next integration units are `material_definition` and `material_library` (no ANL dependency), followed by `biome_manager`, `cavegen`, and `mapgen` which require ANL. ANL license verification remains outstanding.