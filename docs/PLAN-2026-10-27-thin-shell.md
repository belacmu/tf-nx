# Plan: a thinner shell around TF.EX, and matchmaking on TF.EX's own server (2026-10-27)

Goal: each TF.EX release comes over by re-running the translator, with as little hand work as possible. Custom code stays only where
the Switch really differs (rollback, 60 Hz, the console's systems), each piece with a one-line reason (AGENTS.md).

## The measure: the update report

`tools/tfex/update.sh <pin> <next tag>` sorts every upstream change into *generated* (free: re-run the translator) or upkeep
(*stale*: a hand port that changed upstream; *override*: a rule whose replaced member changed; *unported*). That split is the number
this plan pushes down, measured on a real release, not on coverage.

Pin (38c7b1e, lmp-v0.22.2) -> v0.27.0, today: 543 generated of 870. Upkeep lands on a few hand pieces:

| hand piece | hit by v0.27.0 | where |
|---|---|---|
| matchmaking transport | `ConnectToServerAndListen` changed (FailSearch on a failed connect / a lost search) | tfex/MatchmakingService.cpp |
| input gates | `XGamePadInputPatch.InterceptConfirm` and the menu getters changed | xplay.cpp MenuConfirmHook / InputMenuConfirmHook, MainMenuPatch.cpp, MapScenePatch.cpp |
| state API | ITfStateApi changed | rollback.cpp, rnd.cpp, the ITfStateApi rules |
| menu states | MenuState gained values | NetplayScreens.hpp (states 59-65 by hand) |
| protocol models | 11 stale, 92 new message / model members | mostly generated once the models translate whole |

Other hand code, by size: net.cpp 1818 (the Switch's sockets / WebRTC: stays), xplay.cpp 1166, MainMenuPatch.cpp 627 (TF.EX's
menu subclasses emulated on vanilla objects: the AOT build has no managed subclasses), MatchmakingService.cpp 516, EndOfMatch.cpp 344,
tfex/NetplayManager.cpp 290, MapScenePatch.cpp 245 (a hand port of v0.21.0's MapScene), InputLock.cpp 202. overrides.toml: 283
[service] rules (173 S-native, mostly game bindings; 23 without a key, 2 todo), 24 [[replace]].

## Track 1: matchmaking

1. **Baseline on TF.EX's server.** Our fork (`tf-ex-matchmaking-server`, branch switch-online) is upstream 980e86c plus four small
   commits: browsers allowed (ALLOWED_ORIGINS), local players per device (#283), harness log lines, a fixed test seed. Run the lobby
   scenarios against it (`LVB_SERVER=real`: lobby, join, quickplay, rematch; one run each) and list what breaks on the client side.
   docs/SERVER-DIFF-2026-10-26.md lists the behaviour differences to expect (quick play up to 4, rematch resets ready, MatchEnded
   from everyone, the connection check from seating).
   - 2026-10-27: **smoke lobby PASS on the fork** (184549: 0 of 542 ticks differ; both sides' PingMeasurement connected to the room).
   - 2026-10-27: **smoke rematch PASS on the fork** (185338: the server's RematchLobby, the results 2918 -> 3157, match 2 synchronized).
     Next: quickplay (lobbies up to 4), join.
   - Hosting: the only hosted server is ours (Cloudflare, wss://<your-server>); the fork has no deployment and no
     remote of its own yet.
2. **Generate the transport's logic.** Today 13 [service] rules (overrides.toml S-net) hand TF.EX's connection members to
   tfex/MatchmakingService.cpp, whose NetThread re-implements ConnectToServerAndListen (it already misses TF.EX's dropped-connection
   handling: the pending action's fail callback, "Connection dropped"). In order, each step switched over with one scenario:
   a. RunOnGameThread / DrainGameThreadActions: TF.EX's own over a runtime ConcurrentQueue<Action>; the Switch's own parts of today's
      DrainGameThreadActions (making the generated service on the first menu, the incoming messages to HandleMessageContents) move to
      the frame prelude (S-native: the game thread runs managed code).
   b. A runtime ClientWebSocket (State, CloseStatus, ConnectAsync, SendAsync, ReceiveAsync over netws and its I/O thread); then Send,
      Close, EnsureConnection and IsConnectedToServer generated.
   c. ConnectToServerAndListen generated: its listen Task.Run becomes a game-thread pump (a translator rule: each frame the loop takes
      the messages that have arrived and returns when ReceiveAsync would wait; the catch runs on a dropped connection). This is where
      v0.27.0's FailSearch lands.
   d. SwitchPingMeasurement / GetRelayedPeersFrom: TF.EX's bodies over the ping-measurement socket (with its bind / STUN fixed).
   **Done 2026-10-27 (a, b, c):** TF.EX's RunOnGameThread / DrainGameThreadActions, Send, Close, EnsureConnection,
   IsConnectedToServer and ConnectToServerAndListen are generated; 13 S-net rules and the hand NetThread (its connect, send queue,
   KeepAlive fast path) are gone. Underneath: a runtime ClientWebSocket over netws (tfexgen_bcl.cpp) and two translator features:
   `throws = true` rules (a stand-in records a fault, gen::Fault; inside a try the statement after the call jumps to the catch, which
   used to be dropped: TF.EX's connection-failure and dropped-connection handling now run) and `yields = true` (a receive with nothing
   yet ends the loop's pass). The listen loop runs on the game thread, one pass a frame (PumpTransport -> GlueFfi.PumpListener):
   **found: the Switch build's managed System.Threading.Thread runs its work on the caller** (the first try froze the game for 2
   minutes inside the loop; so NetplayManager's RunWorker connect also blocks the game thread, up to its 30 s timeout: backlog). TF.EX's
   HandleMessageContents still runs on the game thread (TFEX-DIFF S-net, the runtime's pools). netws reports the server's Close frame
   as a lost connection, so TF.EX sees its catch there instead of MessageType.Close. Runs: smoke lobby PASS (195741 for a; 203713 for
   b+c: 0 of 585 ticks differ); the server stopped mid-match (203937): TF.EX's "Error while listening" catch, the match played on.
   **d, done 2026-10-27:** TF.EX's SwitchPingMeasurement generated (its hand copy and rule gone); GGRSFFI.ping_measurement_start / stop
   are rules onto net.cpp's measurement, which now gets the match sockets' bind address and STUN server (netplay.txt rtcbind / stun:
   before, nothing set them). smoke lobby PASS (205806, 0 of 587): TF.EX's measurement started, connected and stopped at the match.
   GetRelayedPeersFrom generated too (2026-10-27): a list of strings inside a model list (Lobby.RelayedPairs, ICollection<string[]>)
   is a managed string[] as a LINQ element (runtime ModelElem, the C# element type); its hand shim and rule are gone. smoke lobby PASS
   (210820, 0 of 584); the local server relays no pair, so a non-empty RelayedPairs is unexercised (the real server's connection check). TF.EX's relay ping measurement (UDP to the
   relay) isn't on the Switch: a failed status, as TF.EX logs it (step 3 below).
   The original idea, for reference: TF.EX's `ConnectToServerAndListen`, `SwitchPingMeasurement` and `GetRelayedPeersFrom` are
   TF.EX logic over a WebSocket and UDP. Translate them, with the socket calls as the edge (rules onto net.cpp's socket), so changes
   like v0.27.0's FailSearch arrive generated. tfex/MatchmakingService.cpp shrinks to the socket, its thread and the game-thread
   queue. TFEX_VERSION ("0.21.1", sent as ?version=) comes from the vendor pin.
3. **TF.EX's own relay** (UDP, for pairs that can't connect directly). Done 2026-10-27: the Switch's old match transport (net.cpp's
   np block: packet rings, the relay and WebRTC request paths, np_send / np_recv / np_request / np_request_rtc / np_stop and the
   net.txt "np" stage) had no caller since ggrs_ffi's sockets (ffisock) took over: deleted, with tools/switch-vs-browser.sh (the xnet
   match over it, already unable to play); the update report's required check is now `smoke.sh lobby`. Found with it: the ping
   measurement's bind address and STUN server were only ever set by np_request_rtc, so it has run without STUN (to fix with the
   transport, step 2). And TF.EX's relay isn't implemented
   on the Switch (tfex/NetplayManager.cpp ApplyRelay logs "NOT supported, connecting directly"). The real server sends pairs that fail
   the connection check to its relay and kicks a player who can't reach it, so a Switch behind a strict NAT can't play there: TF.EX's
   relay client (its framing lives in ggrs_ffi, so in ggrs-switch) on the Switch's UDP socket.
4. **Hosting the fork** needs a host with UDP (not Workers). That is a decision for you and whoever is setting up the real server;
   deploying needs your OK. Until then the harness runs the fork locally.
5. **Retire our JS matchmaker's game logic** once the Switch and the browser both play on the fork; keep only what the fork can't do
   (TURN for pages, performance reports), per SERVER-DIFF's "What stays ours".

## Track 2: the client's hand copies of TF.EX, by upkeep cost

1. **Input gates: done 2026-10-27.** TF.EX's XGamePadInputPatch was already generated and run as postfixes on the Switch's
   JoyConInput getters (tfex/InputLock.cpp installs them from a live controller's vtable: the class differs, S-input). What remained:
   MapScenePatch's eight MenuInput hooks on the lobby map (Confirm true; Back, Left, Right, Up, Down, Alt false), which TF.EX's own
   postfixes already do (Confirm true, Back / Left / Right false in a netplay lobby's MapScene): deleted (8 trampolines). The ledger
   entry that mapped XGamePadInput.cs to hand code is gone, so the update report counts its changes as generated (v0.27.0: the four
   XGamePadInputPatch changes left "overrides to review"). Kept, with reasons: InputLock's install on JoyConInput, NpPadInput (the
   console's controller for GGRS), xplay's MenuConfirmHook (test runs only); the roll call's hooks are TF.EX's generated ones.
   KeyboardInput.cs stays unported (no PC keyboard). JoyConInput is every Switch controller (Joy-Con single or pair, handheld, Pro
   Controller): PlayerInput.AssignInputs makes six, one per npad slot, and never a KeyboardInput. smoke lobby PASS (212914, 0 of 568; the map screen confirmed after 147 frames).
2. **MapScenePatch.cpp: done 2026-10-27.** TF.EX's MapScenePatch is hooks = true (InitVersusButtons, Begin, GetRandomVersusTower,
   StartSession generated, each body checking for netplay itself); the hand file's four trampolines are Wrap shells on them. HookGen
   gained `beforeOrig_` (a shell's step between TF.EX's prefixes and the original: the Switch's native session start before
   StartSession's original). Kept, with reasons: a WiderSet-wide lobby's tower port (S-wide: the Switch's six-player list, Tower::
   SelectWide), the lobby screen's fade into the map (FinishTransition), the map's logs. Changed: TF.EX's Begin_Postfix now also runs
   on the rematch's MapScene, and StartSession_Postfix in a wide lobby too (before, skipped there): as on the PC. smoke rematch PASS
   (214405: both map screens through the generated hooks). Wide lobby: smoke lobby PASS (215744, 0 of 569; LVB_PAGEWIDE=1 ... "lobbywide
   1"): its map took the Switch's tower path (RANDOM among 17 buttons, SelectWide's tower 5), StartSession ran TF.EX's prefix and postfix.
3. **Menu states: done 2026-10-27.** translate.toml [enums] export emits TF.EX enums hand code reads as gen/tfexgen_enums.hpp
   (gen::en::MenuState::LobbyBrowser ...), and records them in tfexgen_members.txt (their changes count as generated);
   NetplayScreens.hpp's hand copy (59-65, without 61 and 66-67) is a using-directive onto TF.EX's MenuState, MainMenuPatch's raw 61s are
   SeriesLobby. The same values (build check only). v0.27.0's MenuState change left "stale" (unlabelled: stale 13 -> 12).
4. **IGameContext's store** (17 rules: archers, seats, input lock, polled input): TF.EX's GameContext where only the controller read
   is the Switch's.
5. **EndOfMatch.cpp, tfex/NetplayManager.cpp leftovers, InputLock.cpp**: each member either generated or deleted as dead.
   First pass 2026-10-28: deleted what TF.EX's generated code already does: the hand ApplyRelay (and MatchmakingService's
   GetRelayedPeers / GetRelayUrl copies; TF.EX's NetplayManager.Init runs the generated ApplyRelay), the start's reset of
   _hasFailedInitialConnection / _pendingAbortToVersusOptions (TF.EX's Init does it), the wide map screen's InitVersusButtons shell
   (TF.EX's generated prefix / postfix unlock the four towers in a wide lobby too, as on the PC with WiderSet), and EndOfMatch's
   GenEnd / GenAdvanceError (nothing called them). Wide smoke lobby PASS (070840, 0 of 564). Kept, each with its reason in the file:
   EndOfMatch's QuitTeardownFrame (S-start: xplay owns the match's native state), DropMatchSettings (the Switch's PauseMenu ctor inlines
   AddItem), the AssignInputs skip (S-input), the generated NetplayManager's out-of-GC lists, the test vote (harness); NetplayManager.cpp's
   native start (S-start) and the wide tower port (S-wide); InputLock.cpp (S-input: TF.EX's XGamePadInputPatch installed on JoyConInput).
   Open: the Switch stuck in the level after the browser QUIT mid-match (console 2026-10-28, STATUS.md): TF.EX leaves the level running
   while IsDisconnected (TFGame.Update prefix) and the player QUITs from the pause menu; trace why the Switch's pause / QUIT didn't.
6. **MainMenuPatch.cpp's subclass emulation**: the biggest; needs translator support for TF.EX's subclasses of vanilla menu items.
   Last, since it is a translator feature, not a port.
7. **Rule hygiene**: the 23 unkeyed and 2 todo rules get a key and a reason or go.

## Track 3: the v0.27.0 update as the exam

After Track 1.2 and Track 2.1-2.3, take the vendor pin to v0.27.0 (our fork rebased: #283 and the other Switch commits; the 11
translator gaps the report lists). What still needs hand work then goes to the top of Track 2. Afterwards each release: run the
report, note the hand minutes it took, aim for zero.

### Result (2026-10-27/28): the Switch on v0.27.0

The pin is `fbeec67` in vendor/tfex: local branch `lmp-v0.27.0` = v0.27.0 + #283's two commits (e14aa6d, fbeec67), not pushed yet.
The page that tests it is the browser worktree `harness-tfex027` (branch `claude/tfex-v0.27.0`: the same fork commit built from source,
FortRise 5.5.0-beta.4, v0.27.0's minimum; the page's `?statedump` harness commits); tools/lobby-vs-browser.sh defaults to it.
Checked: testmode 240 (300 s, 0 sync-test mismatches), lobby (0 differing ticks of 569, saved bytes identical on every dumped frame
after 0), rematch (both matches, checksum clean), lobby on TF.EX's own server (`LVB_SERVER=real`, 0 of 567).

The hand work, item by item (the update report: `tools/tfex/reports/v0.22.2-v0.27.0.md`):

| what | kind | fix |
|---|---|---|
| ByteStateEvents' primary constructor, `Enumerable.Repeat`, span `SequenceEqual`, `Clone()?.` casts, `x?.ToList()` into a model list, tuple `==`, `bool?` as a string, `.ToArray()` into a state `IEnumerable` | translator gaps | translator cases (one each) |
| JSON doubles (MatchEnded's raw stats) | runtime gap | Json Writer::Double, JRead/JWrite double |
| MenuButtonGuide.icon (SeriesScreen.GuideWidth), Ice's tiles / oldTiles / oldTilesXOffset / oldTilesAlpha | private fields | translate.toml pins, from the disassembly |
| TriggerArrow.primed / normalSprite, JumpThru.counter | private fields | tools/re/statepins.py (the layout database) |
| GameState.MatchStats: nil on the Switch (every frame desynced) | the state glue was v0.22.2's | `translate.sh state-msgpack`; now a step of new-tag.sh |
| Session.MatchStats: the Switch's 6 seats, the PC's 4 | Switch difference | `[[rewrite]]` SeatsToPcView (S-native, as Scores); the new `$e` placeholder |
| ResultsSaveStats.OnlyCountOnce crashed at the results | runtime bug | GameArr reads a jagged game array (`ulong[][]`: GameStats.Awards) as views |
| `?version=0.21.1` (a hand constant; master too, on 0.22.2) | hand code | `TFEX_VERSION` generated from TF.EX's meta.json (gen/tfexgen_version.hpp) |
| the netplay menu's new RANKED button (QUICK PLAY, RANKED, LOBBIES, PRIVATE) | test scripts | the Switch's lobbyui walk and the page scripts: one more Right |
| v0.27's tutorial on a fresh page profile | test scripts | the page's menu key is `x` (SKIP / CONTINUE) |
| ranked screens, player profile, leaderboard, match-stats report, support link | features | to-do rules (noop, key todo / S-native), not ported yet |

Still open from this update:
- TF.EX's tutorial: done 2026-10-28 (TF.EX's module boot generated: docs/STRATEGY-2026-10-28.md F).
- Ranked, the leaderboard, the player profile, support, changelog history: done 2026-10-28, TF.EX's code, walked against the deployed
  server (STRATEGY G); MatchStatsReport: generated in step 6.
- A wide match: done 2026-10-28. The Switch has 6 seats, WiderSet's state 8: unpadded, every wide match desynced from frame 120 (smoke
  070514: 12 desyncs). SeatsToPcView now pads with default MatchStats structs, as the PC's unused seats are: wide smoke lobby PASS (070840).
- The fork branch `lmp-v0.27.0` needs a push (belacmu/TF.EX) before the browser branch can build from GitHub; the browser branch
  `claude/tfex-v0.27.0` (FortRise beta.4) is local.

## How each step is done

- TF.EX's code ON, the hand code it replaces DELETED, one emulator scenario of that flow (AGENTS.md: no off flags, no batches).
- Order inside a track: the piece the latest update report hits first.
- The server track lives in another repository (the fork); it can run as its own session beside the client work.
- Pushing, deploying the fork or the site, and publishing to the console wait for your OK.
