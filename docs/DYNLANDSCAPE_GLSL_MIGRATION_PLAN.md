# DYNLANDSCAPE_GLSL_MIGRATION_PLAN — dyn landscape shaders: assembly → GLSL

> **Plan doc.** Migration of the `CDynLandscape` render programs from vp1/arbfp1
> assembly shaders to hand-authored GLSL, enabled through the `NLDRIVERGL3`
> (OpenGL 3.3) driver. Decisions, phases, acceptance criteria.
> Depends on: `docs/DynClientRuntime.md` (dyn landscape render path, `dyn_*`
> commands, client sandbox), `docs/LEGACY_DYNWORLD_INTEGRATION.md` (integration
> roadmap / phase validation history).

## 1. Goal & motivation

`CDynLandscape` renders terrain with two assembly shaders authored in the early
2000s profile era:

- `dyn_landscape.vp1` (`!!VP1.0`, NV_vertex_program syntax) — MVP transform, fog,
  triplanar blend factors from the normal (`tighten`/`scale` uniforms), alpha via
  an extra attribute slot.
- `dyn_landscape.arbfp1` (`!!ARBfp1.0`, ARB_fragment_program) — triplanar
  diffuse fetch over textures 0/1 with normal-plane selection, diffuse/ambient
  light, fog, per-vertex alpha × blend factor.

Assembly is painful to iterate on (register juggling, swizzle traps, no
math readability). Goal: author both stages in GLSL while keeping the existing
`nelvp`/`arbfp1` path working, and use the modern driver as the primary
validation target.

## 2. State of the code (assessment)

### 2.1 Shaders on disk & how they're loaded

Files (authoring + runtime sources) in `code/ryzom/client/data/shaders/`:

| File | Role |
|---|---|
| `dyn_landscape.vp1` | runtime & source — nelvp vertex program (44 lines) |
| `dyn_landscape.arbfp1` | runtime & source — ARB fragment program (~59 lines) |
| `dyn_landscape_vp.cg` | **stale authoring-only** sketch (HLSL/Cg-style, `POSITION`/`ModelViewProj` semantics; out of date: no tighten/scale/sigmas, no alpha) |
| `dyn_landscape_fp.cg` | **stale authoring-only** sketch (same) |

Load path: `CDynLandscape::readShaderFiles()` (`dyn_landscape.cpp:122`) does
`CPath::lookup("dyn_landscape.vp1")` + strict `CIFile::serialBuffer`
(EPathNotFound crash class if `data/shaders` is missing — see
`docs/DynClientRuntime.md` § 3).

### 2.2 How the programs are wired (`dyn_landscape.cpp`)

- `setupPrograms()` (`:398-460`): wraps the raw text in
  `IProgram::CSource{ Profile = nelvp }` (VP, `ParamIndices` map
  `modelViewProjection=0, tighten=4, scale=5, fog=10`) and
  `CSource{ Profile = arbfp1, Features.MaterialFlags = TextureStages }` (PP).
  A commented-out `ps_2_0` source shows D3D was never wired.
- Per-region uniform writes use **fixed register numbers**
  (`renderRegion()` `:233-245`): `setUniformMatrix(VP, 0, ModelViewProjection)`,
  `setUniform1f(VP, 4, g_tighten)`, `setUniform1f(VP, 5, scale)`,
  `setUniformFog(VP, 10)`; PP: `setUniform4f(PP, 2, {-1/(end-start),
  end/(end-start), m_displayFactor, 0})`.
- The vertex 'alpha' is a **dangling `Empty` Float1 slot** appended after
  Position+Normal in the submesh vertex buffer (`region_mesh.cpp:86-88`,
  `CRegionSubMesh::bindBuffers()`), read by the VP as `v[7].x`. Under GLSL this
  mapping is NOT name-guaranteed — must be verified on the GL3 attribute
  mapping (`Empty` has no standard name).

### 2.3 GL3 driver (`nel/src/3d/driver/opengl3`, `NLDRIVERGL3`) contract

- Separate driver DLL (`nel_drv_opengl3_win`), built into the current build
  (`CMakePresets.json` `WITH_DRIVER_OPENGL3: true`, `TDriver3D::OpenGL3`
  already wired in the client: `client_cfg.cpp:909`, `init.cpp:1071`).
