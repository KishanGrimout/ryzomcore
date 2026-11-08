# TestEnvironmentNotes — test runner & validation environment quirks

> **System doc.** Everything about running/validating tests in this workspace that is
> environmental rather than code-related: ctest DLL resolution quirks, direct-exe
> conventions, per-configuration validation, process rules during debugging.
> Related plans: `docs/PLAYER_WALL_CLIFF_COLLISION_PLAN.md`,
> `docs/SET_VOXEL_ROUND_TRIP_VALIDATION.md`; roadmap build context: "Working context" in
> `docs/LEGACY_DYNWORLD_INTEGRATION.md`.

## 1. The `0xC0000135` ctest quirk (STATUS_DLL_NOT_FOUND)

- **Symptom**: `ctest`-launched test exes can die with exit code `0xC0000135`
  (STATUS_DLL_NOT_FOUND) — hit with `nel_misc_test`, `nelns_naming_service_test`,
  `nelns_service_instance_manager_test`, and the scenario text parser.
- **It is PATH-dependent, not code**: the DLL resolution depends on the invoking shell's
  PATH. Observed even stranger — a test exe can pass once when launched directly, then
  repeat-fail on later invocations *without any rebuild in between*.
- **First check whenever a dynworld (or any) test "fails"**: run the exe directly from
  `build\bin\Release` before suspecting the change (also for "healthy-looking" exes that
  exit 127 pre-`main` with zero gtest output — that is the same loader signature).
- Candidate chore (not yet done): fix the ctest PATH integration by adding the external
  DLL dirs (ANL `Noise.lib`-adjacent DLLs, OpenAL, etc.) to the test invocation
  environment.
- Related note: `nel_ligo_test` needed `redist\nel_tools_win_x64` on PATH for
  `libxml2.dll` (pre-existing repo dependency, unrelated to dynworld).

## 2. Conventions for dynworld tests

- Test tree: ryzom-side gtest home is `ryzom/tools/server/test_modules/<x>_test/`;
  new dynworld tests belong in `test_modules/game_share_test/` with their own
  `add_test_executable` subtarget (do NOT create a separate dynworld test tree); NeL-misc
  tests stay in `nel/tools/nel_unit_test/ut_*.test.cpp`.
- Collision tests: `test_dynworld_map_collision.cpp` (`ryzom_dynworld_map_collision_test`)
  and `test_dynworld_move_collision.cpp` (`ryzom_dynworld_move_collision_test` — needs an
  explicit `NeL::pacs` link, NOT propagated by `Ryzom::gameshare`).
- After a ground/collision semantics change: run the **whole dynworld exe set** green
  (system doc: `docs/VoxelTerrainCollision.md` § 7 lists the suites) — individual suites
  passing is not enough.

## 3. Process rules while validating collision changes

From the 2026-11-08 session (root-cause hunting near invisible walls):

- **Compile-clean ≠ correct type**: `fabs`/`fabsl` on floats compiles (with warnings
  suppressed in some configs) but is the wrong type — use `fabsf` on float expressions.
- **Never root-cause logic from a mid-flight mangled edit.** A sed-mangled intermediate
  test body produced a deterministic pre-`main` death (exit 127, zero gtest output) that
  looked like an in-run kill, and a `fabs`-on-float entered the suspect list next to it —
  both innocent. Rewrite the test body cleanly, fix type errors, and re-validate the
  suite before trusting any crash signal.
- Useful control: older exes in the same environment still passing at the same moments
  indicates an environment/edit issue, not the code under test.

## 4. Validation configuration

- **Validation config is Release, on user instruction (2026-10-21)**: `nelmisc_r.lib`,
  `ryzom_gameshare_r.lib`, test binary `build\bin\Release\nel_misc_test.exe`.
- Release-only note: `ut_misc_vectori.test.cpp::invalidFloatingPointConversionsAreClamped`
  is `#if !defined(NL_DEBUG)`, so Release runs 71 tests vs 70 in Debug — expected.
- Builds: `cmake --build build --config Release --target <target>`; CMake source-list
  changes require re-running configure first (`cmake --build build --config Release
  --target ZERO_CHECK`). `build_test\` holds a second configure used previously.
- Tool-target caveat: per-project MSBuild works where `cmake --build --target <tool>`
  hits MSB1009 for tool targets.
- Included-dirs pitfalls that cost sessions (recorded to avoid re-deriving):
  - Test targets including `polyvox_defs.h` need `${POLYVOX_INCLUDE_DIR}` in their own
    `target_include_directories` (gameshare gets PolyVox through a directory-scoped
    `INCLUDE_DIRECTORIES`, which does NOT propagate); since U-C1, PolyVox/ANL include
    dirs are `PUBLIC` on `ryzom_gameshare` for normal consumers.
  - `map_cache_manager.h` expects `nel/misc/singleton.h` pre-included by its consumer,
    and `NLMISC::CFile` statics live in `nel/misc/path.h` (include `path.h`, not
    `file.h`, for `createDirectoryTree`/`deleteFile`).
- All Phase-2 validations originally recorded in Debug were re-verified in Release
  (2026-10-21): `nelmisc_r`, `nelpacs_r`, `nel3d_r`, `ryzom_gameshare_r`, `nel_misc_test`,
  plus consumer canaries `zone_lighter.exe`/`ig_lighter.exe`/`unbuild_land.exe`.
