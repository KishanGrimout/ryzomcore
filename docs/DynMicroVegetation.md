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

### Vegetable data source per biome (side banks)

The tile→desc resolution is **per-biome, two-stage**
(`CDynLandscape::resolveVegetDescForGroup`, `dyn_landscape.cpp`):

1. Main `TileBank` (stock path): the `bindMaterials` tile id for the
   `(biome, material)` → its tileset's desc.
2. If empty: the **biome's side bank** — a dedicated `CTileBank` per
   ecosystem, loaded by `CDynLandscape::loadVegetSideBanks()` (called from
   `flushTiles`): desert → `desert.smallbank`, lakes →
   `lacustre.smallbank`, roots → `primes_racines.smallbank`. Tile match in
   the side bank reuses the `bindSet` name-prefix rule against the
   material's texture base names (same case-sensitivity; first tileset with
   a non-empty desc wins). Side banks are desc-only: no texture is ever
   loaded from them, and the season postfix is not applied (the base
   `.vegetset` files exist for all three ecosystems — seasonal lists would
   need postfix plumbing if ever wanted).

Why side banks exist: the dyn continent reuses the **newbieland sheet,
whose ecosystem is jungle** — the main bank holds only jungle tilesets, so
desert/lakes (and roots, whose `Humus` binds `PR-mousse*`) resolved to tiles
with no vegetable desc there and `CDynVegetableBlock::init` skipped their
whole groups (2026-11-14 root cause of "only jungle has veget"). Data
sources: `desert_bank.bnp`/`desert_vegetables.bnp`/`desert_vegetable_sets.bnp`
and lacustre equivalents (already in the sandbox data dir); prime roots has
**no BNP** — it mounts from `pipeline/install/primes_racines_{bank,
vegetables,vegetable_sets}` (added to `PreDataPath` in
`pipeline/client_dev/local/client.cfg`). Side banks live in `CDynLandscape`
(`m_vegetSideBanks`, keyed by biome id) and must outlive the vegetable
blocks (entries store desc pointers into them); reload releases the blocks
first.
### Merged microveget atlas (one stage-1 texture for all biomes)

Vegetable shapes carry Tex0 UVs of **their ecosystem's atlas** (each
per-ecosystem atlas is an independent 512x512 DXT5 texture:
`MicroVeget_Jungle`/`_Fyros`/`_Tryker`/`_prime_roots`, in
`*_maps.bnp` / `primes_racines_maps`). The stock `CVegetableManager` binds
**one** stage-1 texture for the whole render pass
(`_VegetableMaterial.setTexture(1, …)`, set once by `loadTexture`), so a dyn
session — which plants all ecosystems at once — cannot work with the
sheet-driven single atlas: the dyn continent sheet says
`microveget_jungle.tga`, and after the side-bank fix desert/lakes/roots
plants (`Fy_*`/`Tryker_*`/`Pr_*` shapes) rendered **jungle-atlas texels**
(2026-11-14 user report: "desert plants look wrong — mesh or uv mapping";
the shapes themselves are fine, the UV→texture pairing was wrong).

Fix (two halves, both dyn-side, no NeL core change):