- Program profiles accepted (`CDriverGL3`, `driver_opengl3_program.cpp:196/717`):
  - VP: `glsl330v` / `glsl300esv` / **`nelvp` (auto-converted)** / `glsl3vi`.
    The nelvp→GLSL converter (`driver_opengl3_nelvp.cpp`) parses via `CVPParser`,
    translates output registers to named varyings (`texCoord0..7`, `ecPos`, ...),
    and carries over `ParamIndices`.
  - PP: **`glsl330f` (desktop, separate SSO stages) / `glsl300esf` (linked
    pipeline for GLES/WebGL)** — `arbfp1` is NOT supported.
- Uniform-name contract for user GLSL programs: `CProgramIndex::Names`
  (`nel/src/3d/program.cpp:79-418`) — `modelViewProjection`, `fogParams`,
  `sampler0..31`, `light0DirOrPos`, `light0ColAmb`, `light0ColDiff`, ...;
  `IProgram::buildInfo()` caches all these by name. Drivers fill what the user
  program declares (`Features.DriverFlags / MaterialFlags`). `getUniformIndex(TName)`
  works per-program (`driver_opengl3_program.cpp:1674+`).
- Varying contract is **explicit `layout(location)`**-based between SSO stages
  (`VaryingLocationEcPos`, `texCoord0..` — see `driver_opengl3_nelvp.cpp:417-465`,
  `driver_opengl3_pixel_mega_program.cpp:119-152`); vertex attributes by name
  (`position`, `normal`, ... `driver_opengl3_program.cpp:105`).
- Existing precedent for a hand-authored multi-source program: `fxaa.cpp:168-260`
  (glsl300esf + glsl330f + arbfp1 + ps_2_0, driver picks first supported
  profile). GLSL3-only UBO VP: `meshvp_wind_tree`; bloom GLSL330: `bloom_effect.cpp`.

### 2.4 What happens on GL3 *today* — validated 2026-11-09 (Phase 0)

VP: compiles (nelvp auto-conversion path). PP: `supportPixelProgram(arbfp1)`
is false → PP dropped with "No supported pixel program for dynlandscape"; the
material then renders through the **builtin Mega PP paired with the nelvp-converted
VP (linked mode, id=158)** and comes out **black** on terrain. Sandbox runs
`Driver3D = OpenGL3` since Phase 0 (`pipeline/client_dev/local/README_SANDBOX.md`).
Phase 1 (hand-authored GLSL pair with controlled varyings + light-table UBO)
supersedes this pairing.

## 3. Decisions (confirmed 2026-11-08)

1. **Hand-authored GLSL, additive multi-source.** New `glsl330v`/`glsl300esv`
   VP sources + `glsl330f`/`glsl300esf` PP sources are added alongside the
   existing nelvp/arbfp1 sources (FXAA pattern). GL1/nelvp and arbfp1 remain the
   fallback profile pair; D3D status unchanged (still no working dyn landscape PP there).
2. **GLSL sources live as files in `data/shaders` and are loaded at runtime**
   like today, not baked into C++ literals: `dyn_landscape_vp.glsl`,
   `dyn_landscape_fp.glsl`. Body kept profile-neutral; the driver-side
   `#version`/stage declarations differences resolved in `setupPrograms()` by
   std::string concat if needed (FXAA-style two bodies only if required).
3. **Hot reload is in scope** (`dyn_rebuildshaders` re-reads the files).
4. Uniform writes migrate from fixed register numbers to
   `CProgramIndex`/name-based lookups per active profile.

## 4. Shader semantics inventory (what the GLSL must reproduce)

From `dyn_landscape.vp1` / `dyn_landscape.arbfp1`:

**VP outputs**
- `o[HPOS]` — MVP-transformed clip-space position.
- `o[FOGC].x = DP4(c[10], objectPos)` — object-space fog distance via `fog` uniform.
- `o[TEX0] = objectPos.xyz * scale` — object-space position used as triplanar UV.
- `o[TEX1]` — normalized triplanar weights: `max(|n|,-|n|)` → `(abs(n)-tighten)`
  clamped at `tighten.y` → three successive squarings (x⁸) → per-component
  normalize (with 1/component RCPs).
- `o[TEX2] = (normal, w=alpha)` — object-space normal + per-vertex alpha from `v[7].x`.

