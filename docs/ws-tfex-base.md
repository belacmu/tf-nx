# ws/tfex-base: ready for master (2026-10-16)

## Status (2026-10-17 01:35, wrap-up)

**Done and in master (0513364, on the console):** TF.EX upstream as `vendor/tfex` at **v0.21.0**; the port caught up to v0.21.0
(random-tower pool, UnlockData getters, level order, lobby protocol, MatchPlayers, KickedFromLobby, ping measurement + rollcall
ping line, FFA continues after a disconnect); TF.EX's Notification as a real game subclass (generated from TF.EX's C#), toasts
rollback-safe; TF.EX's generated VersusSelectBanner / LobbyPanel / LobbyInfos in the menus; the update report
(`tools/tfex/update.sh`), the C#->C++ translator (`tools/tfex/translate.sh`, 60 % of TF.EX.Patchs + CustomComponent, Harmony
hook generator); TFEX-BASE.md (ledger of intentional differences, design).
**Beyond master on ws/tfex-base (to merge):** the Screen.Offset / OffsetAdd zeroing at each level load in a lobby match (console:
a round drawn shifted; `rnd.cpp`, tested 0/599; the Screen is inside the rolled-back heap, so no restore is needed), plus docs and
tool notes from the three workstreams. Builds; no run after the final merge (runs were stopped for the wrap-up).
**In progress (sub-branches, WIP commits):** `ws/tfex-lockstep`: OrbPickup settings guard, WhiteArcherPrison in netplay,
variant-gated TF.State patches (next: port and test each with one short run). `ws/tfex-update`: RenderPings (in-game ping
overlay) generation; coverage 60 % → 70 % (blockers listed in docs/ws-tfex-update.md). `ws/tfex-subclass`: the first-tick
mismatch investigation (next: dump rnd's draw ring at the first tick / on a stall, rnggametrace).
**Known issues:**
- `notif 1` desync: **fixed** (class made on the main menu, 537efcf); passes 2p / 3p FFA / wide / rematch in Ryujinx; not yet
  on hardware.
- First-tick mismatches seen only from one checkout under heavy load (notif, wide, genpatch runs); none reproduced on a later
  build; cause unproven (suspect: gameplay draws during the menu->Level transition while capture is on). The coordinator's
  RANDOM-host alarm was a stale tick log, not a desync.
- Wide lobby: passes (0/595, 0/596); open on hardware.
- The four TF.State items: Screen.Offset done (above); OrbPickup guard, WhiteArcherPrison, variant-gated patches in progress.
- Not testable with the harness: two players continuing after two leave (the page resets its session); the tfex-server side of
  relayed pairs / CHECKING CONNECTIONS / KickedFromLobby / MatchPlayers (server patch not applied).
**hw.txt switches (new, defaults):** `notif 1` TF.EX's Notification toasts (default **off**); `genpatch <Class>` a generated TF.EX
patch class as hooks (off); `gendemo 1`, `gennotiftest <frame>`, `genpanel 1`, `genbanner 1`, `gennetplayat <n>`, `gentrace 1` translator demos and traces (off);
`notiftest <frame> <scene>` toast test (off). On by default with no switch: ping measurement, the rollcall ping line, the generated
banner / lobby browser panels, the FFA disconnect rule, the Screen.Offset reset (after the next merge).

The branch that makes the mod "TF.EX, modified to run on the Switch" (TFEX-BASE.md). Master is merged in (up to `140ea2d`, the
lobby QUIT crash fix), so it merges into master cleanly (27 commits ahead; the `coordinator` line is already in master).

## What it changes in the Switch build

| change | where | on by default | tested |
|---|---|---|---|
| TF.EX v0.21.0 random-tower pool: the four unlockable towers count as unlocked while the pool is built; **the player's save is no longer changed** (the 0.19.2 port's `UnlockAll()` wrote the flags for good) | `tfex/NetplayManager.cpp` Tower | yes | Ryujinx: tower self-test (8 seeds x 4 unlock sets = TF.EX v0.21.0's C# model) on every boot; lobby matches 0 differing ticks |
| `MapScene.NoRandom` cleared at a lobby match start | `tfex/NetplayManager.cpp` | yes | Ryujinx |
| UnlockData getters false online (purple archer portal, moon break, Sunken City / TowerForge select): the level loader spawned the portal / moon sequence on one side only | `rnd.cpp` (4 hooks, installed from `rnd::Install`) | yes (online mode only) | Ryujinx (hooks installed, first words checked) |
| Level list sorted by folder and name (TF.EX v0.21.0; same order as before on the official towers) | `pcgameplay.cpp` GetLevelsShell | yes | Ryujinx (order logged) |
| `Lobby.MatchPlayers` / SessionPlayers (falls back to Players: no server sends it yet), NormaliseSeats | `tfex/Models.*`, `MatchmakingService.cpp`, `NetplayManager.cpp` | yes | Ryujinx |
| TF.EX's Notification as a real Monocle.Entity subclass, made at run time | `tfex/Notification.*`, `tfex/Game.hpp`, one call site in `MainMenuPatch.cpp` (create failed) | **no: hw.txt `notif 1`** | Ryujinx only (menu, create-failed, rollback matches) |

Everything else is docs and tools, not in the build: TFEX-BASE.md, docs/ws-tfex-*.md, `tools/tfex/` (update report),
new `tools/re/` tools (rtypes, fieldcheck, tfexrefs, genheader, sigs, notif-test), the `vendor/tfex` submodule
(`git submodule update --init vendor/tfex` after merging; not needed to build).

## Hardware check wanted (coordinator)

1. Default hw.txt: menu, a lobby match against the browser, a second match. Watch for: the tower picked (log `[tfex] tower:`),
   `[tfex] tower self-test ... OK`, the four UnlockData hooks installed, nothing new in the crash logger.
2. With `notif 1` in hw.txt, a create-failed toast (netplay.txt server pointing at a closed port, then LOBBIES -> START): the toast
   slides in and out; then a lobby match. This is the runtime-made class on the console's GC for the first time.
   If (2) is fine, `notif` can default to on in a later change.

## Build note

`tools/build-probe.sh` copies `rustlib/libggrs_spike.a` from the main checkout only when it is missing, and make does not relink
when it changes. A checkout made before WebRTC (09:23 here) linked a module without `tfx_rtc_new`: "[rtld] Unresolved symbol:
tfx_rtc_new", then a crash at match start. Fix in such a checkout: copy the current .a and delete the mirror's .elf
(`~/.cache/tfx-build/<checkout>-*/<checkout>-*.elf`).

## Phase 2 (ready for master, 2026-10-16 evening)

`ws/tfex-base` is master (`694d6c6`) plus phase 2; master has nothing newer. Merge it, then `git submodule update vendor/tfex`
(the submodule moved to **v0.21.0**).

| change | where | on by default | tested |
|---|---|---|---|
| TF.EX v0.20/v0.21 lobby protocol: Lobby RelayedPairs / IsCheckingConnections / IsReportEnabled / MatchPlayers, KickedFromLobby, abandoned room, host start waits while connections are checked | `tfex/MatchmakingService.*`, `Models.*`, `NetplayManager.cpp` | yes (today's tfex-server sends none of it: no change in behaviour) | Ryujinx lobby matches, 0 differing ticks |
| Hand Models/Json = TF.EX's JSON: **GameData.MapId absent = -1 (random), was 0**; Series kept; `\n` escapes; member-less messages as `[]` | `tfex/Models.*`, `Json.*` | yes | host test against TF.EX's own classes; lobby match (server accepted `[]`) |
| In-match notifications (Synchronized!, Trying to re sync, DESYNC DETECTED, host/players left, kicked, connection updated), menu ones (failed to retrieve lobbies, can't join, invalid code…) | `NetplayManager.cpp` (one line in xplay.cpp's GGRS event loop), `MainMenuPatch.cpp`, `NetplayScreens.cpp` | **no: `notif 1`** (the old OutlineText stand-ins stay while off) | Ryujinx: 0 differing ticks of 2100 with `notif 1` and rollback |
| Toasts made between ticks replayed at the next tick (survive rollback) | `tfex/BetweenTicks.*` | with `notif 1` | Ryujinx stress: 0 mismatches |
| MapScene.NoRandom cleared on entering online mode | `pcgameplay.cpp` | yes | Ryujinx |
| Generated TF.EX code (translator): models, ConnectionIcon, banner, Notification | `tfex/gen/` | **no**: compiled in; only hw.txt `gendemo 1` / `gennotiftest <frame>` use it | Ryujinx (menu) |

Hardware check: the same as above (default hw.txt lobby match), then `notif 1`: a match with the "Synchronized!" toast, and a
menu error toast. Docs/tools only: TFEX-BASE.md, docs/ws-tfex-*.md, tools/tfex (translator under tools/tfex/translator, needs
`dotnet`), tools/re fixes.

## Phase 3 (2026-10-17)

| change | where | on by default | tested |
|---|---|---|---|
| TF.EX's ping measurement in the lobby (own UDP socket, `<server>/ping_measurement/<room>`), PeerMeasured, rollcall ping line + connection icon + CHECKING CONNECTIONS banner (generated code) | `ggrs-switch/src/ping_measurement.rs`, `net.cpp`, `tfex/MatchmakingService.*`, `tfex/RollcallPings.*`, `MainMenuPatch.cpp` | yes | Ryujinx lobby match, 0 differing ticks of 1199 (notif off) |
| A 3-4 player FFA goes on after a disconnect: the leaver's archer dies (TF.EX PlayerPatch); 2 players / team: the match ends as before | `tfex/PlayerPatch.cpp`, one line in `xplay.cpp` | yes | Ryujinx (a page left mid-match) |
| Notification runs on the translator's generated class | `tfex/Notification.cpp` (wrapper), `tfex/gen/` | no (`notif 1`) | **desyncs with `notif 1`** (every tick differs from the first; found 2026-10-17, being fixed): keep `notif` off |
| Harmony hook generator (TF.EX's patch classes as hooks) | `tfex/gen/` | no (`genpatch <Class>`) | Ryujinx (ScreenTitlePatch) |

**Before building:** ping measurement adds Rust functions (`tfx_pm_*`). Run `ggrs-switch/build.sh` in the main checkout (it
only builds there: a relative path to the browser repo), and in any other checkout copy the new `rustlib/libggrs_spike.a` and
delete the mirror's `.elf`. A module linked with the old library crashes when the lobby starts measuring pings ("[rtld]
Unresolved symbol: tfx_pm_new").
Hardware check: a default lobby match (pings on the lobby screen), a 3-player FFA with one player leaving if possible.

## Hardware checklist (everything TF.EX-base has changed; 2026-10-17)

Run on the console with the build of the merged head; note pass/fail and the guest log per item.
Default hw.txt (notif off):
1. Boot, main menu, NETPLAY menu: no crash; `[tfex] tower self-test ... OK` in the log.
2. Host a lobby, a browser page joins: the ping line (ms + plug/server icon) appears next to each archer on the lobby screen;
   "CHECKING CONNECTIONS…" only if the server sends it (today's doesn't).
3. 2-player lobby match to the results, then a rematch, then QUIT to the netplay menu (the second match: fresh undo log).
4. RANDOM map lobby: the tower matches the page's (`[tfex] tower: map -1 -> random ...` on the Switch = the page's tower).
5. A save with towers locked (Sunken City / Moonstone / TowerForge / Ascension not unlocked): online the random pool still has all
   16 towers, and the save is unchanged afterwards (the old code wrote the unlocks for good).
6. 3-player FFA, one page leaves mid-match: its archer dies, the match goes on in sync; a second leaver ends it.
7. Team match, a page leaves: the match ends at once ("A PLAYER LEFT"), back to the netplay menu.
8. A wide lobby (WiderSet page): same tower, match in sync.
With `notif 1` (only after the tfex-subclass desync fix is merged):
9. A lobby against a closed server port: "Failed to retrieve lobbies" / "Failed to create lobby" toasts slide in and out.
10. A lobby match: "Synchronized!" at the start, the match in sync to the results (rollback on); unplug the network briefly:
    "Trying to re sync", then it clears.

## Phase 3b (2026-10-17 01:15): Notification fixed, generated lobby UI in use

On top of master `0a8af3a`+ (master merged in). New since 9c659af:

| change | where | on by default | tested (Ryujinx, shared, vsstop 600) |
|---|---|---|---|
| **notif 1 desync fixed**: the Notification class is made on the main menu (was: lazily at the first in-match toast, inside the match start) | `tfex/Notification.*`, `MainMenuPatch.cpp` | toasts still `notif 1` | notif 1: 0 of 596 (2 players), 0 of 593 (3-player FFA, both pages), rematch 0 of 2554 |
| **TF.EX's generated VersusSelectBanner, LobbyPanel, LobbyInfos** in the netplay menus and the lobby browser (translated from TF.EX's C#) | `tfex/VersusSelectBanner.*`, `tfex/LobbyInfos.*`, `NetplayScreens.cpp`, `tfex/gen/` | **yes** (stand-ins only while notif is off for the banner) | browser with 3 lobbies: TF.EX's rows, panel, can't-join toast; join from a row |
| Translator phase 3: 60 % coverage, Harmony hook generator (`genpatch <Class>`) | `tfex/gen/`, `tools/tfex/` | no | genpatch ScreenTitle/VersusCoinButton/ReadyBanner: 0 of 599 |
| Wide-tower self-test (MapId -1 / 0 / 17) | `tfex/NetplayManager.cpp` | yes (log only) | wide lobby: 0 of 595 |

Not reproduced any more: the earlier wide and genpatch first-tick mismatches (one was in a private slot with its own data copy;
the notif one was the lazy class). Plain 2-player: 0 of 1198.
Hardware (add to the checklist): the NETPLAY menu's banner (selected button's title and two lines), the lobby browser rows /
panel (map icon, mode, length, can't-join reason), joining from a row; then `notif 1`: items 9-10.
