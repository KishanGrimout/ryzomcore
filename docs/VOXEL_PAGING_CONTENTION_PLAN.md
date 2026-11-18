# VOXEL_PAGING_CONTENTION_PLAN — instrument, profile, and optimize the voxel paging locks

> **Plan doc.** Owns decisions, phases and acceptance criteria for eliminating the
> mutex contention around voxel region paging (`pageIn`, especially from-scratch
> generation). All system knowledge about the locks lives in
> **`docs/VoxelVolumePaging.md`** (lock inventory §2, paging pipeline §3, suspected
> chains §4) — this plan links, it does not duplicate.
> Related: `docs/VoxelTerrainCollision.md` (collision queries are the hot readers),
> `docs/TestEnvironmentNotes.md` (validation env), `docs/ShardRuntimeOps.md` (shard-side
> validation), `docs/LEGACY_DYNWORLD_INTEGRATION.md` (roadmap).

Status: **P0 done 2026-11-16** (thread inventory + D5 verification in
`docs/VoxelVolumePaging.md` § 6.1-6.2; findings F1–F3 recorded).
**P1 done 2026-11-16**: instrumentation built and validated — `voxel_paging_stats.h`
registry (§ 5 table), PIX-macro hook in `polyvox_defs.h`, one PolyVox edit
(`ReadLookupChunk` pair), pager/map/cache scopes, client `dyn_paging_stats`
command, unit suite `ryzom_dynworld_paging_stats_test` (4 tests). Full dynworld
exe set green (12 suites).
**P2 client leg done 2026-11-17**: profile results + chain verdicts in
`docs/VoxelVolumePaging.md` § 6.4 (H1/H2 confirmed, H4 refuted, H5 awaits the
shard leg; D4 decided — blocked-with-fallback semantics for the async path).
**P3 re-ranked** per the measured data (§ below): top target is moving the
page-in body (cache load + generation) out of the chunk write lock.
**P3a' implemented 2026-11-17**: chunk paging flag + claim/lock-free/finalize
protocol in the PolyVox fork (residual edits in `docs/VoxelVolumePaging.md`
§ 5.1), incremental chunk-count eviction gate (P3b partial), F1 soil-stat data
race fixed, `WaitPagingChunk` stat marker added, test suite extended to 6 tests
(page-in wait shape, setVoxel-during-page-in, region-status Locked semantics).
Full dynworld exe set green (12 suites, 75 tests); client exe rebuilt.
**P3a' follow-up (live invariant break, fixed 2026-11-17)**: the sandbox hit
`nlassert(infos != nullptr)` in `CMapClient::prepareExtraction` — old-protocol
invariant "status != NotAvailable ⇒ sector metadata registered" no longer holds
under the flag scheme (`Locked` now means page-in in progress, metadata not yet
delivered). Fix: `CMapBase::emergeSector` (map.cpp) now prefetches whenever status
!= `Available`; getChunk waits on the flag, so the metadata is guaranteed on
return (details + regression test `EmergeSectorWaitsForInProgressPageIn` in
`docs/VoxelVolumePaging.md` § 6.5). Test suite now 7 tests; full dynworld exe set
re-validated green (15 suites, 91 tests); client exe rebuilt. **P3 after-profile
captured 2026-11-17** (`docs/VoxelVolumePaging.md` § 6.6): `readLookupChunk` max
233 ms → 7 ms (H1 eliminated), `createChunk` max 269 ms → 1 ms, waits now isolated
lock-free in `pageInWaitChunk` (182 events, max 404 ms ≈ generation max — the D4
synchronous-wait cohort, P4's target). **P3a' is done; the remaining lock-in-page-in
work (P3d demoted, P3c absorbed) is complete.**
**P3e done 2026-11-17/18** (`docs/VoxelVolumePaging.md` § 6.7): multi-slot TLS
chunk cache (lookups 56 M → ~1 M/window) + movement prefetch (the stock PACS
`refreshLrAround` was a stub; now feeds a latest-wins async prefetch worker over
the 25 m hot box). 10-test paging suite; full dynworld set green (14 suites, 85
tests); client exe rebuilt **with the prefetch** (verified by timestamps:
`ryzom_client_dev_r.exe` 2026-11-18 > `ryzom_gameshare_r.lib` > the prefetch
sources). Movement-wait attribution added (`pageInWaitChunkQuery`, thread-local
`EVoxelPagingRole` raised in `testCylinderMove`/`doMove`) so the next dump can tell
user-visible hitches from background pager churn. **P3e-2 validated in play
2026-11-18** (two windows, 370 s, heavy churn): `pageInWaitChunkQuery` = **0** in
both, while 87 k page-ins happened — the movement thread never blocks; residual
`pageInWaitChunk` (2 828 / 26 s) is background extraction/IG queueing
(`docs/VoxelVolumePaging.md` § 6.7). **Second session (same day)** kept
`pageInWaitChunkQuery` = 0 while the user still counted 4–5 hitches in 42 s, so
the split was extended to page-in **work** (`pageInChunkQuery`,
`pagerPageInQuery`, `cacheTryFillQuery`, `mapGenMakeChunkQuery`) and a per-frame
`frameHitch` probe (gaps ≥ 100 ms, `DYNHITCH` log with the paging deltas during
each gap) was added in `refreshLrAround` — it decides per hitch whether the stall
is terrain work or another main-thread cost. Outcome: the target class is closed
(`docs/VoxelVolumePaging.md` § 6.7), so P4 async paging is deferred (§ P4 status
below) and the open question is per-chunk load cost (`docs/VoxelVolumePaging.md`
§ 6.8), plus the P2/P3 shard leg (H5) when the user drives a shard session.

## 1. Problem statement

User report (2026-11-16): heavy mutex contention in voxel region generation; the
longest lock hold is `pageIn` filling a new region, worst when the terrain must be
generated from scratch. Proposed direction to evaluate: always use the PolyVox
"try" accessors and drive the page-in work manually on the Ryzom side so locks are
never held across generation.

The source audit (`docs/VoxelVolumePaging.md` §2) shows the suspect is not one mutex
but a chain: **H1** — the chunk write lock is held across the entire generation, and
cache-missing readers block on it *while holding the volume's internal lock read*,
starving every other chunk creation/eviction behind one slow `makeChunk`. H2/H3/H5
add who is stuck in that queue (movement thread, mesh-extraction path, GPMS).

Rule for this plan: **no optimization merges before its target chain is measured.**

## 2. Decisions

- **D1 — Instrumentation style.** Lightweight, always-compiled, near-zero-cost
  counters (atomics: count/max/sum of hold & wait times per lock site), dumped on
  demand via a debug command (client `dyn_*` catalog per `docs/DynClientRuntime.md`;
  shard side per `docs/ShardRuntimeOps.md` § 8 convention). No unconditional
  `nlinfo` spam in hot paths. Each marker is registered in
  `docs/VoxelVolumePaging.md` § 5 with a strip gate.
- **D2 — Optimize in measured order:** (a) cheap holds removed from the
  generation-critical section first (file IO under the chunk lock, O(65536) scans,
  double lookups), (b) then the structural change (manual/async paging, D4).
  The user's try-accessor + manual-pageIn proposal is the **end-state**, gated on
  the profile numbers, not the first move.
- **D3 — PolyVox edits.** `D:\Programmation\polyvox` is dynworld-driven fork code;
  edits allowed but each must be minimal and recorded in
  `docs/VoxelVolumePaging.md` § 6 with its justification and strip/keep status
  (mirror of the shared-code footprint discipline in the repo `AGENTS.md`).
- **D4 — Not-yet-paged semantics for collision queries (OPEN — decide at P2 exit).**
  If paging moves off the query thread, `getVoxelAt`/`traceSegment` on a missing
  chunk need defined behavior: *air* (player can walk into void — risks falling
  through unpaged ground, regressing issue #2 invariants), *blocked* (sticky
  invisible walls until the chunk arrives), or *defer/last-known* (reuse cached
  neighbor/sector state). The system doc § 4 gets the chosen semantics as a
  finding; collision tests in `docs/VoxelTerrainCollision.md` § 7 must encode it.
- **D5 — Generation concurrency safety is a prerequisite.** Today concurrent
  `pageIn` calls share one `CMapGen` per map (and `CrcGenerateTable`, noise/biome
  state). Verify thread-safety (or make it per-job) BEFORE any change increases
  concurrent generation (P4 gate).

## 3. Phases

### P0 — Thread & call-site inventory (source only, no code)

- For client and GPMS: table of every caller of `getVoxel`/`getVoxelAt`,
  `setVoxelAt`, `emergeSector`, `extractMesh`, `prefetch` with its thread (main
  loop, move-thread, mesh-extraction worker, GPMS move player, mirror callbacks).
  Confirms H2/H3/H5 callers and feeds the profile reading.
- Verify D5 thread-safety of `CMapGen::makeChunk` and the biome manager under
  concurrent page-ins (one shared `CMapGen` instance per map today).
- Acceptance: inventory table appended to `docs/VoxelVolumePaging.md` § 6; any
  discovered unsafety filed as a finding.

### P1 — Instrumentation build ✅ (done 2026-11-16, registry in `docs/VoxelVolumePaging.md` § 5)

- Markers (all registered in `docs/VoxelVolumePaging.md` § 5, strippable at P5):
  - `VPI` pageIn timing: total hold, split file-load vs `makeChunk` vs
    `saveRegionToCache` vs `regionPagedIn`, per pager type (client/server).
  - `VPL` chunk-lock wait & hold: read-acquire blocked-by-pagein counts and
    cumulative wait (the 2.1-read-waiting-while-2.2-read-held site,
    `PagedVolume.inl:354`).
  - `VPW` internal-lock write wait + hold, incl. the O(65536) scan duration.
  - `VPM` `CMapBase::m_volumeLock` (2.4) hold + queue length at `regionPagedIn`.
  - `VPV` per-`getVoxel` volume read-lock acquire cost sample (2.3).
- Implementation home: dynworld-owned where possible
  (`game_share/dynworld/*`, client `dynworld/*`); PolyVox timing wrappers only at
  the four sites above (D3, minimal, recorded).
- Unit-testable: a `test_modules` test spawning N reader threads on cached chunks
  + 1 thread paging-in an uncached chunk, asserting the counters see the H1 shape.
- Acceptance: counters visible via debug command in a live session; unit test green
  in the full dynworld exe set (`docs/TestEnvironmentNotes.md`).

### P2 — Profile ✅ (client leg done 2026-11-17; shard leg pending)

- Client session profiled; results and chain verdicts in
  `docs/VoxelVolumePaging.md` § 6.4 (H1/H2 confirmed, H3 partial, H4 refuted,
  H5 awaits the shard leg; new findings F4–F8: per-voxel read-path aggregate
  cost, IO-dominated page-in hold, eviction churn, 242 ms measured generation).
- **D4 decision (not-yet-paged semantics)** — inputs now measured: a blocked
  reader currently waits ~6–8 ms average / up to 300 ms for a page-in. Decision
  (2026-11-17, with measured data): queries keep **synchronous wait** semantics
  as the default behavior *today*; the async path (P4) adopts **"blocked with
  last-known fallback"** — a missing chunk reads as solid-blocked for collision
  purposes only after the page-in is queued, and callers (movement) already
  handle the contact as a wall slide. Rationale: "air" would let a player walk
  into unpaged ground during the new queueing window and fall through (issue #2
  invariants); the wait today is short enough (avg 6–8 ms) that synchronous
  reads stay correct while P3 reduces the queueing frequency; P4's job queue
  makes the queueing *bounded* instead of lock-shaped.
  - **Amended 2026-11-17 (user)**: even under P4 async paging, **collision
    queries still block** — the "last-known fallback" applies to non-collision
    readers only; a collision/movement query on a not-yet-paged chunk keeps the
    synchronous-wait semantics.
- Shard leg (GPMS, H5) still to run before P4 server-side work; may be folded
  into the P3 validation session.

### P3 — Quick wins inside the current design (re-ranked by P2 data)

Measured priority order (each still gated on its own before/after numbers):

- **P3a' (was P3a/P3b merged, now the top target)** — move the whole page-in
  body out of the chunk write lock: cache read + LZMA unpack (F5: 95–100 % of
  the hold) *and* generation (F7: 242 ms) both into a per-chunk job executed
  before the locked install; the chunk write lock then covers only the data
  swap-in. This is the P4b installChunk mechanism applied to the *synchronous*
  path first — same PolyVox extension, minimal semantics change (readers still
  wait, but on a chunk state, not on IO).
  - **Implemented 2026-11-17** (claim → lock-free pager body → finalize with a
    chunk paging flag; details + PolyVox residual record in
    `docs/VoxelVolumePaging.md` § 2.1-2.2, § 3, § 5.1). P3c absorbed by design
    (the wait no longer holds the internal lock read). P3b partially done
    (incremental `m_uChunkCount` gates the eviction scan; the scan itself still
    runs per creation during churn — full LRU redesign is P4 scope).
  - **Acceptance (pending measurement)**: same session protocol as P2 client
    leg; expected outcome: `readLookupChunk` max collapses from ~pageIn-max
    (233–300 ms) to ordinary lookup cost while `WaitPagingChunk` max ≈ the
    pageIn max (wait moved, not removed); `getExistingChunk` no longer stacks
    behind page-ins; no issue #2 collision regression (§ suites green above).
- **P3e** — hot read path: batch the region reads (`traceSegment`, `castRay`,
  mesh extraction loops) so the internal-lock lookup + volume read happen once
  per region, not per voxel (F4: 33 M lookups / 8.2 s CPU); upgrade the
  thread-local chunk cache to a small multi-slot map to survive chunk-boundary
  alternation.
  - **P3e-1 done 2026-11-17**: 32-slot direct-mapped TLS chunk cache
    (`polyvox_defs.h/.cpp`, dynworld-owned); `ReadLookupChunk` count 56 M →
    1.0–1.3 M per session window (system doc § 6.7). Regression test
    `MultiSlotChunkCacheSurvivesChunkAlternation` (zero lookups on warm
    alternating reads). Remaining avg/max is internal-lock contention with
    creation/eviction, not page-in coupling.
  - **P3e-2 done 2026-11-17/18**: movement prefetch — the stock PACS
    `refreshLrAround` was a **stub** (all prefetch code commented out); wired it
    to a new latest-wins async prefetch worker in `CMapBase`
    (`prefetchRegionAsync`) over the 25 m `g_prefetchBound` box
    (`docs/VoxelVolumePaging.md` § 6.7). `refreshLrAroundNow` keeps the
    synchronous flavor for load time. D4 amendment honored: collision queries
    still block when they outrun the prefetcher. Regression test
    `PrefetchRegionAsyncPagesInBackground`.
  - **P3e movement-wait attribution (2026-11-18)**: dumps showed `pageInWaitChunk`
    is dominated by background paging/extraction threads, so it cannot be read as
    a hitch count. Added `pageInWaitChunkQuery` (subset paid under the thread-local
    `EVoxelPagingRole::Query`, raised in `testCylinderMove`/`doMove`;
    `docs/VoxelVolumePaging.md` § 6.7). Regression test
    `PageInWaitAttributionFollowsQueryRole`. Acceptance of P3e-2 now reads that
    counter ≈ 0 over a movement window; otherwise the box/lead margin is tuned.
    **Validated 2026-11-18**: 0 query waits in both client windows (97 s and
    273 s of walking under 87 k page-ins) — the wait cohort is gone.
  - **P3e hitch probe + work attribution (2026-11-18, second session)**: the user
    still counted 4–5 hitches in a 42 s window with `pageInWaitChunkQuery` = 0.
    The wait counter alone cannot see a thread that *performs* its own page-in, so
    the `Query`-role split now also covers page-in **work**
    (`pageInChunkQuery`, `pagerPageInQuery`, `cacheTryFillQuery`,
    `mapGenMakeChunkQuery`), and `CDynGlobalRetriever::refreshLrAround` (per-frame
    main-loop hook) counts frame gaps ≥ 100 ms in `frameHitch` and logs a
    `DYNHITCH` line with the paging deltas during each gap
    (`docs/VoxelVolumePaging.md` § 6.7).
  - **Open question (next measurement)**: is the hitch terrain at all? The
    `DYNHITCH` deltas answer it per hitch — non-zero query-thread deltas = paging;
    all-zero = another main-thread cost (zone streaming, mesh upload, GC, …).
  - **First attributed hitch (2026-11-18 20:56)**: `frameHitch` = 1 / 220 ms
    matched the player's count exactly. On the query thread: 10 page-ins, of
    which **one fresh generation of 205 ms** — that was the hitch. A 340 ms gap
    earlier in the session had zero query-thread work (non-terrain). So the
    residual hitch class = a chunk absent entirely, generated synchronously by
    the movement sweep (no wait involved). Next measurement: `DYNQPAGEIN` region
    log (added in `CSlavePager::pageIn`) + camera position in `DYNHITCH` tells
    prefetch lag from a wrong-position lookup.
  - **Validated 2026-11-18 21:04–21:07 (long walk, no hitch felt)**: `frameHitch`
    = **0** over 213 s / 28 k page-ins. The movement thread still performed 34
    page-ins (16 fresh generations) but the worst was **12 ms**; background
    generations averaged 145 ms. So the residual hitch class is a *heavy* fresh
    chunk landing on the movement sweep (the 205 ms case), not a systemic stall.
    (The follow-up — a directional lead on two hot points — is described below.)
  - **Prefetch coverage — camera vs player (decision 2026-11-18, implemented)**:
    the hot box was centred on the *camera* (`View.refinePos`), the position the
    per-frame PACS hook receives. That was not enough: camera collisions query
    the same volume, and the camera can sit behind the player and far from it
    (zoom-out, free look), while the movement/collision queries run at the
    *player* position. The prefetch now covers **both** hot points, each with its
    own directional lead (camera motion / player motion), through two
    latest-wins slots in the worker (`EPrefetchSlot`), merged into one union pass
    when the two regions are within one chunk. The player region is requested
    from inside `testCylinderMove`/`doMove` (they already receive the
    `UGlobalPosition`), so no shared-client-code edit was needed. Knowledge:
    `docs/VoxelVolumePaging.md` § 6.7 ("Two hot points + directional lead").
  - **Validated 2026-11-18 22:16–22:19 (two dumped windows, two-slot + lead
    build)**: `pageInWaitChunkQuery` **0** in both windows, page-ins otherwise
    ≤19 ms. The single hitch (387 ms) was a **365 ms fresh generation of a chunk
    at the point-mirrored location**, caused by the `MVDBG3` collision probe's
    mirrored-column read (every movement-thread page-in of that window was at
    mirrored coordinates) — not by a collision lookup. The `MVDBG*` probes are
    now behind `g_dynCollisionDebug` (default off, `dyn_collisiondebug`); the
    follow-up session (**197 s, 52 867 page-ins**) confirmed it: zero `MVDBG`
    lines and **`pageInChunkQuery` / `mapGenMakeChunkQuery` / `pageInWaitChunkQuery`
    all 0**, i.e. the movement thread paid no page-in at all. P3e-2's target class
    is closed. The only counted gap (121 ms) had no query-thread paging — a
    main-thread cost outside this plan (zone/mesh streaming around load).
    Details: `docs/VoxelVolumePaging.md` § 6.7 and
    `docs/VoxelTerrainCollision.md` § 5.
  - Region-batching at the call sites (`traceSegment`/`castRay`/extraction
    loops) is **deferred**: after P3e-1 the per-voxel cost is lock round-trips,
    not lookups; re-evaluate only if a new profile shows it dominating.
- **P3b** — kill the double `getExistingChunk` scan in `getChunk` and track
  chunk count + eviction candidates incrementally (measured: only ~80 µs per
  scan — low priority now, do opportunistically with P3a').
  **Partially done 2026-11-17**: `m_uChunkCount` maintained incrementally and
  gates the scan; the double lookup was restructured in P3a' (the write branch
  now peeks without taking the chunk lock). Residual (per-creation scan during
  churn) still measured ~80 µs — P4 scope if it ever shows.
- **P3c** — stop holding the internal lock read while waiting on the chunk read
  lock: **absorbed by P3a'** (the wait is now the lock-free flag spin; the
  internal lock is never held across a page-in wait). Verified by the extended
  test suite + profile follow-up.
