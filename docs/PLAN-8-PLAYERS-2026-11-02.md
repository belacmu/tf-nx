# More than 4 players online on wide maps (started 2026-11-02)

Goal: online matches with up to 8 players (or however many the game's seats allow) on wide maps, the Switch and TF.EX (PC,
browser) together. **Wide mode only:** a standard lobby stays at 4.

**Status (2026-11-03):** 8 players on a wide map play online in sync through the results screen in the emulator (`wide8end`, on
the `eight-players` branch with the fork's TF.EX). The table below says where each part lives and what still waits on TF.EX.


## Where each part lives (2026-11-03)

TF.EX's author has three of our PRs open already; the 8-player work goes to him later, one small PR at a time. Until then each
layer has its own home, and only what works with the TF.EX master is on reaches master.

| Layer | What | Home | Status |
|---|---|---|---|
| TF.EX PR A | player counts in one place: `Constants.MAX_PLAYERS = 4`, `MAX_LOCAL_PLAYERS = MAX_PLAYERS - 1`, seat loops over `TFGame.Players.Length`; no change in behaviour | belacmu/TF.EX `player-limits` (bb379bf, on upstream main v0.29.0) | prepared, compiles; pushed to the fork (2026-11-04), no PR |
| TF.EX PR B | raise the limit: `Constants.LobbyMaxPlayers` (the game's seats in WiderSet's wide mode, else MAX_PLAYERS), `LobbyMaxLocalPlayers = LobbyMaxPlayers - 1` (7 on one device), the local players panel in columns of 3, the `Seats` lobby entry, GameContext's inputs to MAX_SEATS, the roll-call icon for seat 5+ | belacmu/TF.EX `wide-lobby-seats` (b4754c8, on PR A) | prepared, compiles, pushed (2026-11-04); no PR |
| TF.EX PR C | TF.State's spawn centre on wide maps (`WrapMath.AddWidth / 2`): wrong for 2-4 players on wide maps too | belacmu/TF.EX `wide-spawn-centre` (f418ca2, on upstream main) | **opened on its own** (2026-11-04): Fcornaire/TF.EX#316, before/after shots (docs/SCREENSHOTS.md), nothing about more players |
| Server PR A | `MAX_LOCAL_PLAYERS = MAX_PLAYERS - 1` | belacmu/tf-ex-matchmaking-server `player-limits` (0360f53, on upstream main) | prepared; pushed to the fork (2026-11-04), no PR |
| Server PR B | a wide lobby's cap is its `Seats` entry; a device's local players checked against the lobby's cap minus one | belacmu/tf-ex-matchmaking-server `wide-lobby-seats` (c9cd841, on PR A; 55 tests pass) | prepared, pushed (2026-11-04); no PR |
| Test builds | TF.EX `eight-players-v2` (954009f); the server's `eight-players-v2` (0830ce8: our harness server switch-online-028 + server PRs A and B: the PR branches alone refuse the test page's origin); the browser's `claude/tfex-eight-players-v2` (6066ed8, tfex-8p: v0.29.0's harness branch + that TF.EX) | the forks / browser repo | for the emulator tests only |
| WiderSet | FinalKillTeams / EndRound loop 4 seats | TFEX-UPSTREAM-CANDIDATES #6 | noted only |
| Switch translation of the fork | gen/ from TF.EX `eight-players-v2` (954009f: master's TF.EX a8b8f89 + PRs A, B, C merged), the `ScreenCenterX` glue rule, the spawn-centre gaps removed, the 5+ player scenarios (wide5, wide7, wide8, wide8end, wideteam5, wide7plus1) | switch-online `eight-players` (rebuilt on master 2026-11-04; the first version is `eight-players-v1`) | waits for TF.EX PRs A-C (or a deliberate fork pin) |
| Switch mod | seat capacities and their errors, model List capacities, PC parity past 4 seats (arrows, team arrows, chest rows, FinalKillTeams, EndRound), megamod's 8-seat pieces online, the Session class-init fix (a 4-player wide team match's arrows), GC-safe tables, state-dump dedup | switch-online `switch-8p-base` -> master | testing (three, team) |
| Harness | any number of pages joining at once, page guests on virtual pads, template profiles, statediff `--before`, `until` in `expect`, test lessons in AGENTS.md | switch-online `switch-8p-base`; browser `claude/tfex-0.29.0` (tfex-0281: d3e8c9e, ad72d93, a472a31, 112d2a7) | built; with the Switch mod |

