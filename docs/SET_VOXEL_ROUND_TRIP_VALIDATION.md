# SET_VOXEL round-trip validation — paused investigation (handoff)

> **Tracked as:** [ryzomcore#6](https://github.com/KishanGrimout/ryzomcore/issues/6)
> **Status:** PAUSED 2026-11-01, at the FE→MS hop. Client→FE leg fully confirmed; full-chain
instrumentation built and deployed but the decisive instrumented retest never ran, because
the mirror topology was broken by the night's service restarts (see §3).
**Framing:** this path has **never been confirmed to work on the legacy repository** — treat
the forward chain as *suspected-never-worked*, not "regression of ours".
**Repo:** `KishanGrimout/ryzomcore` (integration/legacy-dynworld)
**Companion doc:** `docs/LEGACY_DYNWORLD_INTEGRATION.md` (roadmap; § "Runtime findings and
session log" — 2026-10-23..31 entries).

---

## 1. What the chain is and where it was traced

Client → FE → MS → EGS → GPMS for the generic client impulsion
`DYN_WORLD:SET_VOXEL` (add one voxel: env, material, density, position):

```
client  commands.cpp dyn_addvoxel (~:5592)             pushNameToStream + NetMngr.push
   FE    fe_receive_sub.cpp:~1400                      action decode (ACTION_GENERIC_CODE=1)
   FE    id_impulsions.cpp:290 routeImpulsionIdFromClient
         :308  GenericXmlMsgHeaderMngr.getNodeId(bms) -> DYN_WORLD:SET_VOXEL
         :352  CMessage("CLIENT:" + msgName) re-serialised field by field
         :461  sendMessageViaMirror("EGS", msgout)     [game_share/mirror.cpp:2394]
   MS    ms_automaton.cpp:90 cbMessageToForward ("FWDMSG")
         :367 receiveMessageToForwardFromClient         (local vs remote classification)
         :542 tellLocalServicesAndSendMessages          (per-tick "UMM" flush to local client services)
   EGS   client_messages.cpp:3426 cbClientSetVoxel  (registered :3715 "CLIENT:DYN_WORLD:SET_VOXEL")
         :3450 CMessage("SET_VOXEL") -> sendMessageViaMirror("GPMS")
   GPMS  gpm2_service messages.cpp:860/:926 cbSetVoxel -> CWorldPositionManager::setVoxel
```

The FE forward (`sendMessageViaMirror` name-variant, `mirror.cpp:2394`) has **three
silent drop paths** — this is why the hop was invisible for days:

1. `Ignoring attempt to send a message via the mirror before mirror ready` (`localMSId` invalid);
2. `Discarding service ... not 'mirror ready'` (per-connection check, `nlinfo`-level);
3. `No service <name> is online` (`MIRROR_DEBUG`) — **plus** a fourth hard-suspect:
   if the FE's `CUnifiedNetwork::getConnectionList()` contains no connection named `EGS`,
   `counter = 0` and the `FWDMSG` is built **carrying zero destinations** — forwarded
   absolutely nothing, with no warning at all.

## 2. What is already proven (do not re-test these)

| Leg | Evidence | Verdict |
|---|---|---|
| msg.xml both sides | padded `DYN_WORLD` branch (single-child `getPowerOf2(1)==0` trap, fixed earlier); MD5 `D7216080470990FD02BCD29A6004CE79` identical | OK |
| client → FE | `client.log` 22:06:48 `dyn_addvoxel: header ok, serial pos+voxel, NetMngr.push`; FE `FERECV: packet N client 1 action[..] code=1` — **677/677 decoded actions = code 1 (ACTION_GENERIC)** | OK |
| FE bad-packet rejects | only 2× `REJECT: reason=2` (NotSystemLoginCode, client 0) at 22:02:09/22:05:35 = pre-entry login-overlap strays | benign |
| EGS receipt honesty | grep `CLIENT:|DYN_WORLD|SET_VOXEL` over `egs_mainland/log.log` at every test time: zero hits (only char-connect noise) | **never received** |

Conclusion so far: the loss is **in or after `routeImpulsionIdFromClient`'s forward**
— and no instrumented run ever got far enough to say which forward sub-path failed.

## 3. Showstopper found at wrap-up: mirror topology abandoned (2026-10-31 22:12)

The MS restart campaign (22:12–22:13, to deploy the MSGFWD traces) permanently broke
the mirror for everything except the FE:

- 22:12:05–06 — EGS, GPMS, IOS each logged `uncbDisconnection` from MS **and**
  `MIRROR: Local Mirror Service 128 is down!` in their own logs;
- **NeL does not auto-reconnect mirror client services to a restarted MS.** After that
  moment the fresh MS ("MS-128", pid 5044, ready 22:13:00) had exactly ONE client service:
  FS-130 (which re-joined at 22:13:24 only because it too was a fresh process);
- zero `ACKATE`/`ACKATP` forwards in `ms_mainland/log.log` after 22:13 — EGS/GPMS/IOS
  never re-subscribed. Even had the retest run, no `FWDMSG→EGS` delivery was possible.

**Wednesday's early tests (21:58 / 22:03 / 22:06:48) ran while EGS *was* mirror-ready,
but without the RTG/MSGFWD traces deployed — those runs cannot distinguish an FE-internal
drop from a downstream one. Do not treat them as evidence about the forward.**

## 4. Prime suspects, ranked (continue here)

1. **Mirror linkage (KNOWN state, §3).** Restore first (§5) or every other suspect is untestable.
2. **`sendMessageViaMirror("EGS")` destination enumeration** (`mirror.cpp:2394`): walks
   `CUnifiedNetwork::getConnectionList()` matching the *service name* "EGS". If the FE has no
   live connection to EGS (impulsions normally travel only via MS `FWDMSG`), the destination
   vector is empty → carrier leaves with `counter=0` → silent no-op. **This is the leading
   "legacy never worked" candidate.** Verify by dumping the FE's connection list / connection
   idents at startup (`fes_mainland_01/log.log` `+ connect ident` lines) and/or by instrumenting
   `counter` before `carrierMsgout.poke(...)`.
3. **MS local/remote classification** (`ms_automaton.cpp:367→`): if EGS is not in the MS's
   `_ClientServices` when `FWDMSG` arrives, the message is silently queued to
   `_RemoteMSList.pushMessageToRemoteQueue` and never delivered locally.
4. **EGS silent receipt branches** (`client_messages.cpp:3426`): `entity == NULL` (note the
   mislabelled warning banner `<EntityCallbacks::cbDefaultAttack>`) and `!canEntityUseAction()`
   both return without any success log. Add a receipt trace at function entry when resuming.
5. **GPMS side (`cbSetVoxel`, `messages.cpp:860/926`)** — untouched so far; only reach after EGS shows receipt.

## 5. How to resume

1. **Restore a valid mirror state.** Either:
   - cold-start the whole shard via `pipeline\shard_dev\start_mainland.bat` (recommended —
     it is also the last unvalidated Phase-5 item, and avoids session/id churn), **or**
   - warm restart only the orphaned mirror clients, in bat order: EGS → IOS → GPMS
     (bat lines 81/85/89; FS already fresh). EGS restart drops active char sessions.
   - Remember the restart discipline: `Stop-Process` → copy exe → `Start-Process`
     (a `Copy-Item` onto a running exe fails in-use).
2. **Verify pairing** in `ms_mainland/log.log`: `Forwarded ACKATE from EGS-* to FS-*`,
   `PROPMGT: Forwarded ACKATP` for EGS/GPMS/IOS, and FE's mirror-ready. Note FS and EGS
   may both print `*-130`-style ids — always match on the full line/prefix.
3. **User runs the two commands** (char `dyna`, uid 16): `/dyn_voxelselection on` then
   aim at ground + `/dyn_addvoxel Raw Forest Wood 1`. Beware the §2 trap: the terrain
   bumps locally *regardless* of server receipt — judge only by logs.
4. **Read the chain in order** (each instrumented):
   - FE: `RTG: Routed impulsion DYN_WORLD:SET_VOXEL to EGS [...]` — absent ⇒ suspect 2
     (decode/enumeration bails silently ⇒ instrument `counter` at `mirror.cpp:2416-2419`).
   - FE: any `Discarding service|No service .* online|Ignoring attempt` right after.
   - MS: `MSGFWD: forwarded msg to MS service id %hu (counter=%u)` — expect EGS's id;
     `counter=0` ⇒ suspect 2 confirmed at the FE.
   - MS: `MSGFWD: sent UMM with N messages to svc-*` for the EGS id — absent ⇒ suspect 3
     (routed as remote because EGS not in `_ClientServices` at that moment).
   - EGS: then GPMS — add the missing receipt traces first (suspects 4/5 only).
5. **Fix whatever the trace localizes** (enumeration fix, mirror-ready bookkeeping, or
   destination naming), then validate end-to-end **through persistence**
   (`map_cache_manager`/`slave_pager` — confirm the voxel survives a client relog), then strip.

## 6. Instrumentation inventory (strip ALL once Phase 5 completes)

> **State note (2026-11-01):** all of the below was **reverted from the branch** (kept only
> `msg.xml`, docs) to leave the tree clean; **no patch archive survived** — when resuming,
> rebuild FE/MS/client, redeploy. The deployed `code\build` / `pipeline\shard_dev` binaries
> from 2026-10-31 still carry the traces until rebuilt.

Deployed in `code\build` Release and copied to `pipeline\shard_dev\*`:

- **client** `ryzom/client/src/commands.cpp` — `dyn_setvoxel` (~:5535), `dyn_addvoxel`
  (~:5592/5599), `dyn_voxelselection` (~:5615/5618) argument/state traces.
- **FE** `frontend_service/fe_receive_sub.cpp` — `FERECV: received packet ... (N actions ...)` (~:1400)
  and per-action `FERECV: packet %u client %u action[%u] code=%u` (before the generic switch).
- **FE** `frontend_service/id_impulsions.cpp:462` — `RTG: Routed impulsion %s to %s [%s]` (commented
  legacy `nldebug` re-enabled as `nlinfo`).
- **MS** `mirror_service/ms_automaton.cpp` — `MSGFWD: forwarded msg to MS service id %hu (counter=%u)`
  in `receiveMessageToForwardFromClient` (~:389) and `MSGFWD: sent UMM with %u messages to svc-%hu`
  (fires only when >0) in `tellLocalServicesAndSendMessages` (~:552).
- **Kept-by-design SNAIL set** (separate; strip with the above): `LOOP_SNAIL`
  (`nel/src/net/service.cpp`), `MS_SNAIL` (`ms_automaton.cpp` + `tick_proxy.cpp`),
  client cbTick gap alarm (`tick_event_handler.cpp`).

## 7. Known-benign noise (do not chase while resuming)

- MFS-257 `Connection refused` from every service every ~14 s (no mesh platform).
- AES legacy `can't connect to AS-256` warnings.
- REJECT `reason=2 NotSystemLoginCode` from client 0 before each entry.
- First dyn-world entry `.sector` cache warnings (empty cache state).

## 8. Rest-of-Phase-5 queue (after this block closes)

1. SET_VOXEL receipt in EGS **and persistence** in GPMS (`WPM::setVoxel` → map_cache/slave_pager).
2. Render smoke: `g_rebuildShaders`, `g_useVP`, `g_usePP` vs `g_renderLegacyLandscape`.
3. Non-dyn landscape regression.
4. `start_mainland.bat` final validation tick-off (§5 item 1 doubles as this if cold-started).
5. Strip §6 instrumentation, rebuild everything touched, redeploy, re-verify once.

## 9. Issue reference

File: [ryzomcore#6](https://github.com/KishanGrimout/ryzomcore/issues/6) —
`Phase 5: SET_VOXEL client->GPMS round-trip silently drops at the FE->MS forward (legacy path likely never worked)`.

**Title (as filed):** `Phase 5: SET_VOXEL client->GPMS round-trip silently drops at the FE->MS forward (legacy path likely never worked)`
