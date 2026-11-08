# ShardRuntimeOps — local shard runtime operations

> **System doc.** How the local dev shard is deployed, run, restarted, and diagnosed:
> services, ports, mirror topology laws, web/DB environment, login lifecycle, ops kit.
> Roadmap history: `docs/LEGACY_DYNWORLD_INTEGRATION.md` (Phase 5 section).
> Message-chain context: `docs/VoxelMessageChain.md`.
> Client sandbox side: `pipeline/client_dev/local/README_SANDBOX.md` +
> `pipeline/shard_dev/README_SHARD_OPS.md`.

## 1. Ops kit (files)

- `pipeline\shard_dev\start_mainland.bat` — cold start of the 16-service roster.
- `pipeline\shard_dev\status_mainland.bat` — ports 40916 / 3306 / 46997 / 46993 / 47911
  + 16-process count.
- `pipeline\shard_dev\stop_mainland.bat` — reverse-roster graceful→force shutdown +
  optional XAMPP stop.
- `pipeline\client_dev\local\launch_client.bat` — client launcher (sandbox CWD) — see
  the README_SANDBOX next to it.
- Manual startup order: XAMPP apache/mysql → `start_mainland.bat` → `launch_client.bat`.
- Written/read by: `pipeline\shard_dev\README_SHARD_OPS.md` (keep its rules in sync with
  this doc when they change).

## 2. Service roster and launch rules

- `start_mainland.bat` starts **16 services** in this order:
  `rns_mainland` → `ras` → `aes` → `backup_lgs` → `bms_master` → `lgs_unifier_01` →
  `lgs_unifier_02` → `ms_mainland` → `ts_mainland` → `egs_mainland` → `ios_mainland` →
  `gpms_mainland` (`ryzom_gpm2_service.exe`) → `su` → `rws_mainland` →
  `sbs_mainland_01` → `fes_mainland_01`. All launches `start "svc" /min` +
  `nircmd wait`.
- AIS / `*_02` variants deliberately omitted (dyn dev world, no AI).
- **Never click-close any spawned NeL service console window** — services treat it as an
  ordered shutdown and take the shard's open state down with them (see §6).
- **MySQL must be up before SU**; FE console close is the classic accidental kill (§6).
- FE and EGS service ids can collide visually (FE restarted as `FS-130` while EGS is
  `EGS-130`) — always disambiguate by context/full prefix, never by the number alone.
- Restart discipline for replacing a service binary: `Stop-Process` → copy exe →
  `Start-Process` (`Copy-Item` onto a running exe fails in-use).

## 3. AES (port 46997) — the SYN_SENT storm class

- Every service's auto-generated cfg (`live/service_*` includes) creates a
  `gw_aes`/`aes_client` module gateway connecting to `localhost:46997`. **A dead listener
  there = a `Connecting to localhost:46997` retry storm every ~4 s in every service's
  `updateModules`, stalling the whole shard tick.**
- Diagnostic signature: `LOOP_SNAIL` ~4.0–4.2 s per EGS loop in `updateModules`,
  `MS_SNAIL` tick gaps ~5 s, `TS_SNAIL late tock`; netstat shows only SYN_SENT toward
  46997 and nothing LISTENING.
- The proper AES binary is the nelns one, **not** `ryzom_admin_service` (the Oct-23
  "AES" was a temp re-alias of `ryzom_admin_service.exe` launched with
  `--shortadminname=AES`; its `AdminExecutorService` module + `aes_gw` L3Server on
  46997 come from its cfg `StartCommands` in `pipeline\shard_dev\admin_executor_service.cfg`,
  `DontLaunchServicesDirectly = 1; UseExplicitAESRegistration = 1`). That manual run left
  no artifact; don't hunt for "the AES binary" again — it's built from
  `code\nelns\admin_executor_service` (root CMake option `WITH_NELNS=ON`, needed a
  reconfigure; deps NeL::misc+net only → `code\build\bin\Release\admin_executor_service.exe`).