1. **Composite texture** — `CMicroVegetDynAtlasTexture`
   (`dyn_landscape.cpp`): an `ITexture` that composes the four ecosystem
   atlases into one 2x2 1024x1024 RGBA texture at `doGenerate` time
   (raw-row `blit` quarters; `buildMipMaps` precomputed). Installed by
   `CDynLandscape::loadDynVegetableAtlas()` (called from
   `dyn_continent.cpp`; falls back to the stock season-postfixed
   `loadVegetableTexture` when a source is missing). Sources resolve via
   CPath: jungle/desert/lacustre from the legacy bed BNPs, prime roots
   from `pipeline/install/microveget_dyn/microveget_prime_roots.dds` (a
   deployment-local COPY — the whole `primes_racines_maps` dir must not
   be mounted: its `fy_nothing.dds` bytes differ from `desert_maps.bnp`'s
   and would shadow the bed's, verified 2026-11-14).
2. **Shape UV remap** — `CDynLandscape::ensureVegetShapeAtlasRemap(shape,
   biome)`: scales the cached `CVegetableShape::VB` Tex0 of every shape of
   a group's desc into its biome's quarter (uv = quarterMin + uv·0.5)
   BEFORE the first IG of that shape compiles (Tex0 is copied into the
   vegetable VBs at compile time). Biome→quarter: forest+jungle → TL
   (jungle atlas, stock Matis), desert → TR (fyros), lakes → BL (tryker),
   roots → BR (prime roots); quarter table = `DynVegetQuarterMin` in
   `dyn_landscape.cpp` (v axis: the GL3 driver uploads bitmap rows without
   a vertical flip, so v=0 is the first stored row both when composing and
   when remapping). Idempotent per shape name (record in
   `m_vegetShapeAtlasQuarter`, mutable cache); cross-quarter reuse of one
   shape name warns once and keeps the first quarter. The remap is gated
   on the composite being actually installed
   (`m_vegetMergedAtlasInstalled`): under the stock fallback the shapes
   keep their full-range UVs (remapping quarters into a single
   512x512-atlas was the 2026-11-14 "jungle shows desert texels" bug).

**Season support (2026-11-14, user request after merged-atlas sign-off
"desert and jungle look OK")**: the composite is season-aware —
`loadDynVegetableAtlas(uint8 season)` (0..3 = spring/summer/autumn/winter,
>= 4/0xFF = base) names the season-postfixed source variants
(`…_Sp/_Su/_Au/_Wi.dds`, same convention as `CSeasonFileExt`); a missing
seasonal variant falls back to that quarter's BASE (one-shot warning).
`CDynContinent::select` re-runs `complete=true` on a mid-session season
change (continent_manager.cpp), which reinstalls the composite for the new
season. The quarter layout is season-independent, so the per-shape
`m_vegetShapeAtlasQuarter` remap records (and already-compiled Tex0s)
stay valid across season changes — no remap reset needed. All four
ecosystems ship all four seasonal variants (`MicroVeget_*` in the
jungle/desert/lacustre `*_maps.bnp`s, `microveget_prime_roots_*` loose in
`primes_racines_maps`).

Consequences/limits: season is fixed per complete continent select — like
the stock client, no instant atlas swap outside a re-select. Mip level n
blends quarter texels at quarter borders (no gutters) — only visible as
slight distance bleeding at quarter edges.

The remap model was verified against the binary format (2026-11-14,
offline decode of the loose `PR_*.veget` shapes): Tex0 in a shape's VB is
**full 0..1 ecosystem-atlas UV space** (not pre-quarter space), scattered
across all four quadrants of the source atlas; the `.veget` layout is
`CVegetableShape::serial` (ver 1, 4 NELID checks + 4 bools + BendCenterMode
sint32) + `CVertexBuffer::serial` ver 2 with header ver 3
(`SerialOldPreferredMemory` on in the producing build) + subset ver 2
(8-byte UVRouting); shapes parse byte-exact. See plan doc §8.1.

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
- No engine-side change remains (2026-11-14 footprint strip): the
  `NL3D_VegetFadeSlopeScale` knob and the P0 `getVegetProbeState`/
  `updateSortBlockForIg` helpers were removed from
  `nel/src/3d/vegetable_manager.{h,cpp}` and
  `vegetable_blend_layer_model.cpp` (stock files again); the merged-atlas
  hook goes through a dyn-side `dynamic_cast<CDynLandscapeUser*>` in
  `dyn_continent.cpp` instead of a `u_landscape.h` virtual. Sort-block
  refresh uses the stock public `CVegetableSortBlock::updateSortBlock`
  (same idiom as `CDynVegetableBlock`).

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
| `dyn_vegetdump` | Per-block inventory (`block lod=… sector=(…) center=… entries=… igs=…`) + global state. The material/env binding dump reports the final desc resolution: main-bank tile + tileset, "side-bank vegetDesc OK", or "no vegetDesc" per (material, env). |
| `dyn_vegettest` | Synthetic ~30-instance patch at the player feet (P0 probe path, kept). |
| `dyn_renderveget` | Render toggle for the whole vegetable pass. |
| `dyn_vegetseason <0-3|255>` | Force the merged atlas season variant (0=Sp 1=Su 2=Au 3=Wi, 255=base) — §8.2. Only swaps the veget atlas; tile colors etc. still follow the session season. |
| `dyn_vegetbudget <n>` | Set `g_maxVegetCommit`. |
| `dyn_vegetstats` | Render state + dyn-side block inventory (blocks/entries/IGs) + last-frame face count. |

Known removed: `dyn_wireframe` — driver-level polygon-mode switching corrupts
GL3 render state after one frame; **do not re-add** (tombstone comment in
`dyn_landscape_user.cpp`). `dyn_vegfade` — fade-slope A/B knob, removed with
its `NL3D_VegetFadeSlopeScale` engine knob in the 2026-11-14 footprint strip
(the A/B verdict stands: the VP alpha path is live).

## Validation history

Full investigation record with root causes, experiments (including the
disabled late-create fade), probe evidence and user sign-offs:
`docs/DYNWORLD_MICROVEGETATION_PLAN.md` §6 (§6.5 close-pop/SurfaceCenter,
§6.7 lifecycle decoupling, §6.8 reference-point fix, §6.9 near-budget blink
fix, §6.10 probe strip, §6.11 P5 completion + budget sign-off). All 13
dynworld test suites green (2026-11-13, direct-exe Release run); regression
tests in `game_share_test/test_dynworld_vegetation.cpp`.

### 2026-11-14 — desert/lakes/roots vegetation missing (side-bank fix)

User report: only jungle had vegetation; desert and lakes stayed bare (prime
roots too). Root cause (§ "Vegetable data source per biome" above): the dyn
session loads only the jungle ecosystem bank (newbieland sheet →
`Ecosystem = jungle.ecosystem` → `jungle.smallbank`), and the soft-soil
materials of the other biomes bind tiles that live in other ecosystems'
banks — `SawDust` desert/lakes → `f-desert*` / `y-sable-marin` (desert/
lacustre banks), `Humus` roots → `PR-mousse*` (primes_racines bank). In the
jungle bank those groups resolved to tiles with **no vegetable desc**
(proof in the 2026-11-13 20:18 `dyn_vegetdump`: `material 'Humus' env=0/1
-> tile 1573 … OK` vs `material 'SawDust' env=2/3 -> tile 523/1615 tileset
'<no tileset>' EMPTY/MISSING`), so `CDynVegetableBlock::init` skipped them.
The placement engine was never at fault.

Fix: per-biome side banks (see the section above) + `dyn_vegetdump`
resolution reporting. All 13 dynworld test suites green (2026-11-14,
direct-exe Release run); client build green. In-engine validation: pending
user sign-off (expect desert/lakes to plant `Fyros_*`/`Tryker_*` flora and
roots `Pr_*`/`PR_*` flora; jungle unchanged).

### 2026-11-14 — desert plants wrong texels (merged atlas fix)

Follow-up after the side-bank fix: user validated in-engine that
desert/lakes now plant, but reported (1) desert **plants** look wrong
("mesh or uv mapping" — confirmed: the plants, not the terrain) and
(2) lakes sawdust unreachable (blocked by wall/cliff — separate issue,
tracked in the plan doc). Root cause of (1) (§ "Merged microveget atlas"
above): the manager's single stage-1 texture was the sheet's
`microveget_jungle.tga` atlas, so the newly planted non-jungle shapes
sampled jungle texels. Fix: `CMicroVegetDynAtlasTexture` (2x2 composite of
the four ecosystem atlases) + `ensureVegetShapeAtlasRemap` per-biome UV
remap at first shape use. Build green, all 13 dynworld test suites green
(2026-11-14, direct-exe Release run). In-engine validation pending: expect
correct per-ecosystem plant texels (jungle unchanged, desert = Fyros
atlas, lakes = Tryker atlas, roots = prime_roots atlas).

### 2026-11-14 — jungle mixes desert texels (remap ran under the fallback atlas)

User report after the merged-atlas fix: "desert still looks off, but
different. And now jungle is broken: veget looks a bit like desert ones".
Root cause (deployment + code, plan doc §8.1): (1)
`pipeline/install/microveget_dyn` had gone missing from the sandbox
`client.cfg` PreDataPath, so the composite precheck failed and the stock
single-atlas fallback was silently bound; (2) `ensureVegetShapeAtlasRemap`
was unconditional, so shapes still had their Tex0 quarter-compressed while
a single 512x512 atlas was bound — every plant rendered a quarter-sized
crop, jungle showing a fyros-major crop ("bit like desert ones") and
desert another wrong crop ("off, but different"). Fix: restored the
PreDataPath entry AND gated the remap on a new
`m_vegetMergedAtlasInstalled` flag (remap no-ops, one-shot warning, under
the fallback). Build green, 13 dynworld suites green (direct-exe Release
run). In-engine sign-off: desert and jungle look OK now.

### 2026-11-14 — per-season merged atlases (user request after sign-off)

The composite shipped BASE-only at first (documented limitation); user
asked for the season mapping to work. Implemented: season-aware
`loadDynVegetableAtlas(uint8 season)` with `_Sp/_Su/_Au/_Wi` source
variants (per-quarter BASE fallback), installed per complete continent
select (mid-session season changes re-select with the new season); same
quarter layout, remap records stay valid. Build green, 13 dynworld suites
green (direct-exe Release run). The TEMP debug instrumentation from the
atlas rounds (TGA dumps, remap log, fallback info log, load-time one-shot
dump) was stripped in this pass — durable warnings and the `dyn_*` command
catalog remain; season validation hint: `dyn_vegetseason <0-3|255>`.

### 2026-11-14 — shared-engine footprint strip (AGENTS.md dynworld-footprint rule)

Per the new AGENTS.md footprint rule, the branch's shared-file changes were
reduced to zero in `nel/`: `vegetable_manager.{h,cpp}` and
`vegetable_blend_layer_model.cpp` are back to stock (fade knob
`NL3D_VegetFadeSlopeScale`, the P0 `getVegetProbeState` probe accessors, and
`updateSortBlockForIg` stripped — the `dyn_vegettest` probe now calls the
public `CVegetableSortBlock::updateSortBlock` like the production
`CDynVegetableBlock` path); the `u_landscape.h` `loadDynVegetableAtlas`
virtual was replaced by a dyn-side `dynamic_cast<CDynLandscapeUser*>`
dispatch in `dyn_continent.cpp` (the Landscape global is a
`CDynLandscapeUser` in dyn sessions). `dyn_vegfade` went with the knob;
`dyn_vegetstats` now reports the dyn-side block inventory plus the stock
`getNumVegetableFaceRendered()`. Accepted residual shared-file changes
(additive): `game_share` CMake registration of `vegetation_placement`, the
`game_share_test` registration, and the `Voxel::Raw` guard in
`cursor_functions.cpp` (dynworld-only lines in an existing dyn hook).


