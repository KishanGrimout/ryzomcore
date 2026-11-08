# Player blocking on dynworld terrain — plan for GitHub issue #2

Issue: <https://github.com/KishanGrimout/ryzomcore/issues/2> — "Players should be blocked
by walls and cliffs". Currently the player is just placed on the terrain surface, no
matter the height; steep terrain is climbed instead of blocking.

Status: **Milestone M1 reached 2026-11-08** — B1 + B2 implemented, validated in-engine
(a client session walks flat ground and downslopes free, walls block head-on with
slide). Integration roadmap: `docs/LEGACY_DYNWORLD_INTEGRATION.md`.

## Reference documentation

- `docs/VoxelTerrainCollision.md` — **system doc** for this plan's subject: system
  architecture, move semantics, ground-height invariants (do-not-regress list),
  instrumentation registry (`MVNAN`/`MVDBG*`), findings catalog, tests. Read it before
  touching collision code; this plan only tracks decisions and phases.
- `docs/TestEnvironmentNotes.md` — validation environment quirks (`0xC0000135` ctest
  DLL issue, direct-exe convention) referenced by phase validations below.
- `docs/DynClientRuntime.md` (§Debug command catalog, §4 movement: `g_noSnap`) —
  referenced by B4's client-UX work.
- `docs/LegacyAssetInventory.md` — legacy tree survey/background (source working copy,
  legacy assets, external ANL/PolyVox checkouts).

## Milestone log

- **M1 — 2026-11-08** (branch `dev/dyn`): GLM "walking is not teleported/clipped anymore".
  In-engine: flat walk + gentle downslopes free; wall/cliff contacts produce descs.
  23 unit tests green (12 move-collision + 11 map-collision + whole dynworld exe set
  exit 0). Fixes landed on the road there (all found in client sessions, root causes and
  invariants documented in `docs/VoxelTerrainCollision.md` §§3/6):
  1. `getMeanHeight` climb/descent paths unify on the surface voxel (the climb path
     used to read one voxel high → two-voxel phantom drops);
  2. density `+=` + floor quantization restored (a subtract experiment re-buried the
     ground and re-armed the phantom);
  3. `CMapBase` converters scale sector origins by `SideSize` (were
     `SideSizeInVoxels`: every nonzero sector sampled 2 sectors off);
  4. `getVoxelAt` floors per axis (was the truncating `CVectorI` path: the whole
     negative-y half of the world read the NEIGHBORING column — the final root
     cause of the invisible walls);
  5. both ground-search loops in `getMeanHeight` bounded (`4·SideSizeInVoxels`) —
     an empty column used to hang the client.
  Kept instrumentation (strip at B6): `MVNAN:` guards (3 sites), `MVDBG/2/3/4` probes
  in `testCylinderMove`.
- **Known residuals after M1** (next milestones): visual penetration up to ~half a
  voxel where the surface sheet is thin (the quantized snap floors the density
  fraction; M2 candidate: density-fraction standing height); wall silhouette
  penetration (~radius) + thin-wall pass-through (the sweep is a center-point
  column; M2/M3 candidate: real cylinder or side-offset spans, plan §2 note).
  2026-11-08 investigation added the semantic root for all of these — the collision
  model is cell/density-fraction based while the visual mesh is a marching-cubes
  level set; see `docs/VoxelTerrainCollision.md` §8 and the open decision in §6
  below (iso-level semantics vs mesh reuse).

## 1. Verified current behavior (audit findings, 2026-11-07)

> Audit baseline for the plan (kept as the "before" record). The living documentation of
> what is implemented NOW is `docs/VoxelTerrainCollision.md` — §1 where the system lives,
> §2 move semantics.

In dyn sessions both sides use the dyn PACS stack, whose move functions are stubs:

- Client plugs in `CDynRetrieverBank` / `CDynGlobalRetriever` / `CDynMoveContainer`
  (`ryzom/client/src/pacs_client.cpp:96-103`). Server does the same
  (`ryzom/server/src/server_share/dynworld/dyn_continent_container.cpp:57-98`).
- `CDynGlobalRetriever::getMeanHeight()` (`ryzom/common/src/game_share/dynworld/dyn_global_retriever.cpp:357-407`)
  climbs out of solid voxels step-by-step, then interpolates by `Voxel::Density`.
- `CDynGlobalRetriever::doMove()` (`dyn_global_retriever.cpp:419-455`) ignores the
  horizontal segment entirely: it just evaluates height at the requested (x,y) and
  returns it — stepping "into" a cliff teleports the player on top of it.