- Durable deploy: exe in `pipeline\shard_dev\aes\`, launched with cwd
  `pipeline\shard_dev\aes`, args `-C.. -L.. --writepid`; cfg stays at
  `pipeline\shard_dev\admin_executor_service.cfg` (port 46997 from `AESPort`,
  `DontUseNS=1`, `DontUseAES=1`, `LogDirectory` in `aes\`).
- `start_mainland.bat` starts AES **right after ras** (the AES cfg opens `asc_gw` toward
  AS `localhost:46998`; if AS isn't up, AES retries while services connect fine).
- Validation markers: 46997 LISTENING (dual-stack) → all 16 services ESTABLISHED on it
  within seconds → `LOOP_SNAIL`/`MS_SNAIL`/`Connecting to localhost:46997` stop
  instantly; `TS_SNAIL` quiets via ~1 min reconnect window. Restart resilience verified:
  killing AES and relaunching from the `aes\` layout reconnects all services without a
  shard restart.
- Leftover benign noise (do not chase): per-service
  `CAliveCheck W: can't connect to MFS-257 (Connection refused ...:43980)` every ~14 s
  (mail/forum service intentionally not deployed); nelns AES
  `can't connect to AS-256` retries (pre-module-gateway direct-connect path; module work
  happens through `aes_gw`/`asc_gw` regardless).

## 4. NeL mirror topology laws

- **NeL mirror client services do NOT auto-reconnect to a restarted MS.** After an MS
  restart, the fresh MS has exactly the services that also restart; EGS/GPMS/IOS log
  `MIRROR: Local Mirror Service 128 is down!` and never rejoin (zero `ACKATE`/`ACKATP`
  forwards until they are restarted).
- Detect pairing state in `ms_mainland/log.log`: `Forwarded ACKATE from EGS-* to FS-*`,
  `PROPMGT: Forwarded ACKATP` for EGS/GPMS/IOS, and FE mirror-ready.
- Restore options: cold start whole shard via `start_mainland.bat` (preferred), or warm
  restart only the orphaned mirror clients in bat order (EGS → IOS → GPMS). **EGS restart
  drops active char sessions.**
- Consequence for testing: any test that depends on mirror routing (e.g. SET_VOXEL)
  requires a valid mirror state first — see `docs/VoxelMessageChain.md` and its plan §5.

## 5. Web/DB environment (XAMPP on this machine)

