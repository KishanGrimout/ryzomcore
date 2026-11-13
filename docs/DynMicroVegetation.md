# DynMicroVegetation

Client-side microvegetation (small flora: grass tufts, flowers, twigs) planted
on **soft soil** surfaces of the dynworld voxel terrain. Reuses the **stock NeL 3D
vegetable system** (`CVegetableManager` + tile-bank `.veget` data) unchanged;
the dynworld-specific work is the placement engine, the sector-block lifecycle,
and the commit-budget policy. Plan/decision record:
`docs/DYNWORLD_MICROVEGETATION_PLAN.md` (GitHub issue #7, P0–P5 complete).

## What it is

- Pure **client visual**: no server/state involvement, no protocol. Deterministic:
  same voxel data ⇒ identical vegetation on every client.
- Distinct from `Voxel::Object` flora (mapgen-planted trees/bushes rendered via
  `CDynInstanceGroupUser`, `dyn_instance_group_user.cpp:113-162`); the soft-soil
  filter keys on `Raw` soil materials only so the two pipelines never overlap.
- Renders through the stock vegetable path under the GL3 driver (vegetable VP
  alpha, ZSort fade pass). The dyn landscape renders the manager with
  `textureDLM = nullptr` (no dynamic lighting of vegetables).

## Data flow

```
voxel edit / sector (re)extraction
  └─ async worker (dyn_landscape_user.cpp:142 soil table, :207 placement)
       m_soilIds = DYNWORLD::buildSoftSoilTable()        (main thread; needs CMaterialLibrary)
       DYNWORLD::vegetPlacementsFromMesh(sectorId, lod, soilIds, decodedMesh, regMesh)
         └─ computeVegetPlacements (vegetation_placement.cpp)   [pure data, no NL3D]
              soft-soil filter → surface groups → 2 m buckets → placements
              stored on regMesh->m_vegetPlacements (world-space, deterministic seeds)
  └─ main thread, per frame: CDynLandscape::updateVegetableBlocks(refineCenter)
       structural sync vs m_regionsList (lod 0/1 only) → create/erase CDynVegetableBlock
       band walk (4 dist bands × 10 m) → CVegetableManager IG create/delete
       commit budgets: shared g_maxVegetCommit=256, near pool 2×256 (§6.9)
  └─ CDynLandscape::render → stock CVegetableManager render (VP pyramid,
       ZSort blend layers)
```

Placement is computed **only for LOD0/LOD1** meshes; LOD2 (far 350 m box) never
plants (`dyn_landscape_user.cpp:66-68`).

## Key rules and invariants

### Soft-soil rule

A facet is plantable **iff** all three corners are `Raw`, share one
`(material, biome)`, and `soilIds[biome] == material`
(`vegetation_placement.cpp:127`). The table is built by
`buildSoftSoilTable` from each environment's `DefaultSoil`
(`code\ryzom\common\data_leveldesign\dynworld\environments\*.cfg`):
forest/jungle/prime_roots → `Humus`, desert/lakes → `SawDust`. Everything else
(rock, sand-top, water tops, mixed-facet boundaries) stays bare.

### Seed determinism

Bucket index + per-bucket seed derive from sector geometry only (world-space
bucket coordinates); the RNG is re-seeded per bucket, so re-extraction after a
voxel edit replants untouched buckets identically ("planting is deterministic"
— user sign-off 2026-11-13). See `vegetation_placement.cpp` header comment.

### Band-walk reference point = alpha reference point

The band distance classification must use the **same** center as the VP alpha:
`View.refinePos()` (the player, 3rd person) — not `MainCam.getPos()`. Stock
does the same (`CLandscape::refine`, `landscape.cpp:739`; alpha at
`:1829`). Mixing the two was the §6.8 "fading missing"/pop root cause.
Block classification uses `CVegetBucket::SurfaceCenter` (area-weighted
centroid, real z), **not** `Center` (z=0) — the inflated band distance of
z=0 centers caused close-pop (§6.5–6.6). `Center` is unchanged so layout
sizes stay bit-identical.

### Block lifecycle decoupled from sector-transition factor

Blocks are created when the mesh is inserted into `m_regionsList` and erased
only when it leaves it (recreated on pointer change = re-extraction). Deliberately
**not** gated on `m_displayFactor`: a factor gate makes vegetation pop in after
the terrain is visible and vanish at fade-out start (§6.7). Same-lod voxel edits
then do erase+create+fill in one commit window (see budgets).

### Lock / fast-floor discipline

Every block batch create/delete runs inside
`OptFastFloorBegin() … _VegetableManager->lockBuffers() … unlockBuffers() …
OptFastFloorEnd()`. `generateInstance` is main-thread only (VB allocator state).

### Commit budgets (`dyn_landscape.cpp`)

- Shared per-frame pool `g_maxVegetCommit = 256` IG creates (knob via
  `dyn_vegetbudget <n>`), consumed near-first (blocks sorted by distance to the
  view center — reused static buffer, main thread only).
- **Near pool** `nearBudget = 2 × 256` for blocks within
  `NL3D_VEGETABLE_DEFAULT_DIST_MAX` (60 m): a freshly (re)created near block
  fills completely in its creation frame, so erase+create+fill is atomic
  pre-render — kills the voxel-edit vegetation blink (§6.9).
- Worst case (full 16 m sector = 64 buckets × 4 bands = 256 IGs) fits one
  near-pool frame.
- Budget-starved entries retry next frame (all-or-nothing per entry per band).

### Lighting (v1)

White ambient; diffuse shade from the landing facet slope; water state
`AboveWater`. `blendDistMax = (distType+1) × 10` (stock spatial alpha;
a "late-create fade" shift was tried and disabled — tombstone comment in
`createIgForDistType`, plan §6.8).

## The stock manager (untouched except noted)

- Created in the `CLandscape` ctor (`nel/src/3d/landscape.cpp:230`) with VB
  quotas Unlit 50000 / Lighted 5000 AGP vertices (`landscape_def.h:57-58`);
  `CDynLandscape` inherits it, releases its blocks before the base dtor.
- Measured steady state at default density: ~14 clip blocks ≈ 3780 faces ≈
  ~8k AGP verts — ~16% of the Unlit quota (~6x headroom).
- Only engine-side change: `NL3D_VegetFadeSlopeScale` debug knob
  (`nel/src/3d/vegetable_manager.{h,cpp}`) scaling the VP alpha slope
  (1 = stock fade, 0 = no fade).

## Live config

`ClientCfg.MicroVeget` / `MicroVegetDensity` work unchanged for the dyn
landscape: the stock toggle path (`main_loop_utilities.cpp:167-196`,
`continent_manager.cpp:245-256`) calls `enableVegetable` /
`setVegetableDensity`, both forwarded through `CDynLandscapeUser`.

## Debug command catalog (client console)

| Command | Effect |
|---|---|
| `dyn_veget [0|1]` | Toggle microvegetation. Disable releases all blocks (frees VB space) via `CDynLandscape::setVegetableActive`; re-enable recreates via the structural sync. No arg = flip. |
| `dyn_vegetdensity <pct>` | Set density % (forwards to `setVegetableDensity`); no arg prints current. |
| `dyn_vegetdump` | Per-block inventory (`block lod=… sector=(…) center=… entries=… igs=…`) + global state. |
| `dyn_vegettest` | Synthetic ~30-instance patch at the player feet (P0 probe path, kept). |
| `dyn_renderveget` | Render toggle for the whole vegetable pass. |
| `dyn_vegetbudget <n>` | Set `g_maxVegetCommit`. |
| `dyn_vegetstats` | Render state + last-frame face count. |
| `dyn_vegfade [s]` | `NL3D_VegetFadeSlopeScale` A/B knob (1 = stock, 0 = no fade). |

Known removed: `dyn_wireframe` — driver-level polygon-mode switching corrupts
GL3 render state after one frame; **do not re-add** (tombstone comment in
`dyn_landscape_user.cpp`).

## Validation history

Full investigation record with root causes, experiments (including the
disabled late-create fade), probe evidence and user sign-offs:
`docs/DYNWORLD_MICROVEGETATION_PLAN.md` §6 (§6.5 close-pop/SurfaceCenter,
§6.7 lifecycle decoupling, §6.8 reference-point fix, §6.9 near-budget blink
fix, §6.10 probe strip, §6.11 P5 completion + budget sign-off). All 13
dynworld test suites green (2026-11-13, direct-exe Release run); regression
tests in `game_share_test/test_dynworld_vegetation.cpp`.