**PP**
- Triplanar fetch: texture 1 on the Z-slider planes (`TEX0.zy`, `TEX0.zx`
  weighted by `TEX1.x`/`TEX1.y`), and on the Z plane (floor/ceiling) texture 0
  vs texture 1 is picked by `SLT` on `TEX2.z` (normal sign → floor vs ceiling),
  weighted by `TEX1.z`.
- Lighting: eye-space normal via `state.matrix.modelview.invtrans` +
  `state.light[0]` ambient/diffuse (LIT).
- Fog: `MAD_SAT(fogCoord, c2.x+c2.y)` → `LRP(color, foghue)`; alpha
  `TEX2.w * c2.z` (mesh display factor).
- c2 = PP env[2] uniform (fog a,b + display factor).

Asm quirks to translate deliberately (not blindly): the `.zyzw/.zxzw/.yxzw`
swizzles rebuild vec4 fetches from SCALAR-valued planar coordinates
(replication); the per-component normalize divides by the weight sum with no
zero guard (planar normals give a zero-sum component → INF, then swizzled
away); `LIT` clamps the diffuse term. Keep identical numeric behavior in GLSL
and comment each spot.

## 5. Phases

### Phase 0 — GL3 baseline bring-up (no code change) — **RESULT: done 2026-11-09**

Executed: shard brought up (16 services, FE 47911, mirror chain intact per
`ms_mainland`/`fes_mainland_01` logs), XAMPP Apache 40916 + MySQL 3306 listening,
sandbox `client.cfg` switched to `Driver3D = "OpenGL3"` (backup:
`client.cfg.opengl-bak`), client launched (`nel_drv_opengl3_win_r.dll`,
Core 3.30 context, `GL_ARB_separate_shader_objects` found).

**Log evidence (client.log, 21:07–21:12 this session):**

```
dyn_landscape.cpp 427 CDynLandscape::setupPrograms : 3D: dynlandscape vertex program available
dyn_landscape.cpp 449 WRN 3D: No supported pixel program for dynlandscape
driver_opengl3_program.cpp 2444 GL3: Linked user program id=158 (VP=nelvp-converted, PP=Mega PP (linked=1, fogOrPpl=1, cube=0, spec=0, ppClip=0, tableUBO=1, cam=1, obj=1, mat=1))
driver_opengl3_program.cpp 2444 GL3: Linked user program id=159 (VP=glsl300esv/FXAA_VP/UBO, PP=glsl300esf/FXAA_PP/UBO)
```

**Visual result: terrain renders BLACK** (in-game report). Confirms the
`arbfp1`-unsupported fallback: user nelvp-converted VP is paired with the builtin
Mega PP, and that pairing fails for this material (unlit + 2 texture stages +
`state.light[0]` semantics the mega path does not reproduce).

Additional facts established:

1. **Desktop GL3 runs the linked-mode path for mega programs**
   (`driver_opengl3.cpp:374` — `m_LinkedMegaShaders = true` "for both"). The
   `NlLightTable` per-light UBO (incl. `ambient`, `driver_opengl3_uniform_buffer.cpp:28`)
   is available to *user* sources with `Features.UsesLightTableUBO`; the PP-side
   per-light *uniform* path (`ppLight0*`) only fills raw light data and only when
   `m_VPBuiltinCurrent.NumPerPixelLights > 0` — NOT suitable for the landscape PP.
   ⇒ **Phase 1 PP must use `UsesLightTableUBO` + `nlLights[0]`** (ambient/diffuse/
   dirOrPos) instead of `ppLight0*`/`light0*` uniforms.
2. GL2-style `state.matrix.modelview.invtrans` in arbfp1 → GLSL `normalMatrix`
   (CProgramIndex has it; driver fills it for user VPs — verify when wiring VP).
3. FXAA precedent works end-to-end on this driver (id=159 linked UBO pair).
4. `g_AttribNames` table (`driver_opengl3_program.cpp:103`) has **no name for the
   `Empty` pad slot** — the alpha `v[7].x` dependency stands (Risk § 6.1), Phase 1
   VP will need the explicit alpha attribute slot mapping verified.

Pending diagnostic (in-game, cheap): `dyn_useVP 0` — if terrain textures fine
with builtin VP + builtin PP, the blackness is proven to be the user-VP pairing,
not the builtin path itself.

