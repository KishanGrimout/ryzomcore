# VoxelMessageChain — `DYN_WORLD:SET_VOXEL` and the dynworld generic-message chain

> **System doc.** Documents the client→FE→MS→EGS→GPMS message chain for
> `DYN_WORLD:SET_VOXEL`, its msg.xml rules, and its silent-drop paths.
> The related **plan** (paused investigation, ranked suspects, resume protocol,
> instrumentation inventory) is
> `docs/SET_VOXEL_ROUND_TRIP_VALIDATION.md`, tracked as GitHub issue
> [KishanGrimout/ryzomcore#6](https://github.com/KishanGrimout/ryzomcore/issues/6).
> Shard/mirror operational context: `docs/ShardRuntimeOps.md`.

Status: client→FE leg fully proven; EGS receipt never observed so far; investigation
paused 2026-11-01 at the FE→MS hop.

## 1. The chain

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

`SET_VOXEL` message format: `s32 s32 s32 u8 u8 u8 u8` (voxel position + env/material/
density fields); the client builds it in `commands.cpp` with
`GenericMsgHeaderMngr.pushNameToStream("DYN_WORLD:SET_VOXEL", out)` +
`out.serial(pos, voxel)` + `NetMngr.push`.

## 2. msg.xml rules (must-know before editing)

- The branch lives in `code/ryzom/common/data_common/msg.xml` under the client_messages
  description: `<branch name="DYN_WORLD">` with the `SET_VOXEL` leaf
  (`sendto="EGS"`). FE restart is needed after any msg.xml change (its parser runs at
  `frontend_service.cpp:1334`).
- **Never leave a branch with a single child.** `game_share/generic_xml_msg_mngr`
  computes `NbBits = getPowerOf2(childCount)` and `NLMISC::getPowerOf2(1) == 0`, so a
  one-leaf branch gets 0 bits-per-child and `CNode::select()` refuses at the
  `NbBits == 0` check (`generic_xml_msg_mngr.h:414-417`, "parent 'DYN_WORLD' has no bit
  per child" → `pushNameToStream failed: Unknown message name`). Concretely: the
  `DYN_WORLD` branch originally had only `SET_VOXEL` and the client push failed with
  exactly that error before runtime even started.
- Fix deployed (data-only, no rebuild): a `DUMMY` padding leaf added into the
  `DYN_WORLD` branch of **both** `msg.xml` copies that must stay byte-identical:
  - `code/ryzom/common/data_common/msg.xml`
  - `pipeline/client_dev/local/xml_server/msg.xml`
  (verified MD5-identical at the time; an in-file comment explains the ≥2-children
  constraint). Keep the copies in sync whenever this branch changes again.

## 3. Silent drop paths in `sendMessageViaMirror(name)` (`game_share/mirror.cpp:2394`)

Three logged-but-quiet failures plus the fourth hard suspect — this is why the hop was
invisible for days:

1. `Ignoring attempt to send a message via the mirror before mirror ready` (`localMSId`
   invalid);
2. `Discarding service ... not 'mirror ready'` (per-connection check, `nlinfo`-level);
3. `No service <name> is online` (`MIRROR_DEBUG`);
4. **If the caller's `CUnifiedNetwork::getConnectionList()` contains no connection with
   the requested service name, `counter = 0` and the carrier `FWDMSG` is built carrying
   zero destinations** — forwarded absolutely nothing, with no warning at all (leading
   "legacy never worked" candidate, plan §4 suspect 2).

Mirror/topology side conditions (MS restart laws, how to check pairing) are documented in
`docs/ShardRuntimeOps.md`.

## 4. In-world entry points (client)

- `dyn_setvoxel` — apply a voxel under the cursor.
- `dyn_addvoxel` — apply a voxel in the empty cell above the terrain under the cursor;
  e.g. `/dyn_addvoxel Raw Forest Wood 1`.
- `dyn_voxelselection [on,off]` — arms the cursor raycast + cyan wireframe cube. **The
  selection cube is INACTIVE by default**; without it `voxelUnderCursor()` yields
  nothing and the apply commands have no target.
- Environment names come from `environments/*.cfg` `Name = "..."` (case-sensitive:
  `Forest`, `Jungle`, `Desert`, `Lakes`; `Prime roots` is the deep-layer biome, excluded
  from surface noise). Material names from `materials/*.cfg` (`Wood`, `SawDust`, `Mud`,
  `Humus`, `Goo`, `Earth`, `Cinder`, `Chipboard`, `Bark`).

## 5. The local-edit ≠ server-receipt trap

`MAP->setVoxelAt` runs **before** the `DYN_WORLD:SET_VOXEL` push, so terrain visually
bumps even while the server never hears about it. Observed live: voxels appeared in-world
while EGS/FE/GPMS totals silence. **Judge a chain test by logs, never by the landscape.**

Server-side receipt is likewise silent in several branches (EGS: `entity == NULL` and
`!canEntityUseAction()` both return with no success log, and the warning there is
mislabelled `<EntityCallbacks::cbDefaultAttack>`) — receipt tracing lives in the plan
(suspects 4/5).

## 6. Known-benign noise near this chain

- Client-side `.sector` cache warnings (`Failed to open file './cache/map/....sector'`)
  at first dyn-world entry = benign empty-cache state.
- REJECT `reason=2 NotSystemLoginCode` from client 0 before each entry.

Full benign-noise skip lists: plan §7 and `docs/ShardRuntimeOps.md`.

## 7. Dyn routing context (who turns the chain on)

- Server deployment: `IsDynamicContinent = 1;` in the EGS cfg makes EGS send
  `isInDynWorldSession=true` in `CUserCharMsg` (write side `character.cpp:7703`); client
  reader is `net_manager.cpp:430` → `IsInDynWorldSession` →
  `CContinentManager::select()` routes `select("dynamic")` (`continent_manager.cpp:407`)
  → `CDynContinent` + `CDynLandscape`.
- Client init consequences of the routing (shader/material data path) are documented in
  `docs/DynClientRuntime.md`; deployment deltas in `docs/ShardRuntimeOps.md`.
- Verified live **by behavior, not by config echo** — NeL `CVariable`s don't log; the
  proof markers are the absence of legacy `Looking into %s` continent logs and the dyn
  landscape init lines.