- **P3d** — metadata CMutex split: **demoted** (H4 refuted, max 2 ms); only if
  it shows up again after the other changes.
- **F1** — `g_logMinSoil`/`g_logMaxSoil` data race: **fixed 2026-11-17** (thread-local
  stats; they were write-only, so per-worker tracking preserves behavior).

### P4 — Manual/async paging (the structural change; the user's proposal)

**Status (2026-11-18): not required for the plan's target — deferred, not
abandoned.** P4's acceptance names two things: *no lock spans a `makeChunk`/file-IO
call*, and *no movement stall on fresh-terrain entry*. The second is validated
(`docs/VoxelVolumePaging.md` § 6.7: 197 s / 52 867 page-ins with 0 query-thread
page-ins). The first is what P3a' already bought for the paths that matter (the
page-in holds are measured outside the chunk lock, § 6.6). What remains is
*throughput*, not blocking: background threads queue on each other (1 991 waits /
13.3 s / max 380 ms in that window), which delays extraction/mesh/vegetation work
(pop-in), never the player. Whether that is worth P4's structural cost depends on
the per-chunk load cost, i.e. on the disk/decode split — see
`docs/VoxelVolumePaging.md` § 6.8. Revisit P4 only if a new consumer needs page-in
throughput (slower mesh code, larger view distance, server-side dynworld A*).