- `CDynGlobalRetriever::testCylinderMove()` (`dyn_global_retriever.cpp:651-655`) returns
  an always-empty collision list → no rejection ever fires on either side.
- Movement is client-authoritative: client predicts with local primitives, sends
  `POSITION` (`user_entity.cpp:1701-1727`), FE routes it to GPMS
  (`frontend_service/id_impulsions.cpp`), GPMS `cbClientPosition` →
  `CWorldPositionManager::movePlayer()` (`ryzom/server/src/gpm2_service/world_position_manager.cpp:2111-2318`)
  which only clamps speed. Because the dyn `doMove` returns the requested position
  verbatim, `finalPos == targetPos` always → the existing divergence-correction channel
  (`> 1 m` ⇒ mirror-correct + `TP:CORRECT` bury message, lines 2278-2317) never fires.
- Client-side prediction guard exists and works against a real server sim:
  `CUserEntity::checkPos()` (`user_entity.cpp:1769-1879`) runs the same move on a
  `_CheckPrimitive` and reverts to `_LastPositionValidated` when positions diverge.
- Vanilla NeL PACS chain-based collision (`nel/src/pacs/global_retriever.cpp`
  `findCollisionChains` / `testMovementWithCollisionChains`) is bypassed on dyn
  continents; dyn IG prims are deliberately non-collisionable (`ig_callback.cpp:219`, system doc §1).
- Source data available for a collision test: `Voxel::Type` (`Air=0 / Object / Liquid /
  Raw` — solid = not Air, this is what `isValidPosition` checks),
  `Voxel::Density`, and the marching-cubes gradient normal already computed in
  `CMapBase::castRay()` (`ryzom/common/src/game_share/dynworld/map.cpp:279-287`,
  `PolyVox::computeCentralDifferenceGradient`). `castRay` and `getVoxelAt` compile on
  both client and server (same `SRC_DYNWORLD` lib in `game_share/CMakeLists.txt`).

## 2. Design direction

> The current implemented semantics (B1/B2) are documented in
> `docs/VoxelTerrainCollision.md` §2; this section records the design rationale.

Implement a real move test **inside `CDynGlobalRetriever`** (`testCylinderMove` +
`doMove`), not in gpm2 service code. Rationale: the same classes run on client and
server, so one implementation gives client prediction, GPMS server simulation, and the
existing `> 1 m` / `TP:CORRECT` correction + `checkPos()` rollback machinery for free.

Collision model (voxel-grid queries, no mesh extraction per move):

