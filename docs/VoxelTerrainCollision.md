# VoxelTerrainCollision — dynworld voxel terrain collision & movement

> **System doc.** Documents the voxel-terrain movement/collision system, which runs the
> same code on client (prediction) and server (validation).
> The related **plan** (phases, acceptance criteria, open decisions, instrumentations to
> strip) is `docs/PLAYER_WALL_CLIFF_COLLISION_PLAN.md` (GitHub issue
> [KishanGrimout/ryzomcore#2](https://github.com/KishanGrimout/ryzomcore/issues/2)).
> Test-runner environment quirks: see `docs/TestEnvironmentNotes.md`.

Status: **Milestone M1 reached 2026-11-08** — B1 + B2 implemented and validated in-engine
(a client session walks flat ground and downslopes free; walls/cliffs block head-on with
slide). Residual shortcuts (next milestones): visual penetration up to ~half a voxel on
thin surface sheets (quantized snap floors the density fraction), wall silhouette
penetration (~radius) + thin-wall pass-through (center-point column sweep).
Semantic root of these residuals: the collision model is voxel-cell/density-fraction
based while the visual mesh is a marching-cubes level set — see §8.

## 1. Where the system lives

| Piece | Location | Role |
|---|---|---|
| `CMapBase` helpers | `ryzom/common/src/game_share/dynworld/map.{h,cpp}` | voxel query layer: `isBlockingVoxel`, `isBlockingPosition`, `traceSegment` (DDA), `getGroundNormal`, `castRay`, `getVoxelAt` |
| `CDynGlobalRetriever` | `ryzom/common/src/game_share/dynworld/dyn_global_retriever.cpp` | `getMeanHeight` (z provider), `testCylinderMove` (move test), `doMove` |
| Dyn PACS plug-in points | client `ryzom/client/src/pacs_client.cpp:96-103`; server `ryzom/server/src/server_share/dynworld/dyn_continent_container.cpp:57-98` | both sides construct `CDynRetrieverBank` / `CDynGlobalRetriever` / `CDynMoveContainer` |
| Server validation | gpm2 `CWorldPositionManager::movePlayer` (`ryzom/server/src/gpm2_service/world_position_manager.cpp:2111-2318`) | speed clamp; `> 1 m` divergence ⇒ mirror-correct + `TP:CORRECT`; client rollback via `CUserEntity::checkPos()` (`user_entity.cpp:1769-1879`) |
| Tests | `ryzom/tools/server/test_modules/game_share_test/test_dynworld_map_collision.cpp` (11 tests, `ryzom_dynworld_map_collision_test`), `test_dynworld_move_collision.cpp` (12 tests, `ryzom_dynworld_move_collision_test`, links `NeL::pacs` which `Ryzom::gameshare` does NOT propagate) | |

Design property: the move test is implemented **inside `CDynGlobalRetriever`**, not in
gpm2 service code — one implementation serves client prediction, GPMS server simulation,
and the existing divergence-correction machinery. Vanilla NeL chain-based collision
(`nel/src/pacs/global_retriever.cpp` `findCollisionChains`) is bypassed on dyn continents;
dyn IG prims are deliberately non-collisionable (`ig_callback.cpp:219`).

## 2. Move semantics as of B2 (current behavior)

- **Horizontal only** — `testCylinderMove` evaluates the horizontal segment; z comes from
  `getMeanHeight` at the accepted position.
- **Ground walkability rule** — the move is walkable when the destination ground stays
  within **one voxel** of the start ground (pure rise/fall rule; no slope-cosine test, no
  gradient normal in the move path — the earlier 45°/`cos 0.7` model was replaced on
  2026-11-07 because it felt clumsy in play). Anything steeper blocks.
- **Wall sweep** — `traceSegment` (Amanatides & Woo DDA) over the **3 body cells above the
  feet cell**; hits whose voxel top is within one voxel of the destination ground are
  treated as terrain rise (**auto-step zone**), real walls return the face normal + hit
  `t`. NOTE: the auto-step zone is anchored on the **end** ground — see invariant 3 below.
- **Ground-block rejection** — the blocked desc opposes the move **head-on** (destination
  ground gradient flattened to horizontal, flipped to oppose movement), contact at the
  start of the move.
- **PACS reaction compatibility** — all descs use `CSurfaceIdent(-1,-1)`, unresolvable
  through `getSurfaceById` ⇒ PACS treats the contact as a wall and **slides** the
  primitive along it.
- **Contact-time invariant (critical)**: ground-block descs report contact time **0**
  (immediate rejection). A contact reported near the end of the eval window (e.g. 0.999)
  makes the PACS reaction's `NELPACS_DIST_BACK / (dt − ContactTime)` push-back kick grow
  ×1000 per blocked tick, compounding `_Speed` to inf then NaN (`CVectorI::checkedComponent`
  assert from `getVoxelAt`/`isValidPosition`, with `_DeltaPosition` NaN, seen live in a
  client session). This is only reachable since B2's real descs exist — the stub `doMove`
  never used the delta.
- **Solid rule (v1)**: any non-`Air` voxel at body span blocks; `Voxel::Liquid` is
  non-blocking (wading; pass-through stops at the solid behind). No `Object` voxel
  special-casing in v1 — blanket-solid (per-material exception list only if a need
  appears, see plan §6).
- `doMove` does not yet respect blocking (plan B3); today it still evaluates height at the
  requested (x,y) — the stub heritage.

## 3. Ground height model — invariants (do not regress)

Hard-won invariants from the M1 debugging chronicle; every one was a live phantom-block
or hang. When touching ground code, keep all of these true:

1. **Climb and descent paths must end on the surface voxel.** `getMeanHeight`'s
   climb-out loop overshoots onto the first Air voxel above the surface; the descent
   steps back onto the surface voxel; **both paths run the density epilogue
   unconditionally**. Pre-fix, the climb path skipped the epilogue and returned one voxel
   high ⇒ a one-voxel descent was reported as a two-voxel drop ⇒ phantom blocks on
   gentle downslopes (`startGroundZ = 19.5` / `endGroundZ = 18.5` with real surface
   difference of one voxel). Regression test:
   `GentleOneVoxelDownslopeIsNotPhantomBlocked`.
2. **Density contribution is `+=`, never `−=`; floor quantization stays.** A
   subtract-density experiment (plus doubled auto-step) re-buried the ground: the swept
   feet cell mapped onto the own start surface voxel (`traceSegment` embedded hit at
   `t = 0`, `hitTop` = start ground voxel top) and descents pushed the start ground top
   outside the auto-step zone → head-on rejection of gentle downslopes. Doubling
   `g_AutoStep` cannot fix this (the zone anchor moves down with the end ground).
   Restored: density `+=` (full voxel ⇒ voxel top; reference `GroundStandZ = 1.5`), floor
   quantization on (exact voxel-multiple grounds ⇒ clean 1-voxel rule),
   `g_AutoStep` back to 1 voxel. Regression test:
   `GentleFullVoxelDownslopeIsNotPhantomBlocked`.
3. **The auto-step zone is anchored on the END ground.** Any change that lowers the end
   ground (or raises the start ground read) moves the zone boundary with it — enlarging
   the step allowance is not a fix for anchor-related blocks.
4. **All meter→voxel conversions floor per axis.** `getVoxelAt` used to convert through
   the **truncating `CVectorI` float path** (`TVoxelPos(position)`): in the negative-y
   half of the world `trunc(−67658.88) = −67658` vs `floor = −67659` — a full-voxel error
   every other axis-half. Effects seen live (MVDBG4-frozen pair
   `est=(12.465, 26.559, 19.5)`, `sector=(662, −1058, 0)` round-tripping exactly): ground
   walk sampled the NEIGHBORING column, player snapped half a voxel buried, body DDA
   walked the true column's full-solid chest voxel ⇒ `wallBlocked` at `t = 0` on flat
   ground everywhere in negative y. Near-origin tests passed everywhere because
   trunc == floor for positive values (the `+WorldY` positive-half test world) — NEVER
   validate conversion code only near the origin. Fix: `getVoxelAt` floors per axis like
   `traceSegment` already did.
5. **`CMapBase` converters scale sector origins by `SideSize` (meters), not
   `SideSizeInVoxels`.** The pre-M1 code scaled by the wrong constant: every nonzero
   sector sampled 2 sectors off.
6. **Ground search loops are bounded (`4·SideSizeInVoxels`).** Pre-M1, an empty column
   hung the client (unbounded walk).
7. **Keep conversion validation off the mid-flight-mangled-edit path.** Two compile-clean
   red herrings (a `fabs`-on-float call and a sed-mangled intermediate test body) were
   chased during validation before the real root cause was found. See
   `docs/TestEnvironmentNotes.md` § "Process rules while validating collision changes".
   Returning ground = surface voxel base + density fraction, additionally floor-quantized
   (invariant 2) — the quantized ground differs by whole voxels between neighboring
   columns, which is what makes the 1-voxel rule well-defined. When the caller's z sits
   inside a partial surface voxel, the snap reports the surface-voxel base (up to half a
   voxel lower than the old climb-path value) instead of floating on the voxel top;
   resting z from the descent path is unchanged.

`CMapBase::getGroundNormal` already did the step-back onto the surface voxel
(`--groundCell.z`) and a central-difference density gradient over the 6-neighborhood
(same density semantics as `MarchingCubesController`), flipped outward (flat floor ⇒
`(0,0,+1)`, flat fallback when the gradient cancels), search bounded by
`searchMaxVoxels` (default `4·SideSizeInVoxels`, liquids stop the descent). It is no
longer used by the move test itself but stays as the tested free API for the B4/B6
ledge/ceiling work.

## 4. `CMapBase` helper reference (B1)

- `isBlockingVoxel` — static rule: blocks when non-`Air` and non-`Liquid` (blanket
  `Object` rule).
- `isBlockingPosition` — position form of the same rule.
- `traceSegment` — DDA over the voxel cells of a segment in double-precision voxel space
  with **per-axis floor**; returns `TSegmentHit` (`m_t` segment param, `m_voxelPos`,
  outward `m_faceNormal`; embedded start ⇒ `m_t = 0`, no normal); region read-locked
  like `castRay`.
- `getGroundNormal` — see §3.

## 5. Instrumentation registry

Kept-by-design until strip point (plan B6 / Phase-5 validation; then rebuild + redeploy):

| Marker | Where | Purpose | Strip |
|---|---|---|---|
| `MVNAN:` guards (3 `std::isfinite` sites) | `CDynGlobalRetriever::doMove` / `testCylinderMove` / `getMeanHeight` (`dyn_global_retriever.cpp`) | non-finite estimation/delta/ground input is logged and refused (move returns start position) instead of propagating NaN into voxel conversions (`CVectorI::checkedComponent` assert, seen live with `_DeltaPosition` NaN). Keep until the NaN birth site is confirmed fixed (suspects: PACS reaction speed projection with our descs, or `_Speed*(endTime-beginTime)` with poisoned times); then fix-and-strip, or keep as a permanent defensive guard (decide at B6). | B6 decision |
| `MVDBG` freeze-pair ground probe | `testCylinderMove` | replay a dumped estimation/sector pair through the round-trip + ground walk (used to pin the negative-y truncation) | B6 |
| `MVDBG2`/`MVDBG3` probes | `testCylinderMove` | intermediate move/ground debug returns | B6 |
| `MVDBG4` column profile | `testCylinderMove` | column profile z−2..+5 with the height walk vs body DDA comparison (confirmed the `getVoxelAt` fix live) | B6 |

Known leftover that stays: the MVDBG4-style **cursor** conversion
(`voxelUnderCursor()`) uses its own truncating `CVectorI` path too — tooling only,
harmless for movement; left as-is (visible in
`docs/DynClientRuntime.md` voxel-selection notes).

## 6. Findings catalog (root causes already diagnosed — do not re-derive)

1. **Climb/descent asymmetry** in `getMeanHeight` (§3.1) — invisible blocks on gentle
   downslopes; fixed 2026-11-07, regression test added.
2. **Subtract-density experiment** (§3.2) — experiment reintroduced phantom blocks;
   reverted same day, regression test added.
3. **`getVoxelAt` truncation in negative coordinates** (§3.4) — the final root cause of
   the invisible walls; fixed, pinned by MVDBG4 in a client session (out-of-engine tests
   could not catch it: near-origin trunc == floor).
4. **Wrong sector-origin scale** (§3.5) and **unbounded search hang** (§3.6) — M1 fixes;
   unit-tested in the map_collision / move_collision suites.
5. **PACS reaction push-back explosion** (§2 contact-time invariant) — first client walk
   into blocking terrain died in `CVectorI::checkedComponent` with NaN `_DeltaPosition`;
   fixed by reporting ground-block contact at time 0.

100%-static-rule reminders: pack collision constants where they evolve (plan B6); when
changing ground semantics, re-run the full dynworld exe set green before considering the
change landed (list + runner pitfalls in `docs/TestEnvironmentNotes.md`).

## 7. Test layout

- `ryzom_dynworld_map_collision_test` — 11 tests: rule matrix, air sweep, wall face +
  `t`/normal, embedded start, liquid pass-through with solid behind, vertical-sweep
  axis, flat-ground normal, step-edge 45° normal, bounded miss, water-top stop.
- `ryzom_dynworld_move_collision_test` — 12 tests (M1): flat free, wall face contact
  `t`/normal, 1-voxel auto-step accepted, 1 m face blocked at tick distance, cliff foot
  blocked, ledge edge no-fall, liquid free wade, liquid pass-through blocking at the
  solid behind, plus the `Gentle*Downslope*` regressions and the M1 converter/invariant
  regressions.
- Validation convention (Release, exes run directly from `build\bin\Release`; the full
  dynworld exe set must be green after every change): `docs/TestEnvironmentNotes.md`.

## 8. Visual-mesh consistency (marching-cubes surface vs collision model)

> Added 2026-11-08 (user observation: "densities of a single voxel alone do not reflect
> the visual mesh"). This section records how the visual mesh is produced and how its
> semantics differ from the B1/B2 collision model; the plan
> (`docs/PLAYER_WALL_CLIFF_COLLISION_PLAN.md` §6) owns the decision on converging them.
> Session-level investigation of 2026-11-08.

### 8.1 How the visual mesh is produced

- **Only the client extracts meshes**: `CMapClient::extractMesh`
  (`ryzom/client/src/dynworld/map_client.cpp:255-344`), called from
  `CDynLandscapeUser` (`dyn_landscape_user.cpp:163, 402`) with
  `MarchingCubesController controller(g_densityThreshold)`.
  `g_densityThreshold` defaults to `Voxel::MaxDensity / 2.f` = 127.5 and is a
  client-side mutable global (`dyn_landscape_user.cpp:55`, runtime `setThreshold`).
  The server-side `CMapServer` (`map_server.h`) has **no** mesh facility: GPMS sees
  the raw paged volume only.
- Everything the extraction needs (`TSampleVolume`, `VolumeResampler`,
  `extractMarchingCubesMesh`, `MarchingCubesController`) already lives in the shared
  game_share/polyvox layer (`polyvox_defs.h`), so factoring the ~60-line extraction
  out of client src into `CMapBase` is mechanical.
- `MarchingCubesController::convertToDensity` is raw `Voxel::getDensity`, which is 0
  for `Air` **and `Object`** voxels (anything `type <= LastNoDensity`);
  `interpolateDensity` method 0 = `(threshold − da) / (db − da)` between adjacent
  voxel centers (`marching_controller.cpp`). `g_interpolationMethod` (method 1
  branch) is dead-but-mutable global config — a determinism hazard to pin before any
  mesh-faithful collision.
- **Level-set semantics**: the surface exists only where adjacent voxel-center
  densities **cross the threshold**. Consequences:
  - a fully solid voxel (`density = 255`) meshes with its faces exactly on the cell
    boundaries (crossover at the face between a 255 center and a 0 center) — the two
    models coincide there, which is why M1's flat-ground validation felt correct;
  - a lone voxel (or whole cluster) with `density < 127.5` surrounded by air produces
    **no triangles at all** — it is invisible, yet `isValidPosition` (non-Air) counts
    it as ground and `traceSegment` blocks it as a wall.

### 8.2 Where the B1/B2 collision model deviates from the surface

| B1/B2 collision semantic | Marching-cubes semantic | Consequence |
|---|---|---|
| solid = non-`Air`/non-`Liquid` **cell** (any density > 0) | solid = density field **above threshold 127.5** after interpolation | collides with ground/walls the mesh never renders (low-density thin sheets); misses the true surface placement inside the cell |
| ground z = surface-voxel base + `density/MaxDensity`, then **floor-quantized** (§3 invariant 2) | surface z = threshold crossing between voxel centers (per-triangle planar over the cell interior) | quantized snap loses the density fraction ⇒ the documented "visual penetration up to ~½ voxel on thin surface sheets" residual |
| wall face = first non-`Air` cell face hit by the DDA | wall face = polygon on the interpolated level set, potentially well inside the cell (or nonexistent) | wall silhouette penetration (~radius) + thin-wall pass-through residuals |
| `Liquid` non-blocking | liquid participates in the density field (its top is meshed) | wading z computed the same base+fraction way — small mismatch on water tops |
| `Object` blanket-solid (plan §6 decision) | MC's density field ignores `Object` (density 0); its visual is the billboard, not the MC mesh | keep the blanket rule as an **explicit cell solidity added on top** of any iso-level field model |

### 8.3 Option assessment (2026-11-08, feasibility note for plan §6)

**Option A — iso-level collision semantics** (re-derive collision queries from the
same threshold field, no mesh):

- Ground z: walk the destination column to the threshold crossing using
  `MarchingCubesController::interpolateDensity` semantics between voxel centers
  instead of base + fraction; drop the quantization. The 1-voxel walkable rule then
  compares continuous heights.
- Blocking: a body-height sample point is inside terrain when the interpolated
  density crosses the threshold; refine the hit position by one interpolation step
  (or bisection) along the segment between voxel centers.
- `Object` voxels stay explicit cell solids (see §8.2 row 5).
- Cost: a handful of extra voxel samples per move; everything stays inside
  `CDynGlobalRetriever`; deterministic once the constants are pinned in shared config
  (plan B6 config layer).
- Fidelity: matches the mesh within MC's own approximation error — MC interpolates
  only along lattice edges and triangulates cell interiors, so a column-wise crossing
  deviates from the triangle plane by up to a fraction of a voxel on steep/convex
  surfaces. Residuals drop from ~½ voxel to well under ~¼ voxel, but not to zero.

**Option B — reuse the extracted mesh for collision** (exact silhouette), on both
sides:

- **Feasible on GPMS** (each piece verified 2026-11-08): the server already holds the
  full-resolution paged volume (`CMapServer`); the extraction depends only on shared
  layers; the mesh organization matches the generation layout (`TMeshSectorLod`
  32 m at LOD0, 64 voxels/side). No data crosses the client/server boundary —
  collision must be derived server-side, never received from a client.
- Work items: factor `extractMesh` into `CMapBase`; per-sector decoded-mesh cache
  (like the client's) + a flat triangle list with an AABB/quad index for
  segment/cylinder queries; produce today's `CSurfaceIdent(-1,-1)` descs unchanged;
  wire `voxelTouched` → dirty-sector re-extraction so SET_VOXEL batches keep mesh
  and collision in sync (issue #6 chain).
- Cost concerns: extraction is a full 64³ region scan per 32 m LOD0 sector (the same
  work the client pays per sector today) — acceptable only in a worker thread, and it
  joins the map's read-side contention on the volume lock (issue #4); decoded-mesh
  memory ~ a few hundred kB per sector; GPMS caches only sectors entities touch, as
  today.
- Determinism prerequisites: one shared threshold constant (today `g_densityThreshold`
  is client-side and mutable), `g_interpolationMethod` pinned to 0, identical LOD0
  extraction on both sides (no LOD downsampling in the collision path).
- Caveat: the mesh is a **shell** — it cannot answer "am I inside solid", nor recover
  a buried entity. The grid climb path (`getMeanHeight`) stays the z provider /
  recovery path even in this option (plan §2 already fixes the division of labor).

**Option C — hybrid** (extracted mesh near entities + iso-level field queries as
fallback where no mesh is cached). Only worth it if Option A's residual shows in
play; Option B alone has no ground truth in uncached sectors when needed in-frame.

**Recommendation (recorded in plan §6): A now** — it absorbs the M2 candidate
"density-fraction standing height" as an iso-threshold standing height; **B is a
validated reserve**: every individual piece is verified feasible; the decision to pay
the GPMS-side extraction cost should wait until A's measured residual in a client
session is unacceptable (or a feature needs exact silhouettes, e.g. tight overhangs).
