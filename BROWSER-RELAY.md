# Browser side of the Switch-vs-browser match: the relay transport and the `xnet` harness

Status (2026-10-14): **works browser-to-browser** through the local Node relay, including with simulated latency, jitter and loss. Not yet tried against the
Switch. Everything is in the browser repo's worktree, branch `exp/xplay-versus` (commit `a355c15`; `browser-60hz/xplay-versus-net.diff` here is that commit
as a diff on the previous head `8435d2d`). Files:

| what | where |
|---|---|
| relay transport | browser repo `netplay/tfnet.js` (`TFNet.openRelay`), no Rust change |
| harness | `web/Netplay/XplayNet.cs` (command `xnet`), hooks in `web/Netplay/XplayVersus.cs`, `web/BrowserHost.cs` |
| Node relay with network simulation | `tools/relay-server.mjs` (here; backward compatible) |
| run script, compare script, driver | `browser-60hz/xnet-tools/` (`xnet-run.sh`, `cmp.py`, `driver-quiet.mjs`) |
| logs of the runs below, a wire dump | `browser-60hz/xnet-evidence/` |

## 1. The relay transport (browser)

**Selection.** A room URL whose path ends in `/relay/<room>` is handled by the relay transport; any other URL goes the old way (matchbox signaling plus
WebRTC), which is untouched and still the default (checked: `xnet ws://localhost:19999/room` still goes through the old code: `/turn` fetch, "couldn't connect").

```
ws://host:port/relay/<room>?peer=<our uuid>&remote=<the other peer's uuid>[&hello=1]        wss:// for Cloudflare
```

- `peer`: our `PeerId`. It must be a UUID (the Worker checks `^[0-9a-f-]{8,64}$`, `tfnet_id` reports it). The relay logs it, nothing else uses it.
- `remote`: the `PeerId` ggrs-ffi has for the other seat (TF.EX's lobby gives it in `GGRSConfig.Netplay.Players`). The relay does not say who sent a
  frame, so the transport attributes **every** frame to this id. The Switch side does not need this parameter (it is ignored by both relays).
- `hello=1`: the browser sends the 5-byte binary message `HELLO` every 500 ms until the first real frame from the other side arrives. Off by default.
- Room name: `[A-Za-z0-9_-]{1,64}` (Worker). Two peers per room; a third peer's frames would be fed to ggrs as the other peer's (and a malformed one
  would panic ggrs-ffi's `bincode::deserialize(..).unwrap()`, which aborts the wasm), so never put anyone else in the room.

**Framing: none.** One WebSocket **binary** message = one `bincode::serialize(&ggrs::Message)`, exactly the bytes ggrs-ffi gives matchbox's channel (section 3).
Text messages are ignored. A binary message of exactly the 5 bytes `HELLO` (`48 45 4c 4c 4f`) is dropped and never reaches ggrs.

**Peer identity / connection.** The other peer is reported to ggrs-ffi as `Connected` **as soon as our WebSocket is open**, whether or not it has joined yet.
(The "connected after its first frame" variant can't work: `Netplay::init_p2p` waits for the remote peer to be connected *before* it starts the GGRS session, and the
session is what sends the first frame.) Frames sent before the other side joined are lost; GGRS's synchronization handshake retries every 200 ms (5 round trips, then
the `Synchronized` event). When the WebSocket closes the peer is reported `Disconnected` and the socket state becomes "disconnected" (TF.EX then ends the match). A peer
that goes quiet without closing is dropped by GGRS after its 15 s timeout (a `NetworkInterrupted` event after 500 ms of silence, `NetworkResumed` if it comes back).

**The "don't call into the page when nothing changed" optimisation** works the same: the same two counters in wasm memory (`signal[0]` bumped per received
frame, `signal[1]` for peer events and state). Measured over a 1,500-tick run: 4,174 frames received, 3,471 `tfnet_recv_all` calls into the page (only when a frame had arrived),
3 `tfnet_next_event`, 3 `tfnet_state`, 1 `tfnet_id`; sends go through the asynchronous `tfnet_send` (4,189). `?netdelay=`/`?netjitter=` (page-side receive delay) also apply.
Page-side counters: `globalThis.tfnetRelayStats` = `{rx, tx, rxBytes, txBytes, dropped, hello, junk}`, `globalThis.tfnetCalls`.

**Limits to know.** The Node relay accepts messages up to 64 KB. The Cloudflare `relay-worker/worker.js` accepts 1 KB per message and closes a connection that sends
more than 150 messages/s on average (burst 300): a TF.EX peer sends about **2 messages per frame (an `Input` and an `InputAck`) plus 5+5 per second, ~125 messages/s at 60 fps** (measured:
9,991 messages in 81 s from each browser, average 27.5 bytes), which is close to the Worker's limit; raise `RATE` there before using it.
Messages grow with loss and rollbacks (average 54 bytes at 190 ms round trip and 3 % loss).

## 2. What the Switch must send and receive

Join the room (`ws://host:port/relay/<room>?peer=<its own uuid>`), then exchange one `bincode(ggrs::Message)` per binary WebSocket message, nothing else
(no text, no extra header). Use the same GGRS fork (`Fcornaire/ggrs`, commit `12a2d47...`) and the same `Config` (`Input` = the 64-byte struct below, `Address` = anything
that identifies the one peer), so serialization is the library's. The wire layout, for checking (bincode 1.x defaults: little endian, fixed-width integers, `u64` length
prefix for sequences, `u32` enum tag, one byte per `bool`); verified by decoding a dump of a real match (`browser-60hz/xnet-evidence/relay-wire-dump-first40-per-peer.txt`):

