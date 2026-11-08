# UnitTesting — how to add and run unit tests

> **How-to doc.** Where tests live, how to register a new one, how to build and run
> them. Known environment pitfalls (DLL/PATH, test-runner behavior) live in
> `docs/TestEnvironmentNotes.md`.

The testing framework is **GoogleTest**, fetched at configure time (root
`CMakeLists.txt` § `WITH_NEL_TESTS OR WITH_TESTING`: `FetchContent` of pinned upstream
zip + `cmake/add_test_executable.cmake`). In the current configure both `WITH_NEL_TESTS`
and `WITH_TESTING` are ON.

## 1. The two test homes

| Home | Conventions | Guard |
|---|---|---|
| NeL tests — `nel/tools/nel_unit_test/` | one file per topic: `ut_<module>_<topic>.test.cpp`; integration tests end in `.it.cpp`; grouped into targets in that dir's `CMakeLists.txt` (`nel_misc_test`, `nel_ligo_test`, `nel_net_test`, `nel_web_http_client_it`, `nel_sound_openal_it`) | added under `IF(WITH_NEL_TESTS)` (`nel/tools/CMakeLists.txt`) |
| Ryzom tests — `ryzom/tools/server/test_modules/` | convention established upstream by PR #826 "enable ryzom tests"; sibling dirs `egs_test`, `session_browser_test`, `shard_unifier_test`, `game_share_test` | added under `if (WITH_TESTING)` (`ryzom/CMakeLists.txt`) |

**Dynworld tests go into `test_modules/game_share_test/` with their own
`add_test_executable` subtarget — do NOT create a separate dynworld test tree**
(established convention). Current ryzom dynworld targets:
`ryzom_dynworld_{voxel,noise,mapgen_params,environment_definition,map_sector,
deferred_command,disk_file_db,map_collision,move_collision,map_cache}_test`.

## 2. What `add_test_executable(<name> <sources...>)` does

`cmake/add_test_executable.cmake` (included from root `CMakeLists.txt` only when the
test guard is on):

- creates the executable and links `GTest::gtest_main` + `GTest::gmock` — **`main()` is
  provided for you**, test files only contain `TEST(...)`/`TEST_F(...)`/`CHECK_*`;
- `add_test(NAME <name> COMMAND <name>)` → registered with CTest under its target name;
- `target_compile_features(... cxx_std_17)`;
- adds the target to the custom `unit_test` aggregate target (build `unit_test` to build
  every test).

## 3. Adding a test (checklist)

1. Create `test_<topic>.cpp` in the right directory (`ryzom/tools/server/test_modules/
   game_share_test/` for dynworld/test_modules ranges; `nel/tools/nel_unit_test/
   ut_<module>_<topic>.test.cpp` for NeL topics).
2. In that directory's `CMakeLists.txt`:
   ```cmake
   add_test_executable(ryzom_dynworld_<topic>_test
           test_dynworld_<topic>.cpp
   )
   ```
3. Resolve headers explicitly — target-scoped, NOT inherited:
   ```cmake
   target_include_directories(ryzom_dynworld_<topic>_test
           PRIVATE
           ${CMAKE_SOURCE_DIR}/ryzom/common/src        # ryzom game/server headers
           ${POLYVOX_INCLUDE_DIR}                       # if any header pulls polyvox_defs.h
   )
   ```
   (The `${POLYVOX_INCLUDE_DIR}` line is carried explicitly on every existing dynworld
   test target — see §5 pitfalls.)
4. Link what the code under test actually needs, explicitly:
   `NeL::misc` always for ryzom dyn tests; `Ryzom::gameshare` for shared dyn code;
   `NeL::pacs` **must be linked explicitly** when PACS code is included (e.g.
   `ryzom_dynworld_move_collision_test`) — `Ryzom::gameshare` does NOT propagate it.
   NeL test targets link their own module (`NeL::ligo`, `NeL::net`, ...).
