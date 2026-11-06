# Workstream map-select: TF.EX's map screen at a lobby match start

## Status (2026-10-17, rebased on master a73ab62, which contains 0513364)

**Works:** a lobby match starts as in TF.EX. The lobby screen fades out (20 frames), the vanilla map screen selects the lobby's tower or
RANDOM, the RANDOM roll steps to the tower `Tower::Select` picked, the camera zooms in, then StartSession runs, and the GGRS session
starts inside it. A rematch goes from the results straight to the map screen. The map-screen frame counts equal vanilla's
(20 + 6·steps + 30 + 80). The tower choice is still made only in `Tower::Select`; the wide path is unchanged and follows the
tfex-lockstep wide MapId fix, now merged.

**Verified (Ryujinx slot B vs the page; out dir /tmp/xplay-runs-07a0ba51-B):**
- Before the rebases (master 7fec2c2): a fixed map, 0 differing ticks of 2393. RANDOM with 2 rematches (`runs/235459`): towers 5/10/13 on
  both sides, 0 differing ticks of 1950, screenshots of both screens in `ws-map-select/`, and both in the level within one 0.8 s shot.
- On the final base: a fixed map, 0 differing ticks of 892 (100 map frames). RANDOM + rematch (`runs/012539`): towers 12 and 14 on both
  sides, 207 and 219 map frames (expected 208 and 220), **but the Switch wrote no tick log in that run, so it was not compared**.
- Stale / invalid runs, ignore them: `003032` and `010633`, and the 01:06 fixed run, compared a stale `vs-log-3m.txt`. The matches were
  cut before vsstop or the results flush on a Mac at load ~50, so the file held an earlier match: "every tick differs from T -264".
  The 01:13 runs crashed the guest at vsstop because I had truncated that file. It must stay a 3 MB file; reset it with
  `dd if=/dev/zero bs=1048576 count=3`. Control on master without this change: 0 differing ticks of 899.

**Left:** a RANDOM, Switch-hosted run that compares clean on the final base (find out why rematch-test's tick log stayed empty in
`012539`: no flush at the results with a zeroed file?); a wide lobby; 3-4 players; the console.

Branch `claude/map-select-4972a5`, 2026-10-16. The console report (master 7fec2c2): "the switch side never sees the map selection
process, it always skips straight to the empty map, where we have to wait for the browser to go through the auto random map selector".

## What TF.EX does (v0.21.0, vendor/tfex)

1. Lobby screen, all ready, StartLobby: `RollCallElement.StartVersus` (vanilla: State = VersusOptions) and its postfix
   (`TF.EX.Patchs/Entity/MenuItem/RollCallElement.cs:531`): `MainMenu.FadeAction = MainMenu.GotoVersusLevelSelect; State = Fade`.
   `MainMenu.FinishTransition` runs 20 frames after the state change and calls FadeAction: `Engine.Scene = new MapScene(Versus)`.
2. The vanilla `MapScene` (`docs/reference/tf8-decompile/TowerFall/MapScene.cs`), patched by `TF.EX.Patchs/Scene/MapScene.cs`:
   - `InitVersusButtons` prefix/postfix: SunkenCity, Moonstone, TowerForge, Ascension count as unlocked while the buttons are made.
   - `Begin` postfix: deselect the start button, select the button with `Data.ID.X == MapId` (VersusRandomSelect for MapId -1 or when
     there is none), `ScrollToButton`.
   - Input (`XGamePadInput.cs:243`, `:430`, `:492`; `KeyboardInput.cs`): in the lobby's MapScene MenuConfirm is true for every input,
     MenuBack and Left/Right false. So the vanilla `MapScene.Update` confirms as soon as `CanAct` (IntroSequence done) and the
     20-frame `CanConfirmCounter` allow: frame 20 of the map screen.
   - Confirm on a tower (`VersusMapButton.OnConfirm`): LevelSystem = that tower, `DoEnterAreaZoom` = 60-frame camera zoom, then an
     Alarm of 20 frames, then `StartSession`. Confirm on RANDOM (`VersusRandomSelect.OnConfirm`): `GetRandomVersusTower` (TF.EX's
     postfix: the netplay-safe VersusMapButtons, `Calc.Shuffle(new Random(lobby seed))`, the first), then one `SelectLevel` step right
     every 6 frames until that tower, then after `steps * 6 + 30` frames `DoEnterAreaZoom` (60 + 20).
   - `StartSession` prefix/postfix: players, archers, teams, NetplayOptions; ResetRng, EnsureRemoteController. Vanilla StartSession
     = `new Session(CurrentMatchSettings).StartGame()`. The GGRS session (NetplayManager.Init) starts in the Level.
