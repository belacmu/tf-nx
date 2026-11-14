# Workstream tfex-lockstep: TF.EX v0.21.0's lockstep changes (2026-10-16)

## STATUS AND RESUME (stopped 2026-10-17, branch ws/tfex-lockstep at 196a921)

Everything below is committed on `ws/tfex-lockstep`; nothing is running. To land it: merge ws/tfex-lockstep into ws/tfex-base, then
master (ws/tfex-subclass 14b6f0b is needed for the rollcall pings to show), **then run `ggrs-switch/build.sh` in the main checkout**
(the Rust lib with ping measurement and the disconnected-input change is gitignored, `hw-probe/exlaunch/rustlib/libggrs_spike.a`),
then `tools/publish-hw.sh` for the console.

**Done and tested in Ryujinx (lobby vs browser TF.EX 0.19.2, 0 differing ticks unless noted):**
- v0.21.0 lockstep (tower pool, NoRandom, level order, UnlockData getters, SessionPlayers/MatchPlayers, NormaliseSeats); C# selection
  model + `Tower::SelfTest` (standard and wide). Sections 1-3, 7.10.
- v0.20/0.21 protocol and UI: Lobby fields, KickedFromLobby, IsCheckingConnections, relayed-peer helpers, notifications, Models/Json
  fixes (MapId -1, Series kept, escapes, `[]` messages). Sections 5-6.