```
Message      { magic: u16, body: MessageBody }                       magic: random per session; after the handshake ggrs ignores other magics
MessageBody  u32 tag, then:
  0 SyncRequest   { random_request: u32 }
  1 SyncReply     { random_reply: u32 }
  2 Input         { peer_connect_status: Vec<{disconnected: bool, last_frame: i32}>  (must have 2 entries, else the packet is discarded),
                    disconnect_requested: bool, start_frame: i32, ack_frame: i32, bytes: Vec<u8> }
  3 InputAck      { ack_frame: i32 }
  4 QualityReport { frame_advantage: i16, ping: u128 }              ping = milliseconds since the Unix epoch
  5 QualityReply  { pong: u128 }
  6 ChecksumReport{ checksum: u128, frame: i32 }
  7 KeepAlive
Input.bytes: delta (xor with the previous input) + RLE (bitfield_rle) of the pending inputs, each length-prefixed; one input = bincode of TF.EX's Rust `Input`:
  11 x i32 (jump_check jump_pressed shoot_check shoot_pressed alt_shoot_check alt_shoot_pressed dodge_check dodge_pressed arrow_pressed move_x move_y),
  aim_axis {f32,f32}, aim_right_axis {f32,f32}, disconnected i32            = 64 bytes
```

What a peer must do to be accepted:

1. **Answer `SyncRequest`s and send its own** (5 correct `SyncReply` round trips each way). After that, packets with a different `magic` are ignored, so a Switch that
   restarts its session must also restart the browser's (`xnet` again): the old session's magic is remembered.
2. **Inputs for 2 players**: `peer_connect_status` has exactly 2 entries; each `Input` message carries one 64-byte input per remote handle (one here).
3. Both sides use **seat 0 / seat 1** for the two handles, in the order of the room's peer list (the Switch is whichever seat the browser was not started as); the local player must be added with
   the other seat's handle. (`local_player_handle`/remote handle are not on the wire, they only have to be consistent.)
4. **No checksums needed.** The browser has desync detection on (interval 120 frames) and sends a `ChecksumReport` every 120 frames. It only compares checksums it *received*, and a
   peer that never sends one gives it nothing to compare: no `DesyncDetected`, no abort (verified by reading `compare_local_checksums_against_peers`; the logs of the runs below
   show none, as both sides sent checksums there, so the Switch case itself is **not yet run**). The Switch's GGRS must tolerate receiving them: with desync detection **off**,
   `on_checksum_report` has a `debug_assert!(false)` (a debug build of the staticlib would panic on the first one); a release build ignores it. Safest: build release, or
   set `DesyncDetection::On { interval: 120 }` on the Switch too (then it stores up to 32 reports and never has a local checksum to compare, since lockstep never saves).
5. **Lockstep (`max_prediction_window` 0) is fine for the browser** (a predicting peer rolls back whenever the Switch's real input differs from its prediction; the Mac test in
   `PLAN.md` showed the same). Notes from reading the fork: with prediction 0 the receiver keeps only the latest received frame for decoding (`recv_inputs.retain(k >= last - 2*max_prediction)`),
   so an `Input` message whose `start_frame - 1` is older than what it already has cannot be decoded and is silently dropped until the ack makes the sender restart from the right
   frame: harmless on an in-order WebSocket, but it is why loss costs the Switch a stall rather than a rollback. The browser's own input delay is 2 frames by default; the Switch's has to cover the round trip.
