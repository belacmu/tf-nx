# Workstream B: lobby & UI (branch `ws/lobby-ui`)

Goal: TF.EX's NETPLAY entry and host flow on the Switch's main menu, the lobby screen, and the end of a lobby match (back to the
lobby), keeping the scripted lobby comparison identical to the browser. Specs: TFEX-UI-SPEC.md, LOBBY-PROTOCOL.md 4-6/9,
BROWSER-LOBBY-RELAY.md 4.

## Code

| file | TF.EX counterpart | what |
|---|---|---|
| `tfex/Game.hpp` | (vanilla classes) | offsets, static slots and method bodies of the vanilla Switch objects the port uses |
| `tfex/MainMenuPatch.cpp/.hpp` | `MainMenuPatch`, `RollCallElementPatch`, `ReadyBannerPatch`, `NetplayButton`, `CompactCoOpButton`, `VersusModeExtensions` | main menu layout, NETPLAY -> host, lobby screen, start, back to the lobby after a match, the `lobbyui` test driver |
| `xplay.cpp` | `VersusMatchResultsInputGate`, `HUDPatch` (end), `NetplayManager.Reset` | neutral input after the results, end-of-match detection, per-match reset (`EndLobbyMatch`) |

TF.EX subclasses vanilla menu items; the Switch build is AOT, so each TF.EX component is a vanilla object of the closest class, set
up as TF.EX's constructor would, with TF.EX's overrides hooked and dispatched on the instance:
- **NetplayButton** = an `ArchivesButton` (MainModeButton with one Image, drawn with DrawOutline, like TF.EX's) created at (220, 140)
  from (480, 120), title `NETPLAY`, players text `ONLINE MATCHS` (sic), icon swapped with `Image.SwapSubtexture`. TF.EX's own icon
  (`assets/tfex-netplay-icon.png`) is not in the Switch menu atlas; the stand-in is `browseWorkshopButton` (TF.EX's LOBBIES icon).
  Adding the real icon needs a romfs `menuAtlas` change (coordinator / SD card).
  `ArchivesButton.MenuAction` (vtable 0xb0) is hooked from the live object and runs TF.EX's NetplayButton.MenuAction for this instance.
- **CompactCoOpButton** = the vanilla `CoOpButton`, moved to (290, 180) (tweenTo too, tweenFrom (480, 180)), players text "".
  (TF.EX removes the vanilla one and adds its own; keeping the object means co-op's vanilla MenuAction and "return from co-op selects
  it" work unchanged.)
- y = 210 row (Trials (200, 210), Archives (270, 210)) moved to y 220 (tweenTo too). Links as TF.EX (TFEX-UI-SPEC 2.1 step 5,
  no Workshop on the Switch).

## Flow (v1)

Main --NETPLAY--> (TF.EX: NetplaySelect -> LOBBIES -> LobbyBuilder START) the Switch does LobbyBuilder START with the builder's
defaults at once (public Standard lobby, 2 players, mode LMS, map 0, MatchLength 2 unless `lobbylength`), `CreateLobby`, and shows
the lobby screen = vanilla roll call (MenuState 5) immediately; all seats are inert until the server answered.

Lobby screen (RollCallElementPatch): seat = lobby `Seat`. Local seat: vanilla join (A) -> `UpdatePlayer(Ready, archer, alt)`;
vanilla leave (B while joined) -> `UpdatePlayer(Ready false)`; B while not joined -> vanilla goes to Main, plus `LeaveLobby`.
Confirm/Start while joined never starts vanilla versus: when `CanHostStart` it sends `StartLobbyChoice` (RequestStart).
Foreign seats follow the lobby: ready -> portrait joined with the lobby's archer (`SetCharacter` + `Join`), else left.
ReadyBanner: shown when `CanHostStart || IsLobbyReady`.
`StartLobby` -> `NetplayManagerStartLobbyMatch` (as before: native start, relay, GGRS).

End of match (BROWSER-LOBBY-RELAY 4, TF.EX HUDPatch / VersusMatchResultsInputGate):
- `Session.CreateResults` in frame N with a winner (`Session.GetWinner` >= 0; it also runs at every round end, those are round
  results) (hooked; also keeps the Session for the winner and scores).
- Neutral local input from the gate whose current frame is N + 61 (the browser sees the results at the gate right after frame N and
  feeds neutral input once it has counted more than 60 advances: lobby-inst2 made 2319, neutral 2380; the Switch logs the same).
- The browser resets its GGRS session when its results finish (~N + 239). Our relay socket stays open (the server doesn't close it),
  so the end is GGRS's: interrupted (500 ms without input) and not resumed for 1 s more, or disconnected, or the relay closed. Then:
  Level frozen (no Level.Update until the scene changes), `MatchEnded {winner_seat, scores}` + `ArcherSelectChoice`.
- `ArcherSelectLobby` (vote or 33 s) -> TF.EX's ReturnToMenu(Rollcall): `xplay::EndLobbyMatch()` (GGRS session freed, all per-match
  state reset, so the next StartLobby starts afresh), Players cleared, `Engine.Scene = new MainMenu(5)`; the lobby is still held, our
  seat unjoined (the server un-readied everyone).
- The per-tick comparison log covers the first match only.

Switch facts found on the way (Game.hpp has the full list): MainMenu `state` +0xb4, `switchTo` +0xc0 (set_State writes it),
`BackState` +0x9c, `ToStartSelected` +0xc8, `NoGamepadUpdates` static 0x4794a00; MainModeButton vtable a8 BaseScale, b0..d8 the
Image getters/setters, e0 MenuAction, e8 Rotate, f0 AddYMult, f8 BaseTextScale, 100 PlaySound (SWITCH-UI-RE.md's "MenuAction 0xb0"
is wrong: 0xb0 is get_ImageScale); ArchivesButton.MenuAction 0x97cd40 (State = 11); Component Visible +0x22; OutlineText factory
0x941890; `new MainMenu(state)` 0xa9e890, Engine.set_Scene 0x92cc60. On the Switch the joined roll call starts versus with MenuStart
(PlayerInput slot 0x50, "+"), and Back from an unjoined seat goes to VersusOptions (7), not Main. Vanilla gamepad discovery in the roll
call un-joins seats without a local controller, hence NoGamepadUpdates while a lobby is held (as TF.EX).

## Debug options (hw.txt)

- `lobbyui 1`: drives the UI like a player through the controller hook: title A, Right + A (NETPLAY), A (ready), A (start when the
  host can), after the match B (leave) -> main menu.
- `lobbylength <0..3>`: MatchLength of the hosted lobby (0 = Instant, goal 1: a full scripted match ends at T 2292).
- `enddebug 1`: logs scene/relay/GGRS state changes around the end of a lobby match.

## Tooling fixes

- `tools/run-probe.sh` located `ryujinx-activate.sh` relative to the checkout's parent; in a worktree that path is wrong and the run
  silently used combined-mod. Now from the main checkout (`git rev-parse --git-common-dir`).
- `tools/rebaseline-save.sh` failed in a fresh checkout (no `.save-backup` yet).

## Results (Ryujinx + one headless browser page, local tfex-server; `lobbyhost 0, lobbyui 1, lobbyscript 1, npdelay 2, lobbypred 0, lobbylength 0`)

Run ui5 (10:50): main menu built (NETPLAY, compact co-op, row at 220); A on NETPLAY -> CreateLobby -> lobby screen; A -> ready;
browser joins (seat 1 shown joined with its archer); host A -> StartLobbyChoice -> StartLobby -> match. **Per-tick comparison with
the browser: 0 differing ticks of 2557 (T -265..2292).** Results made in frame 2319, neutral input from 2380 (= the browser's).
Browser stopped at 2559 -> MatchEnded (winner 0, 1-0) + ArcherSelectChoice -> ArcherSelectLobby -> back on the lobby screen ->
B -> LeaveLobby (it then went to VersusOptions: fixed to Main, run ui6).

Run ui6 / ui7 (11:00 / 11:45): the whole cycle, ending "back on the main menu"; 0 differing ticks (2552 / 2261 compared). ui7 after
the round-results fix: match results made in 2024, neutral from 2085, remote stopped at 2262, winner 1 (0-1), back to the lobby,
B -> main menu.

Regression, the default harness path (`lobbyhost 1, lobbypred 8, vsstop 4380`, Standard length, rollback): def1 (before the
round-results fix) 1 differing tick of 4378 = T 2153, the known round-change logging artefact (ROLLBACK.md); but its round-end
CreateResults (frame 2299) had wrongly started the neutral-input gate (invisible in the comparison: both sides play the same
confirmed inputs). Fixed; def2 classifies it as round results, 0 differing ticks of 2115 until Ryujinx crashed on the host
(SIGSEGV in AppKit's Signaler, Ryujinx-2026-10-15-113702.ips: a host crash, not the game).

After rebasing on master fdd1cd4 (12:00-12:40):
- def5 (default path, lockstep): 1 differing tick of 2998 (T 1858, round change).
- def6 (default path, rollback `lobbypred 8`): 2 differing ticks of 4377 (round-change frames, as ROLLBACK.md). Before the rebase
  the same run had 174 differing ticks from T 2563: master's rollback fixes, not this branch. (A master-only reference run, mref1,
  was clean for 2417 ticks and then died in "[rb] collecting at frame 2419": workstream A's.)
- ui8 (the whole UI cycle, lockstep): 0 differing ticks of 2257; match over, back to the lobby, B, main menu.

Not done / known gaps:
- No NetplaySelect / LobbyBrowser / LobbyBuilder screens yet (NETPLAY hosts with the builder's defaults); joining a browser lobby
  from the Switch has no UI (the roll call patches handle the joiner's seat, untested).
- The status label (CREATING LOBBY / WAITING FOR PLAYERS on the open seat) and the button styles are not visually checked: there is
  no way to see the hidden Ryujinx window.
- Rollback lobby matches (lobbypred > 0): the results gate isn't reset on rollback (TF.EX does); EndLobbyMatch resets the GC
  setting but not the rollback buffers.
- Rematch is never voted (v1: ARCHER SELECT only).

## Round 2 (after the merge, on master 9f11fd8+)

### Real-controller lobby matches
- `lobbyscript 0`: the local input fed to GGRS is the controller's (`PlayerInputs[...].GetState()`, `NpPadInput`). A joiner (handle 1)
  reads the first local controller when its own slot has none.
- `padtest 1` (debug): during a match the npad hook plays a fixed pattern on controller 1 (stick still/right/left every 1.5 s, A every
  70 ticks, X every 211), so hidden runs exercise the real input path. Run pad2: `lobbyscript 0, padtest 1, lobbyui 1, Instant`:
  input "controller", the Switch's archer walks left/right, **0 differing ticks of 2445** against the browser.

### Joining a browser-hosted lobby
- `VariantsExtensions::ApplyVariants` (tfex/NetplayManager.cpp; TF.EX's MatchVariantsExtensions.ApplyVariants): at every lobby match
  start (host and joiner) DisableAll, then enable each `GameData.Variants` title. Switch layout: MatchSettings.Variants +0x40,
  MatchVariants.Variants (Variant[]) +0x2f8, Variant.set_Value +0xb9c750 (value +0x58, per-player +0x60). The Title is matched
  among the Variant's string fields (pointers into its own heap region; reading a packed-bool field as a pointer crashed, run join2).
  Titles come from the field names on both sides; the browser's default ["NO AUTOBALANCE","SYMMETRICAL TREASURE"] are found.
- `lobbyjoin 1` (until the LobbyBrowser screen exists): NETPLAY joins instead of hosting: GetLobbies every 3 s (TF.EX's refresh
  cooldown) until a lobby passes the checks (Standard, not in game, no series, 2-player with 1 player, LMS, map 0 = Sacred Ground (the
  v1 tower limit), not WiderSet-wide, all variants known), JoinLobby, publish ourselves at once (Platform Switch). The roll call then
  runs with our seat (1); the local seat's element always takes the first local controller (another controller object in that slot,
  e.g. Ryujinx's idle second one, never confirms).
- Run join8: browser hosts (LOBBIES, create, Sacred Ground, Instant, ready, start), Switch joins seat 1, readies; StartLobby, variants
  2 of 2 on, handle 1; **0 differing ticks of 2262**; results 2024, neutral from 2085, end at 2265, back to the lobby, B, main menu.
- Ryujinx sometimes "exited by itself" during the long join-mode menu phase: Apple's Metal driver aborting it while compiling a shader
  (found by the coordinator with /usr/bin/log); rerun.

### Results gate and rollback
The gate's frame is reset when a rollback rewinds to before the frame that made the results (TF.EX's NotifyRollback): a resimulated
frame f is labelled f + 1, so results made in frame N are redone iff N > the restored frame.

### Still open
- LobbyBrowser / NetplaySelect / LobbyBuilder screens (the join is a debug switch; no list UI).
- Towers other than Sacred Ground (MapId != 0 lobbies are refused; random tower needs the seeded pick).
