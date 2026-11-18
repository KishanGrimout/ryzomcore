# VoxelVolumePaging — dynworld voxel volume, chunk paging & lock architecture

> **System doc.** Documents the `TVolume` layer: the PolyVox `PagedVolume<Voxel, VolumeRWLock>`
> integration, the chunk paging pipeline, and every lock on the voxel read/write/page paths.
> The related **plan** (instrumentation → profile → optimize, open decisions) is
> `docs/VOXEL_PAGING_CONTENTION_PLAN.md`. Collision-side consequences of these locks are in
> `docs/VoxelTerrainCollision.md`; test-runner environment quirks in `docs/TestEnvironmentNotes.md`.

Status: **audit recorded 2026-11-16** (contention reported by user; instrument/profil/optimize
work pending — owned by the plan). The lock inventory below is source-audit fact; the
contention chains are hypotheses to be confirmed by the plan's profile phase.

## 1. Where the system lives

| Piece | Location | Role |
|---|---|---|
| `TVolume` typedef | `ryzom/common/src/game_share/dynworld/polyvox_defs.h:73` | `PolyVox::PagedVolume<Voxel, VolumeRWLock>`, chunk side **64** (one chunk == one LOD0 sector: 32 m at 0.5 m voxels) |
| `VolumeRWLock` | `polyvox_defs.h:39-69` | NLMISC::CFastReaderWriter (lock-free reader/writer with upgrade/downgrade), used both as volume-level and chunk-level lock type |
| `MultiThreadChunkCache` | `polyvox_defs.h:87-110`, `polyvox_defs.cpp` | per-thread last-chunk cache (`thread_local`), the only lock-free fast path |
| PolyVox fork | `D:\Programmation\polyvox` | diverges from stock: template `RWLockType`, per-chunk RW lock, `tryGetChunk`, `IChunkCache`, chunk-array eviction — dynworld-driven changes |
| `CMapBase` | `ryzom/common/src/game_share/dynworld/map.{h,cpp}` | owns the volume + sector metadata lists; all collision/voxel queries live here |
| Pagers | `master_pager.cpp` (server, `CMapServer`), `slave_pager.cpp` (client, `CMapClient`) | `pageIn`/`pageOut` implementations |
| Generation | `ryzom/common/src/game_share/dynworld/mapgen/*` | `CMapGen::makeChunk` (noise maps, ground, caves, trees) into a `CBlockMakeData` over the chunk |
| Sector cache | `map_cache_manager.cpp` | `tryFillRegion` (load) / `saveRegionToCache` (save) — file IO |

## 2. Lock inventory (source-audit facts, with entry points)

Four lock layers, from the caller down to the chunk bytes:

### 2.1 Per-chunk RW lock (`PagedVolume::Chunk::m_lock`, type `VolumeRWLock`)

- **Since P3a' (2026-11-17), the chunk write lock is no longer held during
  `pageIn`.** The chunk carries an atomic `m_bPagingInProgress` flag instead:
  raised before the chunk is published into the chunk array, cleared after the
  pager body completes. Readers (getVoxel/setVoxel/samplers/getChunk) check the
  flag and *spin on it with no locks held* (`std::this_thread::yield`) until the
  data is ready, then take the chunk read lock normally; they never observe
  uninitialized buffer contents (flag raised before publication; release-store
  at finalize).
- Read-locked **per voxel read** in `getVoxel` (`PagedVolume.inl:147`) — still the
  per-voxel cost (P3e target, finding F4).
- `getExistingChunk` no longer acquires the chunk lock at all: it is a peek
  (position match + timestamp touch), so the internal-lock read is never held
  across a page-in wait (the old P3c chain, absorbed).
- `isLocked()` ORs the paging flag, so `getRegionStatus` keeps reporting
  `Locked` for a chunk whose page-in is in progress (region-status semantics
  preserved; unit-tested in `ryzom_dynworld_paging_stats_test`).

### 2.2 Per-volume internal lock (`m_internalLock`, type `VolumeRWLock`)

- Read: `tryGetChunk`, `getChunk`'s first lookup, `getExistingChunk` scan (short
  peek — no chunk-lock acquire, no wait).
- **Write**: chunk creation + hash-slot insertion + eviction. The creation's
  O(65536) oldest-chunk scan runs **only when the incrementally-maintained
  `m_uChunkCount` exceeds the limit** (P3b, 2026-11-17); during steady churn it
  still runs per creation (~80 µs, measured — full LRU-list redesign is P4
  scope). Eviction skips chunks whose page-in is in progress (their buffers are
  being filled outside all locks by the pager thread). Also `flush`/`flushAll`
  (which now maintain `m_uChunkCount`).
- Contention shape after P3a': cache-missing readers wait on the *target
  chunk's flag* with no volume lock held, so a slow page-in no longer blocks
  chunk creation/eviction on other chunks, nor other readers. The remaining
  wait is per-target-chunk and bounded by that chunk's page-in only.

### 2.3 Per-volume RW lock (`m_volumeLock`, type `VolumeRWLock`)

- **Read on every `getVoxel` call** (`PagedVolume.inl:132`) — including DDA sweeps and
  raycasts that already hold a region lock (double acquisition, see 2.4 note).
- Write: only `writeLockRegion`/`writeLock` — **no caller today** (grep 2026-11-16:
  zero uses of `writeLockRegion` in ryzom). Writers would starve behind continuous
  readers since no writer is expected, keep an eye when adding any.

### 2.4 `CMapBase::m_volumeLock` (NLMISC::CMutex — plain EXCLUSIVE mutex)