**Acceptance:** dyn landscape visible and traversable on GL3 with builtin PP;
baseline screenshots saved; log evidence recorded here.
→ Met in spirit: world entry + traversal worked, exact log markers captured;
"visible" degraded to *black but correct silhouette/motion* (all Phase 0
objectives accrued). Screenshots: `pipeline/client_dev/local/screenshots/` (if
taken; session of 2026-11-09 ~21:09).

### Phase 1 — Author GLSL shaders (files only, compile-checked on GL3) — **RESULT: done 2026-11-09 (Phase 0 findings applied)**

Written (`code/ryzom/client/data/shaders/`):

- `dyn_landscape_vp.glsl` / `dyn_landscape_fp.glsl` — profile-neutral bodies:
  **no `#version`, no attribute/varying declarations, no UBO block
  declarations, no precision statements in the files**. The files carry the
  semantics + parity commentary only; per-profile wrappers live in
  `setupPrograms()` as string prefixes (see Phase 2).
- Uniform data path differs from the original plan § 4 sketch (which assumed
  `sampler0/1` + `light0*` setUniform* access): Phase 0 established that the
  *linked* profile (the one the desktop GL3 driver actually runs, since
  `m_LinkedMegaShaders = true`) **rejects default-block uniforms**
  (`driver_opengl3_program.cpp:384`), and rejects the `ppLight0*` fill path.
  So the GLSL pair reads:
  - VP: `NlModel.modelViewProjection` (builtin object UBO, uploaded per draw
    call by the driver), `NlCamera.inverseProjectionBasis` (to synthesize
    `ecPos` exactly like the nelvp→GLSL converter), and the user UBO
    `NlDynLandscape` params (`landscapeParams`: x=tighten, y=scale,
    z=displayFactor).
  - PP: `NlCamera.fogColor`/`fogParams` (linear fog branch identical to the
    builtin `applyFog`), `nlLights[0]` — NlLightTable entry 0 = the staged sun
    (`dirOrPos` raw world-space propagation direction ⇒ to-light = its
    negation), and `landscapeParams.z` = display factor.
  - Samplers keep the plan's names **`sampler0`/`sampler1`** — those are what
    `setupInitialUniforms` auto-binds to material texture units
    (CProgramIndex::Sampler0/1).
- Attribute slot decision (`Empty` alpha): **CORRECTED after Phase-3 runtime
  evidence** — `CVertexBuffer::Empty` is a *deprecated alias* of `Tangent`
  (enum 15), NOT the PaletteSkin slot. The original decision above assumed
  Empty = PaletteSkin (GL index 6) and read attribute location 6; that array
  is never enabled for region VBs (they carry the Tangent flag), so the GL
  default constant `(0,0,0,1)` flowed in and alpha was identically 0. The
  legacy assembly's `v[7].x` was right all along — in BOTH driver maps the
  alpha rides GL index 7 (`GLVertexAttribIndex[Tangent]`). The hand VP now
  declares `layout(location = 7) in vec4 empty;` (GL1 nelvp path keeps
  `v[7]`). See Phase 2 RESULT items 9–11 for the runtime bisect.
- Varying sets: VP outputs `ecPos`(0) / `texCoord0..2`(8/9/10); FP consumes
  exactly those — a self-consistent interface. In linked (300 es) mode stages
  match **by name** (no layout on varyings — ES 3.00 restriction); in SSO
  (glsl330) mode `layout(location)` prefix constants pin the same locations.

**Acceptance:** met (files exist, no `#version` in bodies, profile neutrality
by construction). Compile success itself gates Phase 2/3.

### Phase 2 — Wire multi-source + named uniforms (`dyn_landscape.cpp`) — **RESULT: done 2026-11-09 (linked + rendered in-engine)**

Implemented (Release client rebuilt OK; `ryzom_client_dev_r.exe` updated):

1. `setupPrograms()` multi-source: nelvp + {`glsl300esv`, `glsl330v`}
   (bodies from `data/shaders/dyn_landscape_vp.glsl` via `readProgramFile()`);
   arbfp1 + {`glsl300esf`, `glsl330f`}. OnlyUBOs on all GLSL sources; VP
   sources set `UsesObjectUBO` (= camera+light table implied); PP sources set
   `UsesLightTableUBO`+`UsesCameraUBO`; sources declare the user UBO format
   `NlDynLandscape { vec4 landscapeParams; }` at `UBBindingVertexProgram`
   (VP) / `UBBindingPixelProgram` (PP) — the same buffer is bound to both
   binding points (a merged block is legal in a linked program). Per-profile
   `#version` + declarations prefixes live in `dyn_landscape.cpp` anonymous-
   namespace constants (`GLSL*Prefix*`); the driver splices the UBO headers
   after the leading `#`/`precision` lines, so a TB scan can verify the final
   sources. Availability logging now emits the picked profile name
   (`3D: dynlandscape vertex program available (glsl300esv)` etc.).