- Ping measurement (TF.EX's ggrs_ffi, direct), PeerMeasured, connection quality; rollcall ping line on the generated ConnectionIcon
  (needs tfex-subclass's call site; tested on a merge of both). Section 7.
- Disconnected-player kill (PlayerPatch) + the end rule (2 players / TDM end at once, FFA goes on). Sections 7.3, 7.9, 7.11.
- Console findings: Screen.Offset/OffsetAdd at level load (8.1), OrbPickup settings guard (8.2), WhiteArcherPrison broken in netplay
  (8.3), Clumsy Archers RNG (8.4), Offset World RNG (8.4).

**Ported, not tested:** Always Scrolling (StartScroll RNG, 8.4); OrbPickup guard and the relayed icon have no trigger in the tests.

**Not done (resume here, in this order):**
1. Variant-gated TF.State patches (8.4; reachable when the Switch joins a TF.EX lobby with the variant on): bottomless chests
   (TreasureChest.cs), ReturnAsGhosts corpse counter + PlayerGhost (PlayerCorpse.cs, PlayerGhost.cs), DarkPortals (state machine +
   the Bat/Slime enemy patches). Half a day to a day each. Stopgap offered to the manager: refuse/warn on joining a lobby with an
   unported variant (gate: VariantsExtensions::HasAll, tfex/NetplayManager.cpp; message needs a MainMenuPatch/NetplayScreens call site).
2. Initial-connection timeout + failed-connection message (7.4): xplay.cpp NetplayGate (coordinator's file); the message side is ported.
3. Relay transport (TF.EX's relay.rs: WebSocket control + UDP; needs a non-Workers host) and relay ping measurement (5.3, 7.5).
4. tfex-server changes: `docs/ws-tfex-lockstep/tfex-server-v0.21.patch` (not applied, not deployed).
5. "A PLAYER LEFT" notification on a team-match leave (7.11).
6. InputDelayAdvisor (needs a Switch input design) and the in-game ping overlay (tfex-update will generate it) (7.8).

**Hardware checklist (couldn't be tested in Ryujinx):** real per-seat RTTs and Good/Ok colours; STUN candidates; the console's socket
budget with the lobby's second WebRTC session; a level change after a space-orb scroll (8.1, the console finding itself); 2 players
continuing after 2 leaves in a 4-player FFA; leaves in Head Hunters and wide; ping line in wide and for a spectator; the kill against a
real TF.EX v0.21.0; Always Scrolling; anything needing the server patch (relayed icon, kick, CONNECTION UPDATED, CHECKING CONNECTIONS,
MatchPlayers). Full list: section 7.12.

Branch `ws/tfex-lockstep` (cut from master + `ws/tfex-base`). TFEX-BASE.md section 6 table A, section 8 step 4 (the A part).
Upstream read: `git -C vendor/tfex diff v0.19.2 v0.21.0` (covers v0.20.0 and v0.20.1; 7 commits). Files changed here:
`tfex/NetplayManager.cpp`, `tfex/Models.{hpp,cpp}`, `tfex/MatchmakingService.cpp`, `pcgameplay.cpp`, `rnd.cpp` (no xplay.cpp/main.cpp change:
the new hooks install from `rnd::Install`).

## 1. Table A, row by row

| change (TF.EX v0.21.0) | verdict | what the Switch does now |
|---|---|---|
| **Random tower pool**: `MapScenePatch.InitVersusButtons_Prefix/_Postfix` make SunkenCity/Moonstone/TowerForge/Ascension unlocked while the map buttons are made (commit 04ef5d0) | lockstep, **ported** | `NetplayManager.cpp` `Tower::InitVersusButtonsUnlocks` (scoped guard, the four flags restored after) around `SelectStandard`/`SelectWide`. Replaces the 0.19.2 port's `UnlockAll()` (save flags set for good, a copy of the browser's BrowserHost.UnlockArchers): with the arrow-pool guard (LocalSettingsGuard = rnd.cpp UnlockGuard) and the getters below, nothing in a match needs the save flags any more, and the user's save is no longer changed. The comment "each side draws from its own unlocked towers" is gone. |
| **NoRandom cleared** (`TFGame.RequestNetplayEntry`, `NetplayManager` reset) | lockstep in TF.EX (its GetRandomVersusTower drops NoRandom buttons); the Switch never read NoRandom | `Tower::ClearNoRandom()` at the lobby match start: 1.4.1.6 has `MapScene.NoRandomStates` (static bool[], slot +0x4794eb0, made by the static ctor +0xab0d30), zeroed. TF.EX's own call sites (netplay entry = MainMenuPatch.cpp, not this workstream's file) are a follow-up, only for the local map screen afterwards. |
| **Level order**: `VersusLevelSystemExtensions.SortByFolderAndName` + `/`→`\` (commit c9b4f80, "fix linux") | **no change in practice**, ported anyway | The PC's `VersusTowerData` ctor already does `Array.Sort(Directory.GetFiles(dir))` then the DLC dir's; every official tower's levels are in one folder named `NN.oel`, so the new sort gives the same list (Windows/macOS). It fixes Linux (Mono) only. Model (section 3): v0.21.0 order == v0.19.2 order for all 16 towers and 8 seeds. `pcgameplay.cpp` GetLevelsShell now sorts its result the same way (stable, folder first-appearance, OrdinalIgnoreCase) and logs `[pc] GetLevels order (... as listed / CHANGED)`. The `/`→`\` replace is not done (**TFEX-DIFF: S-fspath**, proposed ledger row: nn::fs paths take `/`); it can't change a pick: the strings only meet `levels[0] == lastLevel`, and lastLevel comes from the same list. |
| **UnlockData getters** false in an EX session (`TF.State.Patchs/UnlockData.cs`, new) | lockstep, **ported** | `rnd.cpp` next to UnlockGuard: trampolines on `get_ShouldOpenPurpleArcherPortal` +0xb99e40, `get_ShouldShowMoonBreakSequence` +0xb99450, `get_ShouldSelectSunkenCity` +0xb99c50, `get_ShouldSelectTowerForgeInVersus` +0xb99d40 (bodies from `classmeta.records("TowerFall.UnlockData")`, first words checked; each body read: Moonstone +0x10, SunkenCity +0x11, TowerForge +0x12, purple +0x21). Condition = `pcgameplay::On()` (TF.EX's IsExSession: versus settings in the netplay mode). Callers on the Switch: the level loader (+0xa82b58 purple portal, +0xa83074 moon break: **gameplay entities**, so a save with fewer unlocks added a portal/sequence on one side; UnlockAll covered Moonstone but never the purple portal) and VersusRandomSelect.OnConfirm (+0xbb5890/+0xbb58b0, the forced pick: the lobby path doesn't use it). |
| **Player DoWrapRender** moved from each Player.Update postfix to once at the end of Level.Update (`PlayerPatch.RefreshSprites`) | **nothing to do** (S-rollback) | Player.DoWrapRender (PC) only touches drawing state: wings sprite Y, aimer visibility, bodySprite.Scale.X·Facing, head sprite origin/animation, colors, hair. TF.EX calls it in update only so its TF.State snapshot holds sprite state. The Switch renders after every displayed update and restores sprites with the rest of memory. Evidence that it isn't gameplay: Switch-vs-browser 0.19.2 rollback matches (lobbypred 8, 100-250 rollbacks, STATUS.md) gave 0 differing ticks while the browser ran DoWrapRender inside update and the Switch skips render in resimulated frames. v0.21.0's new place (after all entities' updates) is closer to what the Switch does. |
| **TF.State snapshot fixes**: Lantern homeChain (state + new SceneBegin postfix that only stores a DynamicData field), Arrow `HasUnhittableEntity` false when CannotHit is a dead player, Player `lastCaught` depth only when the arrow is still in the level | **nothing to do** (S-rollback) | They make TF.EX's per-field restore equal to the un-rolled-back state (a dead player can't be hit either way; the chain and lastCaught pointers are restored as pointers by the memory copy). No gameplay change for a side that doesn't roll back, so none for the Switch. |
| **Lobby.SessionPlayers** = MatchPlayers when the server sends it | lockstep (seats → handles, archers, teams), **ported** | `Models`: `Lobby.MatchPlayers` read/written (JSON `MatchPlayers`, written as `[]`), `Lobby::SessionPlayers(&count)` falls back to Players when empty (today's tfex-server). `NetplayManager.cpp` SortedBySeat (handles, archers via ResolveArchers, teams, seat peers) reads SessionPlayers, as TF.EX's MapScene UpdatePlayers/UpdateNumPlayers, ArcherData.ResolveArchers, RestoreArchersFromLobbyIfNeeded and the Teams settings do. |
| (found) **NormaliseSeats** on Players and MatchPlayers | lockstep (missing since 0.19.2) | `MatchmakingService.cpp`: duplicate seats renumbered by list order, in HandleLobbyUpdate and UpdateOwnLobby, as TF.EX. |

### Other v0.19.2..v0.21.0 hunks checked, not lockstep for the Switch
- NetplayManager `ResetPolledInput()` before Connect (#273, a previous input kept from a manual quit): the stale input enters GGRS, so
  both sides see it; an input-correctness fix, not a desync. Follow-up: check whether the Switch can carry a pressed input into a new match.
- Session.EndRound/GotoNextRound skip while InstantReplayFootage bakes: TF.EX-local re-simulation, not ported (no instant replay).
- PlayerInput seat check uses SessionPlayers: TF.EX's XGamepad gate (S-input); our handle count already comes from SessionPlayers.
- RequestNetplayEntry Dark World DLC gate: always passes on the Switch (DarkWorldDLC constant true).
- NetplayRoundLogic/TFGame finalizers/NetplayManager desync mark: MatchReport (D). GGRSConfig relay, Lobby RelayedPairs /
  IsCheckingConnections / IsReportEnabled, KickedFromLobby, PeerMeasured: protocol (B), not this workstream.
- TF.State.Patchs ModIO.cs, InstantReplayViewer.cs, LocalSettingsGuard visibility: no gameplay change.

## 2. Server (tfex-server, `Towerfall Browser/cloudflare/tfex-server`) for MatchPlayers — not changed
Today it builds lobbies from named fields (`matchmaker.js` ~624: unknown client fields such as our `MatchPlayers: []` are dropped) and
never sends MatchPlayers, so every client uses Players (0.19.2 behaviour). To support it (TF.EX's own server is not public; inferred from
the client): in `start(room)` set `lobby.MatchPlayers = structuredClone(lobby.Players)` (the match's seats, archers, teams, peers);
keep it through `RematchLobby`; clear it (`[]`) on ArcherSelectLobby, SeriesLobby (or set per series game), when the match ends or the
lobby closes; include it in every lobby JSON (LobbyUpdate, GetLobbies with names hidden like Players); a player leaving mid-match leaves
MatchPlayers alone (Players changes). Then a spectator or a late joiner can't shift the running match's seats.

## 3. Tests

**Model** `docs/ws-tfex-lockstep/selection-model` (C#, real .NET `System.Random`; written from TF.EX's MapScene.cs / OwnGenLevel and the
PC's InitVersusButtons / GetTowerUnlocked / VersusTowerData / Calc.Shuffle; reads the PC level files for PlayerSpawns and fixedFirst):
`dotnet run -- "<browser gamefiles>/TowerFall.app/Contents/Resources/Content" "<browser gamefiles>/DarkWorldContent"`.
Random tower (MapId -1), v0.21.0 (any unlocks) / v0.19.2 by unlock set (f all, 0 none, 1 SunkenCity only, 6 Moonstone+TowerForge):

| seed | v0.21.0 | 0.19.2 f | 0.19.2 0 | 0.19.2 1 | 0.19.2 6 |
|---|---|---|---|---|---|
| 109144773 | 5 Thornwood | 5 | 5 | 8 | 10 |
| 109144726 | 8 Sunken City | 8 | 7 | 8 | 12 |
| 1288949485 | 8 Sunken City | 8 | 15 | 15 | 6 |
| 42 | 8 Sunken City | 8 | 6 | 15 | 9 |
| 7 | 10 TowerForge | 10 | 1 | 6 | 10 |
| 123456789 | 9 Moonstone | 9 | 5 | 13 | 13 |
| -5 | 7 King's Court | 7 | 7 | 12 | 2 |
| 2147483647 | 0 Sacred Ground | 0 | 13 | 3 | 0 |

So v0.21.0 = 0.19.2 with everything unlocked (what the browser harness, which unlocks all, plays): the Switch with all unlocked was
already right against the browser; a Switch save with fewer unlocks against v0.21.0 picked another tower before this branch.
Level order: "0.21.0 == 0.19.2 for every seed and tower". Seed 109144773 → Thornwood matches the earlier verified lobby run (STATUS.md).

**On the Switch**: `Tower::SelfTest` (once per boot, at the first lobby match start) runs the live selection for these 8 seeds under the
4 unlock sets against the model's v0.21.0 column and logs `[tfex] tower self-test ...: OK|WRONG`.

**Runs** (private Ryujinx slot `lockstep`, `tools/lobby-vs-browser.sh`, browser = TF.EX 0.19.2 which unlocks everything):
Switch-hosted lobby, MapId -1 (random), lobbypred 8 (rollback; resimulated frames seen in the log), build d3348db:

| seed | Switch log | ticks compared (Switch vs browser) |
|---|---|---|
| 109144773 | `tower self-test ...: OK; with nothing unlocked: 5 8 8 8 10 9 7 0`; `tower: map -1 -> random over 16 towers (seed 109144773): 5`; `GetLevels order (sorted by folder and name, as listed): 00 .. 09`; `levels(10): 03 09 07 06 01 04 05 08 02 00` (= model) | **0 differing of 2392** (the harness's stall watchdog ended it at Switch tick 2127: the Mac was loaded with other sessions' Chromes; not a desync) |
| 42 | self-test OK; `random over 16 towers (seed 42): 8` (Sunken City: 0.19.2 with fewer unlocks picked 6/9/15); `GetLevels order (..., as listed): 00 .. 10`; `levels(11): 10 05 00 02 03 06 08 04 09 01 07` (= model) | **0 differing of 1493** (vsstop 1800) |

Also in the log: the four UnlockData hooks installed (first words matched); `MapScene.NoRandom cleared (0 of 0 were set)`: at a
lobby start nothing was NoRandom (the array read as empty: the map screen was never opened in that boot; not proven that the
clear finds a filled array, the lobby flow doesn't read it anyway). Not run: a wide lobby, a team lobby, a server that sends
MatchPlayers (none does). The cross-check is against the browser's TF.EX 0.19.2, which unlocks every tower and has the unchanged level
order, so it plays what v0.21.0 plays for these lobbies. Harness note: a fresh slot's page profile shows TF.EX's "What's new" box, so
the default keys land on QUICK PLAY (runs 1-2 here); run with `MANUAL=1` and press `c` first (or `LVB_PREKEYS=c`).

## 4. Left / follow-ups
- Clear NoRandom at netplay entry too (MainMenuPatch.cpp, TF.EX's RequestNetplayEntry call site).
- tfex-server MatchPlayers (section 2), then a test with a joiner/spectator arriving during a match.
- Ledger row `S-fspath` for TFEX-BASE.md section 3 (the `/`→`\` replace not done).
- A real v0.21.0 opponent: the browser harness runs 0.19.2. It would need the v0.21.0 release zip in the browser repo
  (`vendor/tfex/DShad.TF.EX-v0.21.0.zip`) and a switch-rtc build of it; not done (outside this repo, ask the manager first). Since the
  browser unlocks all towers and its level order is unchanged, 0.19.2-in-the-browser plays the same tower and levels as v0.21.0 would.
- #273's polled-input reset: check on the Switch.

## 5. Phase 2: catching up to v0.21.0 (B, C, leftovers), submodule moved

Worklist: `tools/tfex/update.sh v0.19.2 v0.21.0` (report `tools/tfex/reports/v0.19.2-v0.21.0.md`), after `git merge master` (694d6c6).
`vendor/tfex` is now at **v0.21.0**. `tools/tfex/update.sh v0.21.0 v0.21.0` (`reports/v0.21.0-v0.21.0.md`): no stale entries; "still
behind" = only the six items in 5.4. Manifest: 27 new entries for this work + hashes at v0.21.0 (`manifest.py fill v0.21.0 <glob>` per
entry); overrides: 2 skips; `manifest.py check v0.21.0`: the one key matching nothing (`UpnpService.cs *`) predates this branch.

### 5.1 Done (`git log 694d6c6..ws/tfex-lockstep`)
| item | TF.EX | Switch |
|---|---|---|
| A: NoRandom clear at netplay entry | TFGame.cs RequestNetplayEntry | `pcgameplay::Set(true)` (entering online mode = TF.EX's ApplyNetplayMode) calls `Tower::ClearNoRandom` |
| A: #273 ResetPolledInput at session start | NetplayManager.Init | **n/a**: TF.EX's polled input keeps presses sticky between GGRS polls; the Switch reads `GetState()` fresh each time GGRS asks (xplay.cpp NpPadInput), nothing carries over |
| B: Lobby RelayedPairs / IsCheckingConnections / IsReportEnabled | Models/WebSocket/Lobby.cs | `tfex/Models.*` read + write (pairs of RoomPeerIds, 6 max) |
| B: KickedFromLobby (S→C) | MatchmakingService HandleMessageContents, HandleKickedFromLobby | parsed on the network thread; on the game thread: abandonedRoomId, ResetLobby, ResetPeer, "<REASON> - REMOVED FROM LOBBY", the lobby screen's closed path (TF.EX: NetplaySelect + ui_invalid) |
| B: abandonedRoomId | HandleLobbyUpdate / UpdateOwnLobby | updates of a lobby we were removed from are ignored |
| B: IsAwaitingHostStart waits while connections are checked; IsCheckingConnections | MatchmakingService | `IsWaitingForHostStart`, `IsCheckingConnections` |
| B: relayed peers | GetRelayedPeers(From), IsRelayedWith, GetRelayUrl | same names in MatchmakingService (IsRelayedWith: -1 null, 0, 1) |
| B: failed-connection messages | NetplayManager AbortToVersusOptions(info), FailedConnectionMessage, GetFailedConnectionMessage | ported; **no caller**: the Switch has no initial-connection timeout (a lobby match that never synchronizes just waits) |
| B: ApplyRelay | NetplayManager.Init | relayed peers + relay URL logged; the connection stays direct WebRTC (5.3) |
| C: ConnectionIcon | Patchs/ConnectionIcon.cs | `tfex/ConnectionIcon.*` (Draw.Rect +0x9237b0, Color * float) |
| C: RenderBannerWithDots, RenderCheckingConnections | MainMenu.cs | `tfex/MainMenuBanner.*` (readyBanner, OutlineTextureCentered +0x927050, OutlineTextJustify +0x925550, dots every 400 ms) |
| Notifications in my files | MatchmakingService, NetplayManager.GetEventAndUpdate | "CONNECTION UPDATED", "<reason> - REMOVED FROM LOBBY", "All players left..." and "Host left the game..." (in a level), "Synchronized!", "Trying to re sync" (no animation; cleared on resume/disconnect: layer 4), "DESYNC DETECTED - match ended". The GGRS ones through `NetplayManagerOnEvent`, one line in xplay.cpp's event loop. Not ending the match on desync/disconnect (S-start). Sounds not played. |

ConnectionIcon and the banners are ported but **not called**: their call sites are MainMenuPatch.cpp's Render (rollcall ping line +
icon, CHECKING CONNECTIONS / MATCH STARTING banners) and the in-game ping overlay (GameplayLayer.RenderPings), which the Switch doesn't
have (neither does it show pings: no ping measurement, 5.3). So they are compiled, not seen on screen.

### 5.2 Not done, in my files (needs ping measurement)
PeerMeasured (C→S), ReportMeasuredPeers, SendPeerMeasured, RefreshPingStats / StartOrStopPingMeasurementIfNeeded / SwitchPingMeasurement
with `isRelay`, measuredPingStatsRelayed, relayPingMeasurementUrl: TF.EX's ping measurement is ggrs_ffi's own WebRTC session on
`<server>/ping_measurement/<room>?peer=` (and `/relay/ping_measurement/<room>` over the relay); ggrs-switch has no ping measurement at
all (the 0.19.2 port had none either: no pings in the rollcall). PeerMeasured is sent once a peer has a measured RTT, so it needs the
measurement first. InputDelayAdvisor (UpdateProposal, RelayRollbackFactor) and GameplayLayer's ping overlay (fade) depend on it too and
were never ported. Left as report items (no skip: they are work).

### 5.3 The relay: TF.EX 0.20.0's vs ours (S-transport)
TF.EX 0.20.0: the server measures each pair (ping measurement + PeerMeasured), publishes `RelayedPairs` and `IsCheckingConnections` while
it decides, and the clients connect **only those pairs** through `<server>/relay/room/<room>?peer=<own>` (GGRSConfig RelayUrl /
RelayPeers into ggrs_ffi; the framing is in ggrs_ffi's Rust, **not in vendor/tfex**); every other pair stays direct WebRTC.
Ours: lobby matches are direct WebRTC only (net.cpp RunRtc, tfx-rtc); the old whole-match WebSocket relay (`/relay/<room>?peer=`, net.cpp
`np::Run`) is used by the lobby-free xnet path and tests, not by lobby matches. So adopting TF.EX's scheme would not drop code we use in
lobbies; it would **add** the fallback lobbies don't have, and once it exists the old xnet relay could go (it would be the same server
route with a per-pair header). It needs: (1) ggrs_ffi's relay framing (its source, or a capture from the browser/PC); (2) ping
measurement in ggrs-switch (5.2), since the server decides on measurements; (3) the server routes below. Then the Switch would be a
normal TF.EX relay peer, and S-transport shrinks to "tfx-rtc instead of ggrs_ffi's WebRTC".

### 5.4 Still behind after the move (not my files)
- `MainMenuPatch.MainMenu_Render` (MainMenuPatch.cpp): call `ConnectionIcon::Render` beside the rollcall ping (needs pings) and
  `MainMenuBanner::RenderCheckingConnections` / `RenderBannerWithDots("MATCH STARTING")`.
- `TFGamePatch.TFGameUpdate_Patch` (xplay.cpp): show `NetplayManagerGetFailedConnectionMessage()`, and an initial-connection timeout
  that calls `NetplayManagerAbortToVersusOptions(info)`.
- `TFGamePatch.RequestNetplayEntry` / `ResolveNetplayEntry` (MainMenuPatch.cpp): the Dark World DLC gate (always passes on the Switch:
  DarkWorldDLC is constant true) and NoRandom (done in pcgameplay.cpp); the entries only need their hash refreshed by the file's owner.
- `MatchmakingService.RestoreArchersFromLobbyIfNeeded` (MainMenuPatch.cpp JoinRemote, ResolveArcher): read `SessionPlayers`.
- `GGRSConfig NetplayServerConfig.*` (RelayUrl, RelayPeers): the relay transport, 5.3.
- Also found, A and pre-existing since 0.19.2: `PlayerPatch.Player_Update_Postfix` kills a player whose input is `disconnected`
  (DeathCause.Curse) during capture. The Switch doesn't (a 2-player lobby match ends on the disconnect instead), so a 3-4 player match
  with one player leaving would differ. xplay.cpp (a Player.Update postfix), not done.

### 5.5 tfex-server needs (not changed; nothing deployed)
On top of section 2 (MatchPlayers): `KickedFromLobby {reason}` when the host kicks / a spectator lacks variants; accept `PeerMeasured
{RoomPeerId}`; while all players are ready, set `IsCheckingConnections` until every pair is measured, then publish `RelayedPairs` for the
pairs that failed and clear it; serve `/relay/room/<room>?peer=` (per-pair forwarding in ggrs_ffi's framing) and
`/relay/ping_measurement/<room>?peer=`; send `IsReportEnabled` (false unless reports are wanted) and RelayedPairs/IsCheckingConnections
in every lobby JSON. The Switch tolerates all of them missing (today's server).

### 5.6 Time
Phase 2, by the session's clock: 21:47 (merge, report) → 22:04 (notes). Roughly: reading the report and TF.EX's diffs and deciding
where each of ~90 A/B/C members lands, or that it can't yet (ping measurement, relay framing, callers in files owned elsewhere): about
half; code (models, message handling, event notifications, ConnectionIcon, banners): about a quarter; manifest entries, fills, the
v0.21.0 re-check: about a tenth; the test run, a crash from my own unresolved symbol and the rerun: the rest. The report made "what
changed" instant; "where it goes on the Switch" and "is it possible here" were the cost, and the parts that need new transport code
(5.2, 5.3) aren't in this time at all. (Phase 1, the A items, took the earlier part of the day, mostly RE and the selection model.)

### 5.7 Test (private slot `lockstep`, build after 2d40095 + the fix)
Switch-hosted lobby vs the browser (TF.EX 0.19.2), seed 42, random map, lobbypred 8, `notif 1`: **0 differing ticks of 1798**, to vsstop.
The Switch's log: `MapScene.NoRandom cleared` on entering online mode and at the match start; at the first GGRS event the
Notification class was made and "Synchronized!" created ("[notif] class ... made", "[np] event 2"), with the match in sync after it.
The first try of this build crashed at that same moment: the xplay.cpp one-liner had declared `xplay::tfex_NetplayManagerOnEvent`
(the module loads with unresolved symbols: `[rtld] Unresolved symbol`, guest jump to 0, InstructionAbortLowerEl); fixed by defining
the wrapper in namespace xplay; the ELF has no unresolved symbol of ours now (the `__cxa_guard_*` ones are swkbd.o's, older).
Not exercised (today's server never sends them): KickedFromLobby, RelayedPairs/CONNECTION UPDATED, IsCheckingConnections, MatchPlayers.

## 6. Models/Json fixes from the translator's stage-1 test (2026-10-16)
Found by `tools/tfex/translator/test/models-test.sh` (ws/tfex-update; oracle = TF.EX's model classes + MessagePack), fixed in
`tfex/Models.*`, `tfex/Json.*`, `tfex/MatchmakingService.cpp`:
1. `GameData.MapId` absent read 0 (tower 0 instead of random); now -1, also the struct default (TF.EX's).
2. `Lobby.Series` was dropped; now kept as data: its JSON text as received (`Lobby::Series`, up to 1023 bytes; `Json::Node.src/srcLen`
   gives any object's source span) and written back unchanged. Series lobbies are still not ported (nothing reads it).
3. Strings were written with `\u000a` for a newline; now `\" \\ \b \f \n \r \t`, other control characters `\u00xx`, as MessagePack's
   ConvertToJson.
4. Member-less messages: TF.EX writes `{"KeepAlive":[]}` (MessagePack writes a class with no members as an empty array). Reading:
   HandleMessageContents only looks at the key, so `[]` and `{}` both work. Writing: the 7 we send (KeepAlive, ExitQuickPlay,
   LeaveLobby, GetLobbies, StartLobbyChoice, RematchLobbyChoice, ArcherSelectChoice) now send `[]`; tfex-server takes either
   (`typeof body === "object"`). `tfex/LocalGuest.cpp` (not this workstream's file) still sends `{"KeepAlive":{}}` and `{"LeaveLobby":{}}`.

Host check: `docs/ws-tfex-lockstep/models-check.cpp` (clang++ with Json.cpp + Models.cpp): MapId absent → -1, Series round-trips
verbatim, `\n`/`\t` written as TF.EX does. Field order of Lobby, Player and GameData = the generated `tfexgen_Models` writers'.

Still different from `tfex/gen/tfexgen_Models.*` (which should replace Models.* eventually): fixed-size storage that truncates (names 40,
ids 72, lists 16, 4 players, 8 spectators, 8 mods with 4 data entries, Series 1023 bytes) where TF.EX has none; non-ASCII `\uXXXX`
read as `?`; nulls not kept (a null string reads as "" and is written as ""; TF.EX writes null for the null ones, e.g.
AbortReason); an empty `Kind` is written as "Standard"; `Player.InputDelay` null when absent (TF.EX's is int?: same); only the
Lobby/Player/GameData/CustomMod/EndGameChoice models, the rest of Models/WebSocket is written inline with snprintf in
MatchmakingService.cpp; Series is opaque text, not the model.

Lobby run with these fixes (slot `lockstep`, seed 42, lobbypred 8): the server took the `[]` messages (the Switch host's
`StartLobbyChoice:[]` started the match, KeepAlive answered), **0 differing ticks of 1797**. (One earlier try stopped because the
browser page never booted; the Switch had created the lobby normally.)

## 7. Phase 3: ping measurement, rollcall pings, the disconnected-player kill (2026-10-16, 22:36 → 23:25)

After `git merge ws/tfex-base` (f74a1e7, translator stages 1-4).

### 7.1 Ping measurement (TF.EX's ggrs_ffi, direct) — done
Source: Fcornaire/ggrs-ffi (public; the browser repo's copy `vendor/netplay-src/ggrs-ffi` is cf60182, upstream main 2fe64d9 adds the
relay variant). Protocol: a second matchbox WebRTC socket (unreliable channel 0) on `<server>/ping_measurement/<room>?peer=<own>`;
every 500 ms PING `[0, stamp u64 LE]` to each connected peer, answered PONG `[1, stamp]`; per peer the last 24 outcomes (RTT, or lost
after 1.5 s); stats = median, p90 − median ("spike"), loss %.
- `ggrs-switch/src/ping_measurement.rs`: TF.EX's file without its thread (PeerSamples, handle_packet, ping round, stats, same constants)
  + C API `tfx_pm_*`; `cargo test` (ping_pong_and_stats) passes. **The module's `rustlib/libggrs_spike.a` is gitignored: whoever
  merges must run `ggrs-switch/build.sh`** (from the main checkout; in a worktree the crate's `../../../Towerfall Browser` path doesn't
  resolve, I built from a mirror with that layout).
- `net.cpp` `namespace pm`: its own thread (256 KB stack), UDP socket, tfx-rtc mesh and signaling WebSocket, 2 ms poll as TF.EX's;
  `ping_measurement_start/stop/stats` (GGRSFFI's names; direct only).
- `MatchmakingService`: GetPingTo, GetConnectionQualityTo, RefreshPingStats, StartOrStopPingMeasurementIfNeeded (every frame from
  DrainGameThreadActions, in the MainMenu scene = TFGame.cs:168), SwitchPingMeasurement, ReportMeasuredPeers, SendPeerMeasured
  (`{"PeerMeasured":{"room_peer_id":…}}`), measuredPingStats(+Relayed), reportedMeasuredPeers; `ConnectionQualities` (GetQuality,
  GetQualityByLatency, ToColor) from Models/ConnectionQuality.cs.
- Not done: the relay variant (`isRelay`, `/relay/ping_measurement`): ggrs_ffi's relay.rs transport (below).
- Tested (slot `lockstep`, Switch-hosted lobby vs the browser's TF.EX 0.19.2, which measures through its own ggrs_ffi): the two
  measurement peers connect (ICE), the Switch gets an RTT and sends PeerMeasured once (`[tfex] PeerMeasured <peer> (rtt 304 ms)`; the
  RTT is high because Ryujinx's network thread was on a Mac at load 9-26), then the match: **0 differing ticks of 1197 and of 1791**.
  One run on this build crashed (guest data abort right after the first rollback, frame periods 400-700 ms at load 26, another
  session's Ryujinx crashed a minute before); not reproduced in the next three runs; listed in case it is real.
- Found on the way, my own bug from phase 2: `game::SceneIs` takes the short type name ("MainMenu", "Level"), so measurement never
  started and the phase-2 "All players left..." / "Host left the game..." checks never matched; fixed.

### 7.2 Pings on screen — API ready, call site with tfex-subclass
`tfex/RollcallPings.*`: `RenderRollcallPing(rollcallElement, seat, wide)` = TF.EX MainMenu_Render's body per RollcallElement ("<ms> MS" /
"... MS" in the quality color, PingAt, connection icon) and `RenderCheckingConnections()`, both on the **generated**
`tfex/gen` ConnectionIcon / RenderBannerWithDots. My hand copies `tfex/ConnectionIcon.*`, `tfex/MainMenuBanner.*` are deleted.
They draw immediate-mode, so they need a Render pass: proposed to tfex-subclass (local_39d426e2) a MainMenu.Render postfix in
MainMenuPatch.cpp calling them per RollcallElement in the lobby state. Not on screen until that lands (MainMenu_Render stays in
"still behind"). InputDelayAdvisor and GameplayLayer.RenderPings: asked tfex-update whether the translator can generate them; not
hand-ported.

### 7.3 The disconnected-player kill — done
`tfex/PlayerPatch.cpp`: Player.Update (+0xaef280) postfix, TF.EX's Player_Update_Postfix: capture on, the player's current input
`disconnected` → `Die(DeathCause.Curse = 9, -1)`. The input comes from xplay's `g_npIn` (the frame being simulated, rollback included),
handed over by one install line in xplay.cpp. ggrs-switch now turns a Disconnected player's input into ggrs_ffi's
`Input::disconnected()` (zeros + disconnected 1) in both advance paths, as TF.EX does (before: GGRS's last input).
A second xplay.cpp line: a disconnect before the results ended **every** lobby match (`leftMidMatch`); now only with 2 players or in
Team Deathmatch (TF.EX: an FFA of 3-4 goes on; a team match ends "A PLAYER LEFT", IsUnfinishedTeamMatch).
Test: 3-player lobby (Switch + 2 pages), page LB navigated away 20 s in: `[np] event 3`, `[tfex] PlayerPatch: player 2 disconnected:
Die(Curse, -1)`. Not cross-checked: the remaining page LA (TF.EX 0.19.2) stopped advancing after LB left (its own interruption
handling at this load), so there were no browser ticks to compare.

### 7.4 Initial-connection timeout — not done (coordinator)
TF.EX (TFGame.Update patch + NetplayManager.EstablishSessionIfSynchronized): not synchronized within Constants.CONNECTION_TIMEOUT_MS
(30 s since 0.19.2) → AbortToVersusOptions(info) → the session is reset and the menu shows `GetFailedConnectionMessage()`. On the
Switch it needs, in xplay.cpp's NetplayGate: a timer from StartLobbyMatch, then EndLobbyMatch + back to the lobby screen (as
OnRemoteLeftMidMatch does) + `NetplayManagerAbortToVersusOptions(info)` (ported, tfex/NetplayManager.cpp, it creates the
notification). More than two lines in a file the coordinator is editing: left for them.

### 7.5 Server
`docs/ws-tfex-lockstep/tfex-server-v0.21.patch` (not applied; `git apply --check` passes on the browser repo at 6028684; `node --check`
passes; not run): MatchPlayers, RelayedPairs / IsCheckingConnections / IsReportEnabled, PeerMeasured, the connection check before
StartLobby. `/ping_measurement` is already served (signal.js) and works with the Switch. The relay routes are not in it: TF.EX's relay
(relay.rs) is a WebSocket control channel (Welcome {token, udp_port}, UdpConfirmed, PeerConnected {peer, id}, PeerDisconnected,
SpectatorsFull) plus **UDP** to the server (HELLO 1 / GAME 2 packets with an 8-byte token, pieces ≤ 1200 bytes, reassembly), and a
Cloudflare Worker can't take UDP: it needs another host. So the patch keeps RelayedPairs empty (`RELAY_AVAILABLE = false`).

### 7.6 Still behind (`update.sh v0.21.0 v0.21.0`)
MainMenu_Render (ping line call site), RequestNetplayEntry / ResolveNetplayEntry / RestoreArchersFromLobbyIfNeeded (MainMenuPatch.cpp,
tfex-subclass), TFGameUpdate_Patch (7.4, coordinator), GGRSConfig NetplayServerConfig.* (the relay). Coverage at v0.21.0: 1182 ported
(+27), 596 replaced, 628 skipped, 1081 unaccounted.

### 7.7 Rollcall pings on screen (with tfex-subclass's call site)
tfex-subclass wired the call site on ws/tfex-subclass (14b6f0b): a RollcallElement.Render postfix (+0xb351a0) in MainMenuPatch.cpp
calls `RollcallPings::RenderRollcallPing` per element and `RenderCheckingConnections` once per frame (compiled only when
RollcallPings.hpp exists, i.e. with this branch). Tested on a throwaway merge of ws/tfex-lockstep + ws/tfex-subclass (deleted after),
slot `lockstep`, Switch hosting through the menus (hw.txt `lobbyhost 0`, `lobbyui 1`), the page held in the roll call: the Switch shows
TF.EX's ping line above the page's seat, "306 MS" in red (Bad: the emulated RTT) with the generated direct-connection icon
(`docs/ws-tfex-lockstep/rollcall-pings.png`); then the match: **0 differing ticks of 1199** (and 0 of 1198 for a lobbyhost run of
the same build). MainMenu_Render's manifest entry now points at that hook; it leaves "still behind" once both branches are merged
and its hash is filled (`manifest.py fill v0.21.0 "*MainMenu_Render"`).

### 7.8 InputDelayAdvisor and the in-game ping overlay (answer from tfex-update)
- InputDelayAdvisor: not generated soon. It is built on PC input (mouse over the window, MenuAlt2Check / Alt2Icon, keyboard gestures);
  the Switch first needs a design decision (which button proposes a delay). Not hand-ported either.
- GameplayLayerPatch.RenderPings / UpdatePingAlpha / DrawPingLine: tfex-update will generate them after its coverage work, mapping
  IMatchmakingService's ping calls to `MatchmakingService::GetPingTo` / `GetConnectionQualityTo` / `IsRelayedWith` (int-coded
  nullables: -1 null) and `ConnectionQualities::ToColor`, as weak references until this branch is merged. Not hand-ported here.

### 7.9 Retest of the disconnect rules on ws/tfex-base 9c659af (2026-10-17)
After `git merge ws/tfex-base` (cad79ee): the coordinator's end rule (16448d3) merged with mine as
`ev.kind == 3 && g_lobbyStart && g_resultsFrame < 0 && (++g_remotesGone >= g_npN - 1 || g_lobbyMode == 5)`. Rust lib and module
rebuilt, slot `lockstep`, Mac at load average 57-86 (other sessions' Ryujinx instances running).
- **3-player FFA, one page leaves** (Switch + pages LA, LB; LB navigated away 20 s after sync), twice. In both runs: the Switch's data
  channel to LB closes, GGRS Disconnected, `[tfex] PlayerPatch: player 2 disconnected: Die(Curse, -1)`, and the match **goes on** (to
  frame 233 in the first run, 412 in the second). Second run with the desync check on both sides (hw.txt `npdigest 1`, page
  `digest=1`, browser `[digest] on`): **no GGRS desync event** from the leave to the end, so the Switch and LA stayed in sync. In both runs
  LA then closed its own connection (`[xlobby] GGRS session reset (relay socket closed)`, the browser harness's xlobby, not TF.EX);
  the Switch saw seat 1 leave, was the last one, and ended the match (`the remote disconnected at frame 412 ... ends here`): the rule
  as written. The per-tick comparison has no browser ticks (xlobby writes them only when its match ends normally), hence the digest.
- **2 players, the page leaves** 20 s after sync: the match ends at once (`the remote disconnected at frame 710 ... ends here`), `[lobby]
  the opponent disconnected before the match ended: leaving the lobby`, and the Switch is back on the netplay menu (`[screens]
  NetplaySelect`, `docs/ws-tfex-lockstep/2p-leave-netplay-menu.png`).
Not run: Team Deathmatch with a leaver (first disconnect ends it), 4 players.

### 7.10 The wide-lobby desync on 9c659af (reported 2026-10-17 00:22 from ws-archer3-local): not reproduced
Same command in my slot on this branch (= 9c659af + docs; `git diff 9c659af HEAD -- hw-probe ggrs-switch` empty), vsstop 600:
- seed 1004, MapId 0, wide: **0 differing ticks of 599**. Switch `wide tower: map 0 -> random over 16 towers (seed 1004): 0 (6P tower 0)`
  (no wide button has ID.X == 0: RANDOM, as TF.EX), page `LevelSystem.ID={X:16 Y:0}` (Sacred Ground, wide). Level order: `GetLevels order
  (sorted by folder and name, as listed): 00 .. 09 a00` (every WiderSet tower's list is already sorted, so the v0.21.0 sort changes nothing).
- seed 7, MapId -1, wide: 0 differing ticks of 599 (one earlier try had no browser ticks: the page hadn't flushed its log when the
  short run stopped). Switch 6P tower 10 = ID 26 (TowerForge), page ID 26.
- Model: `WideTower` in the C# selection model (MapId 0..15 → RANDOM over IDs 16..31; MapId 16..31 → that tower), 9 seeds; the Switch's
  `Tower::SelfTest` now checks SelectWide for those 9 seeds × MapId -1, 0, 17 against it: `wide tower self-test ...: OK`.
- Checked, same on both setups: the wide level files (main `.cache/widerset-0.9.0`, the shared Ryujinx romfs and my slot's PCWide: 194
  files, same hash), the save's unlocks (shared and slot saves differ only in play statistics), the page's WiderSet (mine: 0.9.0).
Not checkable: the reporting run's browser log (overwritten by a later run in that checkout's out dir). If it happens again: keep
`lvb-browser.txt` (WiderSet version line, `LevelSystem.ID`, `VersusTower.GetLevels(order)`) and the Switch's `wide tower:` / `GetLevels
order` / `[vs] levels(` lines; the page's own WiderSet copy is cached per Chrome profile (that checkout's page player is LA40, mine LA65).

### 7.11 More disconnect cases and the 3-player ping line (2026-10-17, slot `lockstep`, branch at aa84eac)
- **Ping line, 3 remote players** (Switch hosting through the menus, `lobbyui 1`, `lobbymax 4`, three pages in the roll call): every
  remote seat shows its ping and the direct-connection icon (no relayed pairs: direct is right), `docs/ws-tfex-lockstep/rollcall-3pings.png`;
  PeerMeasured sent for all three (first samples 257 / 304 / 363 ms). The three lines then read the same 358 MS: one PING round
  goes to every peer with one stamp and, under emulation, their PONGs are handled in the same wake-up of the measurement thread, so the
  medians coincide. Expected to differ on hardware (to check there).
- **4-player FFA, leavers one by one** (`npdigest 1`, page `digest=1`): LB leaves → `PlayerPatch: player 2 disconnected: Die(Curse,
  -1)`, the match goes on. Then page LA reset its own session again (`[xlobby] GGRS session reset (relay socket closed)`, ~17 s after
  the first disconnect, as in 7.9: the browser harness's behaviour after a peer leaves), and LC was made to leave: seats 1 and 3 went
  together, the Switch was the last one and ended the match (`ends here`). So "the match ends when one remains" holds; the stretch with
  exactly two players left after two leaves could not be held long enough to watch (the harness page drops out first).
- **Team Deathmatch, 3 players** (`lobbymode 5`, Switch Blue `lobbyteam 0`, both pages Red): LB leaves → `the remote disconnected at
  frame 89 ... ends here`, `[lobby] the opponent disconnected before the match ended: leaving the lobby`, back on the netplay menu
  (`[screens] NetplaySelect`): ends at once, as TF.EX's IsUnfinishedTeamMatch. TFEX-DIFF: TF.EX also shows "A PLAYER LEFT"
  (EndMatch's notification); the Switch's end path doesn't create it.

### 7.12 Not tested here (for the hardware checklist)
- Ping measurement on a real network: real RTTs per peer, different values per seat, quality colours other than red (Good/Ok need
  RTT ≤ 2 / 4 frames at 60 Hz one way, i.e. ≤ ~67 / ~133 ms RTT), STUN-reflexive candidates (here all on 127.0.0.1), the console's
  socket budget with a second WebRTC session in the lobby.
- The relayed icon (no server sends RelayedPairs), KickedFromLobby, CONNECTION UPDATED, IsCheckingConnections / the CHECKING
  CONNECTIONS banner, MatchPlayers: need tfex-server support (docs/ws-tfex-lockstep/tfex-server-v0.21.patch, not applied).
- A 4-player FFA in which two players keep playing after two leave (the browser harness's page resets itself after a peer leaves),
  and sync after a leave checked tick by tick (only the GGRS digest was available).
- A leave in Head Hunters (mode 4) and in a wide lobby.
- The disconnected-player kill against a real TF.EX v0.21.0 (the harness runs 0.19.2; same rule since 0.19.2).
- The initial-connection timeout (not implemented: 7.4).
- Ping line in a wide lobby (WIDE_PING offset (-16, -40)) and for a spectator (sees the host's ping).

## 8. Console findings (2026-10-17)

### 8.1 Screen.Offset at level load — done
TF.State's LevelLoaderXMLStatePatch.LevelLoaderXML_ctor postfix → Reset(): `TFGame.Instance.Screen.Offset = Vector2.Zero` while capture
is on. Ported in rnd.cpp (LevelLoaderCtorHook on LevelLoaderXML(Session) +0xa81e50): Offset (+0x38) **and OffsetAdd** (+0x40) zeroed;
TFEX-DIFF S-rollback for OffsetAdd (the screen shake: TF.State restores it from its snapshot; a level ending mid-shake would otherwise keep
the last shake offset). Offset is draw-only (the manager checked the PC source), so this fixes the console's shifted round-5 picture, not
a desync. Rollback coverage: the Screen is a managed object in Boehm's heap; checked at run time with rb::Known: `[rnd] Screen
0x20a12be420: Offset/OffsetAdd on a page the rollback restores (page class 0)` — a rolled-back space-orb scroll rolls back with the
rest, nothing to add. Test: shared Ryujinx, lobby seed 42, rollback 8, vsstop 600: 0 differing ticks of 599 (and 593, 1195 before the
log change). The log line at the first level load shows "(no rollback)": the undo log starts after the first load.

### 8.2 OrbPickup settings guard — done
TF.State OrbPickupPatch: LocalSettingsGuard around OrbPickup.OnPlayerCollide (the space orb's DoSpaceOrbDelayed reads
Options.RemoveScrollEffects). rnd.cpp OrbPickupCollideHook (+0xadcf00) wraps the original in UnlockGuard (TF.EX's Neutralize/Restore:
RemoveScrollEffects false, SunkenCity/TowerForge/Ascension true). Installed (first word checked); no orb pickup in the test's 600 ticks.

### 8.3 WhiteArcherPrison in netplay — done
TF.EX WhiteArcherPrisonPatch (TF.EX.Patchs/Entity/WhiteArcherPrison.cs): in a netplay/test/replay session Spawn returns
`new WhiteArcherPrisonBroken(xml.Position())`. Vanilla Spawn returns exactly that when Unlocks.WhiteArcherRevealed, so rnd.cpp
WhiteArcherSpawnHook (+0xbcd2b0) makes the flag (Unlocks +0x14) read true around the original in online mode. Only Moonstone 05
(and WiderSet's Moonstone 12) has a prison. Test: lobby map 9, seed 3 (first level Moonstone 05, from the selection model's Random):
`[rnd] WhiteArcherPrison.Spawn in netplay: WhiteArcherPrisonBroken (save's WhiteArcherRevealed 1)`, 0 differing ticks of 599 (this
save already has it revealed; a save without it would have spawned the intact prison and desynced).

### 8.4 Variant-gated TF.State patches
Which a Switch lobby can turn on: a Switch-hosted lobby none (our LobbyBuilder has no variants page); a lobby the Switch joins gets every
listed variant applied (VariantsExtensions::ApplyVariants), and TF.EX's LobbyBuilder offers all vanilla variants, so every one is
reachable by joining. Test recipe for a variant (page hosts, Switch joins): `MANUAL=1 LVB_SYNCMAX=300 tools/lobby-vs-browser.sh -s 700
... "lobbyhost 0" "lobbyui 1" "lobbyjoin 1" ...`; on the page: c, ArrowRight c (LOBBIES), Enter (LobbyBuilder), ArrowDown ×5 + c
(VARIANTS), move to the variant (sections are 7 wide, in MatchVariants' field order: ARCHERS #17 = ClumsyArchers = row 3, column 3),
c to toggle, Enter to create; once the Switch joined: c (ready), c (start).
- **ClumsyArchers (Player.DropArrow, A21) — done:** rnd.cpp DropArrowHook (+0xaf2540) registers the gameplay RNG around it (TF.State
  PlayerStatePatch). Test: page-hosted lobby with Clumsy Archers, Switch joined (`ApplyVariants: 93 variants off, 1 of 1 listed on`),
  rollback 8: 0 differing ticks of 599.
- **OffsetWorld (OrbLogic.DoOffsetWorldVariant, A17) — done; AlwaysScrolling (OrbLogic.StartScroll(), A19) — ported, not tested:**
  rnd.cpp OffsetWorldHook (+0xadb830) and StartScrollHook (+0xadbc20) register the gameplay RNG around them (TF.State OrbLogicPatch;
  its DoOffsetWorldVariant body rewrite only rebuilds the Screen.Offset tween for its snapshot: same draw, draw-only offset). Test:
  page-hosted lobby with Offset World on (ApplyVariants 1 of 1), Switch joined: 0 differing ticks of 599. Always Scrolling didn't get
  toggled in that run (ARENA column 5), so StartScroll is untested.

## Next steps (item 4: variant-gated TF.State patches; the user picks (a) or (b))

Reachable only when the Switch **joins** a TF.EX-hosted lobby with the variant on (Switch-hosted lobbies have no variants). Already on
this branch: Clumsy Archers (DropArrow RNG, ab9edd7, tested 0/599), Offset World (DoOffsetWorldVariant RNG, 196a921, tested 0/599),
Always Scrolling (StartScroll() RNG, 196a921, ported, not tested). Remaining, with sizes:
| variant | TF.State source | work | size |
|---|---|---|---|
| Always Scrolling | OrbLogic.cs StartScroll | test only (toggle ARENA column 5) | 1 run |
| Bottomless chests | Entity/LevelEntity/TreasureChest.cs (~220 lines) | coroutine → counters, tick-exact | ½-1 day |
| Return As Ghosts | Entity/LevelEntity/PlayerCorpse.cs ghost counter + PlayerGhost.cs (~250) | corpse counter + ghost entity patches | 1 day |
| Dark Portals | DarkPortalsVariantSequence.cs (~120) + the Bat / Slime enemy patches | 4-phase counter (coroutine deleted), shuffle and Choose on the gameplay stream (vanilla: unseeded `new Random()`), plus 2 enemy ports | 1 day+ |

How to test each (one short run, vsstop 600, shared Ryujinx): the page hosts, the Switch joins: `MANUAL=1 LVB_SYNCMAX=300
tools/lobby-vs-browser.sh -s 700 "rngcapture 1" "lobbyhost 0" "lobbyui 1" "lobbyjoin 1" "npdelay 2" "lobbypred 8" "vsstop 600"`; on the
page: c, ArrowRight c, Enter, ArrowDown ×5 + c (VARIANTS), cursor to the variant (sections 7 wide, MatchVariants field order; ARENA:
AlwaysScrolling col 5, OffsetWorld col 6, DarkPortals col 7; ARCHERS #10 ReturnAsGhosts = row 2 col 3), c, Enter; once the Switch
joined: c, c. Check the Switch log `ApplyVariants: ... N of N listed on`, then 0 differing ticks. A variant whose effect needs time
(ghosts after a death, portals after the round start) may need a seed/level where it happens within 600 ticks.

Options:
- **(a)** port them in that order (Always Scrolling test, bottomless chests, ghosts, Dark Portals with Bat/Slime), one commit and one
  short test each.
- **(b)** first a stopgap: the Switch refuses (or warns about) joining a lobby whose GameData.Variants include an unported one, then (a).
  Gate: `VariantsExtensions::HasAll` in tfex/NetplayManager.cpp (today it refuses only titles the Switch doesn't know: TF.EX's
  "MISSING CUSTOM VARIANTS"); add the unported titles to its refusal. The refusal text shown to the player needs a call site in
  MainMenuPatch.cpp / NetplayScreens.cpp (tfex-subclass's files).
