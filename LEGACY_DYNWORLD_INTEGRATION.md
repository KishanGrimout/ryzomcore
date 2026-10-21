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

## External dependency version notes

- ANL checkout: `D:\Programmation\ANL`, branch `master`, HEAD `28d40d67f6f3e25149f5580da0e6655cc7ee8467` (2015-11-20, `optimize voronoi noise`). License: Permissive (BSD/MIT-like) - verified at https://github.com/JTippetts/accidental-noise-library/blob/master/LICENSE. Compatible with project.
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

All core dynworld map management and paging modules are integrated. The foundation now includes: voxel types, sector management, material library, biome generation, cave generation, map generation, raycasting, mesh extraction, async command management, and the complete paging system (disk_file_db, map_cache_manager, map_server, master_pager, slave_pager) with LZMA compression support via seven_zip wrapper. The next steps are:
- Review the integrated pager components against current NeL APIs
- Integrate NeL 3D and PACS extensions
- Port server integration (shared server-side dynworld state, persistence, map/continent management)
- Port client rendering and runtime (dynamic landscape renderer, mesh integration, shaders)