1. **Ground/walkability** — the move is walkable when the destination ground stays
   within one voxel of the start ground (climb up/down 1 voxel only), whatever the
   local slope or voxel density; anything steeper blocks (decided 2026-11-07, user:
   the earlier 45°-slope/gradient-normal model felt clumsy — replaced by this pure
   height rule; the blocked desc's normal simply opposes the move).
2. **Walls/overhangs** — sweep the horizontal segment with a voxel DDA (Amanatides &
   Woo) sampling solidity at body-height spans (feet + chest, a real cylinder
   approximation can come later). First solid voxel hit ⇒ stop the move at the hit
   point, compute the blocking face normal, and **project the remaining movement onto
   the wall plane** (slide); return a collision desc so
   `evalNCPrimitiveCollision`/`movePlayer` sees the event
   (slide-along decided 2026-11-07, user).
3. **Step allowance** — permit auto-step of at most one voxel
   (`TLoadingSector::VoxelSize`) so small bumps/stairs are climbable; anything taller
   is a wall (decided 2026-11-07, user).
4. **Ceiling/tunnels** — reject the move if the *target* position is solid at body
   height (today `getMeanHeight` would instead teleport the player onto the roof).
5. **Solids** — any non-`Air` voxel at body span blocks; `Voxel::Liquid` is
   non-blocking (wading). No special-casing of `Object` voxels in v1 (blanket-solid
   rule; see §6).
6. **Walkable-slope threshold** — none since 2026-11-07: the ground rule is a pure
   1-voxel rise/fall check (see 2.1); no slope-cosine test, no gradient normal in
   the move path.

Keep `getMeanHeight` as the z provider; collision decides the accepted (x,y).

> Mesh-consistency note (2026-11-08, user): the cell/density-fraction queries above
> do **not** reproduce the rendered marching-cubes surface (threshold
> `MaxDensity/2` interpolated between voxel centers — thin sheets below the threshold
> are invisible yet block, wall faces sit off the cell boundary). Options (iso-level
> collision semantics now / mesh reuse on GPMS as a reserve, feasibility verified
> piecewise) are analyzed in `docs/VoxelTerrainCollision.md` §8; the decision is
> tracked in §6 below. The "keep getMeanHeight as z provider" division of labor holds
> in every option: the mesh is only a shell and cannot answer "am I inside solid" /
> recover a buried entity, the grid climb path can.

## 3. Phases

- [ ] **B0 — Baseline.** Record current terrain behavior in a short note
      (flat walk, cliff teleport, tunnel pass-through) with a client session — this is
      the before/after comparison and confirms the stub diagnosis at runtime.
- [x] **B1 — Query helpers (no behavior change).** Done 2026-11-07 on
      `integration/legacy-dynworld`: added to `CMapBase`
      (`ryzom/common/src/game_share/dynworld/map.{h,cpp}`):
      - `isBlockingVoxel()` static rule (non-`Air` and non-`Liquid` block; blanket
        `Object` rule), `isBlockingPosition()`,
      - `traceSegment()` — Amanatides & Woo DDA over the voxel cells of a segment,
        returning the first blocking voxel (`TSegmentHit`: `m_t` segment param,
        `m_voxelPos`, outward `m_faceNormal`; embedded start ⇒ `m_t = 0`, no normal),
        double-precision voxel space with per-axis `floor` (not the truncating
        `CVectorI` float path), region read-locked like `castRay`,
      - `getGroundNormal()` — same ground search walk as
        `CDynGlobalRetriever::getMeanHeight` (climb out of non-Air / descend to first
        non-Air, liquids stop the descent), bounded by
        `searchMaxVoxels` (default `4 * SideSizeInVoxels`), then a central-difference
        density gradient over the 6-neighborhood (same density semantics as
        `MarchingCubesController`), flipped outward (flat floor ⇒ `(0,0,+1)`), with a
        flat fallback when the gradient cancels.
      Tests: `ryzom/tools/server/test_modules/game_share_test/test_dynworld_map_collision.cpp`
      (11 tests: rule matrix, air sweep, wall face + `t`/normal, embedded start,
      liquid pass-through with solid behind, vertical-sweep axis, flat-ground normal,
      step-edge 45° normal, bounded miss, water-top stop), registered as
      `ryzom_dynworld_map_collision_test` in the game_share_test CMakeLists.
      Validation (Release): all 11 pass; whole dynworld test set passed
      (map_cache 2, voxel 4, noise 3, mapgen_params 2, environment_definition 1,
      disk_file_db 5, deferred_command 5, map_sector 10, scenario parser 10 — passed
      on first direct run); `ctest`-launched unrelated exes (`nel_misc_test`,
      scenario parser flaky) fail with `0xC0000135` DLL-not-found — pre-existing
      PATH/loader environment quirk, see `docs/TestEnvironmentNotes.md`. B0 runtime baseline still
      pending (needs a client run).
      Next: B2 (`CDynGlobalRetriever::testCylinderMove` for real) consumes exactly
      these three helpers; constants (walkable cos 0.7, 1-voxel step) live in B2/B3.
- [x] **B2 — `testCylinderMove` for real.** Done 2026-11-07: implemented in
      `CDynGlobalRetriever::testCylinderMove`
      (`ryzom/common/src/game_share/dynworld/dyn_global_retriever.cpp`): horizontal
      move only; ground walkability = the destination ground stays within one
      voxel of the start ground (climb up/down 1 voxel only — simplified the
      same day with the user: the earlier `moveLen * slope-ratio + 1 voxel`
      model plus the gradient contour normal felt clumsy in play;
      `getGroundNormal` no longer used by the move test); wall sweep of the
      3 body cells above the feet cell via
      `traceSegment`, hits whose voxel top is within one voxel of the destination
      ground treated as terrain rise (auto-step zone) rather than a wall; wall descs
      carry the face normal + hit `t`, ground descs the move opposed head-on,
      contact time 0 (an immediate
      rejection: a contact at the window end makes the PACS reaction's
      `DIST_BACK/(dt − ContactTime)` push-back explode `_Speed` to inf/NaN —
      found in the client session, see `docs/VoxelTerrainCollision.md` §2); all descs use
      `CSurfaceIdent(-1,-1)` (unresolvable through
      `getSurfaceById` ⇒ PACS treats the contact as a wall and slides).
      Tests: `ryzom/tools/server/test_modules/game_share_test/test_dynworld_move_collision.cpp`
      (8 tests: flat free, wall face contact with `t`/normal, 1-voxel auto-step
      accepted, 1 m face blocked at tick distance, cliff foot blocked, ledge edge
      no-fall, liquid free wade, liquid pass-through blocking at the solid behind),
      target `ryzom_dynworld_move_collision_test` (links `NeL::pacs` — NOT
      propagated by `Ryzom::gameshare`). Validation (Release, direct exes from
      `build\bin\Release`): 8/8 pass; whole dynworld set green (51 tests, 10 exes,
      all exit 0). Caveats discovered while validating: (1) use `fabsf`, not
      `fabsl`, on floats (compile-clean but wrong type); (2) deterministic
      pre-`main` process deaths (exit 127 = the environment's `0xC0000135`
      loader-quirk signature with zero output) appeared on some freshly linked
      exes — including a mid-flight sed-mangled test body — while older exes in
      the same shell still passed; after cleanly rewriting the test body the
      whole suite passes. Never diagnose the collision logic from a mangled
      intermediate edit: rewrite cleanly and re-run before trusting a crash
      signal.
      Follow-up fix (found in a client session, same day): invisible collisions on
      gentle downslopes — `getMeanHeight`'s climb-out path (input z inside a
      partial surface voxel) returned the first *Air* voxel base (surface base +
      1 voxel) while the descent path returned the surface voxel base, so a
      one-voxel descent was compared as a two-voxel drop and the ground rule
      phantom-blocked. Both paths in `getMeanHeight` now end on the surface voxel
      (descent + density epilogue run unconditionally, see§2.1 semantics);
      `getGroundNormal` already stepped back onto the surface voxel and is
      unchanged. Regression test `GentleOneVoxelDownslopeIsNotPhantomBlocked`
      added (9/9; full dynworld set green). Details in `docs/VoxelTerrainCollision.md` § 6.
      Follow-up 2 (same day): an experiment that subtracted the density term
      and commented out the floor quantization (auto-step doubled to 2
      voxels) reintroduced phantom blocks: the sunk ground read put the
      sweep's feet cell onto the start surface voxel (embedded t=0 hit) and
      descents pushed the start ground top outside the auto-step zone
      anchored on the end ground. Density is `+=` and the quantization is
      back; `g_AutoStep` stays 1 voxel (plan §4.2). New regression
      `GentleFullVoxelDownslopeIsNotPhantomBlocked` (10/10; set green).
