# AGENTS.md — working rules for AI agents / contributors

## Documentation location and layout

All project-specific documentation produced or maintained during this integration work
lives in the **`docs/` folder at this repo root**. Keep it the single source of truth.

Two kinds of docs, kept strictly separate:

- **System/reference docs** — one per independent system discovered or worked on. Named
  `CamelCase.md` (NOT all-caps), e.g. `docs/VoxelTerrainCollision.md`.
  **All findings about how a system behaves, its invariants, root causes, error codes,
  runtime lessons and instrumentation registries go INTO the system's doc**, not into a
  chat history and not into the roadmap. If no doc exists for a newly-discovered system,
  create one (CamelCase) and add it to the table of contents below.
- **Plan docs** — named with `_PLAN` suffix or clearly marked as investigations/handoffs
  (e.g. `docs/PLAYER_WALL_CLIFF_COLLISION_PLAN.md`). A plan holds decisions, phases,
  acceptance criteria and progress — it must **link to the system docs it depends on**
  instead of duplicating their content ("decisions vs knowledge" separation).

`docs/LEGACY_DYNWORLD_INTEGRATION.md` is the **integration roadmap**: port status, unit
checklists, integration-level decisions and phase validation history. It is *not* the
place to dump system findings.

Session protocol:

- Read the table of contents below, then the system docs relevant to your task, then the
  roadmap if the task touches port status / integration phases.
- Append findings to the relevant **system doc** (or plan, for plan progress); create a
  new system doc for a new domain; update the roadmap only for integration-level state.

Rules:

- New docs, checklists and notes of project-wide relevance → create them under `docs/`.
- Upstream `README.md` (Ryzom Core project readme) stays at the repo root untouched.
- Deployment-local READMEs that travel with the artifacts they describe
  (e.g. `pipeline/client_dev/local/README_SANDBOX.md`) may stay next to those
  artifacts, but link to them from `docs/` when they document integration-wide state.
- When moving docs, use `git mv` so the history stays connected.

## Documentation index (ToC)

System/reference docs (CamelCase):

| Doc | Topic |
|---|---|
| `docs/VoxelTerrainCollision.md` | dynworld voxel terrain collision & movement (CMapBase helpers, `CDynGlobalRetriever::testCylinderMove`/`doMove`, ground-height invariants, MVDBG/MVNAN instrumentation, phantom-block findings) |
| `docs/VoxelMessageChain.md` | `DYN_WORLD:SET_VOXEL` client→FE→MS→EGS→GPMS message chain, msg.xml rules (single-child-branch trap), `sendMessageViaMirror` silent drop paths, client voxel commands |
| `docs/ShardRuntimeOps.md` | local shard runtime ops: 16-service roster/start-stop scripts, AES port 46997 law, NeL mirror no-reconnect law, XAMPP web/DB, 4010 login lifecycle, shard-side instrumentation registry |
| `docs/DynClientRuntime.md` | client dynworld init & runtime: session routing, `PreDataPath` needs, material library/shader failure modes, `dyn_*` debug command catalog, render toggles |
| `docs/TestEnvironmentNotes.md` | test runner & validation environment: `0xC0000135` ctest DLL quirk, direct-exe convention, Release-validation config, debugging process rules |
| `docs/UnitTesting.md` | how to add and run unit tests: the two test homes (NeL `ut_*`, ryzom `test_modules`), `add_test_executable` checklist, build/run commands, pitfalls |
| `docs/LegacyAssetInventory.md` | survey of the legacy tree `D:\Programmation\ryzomcore_old` (client run dir, data repos, Mercurial access recipe, external ANL/PolyVox checkouts) |

Plan/handoff docs:

| Doc | Topic |
|---|---|
| `docs/LEGACY_DYNWORLD_INTEGRATION.md` | the legacy-dynworld integration roadmap (audit conclusions, port status, unit checklists, phase validation) |
| `docs/PLAYER_WALL_CLIFF_COLLISION_PLAN.md` | GitHub issue #2 — player blocking on walls/cliffs (phases B0–B6, acceptance criteria) |
| `docs/SET_VOXEL_ROUND_TRIP_VALIDATION.md` | GitHub issue #6 — paused SET_VOXEL round-trip investigation (suspects, resume protocol, instrumentation inventory) |
| `docs/DYNLANDSCAPE_GLSL_MIGRATION_PLAN.md` | dyn landscape shaders vp1/arbfp1 assembly → hand-authored GLSL via the GL3 driver (assessment, phases 0–5) |

Deployment-local (stay where they are, linked from here):

- `pipeline/shard_dev/README_SHARD_OPS.md` — manual shard bring-up kit.
- `pipeline/client_dev/local/README_SANDBOX.md` — client sandbox setup.

## Shard research/debugging conventions (summary — details in the system docs)

- Deployed shard: `pipeline/shard_dev/*`, client sandbox: `pipeline/client_dev/local`.
- Runtime error codes shown by the client login UI are defined in the web-shard PHP
  layer (`code/web/.../login_translations.php`), NOT in C++ source — grep `code/web`
  for them; meanings are recorded in `docs/ShardRuntimeOps.md` (e.g. 4010).
- Kept-by-design instrumentation is cataloged per system doc (strip only after the
  relevant validation completes, then rebuild + redeploy):
  - shard side (`LOOP_SNAIL`, `MS_SNAIL`, cbTick gap alarm, FE/MS chain traces):
    `docs/ShardRuntimeOps.md` § 8;
  - collision side (`MVNAN`, `MVDBG2/3/4`): `docs/VoxelTerrainCollision.md` § 5.
- msg.xml: never leave a message branch with a single child (see
  `docs/VoxelMessageChain.md` § 2).