## The rule (flexible: no fixed 8 anywhere in the rule)

- **TF.EX:** while WiderSet is wide, LOBBY SIZE goes up to **`TFGame.Players.Length`**, the game's own seat count. WiderSet makes it 8
  on the PC; the Switch's arrays are 8 because megamod/eightplayer.hpp widens them. Otherwise it stays at `Constants.STANDARD_MAX_PLAYERS`
  (4, vanilla). Every per-seat loop that said `4` now runs to `TFGame.Players.Length`.
- **The host publishes its seat count.** The lobby's `Teuria.WiderSet` mod entry carries `"Seats": TFGame.Players.Length`, next to
  `IsWide` and `Version`.
- **Server:** a wide lobby is capped by that entry's `Seats` (at least 4); a standard lobby by `MAX_PLAYERS` (4). Quick play is unchanged
  (4).
- **Switch:** the rule comes from the translated TF.EX code. Fixed C++ storage has one named **capacity** (`xplay::kNpSeats`,
  lobbymatch.hpp; the model lists' caps in translate.toml), and going past it is a logged pools error, never silent:
  - `[tfex] StartLobby: ERROR: N players, more than the lobby match's K seats`
  - `[vs] ERROR: a lobby match of N players, more than ...`
  - `[tfexgen] POOL FULL: a model List (translate.toml [models.caps]) ...`

## Where everything is

Every piece has its own branch and checkout, apart from what other work uses (2026-11-02): the shared hardware-test page
(tfex-0281) moves with TF.EX releases, and the shared Ryujinx queues behind other sessions' runs.

| Piece | Location | Branch / commits |
|---|---|---|
| TF.EX fork | its own clone `~/Code/Towerfall Switch Mods/tfex-eight-players` (origin belacmu/TF.EX), and this checkout's `vendor/tfex` submodule (which has that clone as remote `eight-players`) | `eight-players` on fb377d5: 88d54a0 (loops, LOBBY SIZE), 84e1358 (the game's seats + `Seats` entry), b29593c (roll-call icon fix), f10e659 (TF.State's spawn centre). Pushed to belacmu/TF.EX `eight-players` (2026-11-03). |
| Switch | this checkout (`.claude/worktrees/gifted-shirley-8238fd`) | branch `eight-players` (was `claude/gifted-shirley-8238fd`). gen/ translated from f10e659 (`tools/tfex/translate.sh all f10e65913e410dd48bdd8813c1b13958d4199036`). |
| Matchmaking server fork | `~/Code/Towerfall Switch Mods/tf-ex-mm-eight-players` (a worktree of tf-ex-matchmaking-server) | `eight-players` on switch-online-028: 383b562, 9eaa584; pushed to belacmu/tf-ex-matchmaking-server (2026-11-03). Build: `PATH=~/.rustup/toolchains/stable-aarch64-apple-darwin/bin:$PATH CARGO_PROFILE_RELEASE_STRIP=false cargo build --release` (with strip on, rust-objcopy can't find libLLVM). `cargo test`: 53 pass. |
| Browser for the test | `~/Code/Towerfall Browser/.claude/worktrees/tfex-8p` (`wide5`'s default page) | `claude/tfex-eight-players` on claude/tfex-0.28.1. fetch-tfex.sh is pinned to `belacmu/TF.EX@f10e659…`, its zip built from the local clone (below). vendor/ and gamefiles/ are copied from tfex-0281. |
| Ryujinx | private slot `eight` (`~/.ryujinx-slots/eight`, made with `tools/rj-slot-setup.sh eight`) | runs with `XPLAY_RJSLOT=eight`: no queue behind other sessions' runs |

Building the fork's TF.EX zip without pushing: the browser branch's `tools/build-tfex.sh` takes `TFEX_LOCAL=<a clone>` (fetches the
commit from there instead of GitHub), so in the browser worktree:
`TFEX_LOCAL=~/Code/Towerfall\ Switch\ Mods/tfex-eight-players tools/fetch-tfex.sh && tools/build.sh` (after moving fetch-tfex.sh's
`TFEX_SOURCE` pin to the new commit). Once the branch is pushed, the same without `TFEX_LOCAL`.

## Every 4-player assumption and what changed

### TF.EX (fork)
| Where | Was | Now |
|---|---|---|
| `LobbyVersusPlayerCountButton.MAX_PLAYERS` | `const 4` | `WiderSet IsWide ? TFGame.Players.Length : STANDARD_MAX_PLAYERS` |
| `MainMenu.AddWiderSetLobbyMod` | `IsWide`, `Version` | + `Seats` = `TFGame.Players.Length` |
| TF.State `RoundLogic.cs` netplay spawns (FFA, overtime, `CountTeam`, `SpawnTeam`) | `Vector2[4]`, `< 4`: **only seats 0-3 spawned** | `TFGame.Players.Length` |
| `NetplayRoundLogic.OtherPlayerCouldWin`, `MatchmakingService.ApplyTeamsToMatchSettings`, `MatchVariants.EnableForAll`, replay teams (TF.EX + TF.Replay), `TFCommands` (test command), TF.State `LevelTestRoundLogic` | `4` | `TFGame.Players.Length` / `PlayerInputs.Length` |
| `RollCallElementPatch.UpdateControllerIcon` | `controls/xb360/player{n}`: no art past player4. A fifth seat **crashed the page's roll call** (null subtexture) | swaps only when the atlas has the icon |
| Unchanged on purpose | `MAX_SEATS = 8` (input arrays, upstream), `MAX_LOCAL_PLAYERS = 3` (per device), series `TEAM_PLAYERS = 4` (2v2), `GameContext.MAX_PLAYERS` (only CurrentInputs' first size), TF.State model defaults `int[4]` (overwritten on save) | |

### Server (fork)
`Lobby::seat_cap()` = the wide entry's `Seats`, else `MAX_PLAYERS`. It drives `max_players()`, `update_settings`, `first_free_seat`. `is_wide()`
moved onto `Lobby`. The builder no longer clamps to 4. Quick play still groups to 4.
The browser repo's `cloudflare/tfex-server/matchmaker.js` (the harness's default server and the live worker) still caps every lobby at
4. Porting the `Seats` rule there is a browser-repo follow-up; until then the test uses `LVB_SERVER=real`.

### Switch
| Where | Was | Now |
|---|---|---|
| translate.toml `[models.caps]` | `Lobby.Players` 4, `MatchPlayers` 4, `RelayedPairs` 6, `MatchEnded.Scores` 4 | 8, 8, 28 (8·7/2), 8: capacities, not the rule |
| `gen::List` (tfexgen_rt.hpp) | **silently dropped** items past its cap. That was why the Switch's lobby stayed at 4/5 while the server seated 5 | PoolFull: logged |
| `svc::ControllerSlots()` (TF.EX's `WiderSet ? 8 : 4`) | 6 (shrank the widened PlayerInputs) | `TFGame.PlayerInputs.Length` |
| `kNpSeats` | 4, in xplay_internal.hpp | the capacity, in lobbymatch.hpp; `LobbyMatch.team`, `SideAll`, seat ticks, vs scripts, `g_lastRemote`, `g_lobbyTeams`, netplay_gate loops and scores all use it |
| lobbymatch.cpp `MaybeNativeStart` | `TFGame.Players` filled `< 6` | to the array's length |
| tfex/NetplayManager.cpp | `kMaxPlayers 4`, refused `n > 4` | `= xplay::kNpSeats`; ERROR past it (counted before the sort truncates) |
| tfex/MainMenuPatch.cpp | test driver / roll-call evidence `< 4`, `lobbymax` 2-4 | `TFGame::Players_Len()` (new, tfex/Game.hpp); `lobbymax` any (TF.EX's button clamps); the `[lobby] seats:` line shows every seat |
| Unchanged | GGRS (`num_players` from config), TF.State seat padding (`PcSeats`: wide 8 / standard 4), rumble `kSeats` 8 | |

## Test runs (`tools/smoke.sh wide5`)

How to run it:

```bash
XPLAY_RJSLOT=eight XPLAY_VISIBLE=0 tools/smoke.sh wide5
```

`wide5` defaults to the eight-player browser worktree and the server fork's binary (`BROWSER_LOBBY_WT`, `TFEX_REAL_SERVER` override
them). Without them it ran against the shared page and the stock server, and the fifth page never joined (215202).

The scenario: `lobbywide 1`, `lobbymax 5`, `lobbylocal2 1`, `vsstop 2400`; `LVB_PAGES=3 LVB_PAGEWIDE=1 LVB_MODS=TF.EX,Teuria.WiderSet
LVB_XLOBBY_EXTRA=wide=1 LVB_PREKEYS=c LVB_SERVER=real`. Three pages plus Ryujinx load the machine heavily: wait for a 1-minute load
under ~16.

1. 170856, 172105: the wide 5-seat lobby was created and the server seated all 5, but **the Switch's lobby showed 4/5**: the model List
   cap of 4 dropped the fifth player silently. Fixed (caps, PoolFull).
2. 175529: the lobby filled and the match started. **Pages A and B crashed in TF.EX's roll call** (`controls/xb360/player5 not found`,
   NullReferenceException in `RollCallElementPatch.UpdateControllerIcon`), so the Switch's GGRS session timed out after 30 s waiting
   for them. The main thread was blocked those 30 s and the server dropped its websocket. Then **the Switch crashed**: an uncaught
   translated throw in `Notification.RemoveSameText` (`Scene.Layers[layerIndex]` with no such layer in the level), from
   `MatchmakingService.DrainGameThreadActions` → `TFGameUpdate_Patch`. The roll-call crash is fixed in the fork (b29593c). The
   notification crash is open (backlog below).
3. 181119 (b29593c, the last run): **the 5-player wide match started and synchronized**. All 5 archers spawned on both sides, so
   TF.State's spawn fix works. Then the **state desynced from frame 60**: every player starts with **2 arrows on the Switch and 3 on the
   PC** (`Entities.Players[0..4].ArrowsInventory.Arrows.Count`; statediff-lvb-browser-LA80.txt). That is likely the Switch's own
   starting-arrow rule for 5+ players (its six-player mode) against the PC's (WiderSet), which `pcgameplay` doesn't cover yet. Frame 0
   also differs in `OrbLogic.Time.EngineTimeMult` (Switch 1.0, PC 0.25); check whether that happens in a 4-player wide match too.
4. 215719 (the PC's starting arrows, WiderSet's team arrows and chest rows): **frame 60 identical on both sides**. Then the session
   stalled at frame 60: page C (the fifth seat, GGRS handle 4) crashed in the harness's input script
   (`XplayVersus.NetPolledInput`: `scripts[netSeat]` with 4 scripts), and every peer waited for it. While stalled, both sides dumped
   frame 60 on every update (26986 times; the Switch's log head ran full). Fixed: a seat past the scripts stands idle (browser
   XplayNet), and a re-save with the same bytes isn't dumped again, only counted (tfexgen_state.cpp DumpSavedFrame, browser
   StateDump.cs). (Two runs before it, 214627 and 215202, ran without the eight-player browser and server: the fifth page never
   joined. `wide5` now defaults to them.)
5. **220913: PASS.** The 5-player wide match (2 Switch players and 3 pages, The Amaranth) runs to `vsstop 2400` in sync: TF.State's
   saved bytes are identical on 39 of 40 dumped frames (only frame 0, the known `EngineTimeMult`), GGRS checksum clean.
6. 222237 (f10e659, TF.State's spawn centre from the width): PASS, the same 39 of 40.
7. `wideteam5` (new: wide5 as Team Deathmatch, the Switch's two BLUE, the three pages RED, 2 v 3):
   - 110955: **the Switch crashed** in the game's `MatchTeams.set_Item` (throws for a seat above 5), called by TF.EX's
     `ApplyTeamsToMatchSettings`, which (fork 88d54a0) sets every seat of `TFGame.Players`: 8 on the Switch (megamod's arrays), and the
     Switch's MatchTeams has 6 fields. That was **every team lobby**, standard ones too. On the PC, WiderSet's get_Item / set_Item
     finalizers serve the seats past the fields wherever it's loaded. Fixed: megamod's four MatchTeams hooks (get_Item / set_Item for
     seats 6-7, GetPlayersOfAllegiance, AmountOfAllegiance) run online too (inert until a 7th seat is joined).
   - 111508: in the match, **the RED team of 3 started with 2 arrows on the Switch, 3 on the PC**. `SetWideMatch`'s TeamStartArrows
     write had never happened in any run: Session's statics are made by its class constructor, which runs at the first Session,
     after the lobby sets the match up, so the array was still null and the code skipped it without a word. Fixed: the game's own
     class-init guard first (as `Session..ctor` does), and an ERROR line if the array is missing. (So a 4-player wide team match with
     a team of 3 desynced the same way before.)
   - `team` 111709 (the standard 1 v 1 team scenario, after the MatchTeams fix): PASS.
   - **112146: PASS**, 39 of 40 dumped frames identical, `[pc] wide: TeamStartArrows 3 3 3 3 2`.

## Who owns what: TF.EX or the Switch (2026-11-02)

On the PC, 5-8 archers is WiderSet's job: it widens the game's arrays (`TFGame.Players` etc. to 8), sets gameplay tables, and
transpiles about 90 game methods (each literal `4` becomes `IsWide ? 8 : 4`, each screen centre `160` becomes `210`, each width `320`
becomes `420`). Sources: WiderSet 0.9.0 decompiled at `.cache/widerset-0.9.0-src` in the main checkout;
`python3 tools/re/widecheck.py <that dir>` lists every rewritten constant against the Switch (needs `.cache/towerfall-pc.il`).

**The one rule that sorts everything: whose code runs during a netplay match.**

- **A. TF.EX owns it:** TF.EX / TF.State replaces the game's method in netplay (a prefix returning false). WiderSet's transpiler on
  the game's method then never runs online, so the replacement must carry WiderSet's change itself. This is TF.EX's code, a fix there
  is a fix on the PC too, and the Switch gets it by translation. **Spawn points are this kind:** TF.State's `SpawnPlayersFFA` /
  `SpawnPlayersTeams` replace the game's.
- **B. The Switch matches the PC:** the game's own method runs on both sides. On the PC, WiderSet's version of it runs; on the
  Switch, the Switch game's own version runs, and that already has its own 5-6-player rules. The Switch has to play the PC's
  (WiderSet's) rule online, the pcgameplay.cpp way (PC rules only while online). TF.EX has nothing to do here.

They are the same question ("what does WiderSet do for more archers?"), answered by different owners. Most of the work is B, because
TF.EX replaces only a few gameplay methods.

### A. TF.EX (fork `eight-players`)
| What | State |
|---|---|
| Spawns: TF.State's FFA / teams / overtime loops to 4 (only 4 archers spawned) | **done** (88d54a0) |
| Spawns: TF.State's screen centre `160f`, in the FFA "two players face each other" rule (`xMLPositions[0].X != 160f`) and the team side split (`spawn.X <= 160f`). WiderSet's transpiler makes the game's own version `IsWide ? 210 : 160`; TF.State's copy keeps 160, so wide netplay spawns differ from WiderSet offline (2-4 players too). Both sides run TF.State, so this is no desync, but it is a TF.EX bug | **done** (f10e659): `ScreenCenterX => WrapMath.AddWidth.X / 2` (WiderSet's 420 wide, so 210; no WiderSet dependency in TF.State). The Switch has no AddWidth vector: translate.toml replaces it with glue `Ledger.ScreenCenterX`, the int width static (+0x4792130, 420 for the wide set). pcgameplay's two gaps that held the Switch game's own spawn methods at 160 (to match TF.State's fixed value) are gone |
| LOBBY SIZE, the `Seats` entry, the lobby's teams, `OtherPlayerCouldWin`, per-player variant values, replays, test command | **done** |
| Roll call controller icon for seat 5+ (crash) | **done** (b29593c) |
| `HUDPatch` seat scores (`Enumerable.Range(0, TFGame.PlayerAmount)`): assumes seats 0..n-1, which holds because TF.EX compacts seats to handles | check only |
| Server match report / `MatchEnded.Scores` with more than 4 seats (the server side of the report) | **to check** |
| Spawn count: a wide level must have `PlayerSpawn`s for every archer. The game's `GetLevels` filter (`PlayerSpawns >= PlayerAmount`) picks such levels on both sides; TF.State tops up from team spawns, and indexes past the list if still short | check with 7-8 players |

### B. The Switch matches the PC (WiderSet) online
Found by listing every read of `TFGame.PlayerAmount` in the Switch's game (57 methods) against the PC's IL, plus WiderSet's
gameplay tables:

| What | Switch game | PC with WiderSet | State |
|---|---|---|---|
| **FFA starting arrows** (`Session.GetSpawnArrows`, +0xb45b50) | `if (PlayerAmount >= 5)` (+0xb45dc8, the Switch's six-player rule) → **2 arrows** | always 3 (the PC has no player-count read there; WiderSet doesn't patch it) | **done**: pcgameplay kGaps nops the `b.ge` (+0xb45dd0) while online (a no-op with 4 players or fewer) |
| Team starting arrows `Session.TeamStartArrows` | `{3,3,2,2,1}`, already `{3,3,3,3,1}` for wide matches (pcgameplay `SetWideMatch`) | `{3,3,3,3,2,2,2}` | **done for teams of up to 5**: SetWideMatch writes `{3,3,3,3,2}` (index 4 = 2). Teams of 6-7 index past the Switch's 5 entries: with seats 7-8 |
| Chest chances `TreasureSpawner.ChestChances` by player count | the Switch's own rows for 5-6 (megamod aliases 7-8 to the 6-player row, local only) | WiderSet's rows 3-6 (`{.9,.9,.8,.8,.2,.1}`, `{.9,.9,.9,.8,.4,.1}`, `{.9,.9,.9,.8,.4,.2}`, `{.9,.9,.9,.9,.4,.2,.1}`) | **done**: rows for 2-4 players are equal (PC IL data blobs against the Switch's rodata +0x38e21f0, +0x38f38dc, +0x38f38f0). SetWideMatch points rows 3.. at WiderSet's (row objects outside the GC heap, restored on leaving online); rows 5-6 only exist once the outer array is 7 long (seats 7-8) |
| `MatchSettings.get_GoalScore` | reads PlayerAmount (4 call sites; PlayerGoals tables extended to 6) | 0 reads (the PC's goal comes from `MatchLengths.PlayerGoals`) | **matches already**: online, the goal is FortRise's translated `get_GoalScore` (mode 11) with `frmods::PlayerGoals`, the PC's rule (5+ players: the 2-player goal; WiderSet doesn't patch it) |
| `TreasureSpawner.GetPickupsForTreasureDraft` `cmp #4`, `RoundLogic..ctor` `mov #4` ×3, `Session..ctor` `mov #4` ×2 (widecheck: WiderSet makes each 8 when wide) | 4 | 8 | **not rules**: the ctors' `mov w1, #4` is NewArr's element size (the counts are already 6 on the Switch, `mov w8, #6`); `cmp #4` is the bounds check before reading index 3. Nothing for 5-6; the counts go to 8 with seats 7-8 |
| `MatchTeams` (WiderSet: indexer finalizers, `GetPlayersOfAllegiance`, `GetAmountOfPlayersOfAllegiance`) and `MatchSettings.GetMaxTeamSize` / `GetPlayerTeamSize` / `GetTeamMismatch` / `CanStartWithTeams` | 6 seats | 8 seats, WiderSet's rules | **matches for 5-6 players**: the Switch's versions loop over its 6 seats (`cmp x21, #6`; GetPlayerTeamSize unrolled 0..5), WiderSet's over 8 (`4` -> `IsWide ? 8 : 4`): the same with seats 7-8 empty. MatchTeams' seats 6-7 are served online by megamod's hooks (wideteam5 110955). Team rules for 7-8: with seats 7-8 |
| Other Switch-only PlayerAmount reads: `VersusAwards.AssignAward`, `VersusRoundResults.LosePointOverflow`, `KillStats.get_Item`; counts differ in `MatchSettings.CleanSettingsVersus` (5 vs 2) | | | display only (awards, results, kill stats: after the level, outside TF.State's saved state); `CleanSettingsVersus` runs at the versus menu, and TF.EX applies the lobby's variants when the match starts. Not changed |
| Seats 7-8: roll call slots, `MatchTeams` 6/7, arrays, results / awards / replay loop bounds | megamod, local only | WiderSet | below (step 4) |
| Frame 0 `OrbLogic.Time.EngineTimeMult` (Switch 1.0, PC 0.25) | | | check whether 4-player wide shows it too |

## Next steps, in order

1. ~~Get `wide5` green~~ **done** (220913); team modes with 5 players **done** (`wideteam5` 112146); A's spawn centre done (f10e659,
   222237). Table B is complete for 5-6 players.
2. **Backlog (a crash):** `Notification.RemoveSameText` throws when its layer index isn't in the scene (a connection-lost notice during
   a level). Find the TF.EX path that calls it with that layer, and check whether the PC throws there too (TF.EX's code) or whether
   the Switch's level lacks a layer the PC has.
3. **Backlog (logging):** TF.EX's `NetplayManager` init blocks the Switch's main thread for its whole 30 s timeout while peers
   connect (`[slow] TFGame.Update over 100 ms: 30019 ms`), and the server then drops the websocket. Check what the PC does.
4. **Seats 7-8 on the Switch (2026-11-03): 7 players in sync** (`wide7` 122021, 122730: 2 Switch players + 5 pages, 38 of 39
   dumped frames identical). The megamod's 8-seat substrate now runs online (megamod.hpp's header lists it; all inert with 6 seats
   or fewer), as WiderSet's does on the PC:
   - `MatchTeams` seats 6-7 (get_Item / set_Item, GetPlayersOfAllegiance, AmountOfAllegiance): every team lobby needed it (above);
   - the roll call: a wide lobby whose `MaxPlayers` is past 6 gets slots 7..MaxPlayers (`[8player] online: roll call slots 7-7 added`),
     in megamod's two-row layout; 6 seats or fewer keep the game's;
   - the loop bounds (`kBoundPatches`) are permanent now, like the arrays and Session counts; the results hooks (VMRResults,
     VPMR ctor / render, AddComponent, AssignAward) run online (they act only past 6 players: the game's results screen throws for 7-8);
   - WiderSet's tables: `TeamStartArrows` (7 entries) and `ChestChances` (7 rows) as whole array objects of ours during a wide match
     (WideTables; the game's arrays kept alive in RuntimeAlloc memory, put back after). The first version replaced rows in place and
     held the replaced rows in a plain C++ static, which the GC doesn't scan.

   **8 players, with 2 pages** (`wide8` 133721: PASS, 39 of 40): the Switch's two and 3 on each page. TF.EX seats up to 3 local
   players a device (`MAX_LOCAL_PLAYERS`); the page's guests are virtual gamepads (browser web/Netplay/TestGamepads.cs, the `pad`
   command), START on the netplay menu before joining, A on the roll call (`LVB_PAGEGUESTS=2`). The roll call: `slots 7-8 added`.

   **The results screen with 8 players** (`wide8end`: Team Deathmatch, Instant, the Switch's two BLUE against the pages' six RED):
   the match ends and megamod's results run online (`[8player] results: added 2 portrait(s) for P7/P8`). On the way:
   - the old seat scripts acted only during the round intro (players are frozen until frame ~1080, script tick ~810 at 240 Hz), so
     every Switch player stood still and every round was a miasma draw; test-scripts/versus-shootup starts at tick 900;
   - `MatchSettings.GetPlayerTeamSize` (unrolled over 0-5) counted a team of 6 as 4: 3 starting arrows on the Switch, the PC's 2
     (134226). megamod PlayerTeamSizeHook adds seats 6-7; `GetMaxTeamSize`'s loop bound to 8 (the TDM level filter reads it);
   - two game loops the PC runs over 4 seats and WiderSet leaves alone, the Switch over 6: `RoundLogic.FinalKillTeams` (GotWin, the
     spotlight: 134948) and `Session.EndRound`'s per-seat stats (PointsFromGoal: 135645). pcgameplay plays the PC's 4 online.
     TFEX-UPSTREAM-CANDIDATES #6 (WiderSet's). A scan of the PC game for methods reading TFGame.Players with a literal 4 that
     WiderSet doesn't patch found the rest in menus, quest / Dark World, or after-the-level screens.
   The results screen's component census logged ~300 lines a screen: 24 at most now, without the known tween type.
   **160819: PASS** (2026-11-03): the 8-player match ends ~70 s after sync (START WITH BOMB ARROWS: the Switch's two jump and shoot
   down, test-scripts/versus-bombs), `[8player] results: added 2 portrait(s) for P7/P8`, GGRS checksum clean, TF.State's saved bytes
   identical with both pages on 28 of 29 frames (frame 0 as always). The whole run: 6.5 min, 3.5 of them the pages' first boot (it
   made the template profile; later runs boot in about a minute). Earlier attempts waited for the miasma (~11000 ticks a round, at
   10-25 ticks a second): AGENTS.md Testing has the lessons.

   **One device with 7 players** (`wide7plus1` 090449, 2026-11-04: PASS, 19 of 20 frames identical, GGRS clean): the Switch alone
   hosts a wide 8-seat lobby, one page joins with its own player and 6 guests on virtual pads (TF.EX PR B's local limit, the server
   PR B's check). 2 min 16 s in all (the page boots in 15 s from its template profile; vsstop 1200: past the intro and some play).

   **The Switch with 7 players** (`wide1plus7` 122303, 2026-11-04: PASS, 19 of 20 frames identical, GGRS clean, StartLobby with 8
   players): the Switch hosts a wide 8-seat lobby with 7 local players (`lobbylocal 7`: controllers 2-7 START on the netplay menu in
   turn, then A on their seats), one page joins alone. Two causes, found with a per-frame probe of each seat's input and its roll call
   slot (raw A, TF.EX's A, NotJoinedUpdate's result):
   - **The Switch's join check capped the roll call at 6** (the fix): the game's RollcallElement.NotJoinedUpdate joins only while
     `TFGame.PlayerAmount < cap[RollcallMode]` (the table at 0x38e22d0, VERSUS 6; StartVersus reads it too). The megamod raises it to 8
     for local play, a word that went back to the game's 6 online, so the 7th local seat's A did nothing (seat 6: A through TF.EX's
     gate, NotJoinedUpdate ran, returned 0). Online it is now the substrate's (WiderSet's 8 on the PC): CreateRollcall sets it to 8
     while the lobby has seats past 6, the game's 6 otherwise (`megamod::SetOnlineWord`). Also: online roll calls past 6 seats start
     seats 7-8 on archers 6 and 7 (the PC's `Characters[i] = i`; retail seeds 0-5, so they started on seat 0's archer).
   - **The test driver pressed every controller's A in the same frame**: TF.EX's RollcallElement.EnterJoined holds every controller
     (DisableAllControllers) until the server has that seat's ready, so one seat joined per 2-second retry round, top seat first, and
     the harness's 60 s ran out. Now each controller presses after the device's earlier seats are ready and TF.EX gives the input back
     (as people do). TF.EX's behaviour, not a bug.
   On hardware: nobody has played 7 controllers on one Switch online yet; the cap is the same word the local 8-player mode uses.
5. ~~Harness for more than 6 players~~ (2026-11-03): `LVB_PAGES=<n>` takes any number of pages (letters B..H); the pages join **all
   at once** (each page's `[xlobby] ... in a lobby` line, browser XplayLobby.cs, tells the harness which ones to retry): 5 pages
   joined in 40 s, 144 s one after another. The time limits grow with the page count (LVB_MENUMAX +30 s, the run +40 s a page).
   statediff leaves out the match's last second (`--before`): the Switch parks at vsstop and stops sending inputs, so the page's last
   frames may be predictions GGRS never confirms (120856, under a load of 127: a "desync" at the last frame 2340).
6. Port the `Seats` cap to the browser repo's matchmaker.js (the live worker). Wide quick play past 4 (both servers).
7. ~~Push the TF.EX and server branches~~ (pushed 2026-11-03: the browser branch's `TFEX_SOURCE` works without `TFEX_LOCAL` now).
   Merge to master, then publish after a boot check (AGENTS.md).
8. Per-tick cost: STATUS.md's VERY IMPORTANT item (+1.15 ms per extra player on the console) decides whether 6-8 players hold 240 Hz.
   That is separate work, already under way in another session.

Upstream PR candidates (deferred, docs/TFEX-UPSTREAM-CANDIDATES.md #5): the TF.State spawn loops, the LOBBY SIZE cap, the roll-call
icon crash, and the `Seats` entry for servers.