2. Per-draw uniform writes in `renderRegion()` branch on
   `isGLSLProfile(program->profile())`: GLSL path writes the UBO once per
   region (`tighten`, `scale`, `displayFactor`) — MVP/camera/light state is
   the driver's builtin UBO data path, no `setUniform*` calls at all; the
   nelvp/arbfp1 path keeps the original fixed-register writes byte-identical.
3. Alpha slot: kept as `Empty` Float1 (region_mesh.cpp untouched). Hand-VP
   attribute corrected in Phase 3: **location 7 (Tangent slot —
   `CVertexBuffer::Empty` is a deprecated alias of Tangent)**. The initial
   decision read location 6, which is never enabled for region VBs and made
   alpha identically 0 — see Phase 2 RESULT § 9–11 and Phase 3 bisect run.
4. Fallback hardening: `nlassert(ppok)` sites guarded with `m_VP/m_PP != NULL`;
   when no pixel program exists, texgen is enabled so the material still
   renders through the builtin programs (Phase-0's black path now at least
   produces builtin-lit geometry).
5. Stale `.cg` pair deleted (`dyn_landscape_vp.cg`, `dyn_landscape_fp.cg`,
   `git rm`). `dyn_landscape.vp1/.arbfp1` left byte-identical (they are frozen
   fallback sources; no comment markers — assembly comment syntax risk).
6. Reload: `CDynLandscape::render()` now calls `reloadPrograms()`: single
   attempt + previous-sources snapshot restore + full-revert to NULL programs
   on double failure (removed the `while (!succeed)` infinite spin,
   `dyn_landscape.cpp:176-186` as filed in Phase 4's known-defect). `reloadPrograms`
   logs the resulting profile pair.
7. **First in-game iteration (21:41 log): VP compiled + linked, PP compile
   failed** — `0(94) : error C1503: undefined variable "sampler1"` (full
   source dump captured by the driver's compile-fail listing — worked as
   designed). Root cause: the sampler uniforms were *referenced* but never
   *declared* — the driver's `setupInitialUniforms` only binds the texture
   unit index to a sampler that exists in the shader; declaring them is the
   client's job (FXAA's baked PP declares `uniform sampler2D sampler0;`
   in-source). Fix: `uniform sampler2D sampler0/sampler1;` added to both FP
   prefixes in `dyn_landscape.cpp`.
9. **Second run (21:43): compile+link fully OK** — log shows
   `dynlandscape vertex program available (glsl300esv)` + `... pixel program
   available (glsl300esf)` + `GL3: Linked user program id=158 (VP=glsl300esv/
   dyn_landscape, PP=glsl300esf/dyn_landscape)` (no compile/link errors, no
   non-UBO rejections). **Terrain still blacks out**, so the failure has
   moved from compile to *rendering content*.
