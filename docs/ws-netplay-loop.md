# The netplay loop: review (2026-10-27)

**Status (2026-10-27):** step 1 is done: TF.EX's whole LevelPatch is generated hooks, on by default (translate.toml unit `LevelPatch`,
hooks = true; tfex/gen/tfexgen_LevelPatch.*): the Level ctor postfix (ReplayRecorder dropped in netplay), Update's prefix (awaiting
synchronization, the session step into Engine.TimeMult / DeltaTime / DeltaTicks, AddPlayersIndicators) and postfix (FlushDeferred,
ClearWaitingNotificationOnceSynchronized with the match-start warnings, the spectators' tally, SetIsRollbackFrame(false),
StepOwnedControllers, UpdateLayersEntityList, RefreshSprites, SynchronizeSfx, SkipLevelLoaderIfNeeded with RestorePauseMenuMusicVolume),
both HandlePausing prefixes. Deleted: xplay.cpp's LevelUpdateHook trampoline and its TF.EX parts (the step, the per-tick ReplayRecorder
null, StepOwnedControllers / RefreshSprites / SynchronizeSfx calls, the hand SkipLevelLoaderIfNeeded), HandlePausingHook, MatchPause.cpp's
hand dispatch of the two prefixes, rnd.cpp's EndOfTickFlush (UpdateLayersEntityList), the gate's hand ShowMatchStartWarnings, and
xplay's own rollback-frame flag (g_rbFrame: IsRollbackFrame now reads TF.EX's NetplayManager._isRollbackFrame, set at the load, cleared by
the postfix). What stays, as Wrap shells (xplay.cpp LevelUpdateShell / HandlePausingShell), S-tools only: the match freeze, the tick log
(VsLogTick, vsstop, VsFlush), rumble per frame, the [sfx] log, statemeasure, the cost timer; Scene.FrameCounter = the GGRS frame until
TFGame.Update / NetplayLogic are generated (steps 2-3); the sync test's no-pause. Rules: TFGamePatch.IsRunningUnsynchronizedUpdate =
false (todo, step 3: the gate never runs the level unsynchronized), ITfStateApi.StepOwnedControllers -> TF.State, and Game.TargetElapsedTime =
round(1e7 / GetSessionFps()) in a session (S-60hz: at 60 Hz the Switch's game keeps its own 170000-tick step, which TF.EX's prefix read
as TimeMult 1.02 and the first run stalled: TF.EX's ManageTimeStep would have set the session's 166667). Translator: a delegate
field made by reflection over a static property setter (AccessTools.PropertySetter + CreateDelegate, also through a helper) is the
setter's call where it's invoked (UnitEmitter SetterInvoke). Checked: smoke rematch PASS (slot B, 60 Hz browser lobby, 11 rollbacks,
REMATCH! voted, GGRS checksum clean); log `[vs] Level.Update frame 1: TF.EX's LevelPatch step: Engine.DeltaTicks 166667, TimeMult
1.0000019 (the session's step 166667: same ...)`. Next: step 2.

What runs a lobby match frame by frame on the Switch, against TF.EX's own loop: what is good, what should become TF.EX's
generated code, and what has to stay ours. Master 5a17ee7f.

