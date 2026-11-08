# LegacyAssetInventory — survey of the legacy tree (`D:\Programmation\ryzomcore_old`)

> **Reference doc.** User-directed survey of the legacy working tree (2026-10-22), kept
> so future sessions know what exists outside the current repo and what is worth
> touching. Related: integration roadmap `docs/LEGACY_DYNWORLD_INTEGRATION.md`
> ("Legacy source" section) and the client runtime notes in
> `docs/DynClientRuntime.md`.

## `ryzom_core_client\` — the dynworld-era client dev run dir (chosen client runtime bed)

- Full client data set at `data\`: `.bnp` packs incl. `newbieland_*`, `characters_*`,
  `gamedev.bnp`, `leveldesign.bnp`; packed sheets at data root.
- `data\shaders\dyn_landscape.{vp1,arbfp1}` — runtime-only shader pair, exactly what
  `CDynLandscape::readShaderFiles` needs.
- **Empty** `data\dynworld\` placeholder (cfgs were found elsewhere — server reads them
  from `common/data_leveldesign`; client material cfg service-side only).
- `client.cfg` = local-dev shard targets (`StartupHost 127.0.0.1:40916`,
  `PatchWanted = 0`, `AllowDebugCommands = 1`) incl. the dyn-era
  `ChunksMemoryCache = 3072` knob (the knob only exists in our integration branch).
- `error_join.log` — a client join-state-machine dump from a failed dyn-era join
  (`st_login` → `ev_bad_login`).
- `client_default.cfg` data plumbing: `PreDataPath = { "user", "patch", "data",
  "examples" }` — client bakes those recursively, so `data\shaders` lands on `CPath` and
  dyn shader lookup works with zero extra config.

## `ryzom_official_client\`, `Ryzom\ryzom_live`, `Ryzom_2.1\`

Live/official client installs (no usable game data beyond `custom_data`/`fonts`;
`Ryzom_2.1` is 2021-era with ring scenarios). Not needed.

## `ryzomcore-data\`

Full data repo (`common`, `continents`, `leveldesign`, `primitives`, `shard`, `sound`,
`translation`, `workspace`); this is the data checkout the legacy server cfg `Paths`
referenced. Candidate for later server rail milestones only; not needed for current
smokes.

## `ryzom_client_data\`

Raw unpacked per-continent client data (source material for building packs). Not needed
now.

## `ryzomcore-script` / `ryzomcore-script_old`

Old Stella-based CI build-script toolkits (pre-CMake orchestration). Superseded; skip.

## `ryzomcore_dyn\`

An older full source checkout **without** dynworld (`code/ryzom/client/src/dynworld`
absent; `code/code` nesting). Obsoleted by `ryzomcore_dev`; skip. Its `web\`
(`private_php`/`public_php`) is the old web-app tree, not dynworld.

## `ryzom_assets\`, `ryzom_tools\`, `nel_tools\`, `markedforaddfiles\`, misc

- `ryzom_assets\` — leveldesign asset library (tilebanks/templates;
  `newbieland_tilebank_dump.txt`), not dynworld-specific.
- `ryzom_tools\`/`nel_tools\` — prebuilt tool exes (geometry/build utilities; our
  `redist\` builds supersede).
- `markedforaddfiles\` — files the author marked for adding, incl. the shader ARB
  assembly sources (`dyn_landscape_{vp,fp}.arb`); the authoring sources themselves live
  in the legacy repo (`common/data_leveldesign/dynworld`).
- `sourcetrail\`, `screenshots\`, `external_vc141\` — analysis DB, pictures, refreshed
  SDK: no dynworld deltas.

## Legacy working copy mechanics (how to read it)

- Path: `D:\Programmation\ryzomcore_old\ryzomcore_dev`, Mercurial tip
  `9514:da0dbd6457ca` (`Merge dynworld into develop`). `hg` is not on PATH; invoke
  explicitly: `& 'C:\Users\mynam\AppData\Local\Atlassian\SourceTree\hg_local\hg.exe' -R
  D:\Programmation\ryzomcore_old\ryzomcore_dev ...` (verified working; tip matches).
  Read the working-copy files directly — they ARE the post-merge content, so a "new"
  legacy file can be copied verbatim into the repo (then adapted to current
  conventions).
- Full legacy delta: `D:\Programmation\ryzomcore_old\ryzomcore_dev\dynworldpatch.diff`
  (~3 MB Mercurial unified diff; 343 changed paths, 135 new files;
  `dynworldpatch_nocode.diff` is the same without code bodies). Each file's delta
  starts at a `diff --git a/<path> b/<path>` marker line. Recipes for extracting and
  diffing individual files: roadmap "Working context" in
  `docs/LEGACY_DYNWORLD_INTEGRATION.md`.

## External dependency checkouts (on this machine)

- ANL: `D:\Programmation\ANL`, branch `master`, HEAD `28d40d67f6f3e25149f5580da0e6655cc7ee8467`
  (2015-11-20, "optimize voronoi noise"). License: permissive BSD/MIT-like — verified.
- PolyVox: `D:\Programmation\polyvox`, branch `kishan-dynworld`, commit
  `c5c63c22db5194029e54dea770d5b10799dbfdd3` (2018-05-01); describes as
  `v0.2.1-1068-gc5c63c22`, clean worktree, MIT (`include/PolyVox/PagedVolume.h`).