- **P4a** Ryzom-side miss detection: queries use `tryGetChunk`-style probes
  (`CMapBase` layer) instead of synchronous `getChunk` page-in — per D4 semantics
  for the miss answer.
- **P4b** Page-in job: build chunk data outside all volume locks (generation into a
  standalone buffer — `CBlockMakeData` already targets a chunk-shaped buffer;
  cache load likewise), then install via a minimal PolyVox `installChunk`-style
  entry (D3): create + insert + state flip under the 2.2 write lock only.
- **P4c** Dedup & ordering: in-progress job registry per chunk (atomic state or
  job map) so concurrent misses join instead of duplicating work; priority by
  distance to the requester (movement > mesh LOD > prefetch); prefetch commands
  (`CPrefetchSectorCommand`) feed the same job queue.
- **P4d** Memory/eviction: the O(65536) eviction from P3b keeps working for
  async-installed chunks; pinned/in-progress chunks must not be evicted mid-job
  (the array-full `logic_error` path, `PagedVolume.inl:455`).
- **P4e** SET_VOXEL interplay: edits landing on a chunk whose page-in is
  in-flight (issue #6 chain, `docs/VoxelMessageChain.md`); dirty marking must
  survive the install hand-off.
- Acceptance: no lock on any path spans a `makeChunk`/file-IO call (asserted by
  `isLocked()` probes + P1 counters in the unit test); client session shows
  no movement stall on fresh-terrain entry; shard session equivalent; full exe
  set green.

### P5 — Validation consolidation & strip

- Re-run the P2 profile scripts; before/after table in `docs/VoxelVolumePaging.md` § 6.
- Strip P1 markers whose validation completed (rebuild + redeploy per convention),
  keeping any promoted to permanent guards with an explicit decision line.
- Update `docs/LEGACY_DYNWORLD_INTEGRATION.md` roadmap only for integration-level
  state (phase validation entry + pointer here).

## 4. Out of scope / follow-ups recorded elsewhere

- Collision semantics of unpaged chunks that change movement rules beyond the D4
  answer → `docs/VoxelTerrainCollision.md` / issue #2 residuals.
- Mesh extraction cost itself (not its locking) → mesh/collision Option work in
  `docs/VoxelTerrainCollision.md` § 8.
- Server-side cache enabling for `CMasterPager` (currently commented out) is a
  P2 finding first — enabling it changes H5's shape and may be the cheapest
  server-side win if the P2 numbers agree.