## TF.EX's loop (the PC)
- `TFGamePatch.TFGameUpdate_Patch` (TF.EX.Patchs/Engine/TFGame.cs:159), a prefix that replaces `TFGame.Update`:
  - each frame: DrainGameThreadActions, ping measurement, PublishCaptureFlag, ManageTimeStep (fixed step, the session's fps);
  - not synchronized: resets the accumulator; AbortIfSynchronizationFailed; HasFailedInitialConnection takes you back to the
    netplay menu; IsDisconnected runs the game without GGRS;
  - `CanRunNetplayFrames` false: runs the level unsynchronized (`IsRunningUnsynchronizedUpdate`);
  - synchronized: `Poll`, then an accumulator over `fpsDelta` (SLOW_RATIO when frames ahead, catch-up for spectators). Each step:
    EstablishSessionIfSynchronized, `NetplayLogic`, then the frame's own advance and the original `TFGame.Update` (a reverse patch).
- `NetplayLogic` (TFGame.cs:682): the polled input, made neutral while the pause overlay or the match-results input gate says so;
  PushLocalPlayersInputs, AdvanceFrame, ResetPolledInput, VersusMatchResultsInputGate.OnFrameAdvanced, UpdateNetplayRequests.
  Then the requests:
  - Save: SetCurrentFrame, CaptureGameState, SaveGameState, replay record, sync-test record.
  - Load: SetIsRollbackFrame, NotifyRollback, then SetIsUpdating around RestoreGameStateBytes, and the replay / sync-test cleanup.
  - Advance (a resimulation): AdvanceGameState, Scene.FrameCounter, `MInput.Update`, then `level.Update()`.
- `LevelPatch` (TF.EX.Patchs/Scene/Level.cs): the Level ctor drops the ReplayRecorder in netplay.
  - `Update` prefix: skipped while awaiting synchronization; DeltaTime / TimeMult / DeltaTicks from the session's step;
    AddPlayersIndicators.
  - `Update` postfix: Notification.FlushDeferred, ClearWaitingNotificationOnceSynchronized, the spectators' series tally,
    SetIsRollbackFrame(false), StepOwnedControllers, UpdateLayersEntityList, RefreshSprites, SynchronizeSfx,
    SkipLevelLoaderIfNeeded (which also restores the pause menu's music volume).
  - `HandlePausing`: two prefixes (EnsureEveryControllerSlot; the LightPauseMenu).
- `NetplayManager` (generated on the Switch) holds the session's state and talks to ggrs_ffi.

## Ours
- **xplay.cpp's TFGame.Update hook (FrameHook, ~line 1040):** DrainGameThreadActions, the ggrs_ffi pump, menus, MaybeNativeStart,
  SessionTimeStep, then `NetplayGate`.
- **netplay_gate.cpp (534 lines):** `NetplayGate` is TFGameUpdate_Patch's synchronized branch, by hand, and `RbGate` is NetplayLogic,
  by hand. Both run on the generated NetplayManager (Poll, AdvanceFrame, the request list, Establish, Save / LoadGameState). Around
  them sit the rollback seam (CaptureGameState / RestoreGameStateBytes plus a side ring for the loop's bookkeeping) and the [desync],
  [rb] cost, allocwatch and step logs.
- **xplay.cpp's LevelUpdateHook (~line 621):** a hand port of LevelPatch's Update prefix and postfix, plus the scripted-test tick log,
  rumble, vsstop and statemeasure. `HandlePausingHook` calls the generated LevelPausing function.
- **lobbymatch.cpp (281 lines):** StartLobbyMatch, NpStart (NetplayManager.Init), EndLobbyMatch.
- **Rules:** 20 `INetplayManager` and 16 `IGameContext` [service] rules into the hand runtime (tfexgen_services.cpp). Some only forward
  to the generated NetplayManager (svc::GenNM); some keep state of their own (GetNetplayMode / IsServerMode from xplay's lobby state,
  `_synchronizationTimer`, the polled input, the archer store).

## Is it good enough?
The core works. The request handling in RbGate matches TF.EX's step for step, and it has held up: 0 sync-test mismatches at 240 Hz
over 67,000 frames, 0 differing ticks in lobby matches against the browser, rollbacks on the console. The logging is valuable (it
found the hardware desyncs).

It is still hand-written TF.EX, though, and it has already drifted. Each item below is TF.EX behaviour the Switch doesn't have, or
has differently:
1. **Neutral input:** TF.EX's VersusMatchResultsInputGate is generated, but the Switch re-implements it by hand in NpLocalInput ("frame
   N + 61, rollback: approximate, no reset"). TF.EX's gate resets on a rollback (NotifyRollback). The pause-overlay neutral input is
   also by hand.
2. **Missing from NetplayLogic:** `ResetPolledInput` and `VersusMatchResultsInputGate.OnFrameAdvanced` after the advance, and
   `MInput.Update` before each resimulated update.
3. **Missing from LevelPatch:**
   - AddPlayersIndicators (TF.EX's occasional player indicator);
   - the spectators' series tally;
   - restoring the pause menu's music volume when a level loads;
   - the Level ctor's ReplayRecorder rule (we null it every tick instead);
   - HandlePausing's EnsureEveryControllerSlot (we do it once at start).

   ClearWaitingNotificationOnceSynchronized runs from the gate's "Synchronized" event instead of the first synchronized Level.Update.
4. **TFEX-DIFFs that are workarounds, not console facts:**
   - waiting for synchronization: the Level stands still (TF.EX runs it unsynchronized);
   - every remote gone: we end the session (TF.EX keeps running the level without GGRS).
5. **The session rate (done 2026-10-27, tfex/SessionRate.cpp).** TF.EX's rate is the default now: every match runs at 240 Hz
   (`Constants.NETPLAY_FPS`, GGRSConfig.Fps) unless it has to be 60. Per match, right after TF.EX's NetplayManager.UpdatePlayers
   (MapScene.StartSession; the `[service]` rule `INetplayManager.UpdatePlayers` -> `svc::UpdatePlayers`), the rate goes into TF.EX's
   own `GGRSConfig.Fps`, chosen as the browser chooses it (web/Netplay/SwitchLobby.cs `UpdatePlayersPostfix`: any lobby with a
   `Platform == "Switch"` player runs at 60 there, GGRS's peers must agree):
   - a lobby player with `Platform == "Browser"` -> 60 Hz, whatever the setting says;
   - otherwise the OPTIONS screen's **60 HZ MODE** (Switch-only, TFEX-DIFF S-60hz; off by default = 240, on = 60), stored in
     `sd:/xplay/switch.settings.json` (`{"SixtyHzMode":false}`; TF.EX.settings.json is TF.EX's own NetplaySettings and has no rate);
     the entry is glue SettingsUi.CreateSwitchEntries (FortRise's on-off entry: A / Confirm toggles it, on every controller type);
   - hw.txt `hz 60|240` (tests) replaces the toggle, not the browser rule. TF.EX's test mode takes 240 from StartTestMode, or hw.txt hz.
   Log: `[np] session rate N Hz (lobby: <reason>; <n> players: <platforms>)`, `[np] session rate N Hz (test mode: ...)`.
   Everything downstream reads TF.EX's code: `INetplayManager.GetSessionFps` is TF.EX's generated GetSessionFps (the hand
   `svc::SessionFps` and xplay's `g_npFps` are deleted; `xplay::SessionHz` is TF.EX's GetSessionFps), ggrs_ffi's config gets
   `c.Fps`, and glue Ffi.ConfigJson converts InputDelay / MaxInputDelay from TF.EX's 240 Hz ticks to `c.Fps` (rounded up) where they
   reach GGRS, as the browser's TfexFps does on its getters. Not changed: xplay.cpp's SessionTimeStep (ManageTimeStep's hand copy,
   leaves the game's own 60 Hz untouched) and EndOfMatch.SyncInputDelay (xplay's script timing, now at TF.EX's rate).
   Checked (ba3d0b9e, Ryujinx slot subclass): smoke lobby PASS, 0 differing ticks of 597, `[np] session rate 60 Hz (lobby: a browser
   player (PLAYER) ...; 2 players: Switch,Browser)`, `input delay 2 ticks at 60 Hz (... 8 at 240 Hz)` (the first try stalled at load
   31: the harness's 50 s limit); testmode.sh 240 PASS, 0 mismatches to frame 56520. Not run: the OPTIONS entry itself, a 240 Hz lobby.
   **Open:** a lobby of Switches only (or Switch + PC TF.EX) runs at each console's own toggle: two Switches with different 60 HZ MODE
   settings would disagree (TF.EX's lobby has no field to carry it). Keep the default (off) on both, or publish it in a lobby field
   later. A Switch + PC TF.EX + browser lobby can't play (the browser picks 60, TF.EX 240) — as before.

## What moves to TF.EX's code, what stays ours

| piece | becomes TF.EX's (generated) | stays ours, and why |
|---|---|---|
| LevelPatch: Update prefix / postfix, ctor, HandlePausing | the whole class as hooks (on by default) | a `Wrap` shell around Level.Update for the test tooling only (tick log, vsstop, rumble, statemeasure, freeze) |
| NetplayLogic (RbGate) | TF.EX's function, called each frame | the rollback seam's bookkeeping and logs, as rules on CaptureGameState / RestoreGameStateBytes that call TF.State's and log around them; the polled input stays the Switch's controller read (IGameContext.GetPolledInput, S-input) |
| VersusMatchResultsInputGate, LightPauseMenu neutral input | TF.EX's (comes with NetplayLogic) | none |
| TFGameUpdate_Patch: abort / failed connection / disconnected / unsynchronized branches, the accumulator | TF.EX's prefix as the TFGame.Update hook (translator: `[HarmonyReversePatch]` = the hooked method's original) | frame pacing on the console (sleep instead of spin when ahead, or nothing if vsync paces it: to measure), ggrs_ffi's pump (no PC threads), the Switch's menus in the same hook (Wrap) |
| ManageTimeStep | TF.EX's, with the session's fps | EnableFixedTimeStep is absent on the Switch (S-60hz: the game always runs fixed steps) |
| NetplayManager's mode / sync timer / cancellation token rules | the generated fields (now that the start path is TF.EX's NetplayRoundLogic) | the native start (MapScene / StartLobbyMatch until that is TF.EX's), the transport (ggrs-switch / relay) |
| the session rate | 240 Hz, TF.EX's GGRSConfig.Fps / GetSessionFps (done) | 60 for a lobby with a browser player (the browser's rule) or with 60 HZ MODE on (SessionRate.cpp) |
| test tooling (vs logs, scripted inputs, [desync], [rb] cost, allocwatch) | none | ours, kept outside TF.EX's code paths (wrappers and logs) |

## Order (each step lands on, hand code deleted, one emulator run of its flow)
1. **(done 2026-10-27)** **LevelPatch whole, as hooks, with a Wrap for the tooling.** It's self-contained and fills the gaps in item 3. Runs: smoke lobby
   (plus rematch, because the results and pause paths change).
2. **NetplayLogic generated: done (2026-10-27, d2be5179).** netplay_gate.cpp's RbGate calls TF.EX's generated NetplayLogic (unit
   NetplayEntry). The hand request loop is deleted, and so is NpLocalInput's neutral input for the results and the pause overlay; TF.EX's
   own gate resets on a rollback. Save and load reach TF.State through ITfStateApi rules: SetCurrentFrame directly, CaptureGameState and
   RestoreGameStateBytes through xplay's seam (SeamCapture / SeamRestore: the side ring, the [rb] / [desync] logs). Resimulations are
   TF.EX's: AdvanceGameState, MInput.Update and level.Update, with ResimBegin / ResimEnd around them in LevelUpdateShell. A sync-test
   mismatch is TF.EX's (AdvanceFrame resets the test session) and is counted.
   Runs: testmode 240 PASS (0 mismatches to frame 70327, about 70,000 rollbacks); smoke lobby PASS (0 of 591 ticks differ).
3. **TFGameUpdate_Patch as the TFGame.Update hook: done (2026-10-27).** TF.EX's prefix (generated, unit NetplayEntry with hooks) drives
   each frame: DrainGameThreadActions, ManageTimeStep, the failed-connection, disconnected and unsynchronized branches, Poll, the
   accumulator at the session's rate, NetplayLogic, the frame's own advance and the original update (the translator now turns a
   `[HarmonyReversePatch]` into the original under the generated hook).
   - Deleted: NetplayGate (the pacing, the sync wait, the hand Poll + event handling, the "every remote gone" and "remote stopped after the
     results" TFEX-DIFFs), EndOfMatch's GenSyncAbort, SessionTimeStep, the Game.TargetElapsedTime read rule.
   - The Switch's own (xplay.cpp FrameShell, a Wrap around TF.EX's prefix): the ggrs_ffi pump, late installs, the lobby screen,
     MatchEndFrame, BeforeTfexUpdate (the sync wait's start, the session's input delay), statistics.
   - Until TF.EX's services exist (the first menu frame; the game's GC isn't up at boot) the game's own update runs (TFEX-DIFF S-native).
   - FortRise's Engine.EnableFixedTimeStep is a rule (its body on the game's fields): TF.EX's ManageTimeStep turns on the 240 Hz fixed step
     with it. INetplayManager.Poll goes through svc::PollForGate (TF.EX's Poll, then the Switch's event logs). The bookkeeping of the frame's
     own update and of each resimulation hangs off TF.EX's state (HaveFramesToReSimulate) in LevelUpdateShell.
   - GameStepRestore (TFEX-DIFF S-60hz): the Switch game's own 17 ms step back on the main menu, where TF.EX's RestoreTimeStep puts FNA's 1/60 s.
   - Runs: testmode 240 (0 mismatches, full speed, before the TargetElapsedTime rule went: at 240 Hz it read the same value); smoke lobby PASS
     (0 of 404 ticks differ); smoke rematch PASS (60 Hz, one tick per TFGame.Update, the vote and the rematch).
   - Logs: `[np] the session's rate: N Hz (TF.EX's ManageTimeStep: ...)`; `[np] TF.EX's TFGame.Update over the last 600 calls: ...` (own
     advances and resimulations per call: 1 at 60 Hz, ~1 at 240 Hz); `[np] after the match (frame +N): ...`.
4. **The rate decision (item 5): done** (240 by default, 60 for browser lobbies / 60 HZ MODE). Next: one console run at 240 against PC TF.EX or a second Switch.
5. **The rules that only hold state: the flags done (2026-10-27).** TF.EX's capture, rollback-frame and resimulation flags are TF.EX's
   own chain: PublishCaptureFlag (generated, every frame from its prefix) -> ExFlags -> Push -> TF.State's SetDriverFlags -> StateFlags.
   - 10 rules deleted, as well as lobbymatch.cpp's own capture switch at match / test start (capture now comes on with the netplay mode, as in
     TF.EX: in a lobby, not at the match's start) and svc::IsRollbackFrame / ReSimulating.
   - New S-tools no-op: TF.State's desktop entity dump at each test-mode round start (StateFlags.IsTestMode is real now).
   - The lobby harness's default `rngcapture 1` no longer changes lobby runs.
   - Runs: testmode 240 PASS (0 mismatches); smoke lobby PASS (0 of 536 ticks differ).
   - **The netplay mode: done (a6575b54).** NetplayManager's `_netplayMode` is TF.EX's own field, set by its SetRoomAndServerMode /
     SetTestMode / SetSpectatorMode / ResetMode (the generated menus, the scenario launch, CreateMain); the 5 mode rules and svc's mode,
     IsServerMode, IsSpectatorMode and (unused) IsDisconnected are gone. Runs: testmode 240 PASS, smoke lobby PASS (log: mode 4 from TF.EX),
     smoke rematch PASS (161712) after 2 FAILs that turned out to be the results race below, not the mode (the Switch's end of match 1 logs
     the same mode, reset and socket close in the passing and failing runs; one run each way with and without the change).
   - **Found: the end-of-match results race (TF.EX's design, not the mode).** Each side ends the session when its own VersusMatchResults is
     `finished` (TF.EX's HUD.Update postfix -> NetplayManager.Reset). TF.State's LoadState rebuilds VersusMatchResults on every rollback and
     replays only the round results' coroutine, so the results' own tween restarts: each side finishes a fixed time after its last rollback,
     which TF.EX's VersusMatchResultsInputGate (neutral input after the prediction window) keeps rare but not equal on both sides. Seen:
     Switch 5560 / page 5544 (pass), 5585 / 5585 (pass), Switch 4735 / page stuck at 4748 unfinished (fail, twice, both with GGRS
     NetworkInterrupted during the results). When the other side's finish is beyond its prediction window it stalls, waits out GGRS's
     14.5 s disconnect, votes late, and the rematch's session then never synchronizes on the page (no Synchronizing; ICE connected). The
     page's log line "GGRS session reset (relay socket closed)" is its own Reset at its own finish, not a socket event. Open: why the
     late rematch never synchronizes, and whether PC TF.EX sees the same race (the same code: page vs page with a stall in the results).
   - **Page vs page (TF.EX on both sides, tools/pvp/page-rematch-stall.sh): no race; the cause is the Switch's.** The results end 239
     frames after they show, on the same frame on both pages: without a stall (G 2024 -> 2263) and with two 3 s main-thread stalls on one
     page during the results (NetworkInterrupted on both, rollbacks of 11, 11 and 13 frames inside the results; G 2319 -> 2558 on both).
     The rematch synchronized both times. So the theory above (a rollback restarts the results) is wrong for TF.EX: TF.State's rebuild
     of VersusMatchResults keeps its progress there. In Switch lobbies the results last 1817-2667 frames (G 2918 -> 4735 / 5560 / 5585)
     with VersusMatchResults rebuilt on each rollback (its ctor hook: 8+ calls).
   - **Ruled out: our glue's Coroutine_Update** (TFEX-DIFF S-performance, no count while StateFlags.IsRestoring). With TF.State's own
     body generated instead: smoke rematch PASS (165854) but the results still 2918 -> 5585 (2667 frames). Deleted anyway (fewer custom
     differences; testmode 240 PASS at 3.14 ms a call against 3.00: docs/TFEX-UPSTREAM-CANDIDATES.md 1).
   - **The cause: the Switch's test script, not the game code (fixed 2026-10-27).** Seat 0's vs script runs to tick 5200 (GGRS
     ~5463), ~2,500 ticks past the match's end, and the emulator lobby's lag (page ping 187-257 ms, a rollback every 20-30 frames)
     kept TF.EX's VersusMatchResultsInputGate from going neutral (60 frames without a rollback), so the script's presses reached GGRS
     and held both sides' results until shortly after the script ended (5560 / 5585). With NpLocalInput's script idle from the match
     results (xplay.cpp; the controller path is unchanged): smoke rematch PASS (180922), the results 2918 -> 3157 on both sides (239
     frames, as page vs page). TF.EX's own exposure stays: a player who presses through the results on a lagging connection makes
     their end input-dependent, and each side resets on its own finish (also on a predicted frame): an upstream note, not a Switch patch.
   - **Also seen: the page's results are long too in a Switch lobby** (161712: page G 2918 -> 5544, ~48 s), so the long results are in
     the state both sides share, not only in the Switch's restore. Next: what keeps VersusMatchResults from finishing in a Switch
     lobby (its TweenIn trigger on the Switch's game build vs the PC's; the lobby's settings; the per-load CoroutineState), compared
     with page vs page (239 frames).
   - The harness: `LVB_FAILON=<regex>` (lobby-vs-browser.sh) stops a run at once on a known failure; smoke rematch uses
     `lobby match #2 over at frame 0` (about 40 s after the vote instead of the 400 s stall limit).
   - **Left:** IGameContext's store (archers, seats, input lock, polled input: the Switch's controllers).

Other work this touches: the Dreadwood wide desync (paused, docs/BACKLOG-2026-10-25.md item 1) and the hardware desync from about frame 3600
(item 3) both run through this loop. Keep their logs: `[desync]`, `statedump`, and `rnggametrace` through `rnd::DumpDraws`.
Local multiplayer (PushLocalPlayersInputs, #283) is already TF.EX's call in RbGate. The matchmaking session's MatchmakingService.cpp
is not part of this.