5. NeL-misc tests: append the new `ut_misc_<topic>.test.cpp` to the
   `nel_misc_test` file list in `nel/tools/nel_unit_test/CMakeLists.txt` (that target is
   one exe bundling all ut_misc files). If the test needs data files, keep them in
   `ut_misc_files/` (already on path via `NEL_UNIT_BASE=${CMAKE_CURRENT_SOURCE_DIR}/`
   compile definition) or follow that pattern for other targets.
6. Re-run CMake configure after CMake-lists changes — a plain build of a NEW target
   requires re-configure first:
   `cmake --build build --config Release --target ZERO_CHECK` (VS17 2022 generator,
   `build/`). Without it the new executable does not exist / is not updated.
7. Build & run (next section).

## 4. Building and running

Validation configuration is **Release** (project decision, user instruction
2026-10-21):

```bat
:: configure build tree after CMake edits
cmake --build build --config Release --target ZERO_CHECK
:: build one test
cmake --build build --config Release --target ryzom_dynworld_move_collision_test
:: run directly (preferred, see quirks below)
build\bin\Release\ryzom_dynworld_move_collision_test.exe
:: or run via CTest
ctest --test-dir build -C Release -R dynworld --output-on-failure
```

- Single test selection: `--gtest_filter=*GentleOneVoxel*` (any gtest flag works: list
  with `--gtest_list_tests`).
- Aggregate: build target `unit_test` builds all test exes.
- Exes land in `build\bin\Release\` (same bin as the services/tools).
- `build_test\` holds a second configure used previously (do not confuse the two trees).
- **Whole-set rule**: after a change to shared semantics (e.g. ground/collision), run
  the **whole dynworld exe set** green — list of suites:
  `docs/VoxelTerrainCollision.md` § 7 (map_collision 11, move_collision 12, map_cache 2,
  voxel 4, noise 3, mapgen_params 2, environment_definition 1, disk_file_db 5,
  deferred_command 5, map_sector 10...).

## 5. Pitfalls (detailed in `docs/TestEnvironmentNotes.md`)

- **`0xC0000135` (STATUS_DLL_NOT_FOUND)** — `ctest`-launched exes can die on DLL
  resolution because the invoking shell's PATH decides; the same exe may pass directly
  once and repeat-fail through ctest. **First check when a test "fails": run the exe
  directly from `build\bin\Release`** before suspecting the change. Fix candidate:
  add the external DLL dirs (ANL `Noise.lib`-adjacent, OpenAL, libxml2 etc.) to the test
  invocation environment.
- **`nel_ligo_test`** needs `redist\nel_tools_win_x64` on PATH for `libxml2.dll`
  (pre-existing repo dependency).
- **Working directory**: tests that touch the filesystem use CWD-relative paths — e.g.
  `CDiskFileDataBase` writes hardcoded `./cache/map/` (its parent is NOT auto-created),
  so `ryzom_dynworld_disk_file_db_test` must run from `build\` (or create dirs with
  `NLMISC::CFile::createDirectoryTree` in the test).
- **Release vs Debug count drift**: `ut_misc_vectori.test.cpp::
  invalidFloatingPointConversionsAreClamped` is `#if !defined(NL_DEBUG)` — Release runs
  71 `nel_misc_test` tests vs 70 in Debug (expected, not a regression).
- **Type discipline in math-heavy tests**: use `fabsf` on floats (`fabs`/`fabsl`
  compile but widen); never validate logic from a mid-flight mangled edit — rewrite
  cleanly and re-run (see `docs/TestEnvironmentNotes.md` §3).
- CMake source-list changes (new files/tests) require the ZERO_CHECK re-configure step
  above; a stale `build/` can also contain dead `.vcxproj` residue from
  never-committed CMake definitions (harmless — verify the target exists in the current
  CMake, not just as a file).

## 6. Related reading

- `docs/TestEnvironmentNotes.md` — runner/environment quirks and debugging process
  rules.
- `docs/VoxelTerrainCollision.md` § 7 — the collision test suites and the
  full-set-green convention.
- `docs/LEGACY_DYNWORLD_INTEGRATION.md` § "Test harness note" — the original
  convention notes for `test_modules/`.
