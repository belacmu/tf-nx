# Several players per device, online, Switch and browser (plan, 2026-10-24)

The user's priority: cross play between the Switch and the browser **including several local players on each device** comes before
perfect compatibility with plain TF.EX (still wanted, second). Background: docs/research-couch-online.md (2026-10-17, option (c)).

## Superseded (2026-10-30): TF.EX v0.28.0 has its own local players
Upstream merged its own version (Fcornaire/TF.EX#306, dfd58c5, 2026-10-29; server "feat: guests support" d30fe19) and our ggrs-ffi
half as it was (Fcornaire/ggrs-ffi#24, merged 2026-10-28). The Switch and the browser run v0.28.0 as released; our TF.EX PR #283 is
still open upstream (no review), now redundant. What upstream did differently:

| | ours (#283, e14aa6d + fbeec67) | upstream (#306 + server d30fe19) |
|---|---|---|
| GGRS | `netplay_set_local_inputs`, local handle list (ggrs-ffi #24) | the same entry points (our #24, merged); local handles read once at connect into `SessionSeats` |
| Protocol | new messages `AddLocalPlayer {player}` / `RemoveLocalPlayer {seat}` while in the lobby | no new message: `local_players` (1..3) on CreateLobby / JoinLobby / JoinPrivate / EnterQuickPlay; the server seats guests itself |
| Guest identity | an extra `Player` with the device's RoomPeerId, told apart by seat (the "main seat" remembered client-side) | `Player.LocalIndex` (0 = owner, >0 = guest, `IsGuest`), on the wire; name "<owner> 2", the owner's variants |
| When to join | in the roll call, any time: a spare device presses confirm, the server adds a seat (5 s timeout, orphan seats given back) | before the lobby: START on another device on the netplay menu (LocalPlayersPanel: JOIN / SWAP / QUIT hints), BACK leaves; the count goes with the lobby request |
| Devices | spare-device list, a periodic re-scan via the game's AssignInputs into a scratch array (hot-plugged pads, split Joy-Cons; fbeec67) | guest inputs list, joinable = attached TFGame/MenuInputs devices; `IsSameDevice` by XGamepad object / keyboard ID; no re-scan |
| Menus | every local device drives its own roll-call slot | only the owner's device drives menus on netplay screens (`KeepOnlyLocalInputInMenus`, also in levels); guests' archers picked in the series screen |
| Limits | lobby size only | max 3 per device; "NOT ENOUGH SEATS FOR N PLAYERS"; ranked refused ("RANKED IS ONE PLAYER PER DEVICE"); min lobby size = largest device + 1 |
| Server | devices, not players, for match start / ping / relay; seats on request | the same rules (peer_count, votes per addr) + quick play and series fill by device size; lowest non-guest becomes host |
| Extras in #306 | | RefusableModeButton (refusals un-confirm the button), custom skins per local player (queue), ping overlay hides guests, Player.InitHead team-head textures, the series screen's guest picks |

What we lose: joining after the lobby opened (in the roll call), and a pad plugged in late (fbeec67) (upstream: leave and come back).
What we gain: no per-lobby seat requests or timeouts, ranked/quick play handled, and no fork to keep. On the Switch the second player
now joins with START on the netplay menu (smoke localtwo); the A-join, late join and lobbylocal2late are gone.
Left: the browser repo's JS server (local.mjs, the worker) still speaks #283's AddLocalPlayer, not `local_players` (STATUS open bugs).

## Status (2026-10-24)
- Switch (branch claude/local-mp): translated from belacmu/TF.EX 761f049 (v0.22.2 + #283), ggrs-ffi-switch with #24. The input per seat
  is the Switch's own poll (`svc::PolledInput(seat)` -> `NpLocalInput`, TFEX-DIFF S-input); `PushLocalPlayersInputs` feeds GGRS every own
  seat before `AdvanceFrame`. Translator: a hosted unit's statics are made at first use (#283's InputService lists allocated at module
  load and crashed the boot).
- Browser (branch claude/local-mp, local): the fork via TFEX_SOURCE, ggrs-ffi patch with #24, the server's local-player rules; harness
  input scripts for 4 seats.
- `tools/smoke.sh localtwo` (2 Switch players, `lobbylocal2`, + 1 page): PASS, 0/592 differing ticks, GGRS checksum clean.
- Not yet run: 1 Switch + 2 local in the browser (the page harness has no second local player yet); the console.

## The contract (already designed, already offered upstream)
Extra local players are extra lobby `Player` records that share the device's `RoomPeerId`. GGRS handles stay the seats' ranks; every
seat of a peer is a local handle on that peer; other peers see one endpoint with several handles and need **no change** (so an
unmodified PC TF.EX peer plays against a device with two local players).

| piece | where it is | state |
|---|---|---|
| TF.EX side (lobby, roll call, input per seat, `netplay_set_local_inputs`) | Fcornaire/TF.EX#283, branch belacmu/TF.EX `local-multiplayer` (on v0.21.1) | open, conflicts with TF.EX main; **its code applies cleanly onto v0.22.2** (only CHANGELOG conflicts) |
| ggrs-ffi side (every own seat `Local`, `netplay_set_local_inputs`) | Fcornaire/ggrs-ffi#24, branch belacmu/ggrs-ffi `local-players` (on v2.1.0) | open; **rebases cleanly onto v2.2.0** |
| server rules (`AddLocalPlayer` / `RemoveLocalPlayer`, devices not players for match start / ping / relay) | browser branch `claude/tfex-local-mp` (5 commits, 2026-10-17, prototype) | not on main; additive (plain TF.EX never sends these) |
| browser | the same branch: TF.EX from source (`TFEX_SOURCE`), ggrs-ffi patch | not on main |
| Switch | only an archer-select prototype with a second connection per extra player (docs/ws-archer3-local.md) | a dead end for matches (research 3.4): to be replaced, not extended |

## The plan
1. **TF.EX v0.22.2 + #283** on a fork branch (`belacmu/TF.EX lmp-v0.22.2`), **ggrs-ffi v2.2.0 + #24** (`belacmu/ggrs-ffi
   lp-v2.2.0`). The PR branches themselves are refreshed onto upstream's latest main separately (step 2 of the user's order).
2. **Browser:** `TFEX_SOURCE=belacmu/TF.EX@<lmp-v0.22.2>`, the ggrs-ffi patch gains #24, the server's local-player rules rebased onto
   main. All inert until a device adds a local player; with the official server an extra player's request times out (as #283 says).
3. **Switch:** translate from the same fork commit (vendor/tfex points at it), so the lobby / roll-call / input logic is TF.EX's own
   translated code. Hand work where the Switch has its own runtime for a single local seat:
   - the netplay gate's local input (`NpLocalInput(g_npSeat)`, 18 uses): one local seat -> a set of local seats, extra seats through
     `netplay_set_local_inputs`;
   - the `[service]` rules for the game context / input service (`svc::GetLocalSeat`, `svc::LocalInput`, overrides.toml: 15 rules) to
     carry per-seat devices;
   - the Switch's controllers (Joy-Cons, the controller applet) as #283's "spare devices";
   - ggrs-ffi-switch gets #24;
   - the archer-select prototype (second connection, `LocalGuest`) deleted.
4. **Checks (emulator, then console):** 2 local on the Switch + 1 in the browser; 1 on the Switch + 2 local in the browser; a plain
   1v1 unchanged (GGRS checksum clean). One smoke scenario each (tools/smoke.sh gains them).
5. **The user's settings toggle** (features plain TF.EX doesn't have): local multiplayer is already inert with a server that doesn't know
   it, so the toggle mostly decides what the menus offer. Decide once the feature runs.

## Open questions for the user
- Pushing two new fork branches (belacmu/TF.EX, belacmu/ggrs-ffi) and the browser's site building TF.EX from that fork (TFEX_SOURCE):
  the site then runs our fork of v0.22.2, identical to v0.22.2 for anyone without a second local player.