- XAMPP lives at `D:\xampp` (moved from `C:\xampp`; do NOT move back). Services start via
  `apache_start.bat`/`mysql_start.bat` or the control panel; mysqld needs
  `--defaults-file=D:\xampp\mysql\bin\my.ini`. Ports: Apache 80/443/**40916** (= client
  `StartupHost`), MySQL 3306.
- The 2026-10-22 dead-Apache root cause: `conf/extra/httpd-vhosts.conf` pointed the shard
  vhosts (80 + 40916) at a stale `E:\...` path; config-parse abort happened before any
  log. Fix pattern for stale paths in that config tree: vhosts →
  `D:\Programmation\ryzomcore_old\ryzomcore_dev\code\web\public_php` (the current repo
  has **no `web/`** — U-S5), remaining `C:/xampp` refs in loaded
  `httpd-{autoindex,dav,multilang-errordoc}.conf` → `D:/xampp`, stale `E:` refs in
  `public_php/config.php` (`$PUBLIC_PHP_PATH`/`$PRIVATE_PHP_PATH`, backslash-escaped).
- Smarty caches under `ams/templates_c` are read-only + stale — clear them, they
  regenerate.
- Health checks: `httpd -t` clean; `r2_login.php` answers protocol-correct
  `0:Missing cmd (3002)`; AMS login page renders. MySQL carries the shard DB set:
  `nel`, `nel_ams`(+`_lib`), `nel_tool`, `webauth`, `ring_open` (with
  `sessions`/`session_participant`/`known_users`).

## 6. Login lifecycle: error 4010 + "shard offline"

- **4010** ("game servers closed") = `joinSession` failure. Defined **only in the
  web-shard PHP layer**: `code/web/public_php/.../login_translations.php:196` —
  `$ErrMsgs[4010]['dbg'] = '(joinSession error %1: %2 for userId %3)'`,
  `'lnk' = $msgGameServersClosed`. It is NOT searchable in `code/ryzom/{client,server}/src`
  — grep the `code/web` tree for such codes. Beware `24010` in `prod.inc.php:293` — a
  different key space (DEFAULT_ERROR_MESSAGE table), not this 4010.
- Diagnostic order for a 4010 right after restarting a shard service: SU
  `session_user_service` log (joinSession attempt + reason) → FE/registration logs →
  whether the restarted service re-registered with the naming/welcome chain.
- The 2026-10-23 chain (root cause): user closed the FE console window
  (`service.cpp:1366: "The window displayer was closed by user, need to quit"` — clean
  `Service ends`) → welcome service saw `FESDisconnection` → `reportOnlineStatus(false)`
  → SU `Shard 901 is now offline and closed` → `UPDATE shard SET WSOnline='0'` → next
  login's `on_joinMainland` couldn't route through the closed shard → 4010. All log-side,
  nothing wrong in code.
- Lessons (recorded here, they apply to every future run):
  (a) never close spawned service consoles (§2);
  (b) after an FE restart, expect 4010s until the welcome → SU open-state report lands
  again (or a full cold start);
  (c) authoritative shard-open state = the SU's `UPDATE shard SET ... WSOnline = ...`
  query lines in the log;
  (d) an FE restart is otherwise cascade-safe (EGS/GPMS/IOS rejoin it via mirror
  `cbMirrorUp`).

## 7. Deployment model and dyn deltas (patchman templates)

- The workspace root IS the Core 4 Quick Start package (`pipeline/`, `patchman/`,
  `leveldesign/`, `external/`), and `pipeline/shard_dev/` is already generated for this
  host (e.g. `gpms_mainland/gpm_service.cfg` has this machine's host baked in).
  Service cfgs are generated by the deployment pipeline from the patchman template repo
  (e.g. `patchman/cfg/02_shard_type_std_mainland.cfg`, which inlines `UsedContinents`
  incl. `newbieland=20` rather than using a separate `used_continents.cfg`; both feed the
  same config variable).
- Consequences: (a) no hand-assembled cfg run dir is needed — the pipeline is the native
  deployment path here; (b) **the patchman templates have no dynworld integration**: no
  `IsDynamicContinent`, no `gpm2` — the deployed shard ran stock `gpm_service`, while the
  dynworld shard runs `ryzom_gpm2_service` (legacy `shard_start_cmake.bat` l.32).
- Dynworld deltas to carry at template level (from U-S5, recorded in the roadmap):
  `IsDynamicContinent = 1;` in `used_continents`/`egs` cfg (applied live 2026-10-31:
  added to `pipeline\shard_dev\egs_mainland\entities_game_service.cfg` right before
  `FSListenHost`), `LoadPacsCol = 0;` for gpm-style cfgs as applicable, and
  `gpms_mainland` running `ryzom_gpm2_service.exe` with its own cfg set
  (`gpm2_service.cfg`/`gpm2_service_default.cfg` chains). Smoke-first: standalone
  `ryzom_gpm2_service` (material-library init only, no shard) before full bring-up.
- Server narrowly avoids a trap: gpm2 never touches `CMaterialLibrary`/`getBiome` (only
  the client and legacy non-deployed `gpm_service.cpp:372` do) — that data path is
  client-only (see `docs/DynClientRuntime.md`).
- Phase-5 runtime milestone log (cold-start checklist, in-flight validation items):
  roadmap § 5 in `docs/LEGACY_DYNWORLD_INTEGRATION.md`.

## 8. Kept-by-design instrumentation registry (shard side)

Strip only after the relevant validation completes, then rebuild + redeploy:

| Marker | Where | Purpose / strip condition |
|---|---|---|
| `LOOP_SNAIL` | `nel/src/net/service.cpp` | per-loop timing alarm (AES-storm diagnosis) — strip at Phase 5 validation end |
| `MS_SNAIL` | `ms_automaton.cpp`, `tick_proxy.cpp` | mirror/tick gap alarm — same |
| `FERECV:` traces | `frontend_service/fe_receive_sub.cpp` ~:1400 | chain tracing used by SET_VOXEL issue — currently **reverted** (2026-11-01); re-implement from `docs/SET_VOXEL_ROUND_TRIP_VALIDATION.md` §1/§6 anchors when resuming |
| `RTG:` routed-impulsion trace | `frontend_service/id_impulsions.cpp:462` | same — reverted |
| `MSGFWD:` traces | `mirror_service/ms_automaton.cpp` ~:389/~:552 | same — reverted |
| client cbTick gap alarm | `tick_event_handler.cpp` | client-side stall alarm — Phase 5 |

(The SET_VOXEL chain markers were reverted without an archived patch; the deployed
`code\build` / `pipeline\shard_dev` binaries of 2026-10-31 still contain them until
rebuilt.)