3. RematchLobby (`MatchmakingService.cs:747-765`): NetplayManager.Reset, then from the Level straight to `new MapScene(Versus)` (same
   archers), so a rematch shows the map screen (and the RANDOM roll) again.

So the map screen lasts `20 + 80 = 100` frames for a fixed map and `20 + 6 * steps + 30 + 80` frames for RANDOM (steps = buttons from
RANDOM to the tower: tower list index + 1 with all 16 towers).

## What the Switch did

`xplay.cpp MaybeNativeStart` called `MapScene.StartSession` (+0xab54e0) directly from the lobby screen (VIABILITY-TESTS.md "Starting a
versus match natively", the native start built for the scripted versus test), with the lobby's tower set as the LevelSystem. No fade,
no map screen: the console sat in the level (GGRS waiting for the other side) while the page ran its fade, map screen and roll (2-4 s).
A rematch went back to the lobby screen for a frame and started natively again.

## The port

`hw-probe/exlaunch/source/program/tfex/MapScenePatch.cpp` (one unit for TF.EX's `Scene/MapScene.cs`, with the MapScene branches of
the input patches and StartVersus_Postfix's fade), on the Switch's own vanilla MapScene (1.4.1.6 has the PC's MapScene code):

| TF.EX | Switch |
|---|---|
| StartVersus_Postfix: State = Fade, FadeAction = GotoVersusLevelSelect | `Enter`: `MainMenu.set_State(Fade)`; `MainMenu.FinishTransition` (+0xa9fdf0) hook calls `GotoVersusLevelSelect` (+0xaa07a0) in place of the (null) FadeAction |
| RematchLobby: `Engine.Scene = new MapScene(Versus)` from the Level | `MainMenuPatch.cpp EndgameFrame`: `EndLobbyMatch` (NetplayManager.Reset) then the lobby match start from the Level; `Enter` calls `GotoVersusLevelSelect` at once (no lobby screen in between any more) |
| InitVersusButtons prefix/postfix | hook on +0xab1fe0 around `Tower::InitVersusButtonsUnlocks` |
| Begin_Postfix | hook on `MapScene.Begin` (+0xab1240, vtable 0x20): OnDeselect (+0xaa9360), Selection (+0x50) = the button, OnSelect (+0xaa9330), ScrollToButton (+0xab4730). The button: the VersusMapButton whose Title is the tower's (`Tower::ButtonIsTower`), VersusRandomSelect when `Tower::Select` went through RANDOM |
| GetRandomVersusTower postfix | hook on +0xab2a50 (VersusRandomSelect.OnConfirm calls it at +0xbb5900): returns the button of the tower `tfex::Tower::Select` picked (the same shuffle; checked against TF.EX's model at each boot, `[tfex] tower self-test`). The vanilla body (unseeded `new Random()`) is not run |
| MenuConfirm true / Back, Left, Right false in the MapScene | hooks on the static MenuInput getters `MapScene.Update` (+0xab3bb0) reads: ConfirmOrStart +0xac74a0 true; Back +0xac6c20, AltCheck +0xac7170, Alt2 +0xac7280, Left +0xac7930, Right +0xac7a70, Up +0xac76b0, Down +0xac77f0 false; only while the scene is MapScene and the lobby's map screen is active |
| StartSession prefix/postfix | hook on +0xab54e0: `xplay::LobbyStartSession` (the LevelSystem set again from the tower, controller slots, mode/teams, menu-confirm hooks, OneFrameDelay, `NpStart` = the GGRS session, write tracking), then the game's StartSession |

`xplay.cpp MaybeNativeStart` keeps the part before the screen (players, archers, the LevelSystem and `SixPlayer` for a wide lobby,
so `GotoVersusLevelSelect` opens the 6P map, `pcgameplay::Set(true)` so UnlockDataPatch's getters are false during the screen:
VersusRandomSelect's forced Sunken City / TowerForge pick can't happen) and calls `MapScenePatch::Enter`. The rest moved into
`FillControllers` and `NativeSessionSetup`, called from `LobbyStartSession` for a lobby match and, unchanged, right before the direct
StartSession call for the non-lobby native test start (`vstart 1`). `lobbymatch.hpp`: `LobbyMatch.randomMap` (from
`Tower::LastWasRandom`). `tools/tfex/manifest.toml` / `overrides.toml` point the MapScenePatch members and StartVersus_Postfix at the port.

What stays the same: the tower (still `Tower::Select`, and the LevelSystem is set from it again in StartSession, so the screen can't
change the level played); the GGRS session starts at the same point as before relative to StartSession (inside it, before
Session.StartGame), as TF.EX's starts with the level; the gameplay RNG is reseeded at StartGame as before.

## Ryujinx runs (private slot B, tools/lobby-vs-browser.sh, tools/rematch-test.sh, window visible, screenshots every 0.8 s)

**Fixed map** (lobby MapId 0, standard length; `XPLAY_RJSLOT=B XPLAY_VISIBLE=1 LVB_PREKEYS=c LVB_JOINMAX=300 tools/lobby-vs-browser.sh`):
`MainMenu.State = Fade` -> 0.29 s later the map screen, `Begin: 17 buttons, selected VersusMapButton`, `StartSession after 100 map
frames` (= TF.EX's 20 + 60 + 20). Both screens showed the Sacred Ground map in the same screenshot and were both in the level in the
next one (0.8 s): `ws-map-select/fixed-switch-map.png`, `fixed-page-map.png`. **0 differing ticks of 2393.** (The run's watchdog then
stopped it as "stalled": the Mac ran three Ryujinx instances and the match ran at ~12 ticks/s; the compared ticks are all equal.)

**RANDOM + two rematches** (`XPLAY_RJSLOT=B XPLAY_VISIBLE=1 LVB_JOINMAX=300 tools/rematch-test.sh "lobbymap -1"`, instant matches,
both vote REMATCH!):

| match | seed | Switch tower (Select) | page LevelSystem | Switch map frames (expected) |
|---|---|---|---|---|
| 1 (from the lobby screen) | 1058272106 | 5 THORNWOOD | `{X:5 Y:0}` THORNWOOD | 165 (20 + 6*6 + 30 + 80 = 166) |
| 2 (rematch, from the Level) | 988451856 | 10 TOWERFORGE | `{X:10 Y:0}` TOWERFORGE | 195 (196) |
| 3 (rematch) | 1170204032 | 13 DREADWOOD | `{X:13 Y:0}` | 213 (214) |

(The one-frame difference: the Switch counts MapScene.Update calls from the first, the count is taken in StartSession.) Screenshots of
match 1: the roll starting on both (`random-switch-roll-start.png`: the Switch on RANDOM, `random-page-roll-start.png`: the page one
step further, on Sacred Ground; the page's FortRise build has two more buttons left of RANDOM), both on THORNWOOD 0.8 s later
(`random-*-thornwood.png`), both in the Thornwood level in the same shot (`random-*-level.png`; the page shows TF.EX's "WAITING FOR
OTHER PLAYERS..." until GGRS synchronizes, ~3 s later on both sides). Between match 1 and match 2 the page's match starts are 95.13 s
apart (xlobby `t=42338ms`, `t=137468ms`) and the Switch's StartSessions 95.37 s (00:01:26.216, 00:03:01.585): the rematch arrives
together too. **0 differing ticks of 1950** (match 1, the part the compare covers). Logs: `/tmp/xplay-runs-07a0ba51-B/runs/235459/`.

## Not tested / open

- **Wide (WiderSet) lobby**: SixPlayer is set before the screen, so `GotoVersusLevelSelect` opens the 6P map (`InitSixPlayerButtons`
  +0xab2920) and the button is found by title among the 6P towers; not run (needs the page with WiderSet). The tower choice is not
  touched by this work, normal or wide: it is still made only in `tfex::Tower::Select` / `SelectStandard` / `SelectWide`, and the map
  screen only shows it (Begin starts on RANDOM exactly when Select took its RANDOM branch, `Tower::LastWasRandom`; GetRandomVersusTower
  returns Select's tower; StartSession sets the LevelSystem from it). A fix to Select (e.g. the wide MapId 0 desync found on 9c659af:
  TF.EX finds no wide button with ID 0 and goes RANDOM) applies unchanged as long as it goes through Select's random branch.
- 3-4 player lobbies and team lobbies: nothing in the screen depends on them; not run.
- On the console: the timing above is Ryujinx's; the console's frame rate in the map screen (no GGRS yet) should be the game's own.
- TF.EX's sounds on the way (StartVersus's `ui_click`; RematchLobby's `ui_click`) are not played; the map screen's own sounds are.
- The vanilla MapScene `IntroSequence` waits on `SaveData.Unlocks.GetArcherToIntro()` (an archer-unlock panel) and on a save if
  `ShouldSave`: a console save with an archer still to introduce would show that panel first (as TF.EX on a PC with such a save).

## What to look for on the console

At StartLobby the lobby screen fades out, the map screen opens on RANDOM (or the lobby's tower), the cursor steps right to the rolled
tower, the camera zooms in, the level appears, "Synchronized!" a moment later, at about the same moment as the page. No input moves the
map cursor or leaves the screen. After REMATCH! votes: straight from the results to the map screen again (no lobby screen in between).
Log lines: `[map] ...` (sd:/xplay/hw-log.txt).