- Held in `regionPagedIn` / `regionPagedOut` (`map.cpp:509,539`) — i.e. at the END of
  every `pageIn`/`pageOut`, notifying listeners and moving sector metadata — and in
  `getSectorInfo` (`map.cpp:57`), `diffSectors` (`map.cpp:110`), `getLoadedSectors`
  (`map.cpp:365`).
- Not the volume bytes — it guards the sector-metadata lists. Exclusive (not RW), so
  each paged-in sector briefly serializes with every `getSectorInfo` caller.
- `traceSegment` / `getGroundNormal` use `TVolume::AutoReadLockRegion` (chunk-level
  read locks across the swept region, `map.cpp:238,339`); their inner `getVoxel` calls
  re-acquire 2.3 read + 2.1 read per voxel (layered, not recursive-deadlocking today
  because the region path locks chunks directly and `getVoxel` locks volume+chunk).

### 2.5 Misc

- `biome_manager` TLS-array lock (`biome_manager.h:40`, short, registration only).
- `map_deferred_command_manager` queue lock (command hand-off only).
- `castRay`'s per-voxel path goes through `getVoxel` (2.3 read + 2.1 read per step) on
  top of its own iteration logic; `traceSegment`'s DDA is under an
  `AutoReadLockRegion` and still calls `getVoxel` per step (same double-locking).

## 3. Paging pipeline (the pageIn path end to end)

Updated for the P3a' lock protocol (2026-11-17):

```
getVoxel(x,y,z)                                   PagedVolume.inl:130
  volume read lock (2.3)                          :132
  thread-local cache hit? (+ paging-flag check)   :142  (fast path, no lock)
  miss → getChunk(cx,cy,cz)                       :145  → PagedVolume.inl:399  (re-lookup loop)
      internal read lookup (2.2) — peek, no chunk lock
      hit + flag clear → done (chunk ready)
      hit + flag set    → spin on m_bPagingInProgress (NO locks held), re-lookup
      miss → internal WRITE (2.2)
        create Chunk, raise m_bPagingInProgress BEFORE publication,
        slot insert, eviction only if m_uChunkCount > limit (P3b)
      (internal lock released)
      chunk->pageIn()                             ← NO locks held (P3a')
        pager->pageIn(region, chunk)
          CSlavePager:  tryFillRegion (file IO)   slave_pager.cpp:43
                        else makeChunk + saveRegionToCache (file IO!)  :47,54
          CMasterPager: makeChunk only (cache load/save commented out)   master_pager.cpp:38-47
          map.regionPagedIn(...)                  → CMapBase::m_volumeLock CMutex (2.4)
      finalize: chunk writeLock+unlock (barrier), flag release-store false
  chunk read lock (2.1)
  return voxel
```

Concurrent miss on the SAME chunk by other threads: they find the chunk in the
read-branch peek, see the paging flag, spin **with no volume lock held** (the
`WaitPagingChunk` site), then re-lookup — the pile-up no longer holds 2.2 read
(the old H1/P3c chain). Cross-chunk work (creations, evictions, other chunks'
reads) proceeds during the wait.

## 4. Suspected contention chains (to confirm in profile phase)

| # | Chain | Symptom |
|---|---|---|
| H1 | From-scratch `makeChunk` under chunk write lock (2.1) → cache-missing readers queue on 2.1 **while holding 2.2 read** → chunk creation/eviction writer starves behind them → whole volume's miss path frozen for the generation duration | frame hitches / multi-service stalls exactly when a new region is touched |
| H2 | Client main-thread `getVoxelAt` (collision query) hits an un-paged chunk → synchronous full `makeChunk` in the movement path (no prefetch covered that chunk) | long-frame stall on the player thread |
| H3 | `prepareExtraction` (`map_client.cpp:198-251`) `emergeSector`s whole sectors synchronously in the extraction path → serialized page-ins each holding 2.1 write; mesh-extraction + movement threads contend | extraction stalls movement, movement stalls extraction |
| H4 | `regionPagedIn` exclusive CMutex (2.4) held per paged sector while `getSectorInfo` is called from hot paths | short but global serialization; matters when many sectors page in a burst |
| H5 | Server side: `CMasterPager` always generates from scratch (cache commented out, `master_pager.cpp:38`) → H1 is the *steady state* on GPMS whenever entities roam into fresh sectors | GPMS move-thread stalls |

## 5. Instrumentation registry

Registered by plan P1 (2026-11-16). All markers are **always-compiled, near-zero-cost
atomic counters** (count / total / max ticks) in `voxel_paging_stats.h`
(`game_share/dynworld/`), dumped via the client `dyn_paging_stats` command
(`[reset]` argument clears for a fresh window) and callable from tests via
`voxelPagingStats().dump()`. The PolyVox event sites are fed through the PIX-macro
hook in `polyvox_defs.h` (the fork expects the includer to define `PIX*`; no
PolyVox change beyond the one below). Unit coverage:
`ryzom_dynworld_paging_stats_test` (10 tests).