- [ ] **B3 — `doMove` respects blocking.** Stop at the first collision event, clamp
      the accepted segment, z = `getMeanHeight(accepted)`; keep the code path minimal
      and shared.
- [ ] **B4 — Client UX.** Investigate `g_noSnap` (hard-disabled visual snap,
      `entity_cl.cpp:1628,1698-1699`, toggle command `dyn_snap` in
      `commands.cpp:5392-5395`): decide whether to keep it disabled (PACS z is
      authoritative) once movement is blocked. Decide ledge-fall behavior (see open
      questions).
- [ ] **B5 — Server validation.** Verify GPMS2 reproduces the same accepted positions:
      `movePlayer` finalPos ≠ targetPos ⇒ `TP:CORRECT` fires; client `checkPos()`
      reverts the prediction; creature (non-ring path) and ring `checkMove` path both
      behave (ring path todo at `move_checker.cpp:172-174` may need the same test
      wired).
- [ ] **B6 — Tuning, perf, cleanup.** Constants in a config layer (continent cfg or
      `client_default.cfg`-style); measure per-tick query cost (map reads race with
      generation — see issue #4 contention); strip temp logs per §5; update roadmap.

Out of scope for v1 (decided 2026-11-07, user: no falling/gravity):
jumping/gravity impulses, ledge falls and vertical motion of any kind, collision with
NPC/primitive-based collision content (vanilla trag prims), creature-collision
authoring — file later issues if they surface.

Proposed phases (2026-11-08, **not committed** — pending the §6 collision-vs-mesh
decision; analysis in `docs/VoxelTerrainCollision.md` §8):

- [ ] **B7 — iso-level collision semantics (Option A).** Replace the
      base+density-fraction quantized ground and the cell-solidity sweep with
      threshold-field queries sharing `MarchingCubesController` semantics
      (interpolated density crossing `MaxDensity/2` between voxel-center samples;
      hit refinement along the segment between centers; drop the floor
      quantization; `Object` voxels stay explicit cell solids). Absorbs the M2
      candidate "density-fraction standing height" (residual 1) and most of the
      wall-face mismatch; constants pinned in the B6 config layer. Re-derive/retire
      the §3 ground-height invariants that assume quantization (invariant 2, part of
      3), re-validate the full dynworld set + `Gentle*` regressions, and re-measure
      the residuals in a client session before considering B8.
- [ ] **B8 — mesh-reuse collision (Option B, conditional).** Only taken if B7's
      residual (~¼ voxel worst case, MC lattice-edge interpolation vs column
      crossing) is visible in play or exact silhouettes become a requirement.
      Prerequisites and work items in system doc §8.3: factor `extractMesh` into
      the shared lib, per-sector mesh cache + triangle index on client and GPMS,
      dirty-sector re-extraction on SET_VOXEL, shared threshold constant +
      pinned `g_interpolationMethod`, worker-thread extraction (issue #4
      contention), grid stays the z provider / buried-entity recovery.

## 4. Acceptance criteria

1. Walking into a 1+ voxel wall stops the forward movement into the wall and lets the
   player slide along it (client + sync on server mirror).
2. Walking into terrain rising more than one voxel between start and destination
   blocks forward motion with sliding along the obstacle plane; slopes, however
   steep, are climbable step by step (1 voxel per move).
3. Slopes below the threshold remain walkable; single-voxel steps auto-climb.
4. Moving into an occupied tunnel/overhang is rejected (no "pop to roof").
5. Forced divergence: legal client message but positions the server does not accept ⇒
   server mirror never adopts the illegal position and `TP:CORRECT` is sent.
6. No stutter regression on generation-side contention (issue #4 metrics).

## 5. Instrumentation policy

Policy: log in `testCylinderMove`/`doMove` only when a move is **rejected** (event,
not per-frame), tagged e.g. `MVBLK:`; one server-side counter/engine dump in
`movePlayer` when `TP:CORRECT` fires.

The kept-and-strip marker registry lives in the system doc —
`docs/VoxelTerrainCollision.md` § 5 (`MVNAN` guards ×3 with their strip conditions, and
the `MVDBG`/`MVDBG2/3/4` probes). Update that registry, not this plan, when
instrumentation is added or stripped.

## 6. Open decisions

Resolved 2026-11-07:

- Steep-slope and wall response: **slide along the wall** (project remaining movement
  on the blocking plane), not hard stop.
- **Walkable-slope threshold:** 45° (`cos 0.7`) as of 2026-11-07, **replaced the
  same day by the pure 1-voxel rise/fall rule** (user: the gradient/slope model
  felt clumsy in play — simplified per §2.1, no more slope-cosine test and no more
  gradient normal in the move path; `getGroundNormal` stays as a tested CMapBase
  API for the ledge work in B4/B6). **Step height:** 1 voxel.
- **Falling/ledges:** out of scope for #2 v1 (no vertical motion at all);
  reevaluate once blocking removes the cliff-teleport class.

Still open:

- **Object-voxel rule (working decision until user says otherwise):** v1 uses a
  blanket rule — every non-`Air` voxel at body height blocks, `Liquid` never blocks.
  `Object` voxels (decor/entity billboards carrying a `SheetId`) are therefore just
  solid; if some objects should be walked through, add a per-material/per-subtype
  exception list later instead of blocking config now.
- **Server map coverage:** collision queries on GPMS need the voxel map to be resident
  where the entity is (`map_cache_manager` / slave paging) — confirm what the server
  has loaded before trusting server-side rejection (ties into issue #6's mirror-link
  findings and issue #1 "make GPMS2 authoritative").
- **Collision vs visual mesh (opened 2026-11-08, user observation).** The B1/B2
  queries are voxel-cell true, not mesh-faithful: the rendered terrain is a
  marching-cubes level set at threshold `MaxDensity/2` interpolated between voxel
  centers (`docs/VoxelTerrainCollision.md` §8 for the full mismatch table). Feasibility
  verified 2026-11-08: intermediate work items are mechanical, the server can extract
  and cache per-sector meshes since it already holds the full-resolution volume (no
  data crossing the wire).
  - **Working recommendation:** implement Option A (B7, iso-level semantics) next —
    it subsumes the planned M2 "density-fraction standing height" candidate, is a
    contained change inside `CDynGlobalRetriever`, and collapses the residuals from
    ~½ voxel to under ~¼ voxel.
  - **Reserve:** mesh reuse on GPMS (Option B, B8) is a validated long-term
    candidate — adopt only if A's residual shows in play, or exact silhouettes
    (tight overhangs) become a requirement; it pays one 64³ extraction scan per
    32 m sector server-side plus introduces two determinism prerequisites
    (shared threshold constant, pinned interpolation method) that should land with
    the B6 config layer either way.
  - Status: **waiting on user decision** to commit B7/B8 wording and scheduling
    relative to B3–B6.