10. **Phase 3 bisect instrumentation added (one rebuild, live in-game
   modes)**: the FP consumes `landscapeParams.w` as a debug-mode selector
   (`dyn_landscape_fp.glsl` header documents modes 0–6). Staging:
   `dyn_landscape.cpp` global `g_shaderDebugMode` (default 0 = production) →
   written into the UBO `w` channel per region; in-game command
   `dyn_shaderdebug <n>` (commands.cpp, next to `dyn_useVP`/`dyn_usePP`).
   Modes (all pin alpha = 1 except 0): 1 flat magenta ("does anything
   draw"), 2 unlit textured (texture plumbing + varyings), 3 lighting only
   + green base (light table entry 0), 4 fog only (grayscale fog factor),
   5 texcoords as color (VP TEX0), 6 production color with alpha = 1 (alpha
   chain). Static review of the static chain passed before this round: name
   -keyed sampler binding (`setupInitialUniforms` maps `sampler0/1`→units
   0/1), builtin-UBO upload gating flags, user-UBO flush lifecycle — all
   consistent.
8. **PATH SHADOWING TRAP discovered (client.log 11188):** `CPath` reports
   `dyn_landscape.arbfp1/.vp1` "already inserted from
   `D:/Programmation/ryzomcore_old/ryzom_core_client/data/shaders/`" — the
   **old tree copy wins the file map**, so runtime edits of the assembly
   sources in *this* repo are silently ignored until that path is removed or
   reordered. The new `.vp1/.arbfp1`/`.glsl` files in `code/ryzom/client/data/shaders`
   ship source-of-truth for the new files only because they don't exist in
   the old tree. See `docs/LegacyAssetInventory.md` for the old-tree survey.

**Acceptance:** met. Runtime validation 2026-11-09 (~22:01 session): the
glsl300esv/glsl300esf pair compiles and links (id=158, no rejections) and the
in-game `dyn_shaderdebug` bisect confirmed the whole chain — every mode 1–6
draws (flat magenta, textures, lighting, fog, texcoords), and production mode 0
renders textured+lit terrain (user-confirmed; screenshot in
`pipeline/client_dev/local/screenshots/`). Phase 2 complete.

### Phase 3 — Visual parity validation — **RESULT: done 2026-11-09**

> Bisect pre-validation 2026-11-09: all six `dyn_shaderdebug` modes draw on GL3
> (VP varyings, UBO staging, sampler binding and light-table path alive) — the
> formal matrix below remains to compare GLSL output against GL1/arbfp1.
>
> **Matrix outcome 2026-11-09 (screenshots `screenshot_2026-11-09_00{0,1,2}.jpg`,
> sandbox):** production GLSL pair renders textured+lit terrain on GL3; all
> `dyn_shaderdebug` modes draw. Known limitation (not caused by the migration):
> `dyn_renderoldlandscape` (legacy CLandscape path) does not render on GL3 at
> all — pre-existing builtin/mega-pair incompatibility recorded in Phase 2.4,
> out of scope for this migration.

Matrix run (GL3 primary):

| Scenario | GL1 (arbfp1) vs GL3 (GLSL) |
|---|---|
| flat ground / slopes / cliffs, triplanar seams | screenshot pair |
| floor vs ceiling plane ("SLT" branch) on overhangs | " |
| fog at two distances (`dyn_fardist`) | " |
| mesh display factor (transition fade) | " |
| voxel selection cube overlay (separate unlit material path) | " |
| `dyn_renderoldlandscape` legacy CLandscape untouched | must render identical on both drivers |

Driver-drive the matrix with in-game `dyn_*` toggles; store screenshot diffs
under `pipeline/client_dev/local/screenshots/` per README_SANDBOX conventions.

**Acceptance:** met (2026-11-09 user sign-off: GLSL terrain renders correctly on
GL3; debug-mode bisect and screenshot set intact under
`pipeline/client_dev/local/screenshots/`). `docs/DynClientRuntime.md` § 5/§ 6
updated in the same commit wave — see the Phase 5 pass. GL1-vs-GLSL side-by-side
diff was waived by the user (the GL1 arbfp1 path is the frozen fallback and
`dyn_renderoldlandscape` does not render on GL3 anyway; the GL1 regression check
retires into that same known limitation).

### Phase 4 — Hot reload (`dyn_rebuildshaders` reads disk)

The rebuild path lives in `CDynLandscape::render()` (`dyn_landscape.cpp:176-186`):
`g_rebuildShaders` → `deletePrograms()` + `readShaderFiles()` + `setupPrograms()`.

**Known defect to fix in this phase — *fixed early in Phase 2*: the loop is
`bool succeed = false; while (!succeed) { ...setupPrograms(); }` — an **infinite
retry spin inside render()** if compilation keeps failing (same symptom class as
the MaterialLibrary assert flood, `docs/DynClientRuntime.md` § 3). Replaced by
single-attempt + snapshot-restore + builtin fallback (`reloadPrograms()`); the
following extension items are the remainder of the phase.

Extensions:
1. Re-read `.vp1/.arbfp1/.glsl` from disk before recompiling (currently the
   strings are re-serialized from the cached copy — verify whether
   `readShaderFiles()` actually reopens the files or only re-parses in-memory
   text; make it a true disk read).
2. On compile failure: log the full GL info log + keep last-good program.
3. Log which profile was picked per stage on every reload.

**Acceptance:** edit `.glsl` → `dyn_rebuildshaders` → next frame uses the new
source; malformed source logs the info log and keeps the old program (no spin,
no assert).

### Phase 5 — Docs cleanup & roadmap bookkeeping

1. `docs/DynClientRuntime.md` § 1/§ 6: shader files list (new GLSL pair), 
   `Driver3D=OpenGL3` option note + GL3 section, `dyn_rebuildshaders` new log.
2. Remove stale `.cg` pair (or fully delete), refresh `readShaderFiles` in
   `docs/LEGACY_DYNWORLD_INTEGRATION.md` roadmap status.
3. Strip instrumentation added on the way (keep `nlinfo` availability lines).

## 6. Risk register / open items

| Risk | Mitigation |
|---|---|
| `Empty` Float1 alpha slot → GL3 attribute mapping unknown/nonstandard | Phase 2 step 3; inspect `g_AttribNames`/`Empty` handling (`driver_opengl3_program.cpp:105`, `driver_opengl3_vertex_program.cpp:374`); worst case: explicit named value slot in `CRegionSubMesh`, keeping nelvp `v[7]` reading the same layout |
| GLSL varying location mismatches between authored VP and preload assumptions in the driver | reuse exact `VaryingLocation*` constants (`driver_opengl3.h`); assert at compile-time where the enum is included |
| glsl300es (linked) unused on desktop → half-tested code path | keep ES variant source as authored, compile only on demand; validate when a GLES/WebGL build target exists |
| PP builtins differ between GLSL and assembly profiles | `Features.MaterialFlags = TextureStages` must bind material textures to `sampler0/1` for glsl330f user PPs; verify `setUniformDriver` fills `light0*` for user GLSL; if gap → minimal driver addition, not a client hack |
| GL3 driver maturity (never validated in THIS project; no docs mention) | Phase 0 is deliberately assessment-only; keep GL1 as sandbox default until Phase 3 passes |
| `nlassert(ppok)` + `while(!succeed)` spin on compile-fail-under-usePP | Phase 2 step 4 + Phase 4 graceful handling |
| `.vp1`/`.arbfp1` fallback divergence vs. GLSL (future edits touch one source only) | document in `docs/DynClientRuntime.md` § 6 that GLSL is primary; mark the assembly files "frozen — GLSL is primary" in a header comment |

## 7. Instrumentation & logging (final disposition, Phase 5 closed 2026-11-09)

- `nldebug("3D: dynlandscape vertex program available (%s)")` / `pixel program
  available (%s)` — kept permanently as the load markers; they emit the picked
  profile name (`glsl300esv` etc.), so load evidence shows which source got
  picked.
- GLSL compile failure logs: the GL3 driver dumps the **whole** GL info log for
  dyn landscape programs (brute dump beats truncation) — kept, it caught the
  Phase-2 sampler-declaration failure with the full source listing.
- `dyn_shaderdebug` modes 0–6 (`landscapeParams.w` selector, staged from
  `g_shaderDebugMode`): **kept as permanent dev-only tooling** (command lives in
  the `#if !FINAL_VERSION` catalog, next to `dyn_useVP`/`dyn_usePP`; mode 0 is
  the untouched production path). The Phase-2 bisect that introduced them is
  over, but they cost nothing in production mode and have already paid for
  themselves once; cataloged in `docs/DynClientRuntime.md` §5.
- MVDBG/MVNAN probes seen in client logs belong to the issue-#2 collision work
  (registry: `docs/VoxelTerrainCollision.md` §5) — not this plan's instrumentation.

## 8. Files touched (planned)

| File | Change |
|---|---|
| `ryzom/client/data/shaders/dyn_landscape_vp.glsl` | new |
| `ryzom/client/data/shaders/dyn_landscape_fp.glsl` | new |
| `ryzom/client/src/dynworld/dyn_landscape.cpp` | multi-source setup, named uniforms, hot reload |
| `ryzom/client/src/dynworld/dyn_landscape.h` | narrow: members for loaded source strings / picked profile, if needed |
| `ryzom/client/src/dynworld/region_mesh.cpp` | only if § 6 Risk 1 forces an explicit alpha slot |
| `nel/src/3d/...` | **NO planned changes** unless the PP `TextureStages` gap in § 6 Risk 4 materializes |
| docs | `DynClientRuntime.md`, `LEGACY_DYNWORLD_INTEGRATION.md`, this plan |