| Marker | Fed by | What it measures | Strip gate |
|---|---|---|---|
| `ReadLookupChunk` | PolyVox `getChunk` read-path lookup (`PagedVolume.inl:405`, one added `PIX` pair — strippable PolyVox edit) | VPL: cache-miss reader lookup cost; since P3a' no longer includes the page-in wait (that moved to `WaitPagingChunk`) | plan P5 |
| `WaitPagingChunk` | PolyVox `getChunk` flag spin (added in P3a') | VPL: reader wait for a chunk whose page-in is in progress, no locks held | plan P5 |
| `pageInWaitChunkQuery` | `CVoxelPagingPix` accounting, when the waiting thread's `EVoxelPagingRole` is `Query` (set by `CVoxelPagingRoleScope` in `CDynGlobalRetriever::testCylinderMove`/`doMove`) | VPL: the subset of `WaitPagingChunk` paid by the movement/collision query thread — the user-visible hitch cohort (D4) | plan P5 |
| `pageInChunkQuery` / `pagerPageInQuery` / `cacheTryFillQuery` / `mapGenMakeChunkQuery` | same `Query`-role split, applied to the page-in **work** sites | VPI: page-in work *performed* on the movement/collision thread (self page-in — pays no wait, so the wait counter cannot see it) | plan P5 |
| `frameHitch` | `CDynGlobalRetriever::refreshLrAround`, called once per main-loop frame | main-loop gap count/total/max (gaps ≥ 100 ms) — the hitch counter; each gap logs a `DYNHITCH` line with the paging deltas accumulated during it | plan P5 |
| `DYNQPAGEIN` (log, not a counter) | `CSlavePager::pageIn`, only when `voxelPagingRole() == Query` | chunk region + `source=cache|generated` of a page-in *performed by* the movement/collision thread; pairs with the `DYNHITCH` camera position to classify prefetch lag vs a wrong-position lookup | plan P5 |
| `GetExistingChunk` / `CreateChunk` / `SearchFreeChunkSlot` / `SearchOldestChunk` / `DeleteOldestChunk` / `PageInChunk` | PolyVox write-branch event sites (pre-existing `PIX` markers, now wired) | VPW: internal-lock write work; `SearchOldestChunk` only fires when over the chunk limit (P3b); `PageInChunk` = the pager body with **no locks held** since P3a' | plan P5 |
| `pagerPageIn` | `CSlavePager`/`CMasterPager::pageIn` | VPI: total pageIn (chunk-lock hold from the pager side; nested sums include the sub-sites below) | plan P5 |
| `cacheTryFill` / `cacheReadChunk` | `CSlavePager` + `map_cache_manager.cpp` | VPI: cache load path total / the disk+LZMA unpack part | plan P5 |
| `mapGenMakeChunk` | both pagers around `CMapGen::makeChunk` | VPI: from-scratch generation | plan P5 |
| `cacheSaveCall` | `CSlavePager` around `saveRegionToCache` | VPI: synchronous save part (copy; compression already deferred) | plan P5 |
| `regionPagedIn` / `regionPagedOut` | both pagers around the map notification | VPI: notification incl. metadata lock (nested: includes `mapLock*` below) | plan P5 |
| `mapLockGetSectorInfo` / `mapLockDiffSectors` / `mapLockGetLoadedSectors` / `mapLockRegionPagedIn` / `mapLockRegionPagedOut` | `map.cpp` around each `CMapBase::m_volumeLock` (exclusive CMutex) scope | VPM: metadata-lock hold per site | plan P5 |
| per-`getVoxel` volume read acquire cost (VPV) | not a counter — to be estimated by microbench/profiler in P2 (per-voxel counters would defeat their purpose) | 2.3 volume RW-lock acquire/release overhead | plan P2 |

The one PolyVox edit of P1 (D3): the `ReadLookupChunk` PIX pair around the read-path
lookup in `getChunk` — recorded as the single strippable PolyVox residual of P1.

### 5.1 PolyVox fork residual edits (D3 record, strippable at plan P5)

The P3a'/P3b restructuring (2026-11-17) required the following edits in
`D:\Programmation\polyvox\include\PolyVox\` (the fork has no dynworld-side
extension point for chunk-publication ordering; justification: moving the
page-in body out of the chunk write lock is exactly the fork's locking logic):

- `PagedVolume.h`: `#include <atomic>`; `Chunk::m_bPagingInProgress`
  (std::atomic<bool>) + public `isPagingInProgress()`; volume member
  `m_uChunkCount` (incremental count, P3b).
- `PagedVolume.inl`: `#include <thread>`; `getExistingChunk` converted to a
  peek (no chunk-lock acquire/wait); `getChunk` restructured into the
  claim → lock-free pager body → finalize protocol (re-lookup loop,
  eviction skip of in-progress chunks, incremental count gate); paging-flag
  checks added to the `getVoxel`/`setVoxel` cache-hit fast paths;
  `flush`/`flushAll`/`calculateSizeInBytes` maintain/use `m_uChunkCount`;
  added `WaitPagingChunk` PIX site.
- `PagedVolumeChunk.inl`: `isLocked()` ORs the paging flag; (the flag itself
  is default-initialized in the header).
- `PagedVolumeSampler.inl`: paging-flag check on the cache-hit fast path.

All are marked with `NOTE(dynworld) P3a'`/`P3b` comments in place. Semantics
preserved: readers never observe uninitialized chunk data, region status keeps
reporting `Locked` during page-ins, eviction never targets a chunk whose buffer
is being filled, and `~Chunk`/pageOut behavior is unchanged. Pre-existing
hazard NOT introduced nor fixed by these edits: `flush`/`flushAll` can destroy
a chunk while a pager thread fills it (destructor `pageOut` + `delete[] m_tData`
race) — the timestamp ordering makes eviction safe, but a concurrent flushAll
during active paging was unsafe before P3a' and remains so; flagged for the P4
async design.

## 6. Findings catalog

(Append dated, numbered findings here once profiling starts; do not re-derive
diagnosed root causes.)

### 6.1 Thread & call-site inventory (P0, source audit 2026-11-16)

**Client** (`CMapClient` + `CSlavePager`; volume owned by `CDynContinent::MAP`):

| Site | Path | Thread |
|---|---|---|
| Movement collision | `pacs_client` → `CDynGlobalRetriever::testCylinderMove`/`getMeanHeight` → `getVoxelAt`/`traceSegment` (`dyn_global_retriever.cpp:403,785-814`) | main (movement update) |
| Voxel selection/edit | `cursor_functions.cpp:611,985-1008` (`getVoxelAt`/`setVoxelAt`), `voxelUnderCursor` → `castRay` (`map_client.cpp:376`) | main |
| Debug commands | `commands.cpp:5522,5590` (`setVoxelAt`) | main |
| IG placement check | `dyn_instance_group_user.cpp:133` (`getVoxelAt`) | main |
| Landscape ray casts | `dyn_landscape.cpp:1960,1967` (`castRay`) | main (verify) |
| Weather clip grid | `precipitation_clip_grid.cpp:665` (`getMeanHeight`) | verify |
| Prim load ground | `prim_file.cpp:85` (`getMeanHeight`) | main (load) |
| **Mesh extraction** | worker `CGenerateMeshCommand::execute` (`dyn_landscape_user.cpp:157-210`) → `extractMesh` → `prepareExtraction` (`map_client.cpp:198-251`) → **synchronous `emergeSector` burst** (`map_client.cpp:221,246`) | **CParallelTaskManager worker** |
| **Prefetch commands** | `CPrefetchSectorCommand` jobs (`dyn_landscape_user.cpp:225`, `dyn_landscapeig_manager.cpp:124`) → `emergeSector` → `m_volume->prefetch` → `getChunk`/`pageIn` per chunk | **CParallelTaskManager workers** |
| **Cache save** | `CompressAndSaveRequest` (priority −3, `map_cache_manager.cpp:96,235`) | workers (the 256 KB copy is still made inside `pageIn`, under the chunk write lock) |

Consequence: **concurrent `pageIn` on the same map's pager is real today** — several
mesh/prefetch commands run in parallel on worker threads while main-thread queries
miss into the same volume.

**Server** (`CMapServer` + `CMasterPager`, per `dyn_continent_container.cpp`):

| Site | Path | Thread |
|---|---|---|
| Move validation | GPMS `movePlayer` → `testCylinderMove`/`getMeanHeight` (`world_position_manager.cpp:2111-2318`) | GPMS main loop |
| SET_VOXEL apply | `CWorldPositionManager::setVoxel` → `setVoxelAt` (`world_position_manager.cpp:3358-3372`) | GPMS main loop |

- **No server-side prefetch/emerge exists** — server chunks page in *lazily from the
  move-validation query itself*; a fresh sector's first move pays a full
  from-scratch `makeChunk` on the GPMS move loop (stall, not multi-thread contention;
  `CMasterPager` has the cache load/save commented out, `master_pager.cpp:38`).

### 6.2 D5 verification — generation-path thread safety (P0, 2026-11-16)

| Shared state | Verdict |
|---|---|
| `CBiomeManager` noise impls | **Safe** — `NLMISC::CTDS` per-thread impl (`biome_manager.h:35`), each thread lazily builds its own generators (`biome_manager.cpp:54-68`) |
| `CMapGen` members (`flags`, `csize`) | **Safe** — read-only during `makeChunk` |
| Mapgen noise/RNG in flora/cave paths | **Safe** — function-local (`mapgen.cpp:691,694,883-884`) |
| `g_logMinSoil`/`g_logMaxSoil` (`mapgen.cpp:125-126`) | **DATA RACE (F1)** — written per-voxel in `cacheTerrainLevel` (`mapgen.cpp:170-173`) from concurrent workers; stats-only but UB. Trivial fix (atomics/TL) — fold into P3 |
| `CMaterialLibrary` | **Invariant, not enforced** — plain static-pointer singleton (`nel/misc/singleton.h:124-126`, no sync); `getEnvironmentDef`/`getMaterialId` are read-only after load. Depends on the library being fully loaded on the main thread before any worker runs `makeChunk` (the `CGenerateMeshCommand` ctor comment acknowledges this discipline). Record as a hard invariant |
| `CDiskFileDataBase` | One file per sector id, fresh `CIFile` per call (`disk_file_db.h:47-80`) → concurrent reads of different sectors are safe; a read racing a deferred compress-save of the *same* sector is theoretically possible (low risk; note) |
| `CMapDeferredCommandManager` | `CFairMutex` queue — fine |

### 6.3 Other P0 facts

- **F2**: `CMapCacheManagerImpl::readChunk` does the disk read **and LZMA unpack of a
  256 KB chunk** synchronously inside `pageIn` (`map_cache_manager.cpp:99-135`), i.e.
  under the chunk write lock; the save side is already deferred (`saveChunk` copies
  into a `CMemStream` then queues compression) — the copy itself is under the lock.
- **F3**: server has no prefetch at all; client's `prepareExtraction` bursts
  synchronous `emergeSector` loops both on workers and on the main-thread
  extraction path (`dyn_landscape_user.cpp:453`).
- `PagedVolume` eviction deletes the oldest chunk by simple `m_arrayChunks[idx] =
  nullptr` (`PagedVolume.inl:481-485`) — the shared_ptr may keep it alive while
  readers hold it, but the slot is freed for creation (no pageOut on explicit
  delete; `~Chunk` pages out via the destructor when the last reference dies).

### 6.4 Profile results (P2, client sandbox session 2026-11-17)

Session: normal play + fast traversal into fresh terrain; three `dyn_paging_stats`
dumps (two 10 s windows after resets, one session-cumulative). Raw dumps in the
client log (`pipeline/client_dev/local/client*.log`, 18:26–18:27). Chunk = 1 MB
(Voxel = 4 B, 64³); volume limit 1024 chunks (`ClientCfg.ChunksMemoryCache` = 1024 MB).

| Marker | 10 s active window | Session cumulative | Reading |
|---|---|---|---|
| `pageInChunk` / `pagerPageIn` | 1367, total 8.50 s, **avg 6.2 ms, max 63 ms** | 3473, total 17.3 s, **avg 5.0 ms, max 246 ms** | chunk write lock held for the whole page-in, continuously across workers |
| `cacheTryFill` / `cacheReadChunk` | 1367, 8.49 s, max 63 ms | 3473, 16.4 s, max 246 ms | **95–100 % of the page-in hold is disk read + LZMA unpack** of the 1 MB sector file |
| `mapGenMakeChunk` | 0 | 4, total 0.843 s, **max 242 ms** | from-scratch generation ≈ 240 ms per chunk when the cache misses (matches H5's server steady state) |
| `readLookupChunk` | **14.9 M reads / 10 s**, total 3.68 s CPU, max 24 ms | **33.2 M reads**, total 8.2 s CPU, **max 233 ms** | per-voxel internal-lock read lookup ≈ 0.5 µs each; the max values track the page-in maxima → readers queue behind page-ins (the H1 shape) |
| `createChunk` | 1367, max 3 ms | 2754–3473, max **269 ms** (one outlier) | creation is fast in steady state; the single 269 ms outlier in the cumulative window needs no action (see F8) |
| `searchOldestChunk` | 1367, max <1 ms | 3473, 0.276 s total (~80 µs each) | O(65536) scan is not a bottleneck |
| `deleteOldestChunk` | 1367 (per creation) | 3473 (per creation) | **eviction churn: every creation evicts one chunk** (F6) |
| `regionPagedIn` + `mapLockRegionPagedIn` | 1367, max ≤2 ms | 3473, 17 ms total | H4 metadata CMutex **refuted** as a contention source |
| `mapLockGetSectorInfo` | 10894, 4 ms total | 23612, 8 ms total | idem |

**Chain verdicts:**

- **H1 CONFIRMED** — the chunk write lock is held across the entire page-in
  (~6–8 ms average, up to 300 ms), and cache-missing readers queue on it while
  holding the internal lock read (`readLookupChunk` max 233–300 ms ≈ the page-in
  maxima). Page-in work runs *continuously* across worker threads (8.5 s of pageIn
  inside a 10 s window), so the stall window is a steady-state condition, not a burst.
- **H2 CONFIRMED as a stall risk** — a collision query thread that misses on a chunk
  currently paging in waits up to ~300 ms (readLookupChunk maxima); the movement
  thread is one of the readers (likely the main-thread hitch the user reports).
- **H3 PARTIALLY CONFIRMED** — page-in bursts (1367 chunks / 10 s) come from the
  extraction prefetch path, but they already run on workers; the cost shows up as
  reader queueing rather than extraction stalls.
- **H4 REFUTED** — `CMapBase::m_volumeLock` (metadata CMutex) is negligible.
- **H5 UNMEASURED server-side** (shard leg still pending); client-side the cache
  covers 99.9 % of page-ins (4/3473 generate from scratch, ~242 ms each).

**New findings:**

- **F4 (per-voxel read path aggregate cost)**: `readLookupChunk` = 33 M calls with
  8.2 s CPU across the session (~0.5 µs per voxel read). Every `getVoxel` pays a
  volume RW read + internal-lock read lookup + hash-slot scan + chunk RW read
  (three lock round-trips and a potential O(65536)-bounded array walk), because
  the single-slot thread-local chunk cache defeats itself whenever reads alternate
  chunks (DDA sweeps, marching-cubes extraction over chunk boundaries). Aggregate
  core-time cost is comparable to the page-in stalls themselves — P3e target.
- **F5 (page-in hold is IO, not generation, on the client)**: 95–100 % of the
  chunk-lock hold is `cacheReadChunk` (disk + LZMA unpack). The P3a/P4a principle
  "generate/load outside the lock" applies equally to the cache-load path: the
  read+unpack should also move out of the chunk write lock (decompress to a buffer,
  then a short locked install).
- **F6 (eviction churn)**: the working set exceeds the 1024-chunk limit during
  traversal — every creation evicts the oldest chunk. Consequence: page-ins are
  constant during play, so any lock-hold reduction has steady-state value; also
  returning to previously-visited areas re-reads them from disk. Not a defect —
  tune `ChunksMemoryCache` upward when minimizing stalls matters.
- **F7 (from-scratch generation measured)**: `mapGenMakeChunk` ≈ 242 ms per chunk
  (64³, caves disabled). This is what the GPMS move loop would pay per fresh
  sector (H5) — server-side async paging (P4) or cache enabling are the candidates.
- **F8 (createChunk outlier)**: one 269 ms `createChunk` in the cumulative window —
  1 MB allocation occasionally hitting a slow heap/commit path. Single sample, no
  action; watch for recurrence after P3.

### 6.5 P3a' invariant break found live: `Locked` no longer implies sector metadata (2026-11-17)

**Symptom**: `nlassert(infos != nullptr)` in `CMapClient::prepareExtraction`
(map_client.cpp:227, via `CGenerateIgCommand::execute` → `CDynInstanceGroupUser::init`)
during normal IG load in the sandbox.

**Root cause**: an old-protocol invariant broke. Before P3a', `getRegionStatus` reporting
anything other than `NotAvailable` implied the sector metadata was registered, because
`tryGetChunk`/`getChunk` blocked on the chunk write lock until the whole page-in —
including the pager's `regionPagedIn` (metadata registration) — had completed. Under
P3a' the page-in no longer holds the chunk write lock: a chunk whose page-in is in
progress reports `Locked` while its metadata is not yet registered. The metadata
registration (`regionPagedIn`, called by the pager inside its pageIn body) happens
before finalize clears the paging flag, so a thread that blocks in `getChunk` on the
flag is guaranteed to see the metadata when it returns — but a thread that merely
polls the status enum is not. `CMapBase::emergeSector` (map.cpp:83) treated
`!= NotAvailable` as "already
loaded", skipped `prefetch`, and the caller's `getSectorInfo` returned nullptr.

**Fix (dynworld-owned, map.cpp)**: `emergeSector` now prefetches whenever
`isSectorLoaded(sectorIndex) != SectorStatus::Available`. The `prefetch` → `getChunk`
call waits on the chunk's paging flag, so it blocks until the in-progress page-in
completes and `regionPagedIn` has delivered the metadata — restoring the invariant
"a sector that is not `NotAvailable` has metadata" through emergeSector, which becomes
its guarantor. Both client prefetch managers (`dyn_landscape_user.cpp`,
`dyn_landscapeig_manager.cpp`) were audited: they already handle `Locked` correctly
(re-issue the prefetch command / keep `m_prefetchDone = false`), so no other caller
needed a change. `getSectorInfo` has no other unguarded caller paths.

**Regression test**: `DynWorldPagingStats.EmergeSectorWaitsForInProgressPageIn`
(ryzom_dynworld_paging_stats_test) — an emerger thread calls `emergeSector` while a page-in
is blocked inside the test pager; it must not complete before the pager is released, and
the metadata must be registered (`IsFull`) when `emergeSector` returns. Full dynworld
exe set (15 suites) green after the fix; client rebuilt.

**Takeaway**: after P3a', `getRegionStatus`/`isSectorLoaded` returning `Locked` means
"page-in in progress" and says nothing about metadata; any new consumer that needs
metadata must go through a blocking call (`emergeSector`, `getChunk`) rather than
trusting the status enum.

### 6.6 P3a' after-profile (client sandbox session 2026-11-17, 20:27–20:34)

Session: fresh-terrain traversal, ~7 min, heavy generation load (601 from-scratch
generations vs 4 in the P2 baseline). Cumulative dump at 20:34:26 (raw lines in
`pipeline/client_dev/local/client.log`); counters reset after the dump.

| Marker | P2 baseline (§ 6.4) | P3a' after | Verdict |
|---|---|---|---|
| `readLookupChunk` | 33.2 M, 8.2 s CPU, **max 233 ms** | 56.0 M, 9.35 s CPU, **max 7 ms** | reader max collapsed to ordinary lookup cost — H1 stall gone; avg ~0.25 → ~0.167 µs/call |
| `pageInWaitChunk` | (n/a) | 182 waits, 12.4 s total, **max 404 ms** | waiters now isolated: only threads touching a chunk mid-page-in spin on the flag, lock-free; max ≈ `pageInChunk` max (425 ms) |
| `pageInChunk` / `pagerPageIn` | 3473, 17.3 s, avg 5.0 ms, max 246 ms | 9397, 122.4 s, avg 13 ms, max 425 ms | pager time dominated by 601 fresh generations (99 s, avg ~165 ms) — running lock-free off the readers' path |
| `cacheReadChunk` | 3473, 16.4 s, max 246 ms | 9398, 23.2 s, **max 49 ms** | IO max down 5× |
| `mapGenMakeChunk` | 4, max 242 ms | 601, 99 s, avg ~165 ms, max 425 ms | generation stress session; no reader-side coupling anymore |
| `createChunk` | 2754–3473, max 269 ms | 9398, **max 1 ms** | creation outlier gone |
| `getExistingChunk` | (n/a) | 9449, max 1 ms | peek path cheap |
| `searchOldestChunk` / `deleteOldestChunk` | ~80 µs each | 9398 each, 1.07 s / 0.074 s, max 2 ms | eviction O(65536) scan still cheap |
| `mapLockRegionPagedIn` / `Out` / `GetSectorInfo` | negligible | 47 / 48 ms / 14 ms over 36–94 k calls | H4 still refuted |

**Verdict (P3a' validated)**:
- **H1 eliminated** — `readLookupChunk` max 233 ms → 7 ms: readers no longer queue
  behind page-ins. The movement thread and extraction workers only wait when they
  genuinely need the chunk being paged in (`pageInWaitChunk`, 182 events), and that
  wait holds no locks.
- The residual 12.4 s of `pageInWaitChunk` (max 404 ms ≈ generation max) is the
  **D4 synchronous-wait cohort** — inherent until P4 async paging ("blocked with
  last-known fallback") lands; then waits become non-blocking collision fallbacks.
- F8's createChunk outlier did not recur (max 1 ms over 9398 events).
- Session caveat: this was a generation-heavy window (fresh terrain, 601
  generations); steady-state cache-hit play will show even less pager work.

### 6.7 P3e — hot read path: multi-slot TLS cache + movement prefetch (2026-11-17/18)

**P3e-1: multi-slot TLS chunk cache** (`polyvox_defs.h/.cpp`, dynworld-owned — no
PolyVox edit). The single-slot `MultiThreadChunkCache` was defeated by any sweep
alternating chunks (DDA collision sweeps, marching-cubes samplers), so every
voxel read fell through to `getChunk`'s internal-lock lookup — 33–56 **million**
`ReadLookupChunk` events per session (F4). Replacement: direct-mapped 32-slot TLS
cache keyed by chunk coordinates, spatial hash with odd multiplicative constants,
so chunk-space neighbors (+/−1 on one axis) never land in the same slot —
boundary-crossing sweeps keep all their working slots warm. `dirtyCache()` clears
every slot (flushAll/flush semantics unchanged).

Measured (client session 2026-11-18, cumulative windows):

| Marker | P3a' baseline (§ 6.6) | after multi-slot cache |
|---|---|---|
| `readLookupChunk` count | 56.0 M / 9.35 s | **1.0–1.3 M** per session window |
| `readLookupChunk` avg / max | ~0.17 µs / 7 ms | ~6.7 µs / 27–31 ms |
| per-voxel behavior | most reads paid internal lock + hash probe | warm alternating reads are pure TLS hits |

`readLookupChunk`'s remaining avg/max is internal-lock contention against the 30 k+
chunk creations/evictions of a long heavy-traversal session (eviction scan under
the internal write lock) plus OS descheduling under paging load — it is no longer
coupled to the page-in holds (page-in max 422 ms, lookup max 31 ms).

Regression test: `MultiSlotChunkCacheSurvivesChunkAlternation` — 3 chunks × 100
alternating warm reads produce ZERO `ReadLookupChunk` events and per-chunk values
stay distinct.

**P3e-2: movement/collision prefetch** (the hitch fix). The hitches reported in
play were the D4 synchronous-wait cohort: movement/collision queries blocking on
a chunk whose page-in (from-scratch generation, ~165–425 ms) was in progress
(`pageInWaitChunk`, up to ~420 ms max). Investigation finding: **the stock PACS
around-position prefetch (`CDynGlobalRetriever::refreshLrAround` /
`refreshLrAroundMax`) was a stub** — the `selectInstances` box + `emergeSector`
loop were entirely commented out; only the client's *visual* landscape/IG
prefetch kept the camera neighborhood warm, which is why cursor-raycast reads look
smooth while movement occasionally hitched.

Fix (dynworld-owned):
- `CMapBase::prefetchRegionAsync(region)` — a latest-wins async prefetch worker
  (one thread per map, created in `initMap`, joined first thing in `~CMapBase`
  before the volume dies). The worker calls `m_volume->prefetch(region)`;
  requests overwrite a pending slot so per-frame callers cannot queue up work.
  `prefetchRegion(region)` is the synchronous flavor (blocking load-time calls).
- `CDynGlobalRetriever::refreshLrAround` (called every main-loop frame with the
  camera position) now feeds the async worker; `refreshLrAroundNow` (continent
  init under the progress bar) uses the synchronous prefetch. Region: the tuned
  `g_prefetchBound` (25 m) box in global voxel coords — the caller's radius
  (static-PACS streaming radius, 400 m) is deliberately ignored, it would thrash
  the 1024-chunk volume limit. Coverage is camera-centred only; covering the
  player/collision position as well is tracked in the plan.
- Semantics unchanged (D4): queries still block synchronously when they outrun
  the prefetcher; the prefetch only makes that rare (walk speed ≈ 3 voxels/s vs
  165 ms generation per 64-voxel chunk).

Regression test: `PrefetchRegionAsyncPagesInBackground` — the async call returns
without blocking on the pager, the region becomes `Available` on the worker, and
back-to-back (latest-wins) requests still complete.

**Movement-wait attribution** (added 2026-11-18, before accepting the prefetch as a
hitch fix). `pageInWaitChunk` alone cannot tell a user-visible hitch from a
background thread waiting on the prefetcher's own generation: with ~30 k page-ins
per heavy-traversal window, most waits occur on extraction/IG/paging threads.
`voxel_paging_stats.h` now carries a thread-local `EVoxelPagingRole`
(`None`/`Query`) with an RAII `CVoxelPagingRoleScope`; `CVoxelPagingPix` splits
every `WaitPagingChunk` end event made under the `Query` role into the
`pageInWaitChunkQuery` counter. The role is raised at the two entry points a
movement tick uses, `CDynGlobalRetriever::testCylinderMove` and
`CDynGlobalRetriever::doMove`; it is thread-local and restored by the scope, so
worker threads are never marked.

Reading the dump: `pageInWaitChunkQuery` ≈ 0 over a movement window means the
movement/collision thread no longer blocks on page-ins and the remaining
`pageInWaitChunk` is background churn (the prefetch working as intended);
non-zero means the prefetcher is being outrun (too small a box / too late a lead)
and the box or lead margin needs tuning.

Regression test: `PageInWaitAttributionFollowsQueryRole` — a wait paid by a
`Query`-marked thread lands in both counters, an off-role wait only in the total,
and the role marker is thread-local and nesting-safe.

**Measured validation (client build 2026-11-18 08:36, two fresh windows):**

| Marker | window 1 (97 s) | window 2 (273 s) |
|---|---|---|
| `pageInWaitChunkQuery` | **0** | **0** |
| `pageInWaitChunk` (all threads) | 581 / 6.85 s / max 418 ms | 2828 / 25.97 s / max 418 ms |
| `pageInChunk` | 14 102 | 87 262 |
| `readLookupChunk` | 969 926 / 3.74 s / max 36 ms | 2 610 852 / 21.0 s / max 23 ms |
| `mapGenMakeChunk` | 137 | 571 (0.65 % of page-ins) |
| `cacheTryFill` | 69.9 s | 250.5 s |

Reading: over 370 s of walking, the movement/collision thread paid **zero**
page-in waits, while the same volume served 87 k page-ins (peak ≈ 320/s) and
remained so far over the 1024-chunk limit that `searchOldestChunk` ran on
essentially every creation. The prefetch + P3a' flag spin completely removed the
D4 movement cohort — the residual waits all belong to background
extraction/IG/paging threads waiting on each other's in-flight chunks, which is
normal queueing and not user-visible.

Side observation for P4: ~99 % of page-ins are `cacheTryFill` (disk + LZMA) and
the volume thrashes the limit continuously, so the next meaningful cost is the
cache read/decompress pipeline itself, not locking.

**Blind spot closed + hitch probe (2026-11-18, second session).** In a 42 s
window the user still counted **4–5 hitches** while `pageInWaitChunkQuery` stayed
0. Root cause of the ambiguity: a thread that *performs* its own page-in pays no
wait at all — `getChunk` pages a missing chunk in on the caller's thread. The
attribution was therefore extended from waits to **work**: `pageInChunkQuery`,
`pagerPageInQuery`, `cacheTryFillQuery`, `mapGenMakeChunkQuery` (same
`Query`-role split applied to the page-in sites; `CVoxelPagingStatScope` now feeds
`accountVoxelPagingStat` so pointer-identity splitting covers the pager-side
scopes too).

Same window, for the record (42 s, 10 910 page-ins ≈ 260/s, 7 fresh generations):

| Marker | value |
|---|---|
| `pageInWaitChunkQuery` | 0 |
| `pageInWaitChunk` | 197 / 0.42 s total / **max 18 ms** |
| `pageInChunk` | 10 910 / 24.96 s / max 373 ms |
| `cacheTryFill` | 10 910 / 22.98 s / **max 39 ms** |
| `mapGenMakeChunk` | 7 / 1.91 s / **max 372 ms** |
| `readLookupChunk` | 298 946 / 2.02 s / max 4 ms |

To attribute the hitches themselves, `CDynGlobalRetriever::refreshLrAround` (the
per-frame PACS hook the client calls from the main loop) now measures the
frame-to-frame gap: gaps ≥ 100 ms are counted in the `frameHitch` stat and logged
as `DYNHITCH: main loop gap ... | paging during gap: pageIns=… (on query thread
…), freshGen=…, cacheFill=…, pageInWaits=…`. One line per hitch is enough to tell
whether the stall is terrain work (deltas ≠ 0, especially the query-thread ones)
or something else in the frame (all deltas 0). Zero shared-client-code edits: the
hook is a virtual the client already calls every frame.

**First attributed hitch (2026-11-18 20:56:39, client built 20:54).** The user
counted **1** hitch; the dump reported `frameHitch` count = 1, total/max = 220 ms —
the probe measures exactly what the player feels. That window (28 s):

```
DYNHITCH: main loop gap 220 ms pos=(...) | paging during gap:
          pageIns=80 (on query thread 1), freshGen=1 (on query thread 1),
          cacheFill=80 (on query thread 1), pageInWaits=0 (on query thread 0)
pageInChunkQuery      = 10 / 0.271 s  (max 206 ms)
cacheTryFillQuery     = 10 / 0.066 s  (max  10 ms)
mapGenMakeChunkQuery  =  1 / 0.205 s  (max 205 ms)   <-- the hitch
pageInWaitChunkQuery  =  0
```

Reading: the movement thread's total page-in work in the whole window was ten
chunks — nine cheap cache fills (66 ms for all ten minus the generation) plus
**one fresh generation of 205 ms**, which is the 220 ms frame gap. So the residual
hitch class is: a chunk that is *absent entirely* (never prefetched, so no wait is
paid) gets generated synchronously by the movement sweep. Candidates: prefetch
lag (the worker had not generated that chunk yet — one fresh chunk costs ~205 ms,
so at walking speed the lead must exceed that), eviction of a prefetched chunk
(least likely: `getExistingChunk` refreshes the LRU stamp, and the prefetch box is
touched every frame), or a wrong-position lookup landing on a chunk that the hot
box never covers. The earlier 340 ms gap in the same session had `on query
thread 0` everywhere — a non-terrain stall, consistent with the user's guess that
not all hitches are terrain.

Follow-up instrument: `CSlavePager::pageIn` now logs a `DYNQPAGEIN` line with the
region and `source=cache|generated` whenever the page-in runs on a query thread,
and the `DYNHITCH` line carries the camera position — the distance between them
tells prefetch lag (adjacent chunk) from a wrong-position lookup (far chunk).

**Validation session (2026-11-18 21:03:53–21:07:26, long walk, no hitch felt):**

| Marker | value |
|---|---|
| `frameHitch` | **0** |
| `pageInWaitChunkQuery` | 0 |
| `pageInChunkQuery` | 34 / 0.271 s / **max 12 ms** |
| `cacheTryFillQuery` | 34 / 0.136 s / max 12 ms |
| `mapGenMakeChunkQuery` | **16** / 0.130 s / **max 11 ms** |
| `pageInChunk` (all threads) | 28 103 / 249 s / max 398 ms |
| `mapGenMakeChunk` (all threads) | 1 127 / 163 s / avg 145 ms / max 397 ms |

Two conclusions. First, the probe agrees with the player: zero gaps ≥ 100 ms over
213 s while the volume churned 28 k page-ins. Second, the residual mechanism is
confirmed but terrain-dependent: the movement thread *did* perform 34 page-ins
(16 of them from-scratch generations) yet the worst cost only 12 ms, because the
terrain there generates cheaply — while the same volume's background generations
averaged 145 ms. So a hitch occurs only when a *heavy* fresh chunk (the ~205 ms
class) happens to be the one the sweep touches first, which is what the
`DYNQPAGEIN`/`DYNHITCH` pair now pinpoints per occurrence.