6. Keep sending: `Input`+`InputAck` every frame, a `QualityReport` every 200 ms, a `KeepAlive` every 200 ms when idle. 500 ms of silence = `NetworkInterrupted` on the browser, 15 s = disconnect.

## 3. Session parameters TF.EX uses (`Netplay::base_session_builder`, at 60 fps as forced by `tfexfps=60`)

| parameter | value (60 fps) | on the wire? |
|---|---|---|
| input delay | `GGRSConfig.InputDelay` = `NetplayPreferences.InputDelayFrames` = `ToFrames(20 ms)` = **2 frames** (the 240 Hz build gives 5); `xnet delay=D` overrides | no (applies to the local player's input only; the two peers may differ, `delay=2` vs `delay=6` was tested) |
| max input delay | `ToFrames(332 ms)` = 20 frames (size of the input queue) | no |
| max prediction window | `15 * fps / 60` = **15** frames | no |
| max rollback window | `45 * fps / 60` = 45 | no |
| fps | 60 | no (time sync uses it locally; `frame_advantage` in `QualityReport` is in frames) |
| disconnect timeout | 15 s (`NetworkInterrupted` notification starts at the builder default, 500 ms) | no |
| max frames behind / catch-up speed | 50 / 4 | no |
| desync detection | `On { interval: 120 * fps / 60 = 120 }` | `ChecksumReport` messages at frames 120, 240, ... (checksum = fletcher16 of the bincode'd saved state, widened to u128) |
| players | 2 (`peer_connect_status` has 2 entries) | yes, implicitly |
| connection timeout | 30 s waiting for the remote peer to be connected, before the session starts | no |
| late spectators | only when `IsHost` (xnet `host=1`; default off) | no |

So **none of the numbers are in the messages**: the Switch mirrors only what it needs for its own behaviour (2 players, same `Input` type; its own delay and window). The one
TF.EX behaviour tied to the peer: when the browser is "ahead" (`frames_ahead() > 0`, from the quality reports) its frame driver stretches the tick period by 10 %.

## 4. The match, the input timing and the log (what the Switch must reproduce to compare)

`xnet` runs the same match as `test lms 0 0 42 2 #NONE# 2` (TEST5-MATCH-SETUP.md): Last Man Standing, Standard length, `PlayerGoals` 2, tower 0 (Sacred Ground), no variants, seed 42,
2 players with archers = seat index, `ShowControls` off, `MenuConfirm` true, `OneFrameDelay` true, session at 60 Hz. The `[xplay2setup]` dump is identical to the sync-test run's
(rng after `OnLevelLoadFinish` `68393fba09e21e52`, after `StartRound` `69710fca3ad62567`, level order, `GoalScore=2`, `RandomLevelSeed=42`).

**Local input** (script format and semantics as in TEST5-MATCH-SETUP.md 9.2): the input GGRS applies in the `AdvanceFrame` whose `Level.Update` sees `netplay_current_frame() == G`
(1-based count of `AdvanceFrame` requests = of `Level.Update` calls, since the counter advances only in a Level scene) is script step **T = G - 266** (tick 0 = G 266, measured: it is
detected and warned about if different). With input delay D, the input handed to GGRS when `netplay_current_frame() == c` (that is `session.current_frame()`, before the call to
`advance_frame`) is for G = c + D + 1, so the script at T = c + D + 1 - 266, `pressed` = flag(T) and not flag(T-1), neutral for T < 0. A pure function of the frame: rollbacks and unequal delays change nothing.
On the Switch with lockstep: the same formula with its own D.

**Log lines** (to stdout, console of the page; the same as `xplay2`, so `tools/compare-vs.py` reads them):
`[xplay2setup] ...` (once), `[xplay2pre] T <-265..-1> rng=.. p0 .. p1 ..`, `[xplay2] cfg ...`, `[xplay2] origin ggrs_frame=266`, then per tick
`[xplay2] T <t> rng=<16 hex> p0 x y sx sy alive=1 p1 ...`, `[xplay2] R <t> arrows=.. q0=.. q1=.. list=..`, `[xplay2] U <t> seat=<s> round=<r>`, `[xplay2] A <t> round=.. [round_start|round_ending] scores=.. arrows=.. sumx=.. sumy=.. quivers=.. timemult=.. resim=<n>`
(`resim=` is the number of repeated `Level.Update` calls so far: not comparable to the reference). **A tick is printed only once its last simulation is known**: with rollbacks the first time the game
reaches a frame is usually with a *predicted* remote input, so every `Level.Update`'s state is kept per frame (a later simulation replaces it) and printed once the frame is 24 frames behind the newest, past the
prediction window (15), or after 4 s without any frame (the match is over). `[xnet] t=<ms> ...` lines: `cfg`, the GGRS events (`Synchronizing/Synchronized/NetworkInterrupted/Disconnected/DesyncDetected`, with timestamps; the
Rust side logs the same events itself), the first 5 rollbacks, a `stats` line every 300 ticks (ping, send queue, frame advantage, rollbacks so far, frames resimulated, deepest rollback) and a final summary.

**Difference to the sync-test reference.** The reference (`test-scripts/versus/browser-reference-log*.txt`) has **no tick T=2153**: at the round change the sync test consumed one GGRS frame without a
`Level.Update`, so round 1's first update is T=2154 there and T=2153 in a real P2P run (contiguous frames); every other tick compares identical by T index (the intro ends at T=2321 in both:
168 updates instead of 167). That is the "round 1's intro is one tick longer" difference the Switch needed `vsshift 2155 1` for: **a Switch with the natural intro length matches the P2P log without a shift**
(`compare-vs.py` compares by tick index and ignores a tick present on one side only; the `A` line of the round start is at T=2153 here, 2154 in the reference).

## 5. Running the harness

```
# 0. in the worktree <WT> = browser repo, branch exp/xplay-versus:  tools/build.sh      (30 s; tools/build-netplay.sh is only needed if the Rust side changes: it does not)
# 1. the page:   python3 tools/serve.py 18080                  (from <WT>)
# 2. the relay:  node tools/relay-server.mjs --port 18800 [--delay 30 --jitter 10 --loss 0.01 [--reorder]] [--dump 40]     (this repo)
# 3. a browser per seat (any Chrome; headless: tools/netplay-driver.mjs in <WT>), URL (the Switch is the other seat, uuids are yours):
http://localhost:18080/?mute&nointro&autoplay&mods=TF.EX&fortrise&tfexfps=60&command=xnet ws://localhost:18800/relay/ROOM 0 UUID0 UUID1 script=<script0 string> seed=42 maxticks=4400
#   xnet <roomUrl> <seat 0|1> <localPeerUuid> <remotePeerUuid> [script=<path or inline>] [seed=42] [maxticks=N] [fps=60] [delay=D] [origin=266] [host=1] [hello=1]
#        [synctimeout=120] [margin=24] [replay=off]       (the command string is %-encoded in the URL; the page needs TF.EX and ?tfexfps=60)
```
One command for two local browsers (two fresh Chrome profiles, deleted afterwards; starts and stops the page server and relay):
`browser-60hz/xnet-tools/xnet-run.sh <label> <maxticks> [relay options] -- [xnet options]` (env `X0OPTS`/`X1OPTS` for one seat, `RELAY_SERVER`, `OUT`), then
`python3 browser-60hz/xnet-tools/cmp.py out/<label>-0.txt out/<label>-1.txt test-scripts/versus/browser-reference-log-arrows.txt` (compares T/R/U/A lines tick by tick).
Against the Switch: start the page with `xnet ... <switch-uuid>` as `remotePeerUuid`, point the Switch at the same room; the browser waits up to `synctimeout` (120 s) for GGRS to synchronise, then starts
the match. The match ends at T=4016 (Level.Update 4282), the log then flushes the last 24 ticks 4 s later (`stopped logging after 4324 ticks`).

**What `xnet` does instead of the lobby** (TF.EX code paths bypassed): the lobby/matchmaking server and `Lobby` state (room, `Players`/seats from the server, `SeriesInputDelay`, ready-up, `RequestStart`);
roll call, archer select and `ArcherService.ApplyToGame` (here `Characters = 0,1,2,3`, archers = seats); `MapScene.StartSession` patches (`NetplayOptions.Apply`, `UpdatePlayers`, `RestoreArchersFromLobby`, `CompactSeatsToHandles`, `ApplyTeamsToMatchSettings`)
and the TF.EX netplay `Modes` value with `NetplayRoundLogic` (we run plain `LastManStanding`, the logic the reference used); `NetplayRoundLogic.OnLevelLoadFinish -> NetplayManager.Init` and its
`EstablishSessionIfSynchronized -> OnSessionEstablished -> SpawnFirstRound` (which would spawn the archers a second time, so `_pendingSessionRoundLogic` is cleared); the 30 s `AbortIfSynchronizationFailed` (replaced by `synctimeout`).
What it calls: `NetplayManager.SetRoomAndServerMode(url, host)`, `GGRSConfig.Netplay.{Players, LocalPeerId, NumPlayers}`, `SetLocalSeat`, `StateApi.SetSeed`, `ReplayService.Initialize`, then **`TFCommands.StartGame(LastManStanding, map 0, startLevel 0, 2 players)`** (what `test` runs) which does
`Init -> native init_p2p` (a real P2PSession: relay `WebRtcChannel`, 2 players, the parameters of section 3), with `Session.StartGame` held back (Harmony prefix) until `IsSynchronized()` on this side, as the lobby flow leaves the level frozen until then.
Four deliberate deviations make it equal to the reference rather than to a lobby match: `ExFlags.IsTestMode` is forced true (so the rules test mode changes stay: `PlayerGoals = 2`, `RandomLevelSeed = seed`, unlock neutralisation `LocalSettingsGuard`, ...; the other
effects of that flag are rendering/sound sync), the seats' `PlayerInputs` are `KeyboardInput` (TF.EX's `GetState` postfix replaces their state with the GGRS input; a `FakeController` would give neutral input outside test mode), the vanilla kill-cam `ReplayRecorder` is dropped as TF.EX's `Level` constructor patch
does for its own modes (without it a rollback near a round's end made `Session.EndRound` throw), and the `TFGame.Update` loop is TF.EX's own (accumulator, GGRS requests, 10 % slow-down when ahead, `Poll` per frame).
Not exercised: TF.EX's own start path with `NetplayRoundLogic` and the lobby seat/handle mapping; a real controller; spectators.

## 6. Results (two headless Chromes on this Mac, local Node relay, seed 42, the `script0/1.txt` pair)

All runs: both browsers synchronise within ~0.2-1 s and play on; **both logs are identical to each other (every T, R, U, A line, about 4,590 lines of T for the full match) and identical to
`browser-reference-log-arrows.txt` for all overlapping ticks**, apart from the T=2153 line described in section 4. No desync, disconnect or error.

| run (relay options, per direction) | ticks | rollbacks seat 0 / seat 1 (frames resimulated, deepest) | relay |
|---|---|---|---|
| `base`: none | full match, to T=4324 (both rounds, match end) | 0 / 0 | 19,979 messages, 0 dropped |
| `lat`: 30 ms + 0-10 ms jitter (in order), 1 % loss | 2,500 | 156 (254, 3) / 41 (60, 4) | 131 dropped of 12,357 |
| `fulllat`: same | full match, to T=4324 | 63 (89, 2) / 313 (487, 3) | 177 dropped of 19,900 |
| `latro`: same + `--reorder` (each message its own delay, may overtake) | 2,500 | 86 (104, 2) / 129 (265, 4) | 187 dropped of 19,899 |
| `harsh`: 80 ms + 0-30 ms, 3 % loss (round trip ~190 ms) | 2,500 | 198 (824, 6) / 196 (909, 7) | 357 dropped of 12,415 |
| `hellodelay`: 15 ms + 0-5 ms, `hello=1`, **input delay 2 on seat 0 and 6 on seat 1** | 1,500 | 0 / 0 | 0 dropped (5 HELLO frames filtered) |

(GGRS `ping` stat ~72-79 ms in the `lat` runs. Rollbacks differ per seat because they depend on who changes input when. Idle runs have 0 rollbacks: the remote input arrives before it is needed
at 2 frames of delay; the single repeated `Level.Update` in them is at a round change.) Logs: `browser-60hz/xnet-evidence/`.

## 7. Open problems

- **Not run against the Switch.** The lockstep peer, the `debug_assert` on checksum reports with detection off, and the Cloudflare relay's rate limit (about 125 messages/s from a TF.EX peer against 150/s) are read, not measured.
- A Switch that starts its GGRS session late is fine (the browser waits up to 120 s); one that restarts mid-match is not (magic, section 2).
- `xnet` uses TF.EX's test-mode rule set inside a real P2P session (section 5); a match started from the lobby with `NetplayRoundLogic` was not compared with the reference.
- The page crashes (`NullReferenceException`) if the kill-cam recorder is left alive and a rollback hits a round end; fixed in the harness by dropping the recorder, but TF.EX's own lobby path relies on its mode check for that.
