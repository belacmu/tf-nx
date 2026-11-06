# ws-ggrs-ffi: TF.EX's ggrs_ffi on the Switch (docs/ROOT-CAUSE-2026-10-18.md, step 1)

## Emulator runs to reproduce the console faults (2026-10-19 07:4x; hidden Ryujinx, XPLAY_VISIBLE=0; App Nap off; master 2e25135; no code change)
1. **Menu crash: not reproduced.** `tools/smoke.sh quit` (an Instant lobby match, QUIT from the end-of-match menu, back on the lobby
   screen, MainMenu 62, menu frames after it): PASS, no guest exception. But it can't reproduce the console's path: the Switch hosted
   as seat 0 with its controller already at index 0 (`EnsureLocalControllerSeat: seat 0, local controller at 0 of 6`), so
   EnsureLocalControllerSeat moved nothing and left no null PlayerInputs slot; and Ryujinx emulates a **Pro Controller** for every
   player (Config.json input_config: ProController, WindowKeyboard), while the console crashed in JoyConInput.UpdateStyle for a JoyCon.
   A reproduction needs the Switch in a seat other than its controller's index (a joining Switch: smoke `join`) and is still not a JoyCon.
2. **Desync: neither run desynced.**
   a. Local server, **no fixed seed** (lobby seed 2020518583), both sides moving (the page's scripts, the Switch's `lobbyscript 1`), to
      vsstop 1300: `SMOKE lobby PASS: in sync (0 differing ticks of 1297), digest clean`, rollbacks from frame 271 (several).
      Not the real TF.EX server: the harness joins "the first lobby" on the page's lobby browser, which on the real server could be a real
      player's lobby, and a private lobby needs the Switch to log its join code (it doesn't; a product change, excluded today).
   b. Local server, fixed seed 1374205500, the same inputs: `PASS: in sync (0 differing ticks of 1293), digest clean`, 10 rollbacks.
   **What still differs from the console** (the console desynced from frame 360 in every lobby match, before any rollback):
   - **write tracking:** live on the console (`[rb] write tracking: self-test passed (faults 37)`, `write tracking (live): 23897 pages`:
     every write to a tracked heap page faults once and the handler resumes it); in Ryujinx its self-test fails and the undo log
     compares pages instead (no faults). The console's boot reports resumes that changed SIMD registers (`[wt] ... 14 with the entry
     copy's vector registers != the kernel's`). A resume that corrupts registers changes the computation itself: a desync with no
     rollback, and the same as the overwritten request list of 20:55. **The prime suspect; for Integration B (rollback.cpp / wt).**
     A console check that settles it: one lobby match with write tracking off (the comparing undo log, as in Ryujinx).
   - real controllers and human timing vs the scripts; internet latency vs loopback;
   - the real server (lobby data from production) vs the harness's local server.mjs;
   - the Switch's seat (the console was host and guest in turn; these runs host, seat 0); the console's cores and thread priorities.
   The browser page is the same test-page build in both (857589f).

## Item 3, the desyncs: the checksum path checked (2026-10-18 21:0x; no code change)
- **The digest is the same on both sides.** The Switch (npdigest::Compute: rnd's gameplay RNG state, each seat's first Player alive /
  position / speed, the arrows' positions; the scene from TFGame +0xe8) and the browser (test-page web/Netplay/GameplayDigest.cs: the
  same inputs from RngService.Gameplay.Snapshot and the Level's Player / Arrow lists; on in every session with a Switch player,
  SwitchLobby.Active) compute it at the same point: the state at the start of frame f, handed to netplay_set_pending_checksum right
  before ggrs_ffi's save of f (the browser in a SaveGameState prefix, the Switch in the gate's Save request, which comes from the same
  request list in GGRS's order, as TF.EX's NetplayLogic consumes it).
- **The constant digest 840-1320 (20:33) is real:** both players stood still at the same positions (that test crashed when the browser
  player first moved), so nothing changed.
- **The 20:52 lobby desync is a simulation difference:** frames 120 and 240 have the same player positions (nobody had moved), the
  Switch's gameplay RNG changed between them, frame 120 agreed with the browser and 240 didn't: the two sides drew the gameplay RNG a
  different number of times between frames 120 and 240 with no input, 0 rollbacks. That is gameplay parity (which draw), not the netplay
  path. Next: the browser's `[digest] frame 240` line from that match (GameplayDigest logs it on DesyncDetected) against the Switch's
  `[digest] frame 240 local 4c784b62a7d8c77c: rng=62eeca8145e94ef5 ...`: the RNG words show which side drew extra; then rnd's draw log
  (`[np] the last 256 gameplay draws`, logged at the first desync) against the page's rngtrace for those frames.
- The quick-play desync (20:55, from frame 600 after two rollbacks, the miasma early on the Switch) needs the same browser lines; the
  frame counter matches TF.EX's (Scene.FrameCounter = state frame + 1).

## The 20:55 request drift: the list was overwritten (this commit)
05150ba's new lines found it: from frame ~205, before any rollback, the request VList's count read -1435254400 (the low half of a heap
pointer), TF.EX's list was empty while ggrs_ffi held [save, advance], and the gate's resync then led to SaveGameState's abort. The
NetplayManager is rooted (g_nmRoot, uncollectable, scanned) and its containers came from uncollectable memory, so the GC didn't free
them; but they lived in the GC heap that rollback.cpp write-tracks and restores, which option 1 says the session's state must stay out
of. The first error came right after a write-tracking cold hit on a heap page (`[rb] cold hit frame 205`): worth a look by Integration B
(the boot's `[wt] ... vector registers != the kernel's` line reports resumes that changed SIMD registers; a bad resume in the middle of a
260-byte struct copy would write register garbage like this). Fix: the request list and the per-seat stats live in module memory (never
collected, tracked or restored); the resync is removed (a mismatch is only logged). Console check: no `[tfexgen] ERROR: a VList says it
holds`, no `[np] ERROR: frame ... ggrs_ffi holds`, no SaveGameState abort.
Not done (item 3, desyncs): the lobby desync at frame 240 with 0 rollbacks and the quick-play miasma timing need the browser's side: the
checksum the Switch hands ggrs_ffi is npdigest's GameplayDigest (the same as the hand session's, digest-clean against the harness browser
in Ryujinx today); whether test-page's Switch-lobby mode (PR #71) computes the same version is the first check. The frame counter matches
TF.EX's (Scene.FrameCounter = the state frame + 1 = netplay_current_frame after the advance). A digest that stays the same for 480 frames
(20:33, frames 840-1320) is what a paused or finished round gives (no movement, no draws): check what the scene was then.

## The 20:41 "Netplay request is not empty" (this commit)
89ded0a played 248 frames, then ggrs_ffi refused an advance: its own request list wasn't empty while TF.EX's was. Not found from the log
(the step lines stop at frame 5). The gate now compares the two lists before each advance and takes ggrs_ffi's when they differ, so
every request is consumed every frame (TF.EX's NetplayLogic), and logs both lists when they differ or when a frame ends with requests
left. Console check: no `[tfex] Unrecoverable netplay error ... Netplay request is not empty`; if `[np] ERROR: frame N ... ggrs_ffi holds
... TF.EX's list ...` or `[np] frame N ... ends with requests left` appear, those lines name the frame and the kinds: the next step.
Publishing: the Rust library changed (ggrs-switch/build.sh).

## The 20:33 crash at the first rollback (this commit)
6b8d224 synchronized and played on the console, then crashed when the browser player first moved (the first rollback): RenderPings wrote
up the main thread's stack. Cause: UpdateNetworkStats' perSeat map comes from a gen::NewVal ring whose slots were allocated lazily inside the
rollback window (the boot log's "[rb] WARNING: a runtime container ... allocated inside the rollback window" lines); a load to an earlier
frame freed them under the module's ring, game objects reused the memory, KeepVal copied the garbage map into _networkStatsPerSeat, and
MapKeys looped over its count; the compiler had removed Snap::Add's capacity check (an array index past 64 is undefined, so it assumed
none). Fix: runtime containers made during a lobby match come from a 1 MB arena reserved before it (never freed by a restore); every loop
over a runtime container is bounded by its capacity (Live(); an out-of-range count is an `[tfexgen] ERROR: a ValMap says it holds ...`
line and reads as empty); the NetplayManager's keep across a load carries values and container contents only, no pointers.
Console check: `[tfexgen] runtime arena reserved (1024 KB ...)` at the match start, `[rb] rollback #1 ...` and `[rb] TF.EX's NetplayManager
kept across the load` lines with play continuing past them, no `[tfexgen] ERROR: a ... says it holds`, no `[rb] WARNING: a runtime
container ... allocated inside the rollback window`. (The `runtime-made class (MakeClass)` warning is a separate hazard, not changed here.)

## The 20:14 console freeze at netplay_init (b8538d4)
bc17463 on the console: the module froze right after `ffisock: socket 1 open` (host of 3, wide team), and in round 2 (guest) around the
match start; the log just ended. Cause: Rust's std Mutex waits with the futex syscall, which shim.c failed at once, so every contended
lock was a spinlock. Since the try/finally fix Connect really runs on its worker and holds ggrs_ffi's lock through netplay_init, while
ffisock (priority 12, above the game's threads) drives the room's sockets under the socket's lock and the game thread pumps: a priority-12
spinner on the core of a lower-priority holder never lets it run (Horizon's scheduler is strictly by priority), and the log writer on
that core starved too. Ryujinx gives each guest core its own host thread, which is why no emulator run showed it; the 19:00 run (the
Switch only answered: little work under the lock) got through by timing.
Fix: shim.c's futex: FUTEX_WAIT(_BITSET) sleeps 50 us while the word holds the expected value (a spurious wake-up), FUTEX_WAKE is a
no-op. The hand code's ggrs_ffi reads (spike lines, frame-log commit, telemetry) skip during netplay_init (NpFrame / NpConfirmed: -1).
Console check: `[ffi] netplay_init config`, `ffisock 1: [rtc] offer to ...` (as host) / `answer to ...`, ICE Connected, channel open,
`[tfex] Netplay initialization succeeded`, `[ggrsffi] Starting p2p session`, `[np] Synchronizing ...`, `[np] lobby match #N synchronized`,
`[np] step: the frame's own advance`; `[audio] frame` lines keep coming through the connection (the game doesn't stand still); the log
keeps its tail after any hang (the writer is no longer starved).

## Three fixes for the next console build (2026-10-18 evening; build-probe and check-gen pass for each; no emulator runs, as asked)
1. **try/finally** (03c9f1f): a finally is a scope guard. TF.EX has try/finally in 14 files; the translated ones are NetplayManager.NativeInit,
   SessionPatch.PlayWinnerVictoryMusic, RollCallElementPatch.UpdatePlayerUI (2): all now `auto finally_N = gen::MakeDefer(...)`. Not
   translated (no change): TFGamePatch.NetplayLogic, EnumerableExtensions (async), InstantReplayFootage.Bake / Restore, MatchmakingService.Send,
   both ReplayServices, TfStateApi.LoadGameStateBytes / RestoreGameStateBytes, LevelExtensions.LoadHats, InstantReplayViewerPatch.Tick,
   LevelPlaybackOverlay, Takeover.CaptureLiveInput.
2. **bool.TryParse** (17b79bf): m_Boolean_TryParse_9ee6 was 0x410f90 = System.Version.TryParse, found by name in System.ValueType's
   classmeta records. Those records are a sliding window over neighbouring classes for corlib types (Version's Parse/TryParse show up in
   ~30 System.* types' lists); Boolean.TryParse is in none (not in the AOT build). bind.py now takes a body by name only from the requested
   type itself; bool.TryParse is a [service] rule to svc::BoolTryParse (.NET's semantics). Console check: joining a wide lobby shows no
   "MISSING VALUE". **Other bindings taken by name from a base type** (same evidence, cached in binding.json, not re-resolved here; each
   needs a look): m_Boolean_ToString_0 and m_Enum_ToString_0 -> System.Object 0x270100 (both the same body), m_Guid_ToString_0 ->
   System.ValueType 0x40f890, m_DeflateStream__ctor_0 -> System.IO.Stream 0x23ea40, m_MemoryStream__ctor_0 / _2 -> System.Object
   0x26fff0 / 0x270230, m_StreamReader__ctor_1 -> System.Object 0x270160, m_Texture2D__ctor_0 -> GraphicsResource 0xbaf80,
   m_Assembly_GetType_1 -> System.Object 0x273dd0, m_Type_GetFields_1 -> System.Object 0x287430, m_Type_GetField_0 / _1,
   m_Type_get_Assembly, m_Type_get_BaseType, m_PropertyInfo_GetIndexParameters / get_CanRead / get_PropertyType ->
   System.Reflection.MemberInfo. (The other 210 "owner differs" entries are generic instances named by their definition: fine.)
3. **prev-hw-log.txt at boot** (this commit): ReadPrevious set its done flag first and copied for seconds on the writer thread (since
   496735a); devupdate saw 0 bytes and skipped silently. Console check: the boot logs `[dev] the previous session's log (N bytes)
   uploaded; hw-log.txt ...` and .devserve/results gets a prev-hw-log.txt at every boot.

## The console's "never synchronizes" (2026-10-18 19:00, master 73c6218): fixed
Three causes, found with `tools/smoke.sh lobby` (Ryujinx + the browser page):
1. **The translator emitted a `finally` after the try body's `return`.** NetplayManager.NativeInit is
   `try { return netplay_init(..); } finally { GGRSFFI.IsInInit = false; }`, so IsInInit stayed true after Connect and the gate never
   polled ggrs_ffi: no Synchronizing event, the 30 s abort. Fix: a finally is a scope guard (gen::MakeDefer) around the body
   (translator UnitEmitter.cs); RollCallElementPatch.UpdatePlayerUI and SessionPatch.PlayWinnerVictoryMusic regenerate the same way
   (no return in their try: same order of effects).
2. **A double free of ggrs_ffi's event array.** TF.EX's GetEventAndUpdate calls Dispose inside its `using`, which disposes again; a
   SafeHandle frees once, our FreeEvents freed twice and corrupted the heap (a guest crash on the first Synchronizing events). Fix: the
   ffi frees (events, inputs, requests, status) clear the held struct.
3. **The harness went to the real server.** The emulator's sd:/xplay/TF.EX.settings.json (written 15:32 by an earlier run) names the
   real server, and TF.EX's settings win over netplay.txt: every harness lobby run on the shared instance since then went there.
   tools/lobby-vs-browser.sh now sets the settings file aside for the run and puts it back.
Checked: `tools/smoke.sh lobby` PASS (0 differing ticks of 592, digest clean); with latency (`LVB_URLEXTRA="netdelay=40&netjitter=20"`)
PASS (0 of 598, rollbacks of depth 4-16, `[rb] TF.EX's NetplayManager kept across the load: N requests left` on each, N >= 5).
The gate now logs its first steps per match (`[np] step: ...`: 12 polls before synchronization, the first 6 synchronized frames,
at most 60 lines).

## Switch-over: done (2026-10-18, b09863d + b84f85e on claude/modest-volhard-7c2d0d)
TF.EX's generated NetplayManager drives the lobby match's session over ggrs_ffi; the hand GGRS session is deleted. No flag.
- **Rules** (overrides.toml / translate.toml): the GGRSFFI.* wildcard and the hand GGRSFFI / Status / Events rules are gone, so every
  GGRSFFI call is a direct C call into ggrs_ffi (construct 1); construct 3's wrappers with `ffi::Hold` for call results; Inputs /
  Requests ToModel; SafeRawBytes = the snapshot handle ([[rewrite]] to glue Ffi.RawBytes / RawBytesFFI); NetplayManager's session
  methods translated (only spectators, replay, GetSessionFps, PublishCaptureFlag, AdvanceGameState excluded); its fields are its own
  (the svc field rules gone); ResolveNetplayManager = the generated object; GGRSFFI.IsInInit a real flag; ping_measurement_start/stop
  noop (MatchmakingService.cpp's hand ping measurement keeps net.cpp pm); UpdatePlayers without spectators.
- **Translator fixes** (tfex-translator's files; small): `$self` = the receiver for a TF.EX property rule (was the enclosing self); a
  Task.Run [[rewrite]] wins over the inlining (Connect now runs on the worker, construct 4 was inlined before). The "unsupported: new
  SafeBytes / SafeRawBytes" report lines are only the rewrite's `$new` fallback (the generated code uses the glue).
- **TFEX-DIFFs kept, each with its reason in the code:** save/load = rollback.cpp's undo log with a 16-byte handle (performance);
  the NetplayManager's fields and request list survive a heap restore (xplay KeepSave/KeepRestore, NmKeep*); AdvanceGameState by hand
  (TF.EX's `inputs.ToList()` is a List<Input> the game's AOT build doesn't have; the inputs go to xplay's g_npIn, the Switch's
  InputService); the config's delays in 60 Hz frames and Fps 60 (S-60hz); the Level stands still while waiting for synchronization
  (TF.EX runs it unsynchronized with no round); netplay_switch_pump once a frame and the ffisock thread (no Rust threads).
- **Deleted:** ggrs-switch's tfx_session_* API and its Mac tests (digesttest, peer, selftest); xplay's hand GGRS driver (NetplayGate's
  tfx_poll / event loop / tfx_advance(_requests), lockstep path); lobbymatch's tfx_session_new setup and delay conversion; the svc
  session-state functions (IsSynchronized, IsInit, CurrentFrame, LocalHandle, NetplayEvents / AddNetplayEvent, network stats); the
  np_request_rtc call (ggrs_ffi joins the room itself).
- **Kept for a follow-up:** net.cpp's RunRtc / np rings / np_send / np_recv (now unused: no caller requests them) and the
  non-lobby native versus test's session (hw.txt `np`: it has no session now; lobby matches only); ggrs-switch's rtc.rs (tfx_rtc_*) and
  ping_measurement.rs (tfx_pm_*), still used by the lobby's ping measurement in net.cpp pm (TF.EX's own: ggrs_ffi_ping_measurement_*).
- **Checked:** build-probe OK, check-gen OK; one Ryujinx boot (`tools/run-vs.sh -s 120 "vsstop 300"`, shared instance): no guest
  exception, hwlog `ffisock: socket thread started` and `[lobby] TF.EX's NetplayManager (generated) made`, no `[ggrsffi] ERROR`; the
  offline versus run compared 0 differing ticks of 299. Not run: any lobby match (the user: the console test covers the match).
- **Next test day, in addition to the checklist below:** `[lobby] TF.EX's NetplayManager.Init: Connect runs on the worker`; `[ffi]
  netplay_init config: {...}` with InputDelay 2 (20 ms), Fps 60, Players = the RoomPeerIds in seat order, LocalPeerId ours,
  ServerConf.RoomUrl = TF.EX's room url (+ "?next=" ...), RelayUrl as the lobby's; `[rb] TF.EX's NetplayManager kept across the load: N
  requests left` on the first 10 rollbacks (N >= 1, the loop goes on after the load); no `[rb] FATAL: ggrs_ffi handed back a state that
  isn't this match's snapshot handle`; `[np] AdvanceFrame: PredictionThreshold` lines only briefly; REMATCH: a second Init after
  TF.EX's Reset (netplay_reset; `ffisock: socket <h> closed`, a new `open`).
- **Risks to watch:** a Reset while Connect still waits in netplay_init (abort before sync) blocks the game thread on ggrs_ffi's lock
  until the init returns (up to ConnectionTimeoutMs 30 s); undisposed statuses free their message when their Hold slot comes round.

## Switch-over blocker (2026-10-18, before any rule change; stopped at 78%)
Found while starting the switch-over on master 2665601 (all five translator constructs in). Nothing was changed in overrides.toml
or the session code: a half switch-over would break master's lobby matches, and the user wants no flag.

**The generated NetplayManager lives in the GC heap, and rollback.cpp's load restores the GC heap.** overrides.toml says so at its
NetplayManager section ("a rollback restores the GC heap, so a field of the generated object written in a match would be taken
back by the next load"); that is why the session state was kept in xplay.cpp. TF.EX's own driver (TFGame.cs NetplayLogic) runs the
load inside its request loop:
`UpdateNetplayRequests()` (allocates `_netplayRequests`, a List) -> `ConsumeNetplayRequest()` ... `LoadGameState` ->
`StateApi.RestoreGameStateBytes` (on the Switch: rb::Rollback) -> the loop goes on reading `_netplayRequests` and
`_framesToReSimulate`, `_isRollbackFrame`, `_isInit`... On PC the restore only touches TF.State's entities. On the Switch the
restore puts the allocator and every heap word back to the target frame:
- the NetplayManager's scalar fields written since that frame go back (`_framesToReSimulate`, `_isRollbackFrame`, `_framesAhead`,
  `_events`, `_networkStats`...), and
- `_netplayRequests` (allocated this frame) becomes free memory that the resimulation's allocations overwrite: the loop then reads
  a dangling list.
rb::KeepOps (KeepSave / KeepRestore, xplay.cpp:1321) only carries plain memory (the GPU buffer slots, rumble) across a restore.

Options (a decision for Integration B, rollback.cpp's owner, and the coordinator):
1. **Keep the NetplayManager's session fields across a restore**: KeepSave copies the generated object's scalar fields (the
   Self struct's session part) before rb::Rollback and KeepRestore writes them back; the request list is read once into
   native memory before any request runs (the Switch's NetplayLogic: TFEX-DIFF), so no GC object of the request loop is
   live across the load. Smallest change; needs the field list from the generated Self struct (tfexgen_NetplayManager.hpp).
2. **Keep the generated NetplayManager outside the tracked heap**: allocate it (and its session lists) from uncollectable
   memory that write tracking ignores. Cleaner for every field, but the translator's lists are GC lists.
3. **Restore only TF.State's part on a load** (what PC does): a large change to rollback.cpp; not for now.
Recommendation: 1. It keeps rollback.cpp as it is (the Switch's existing solution underneath) and keeps TF.EX's NetplayManager
code as generated; the one TFEX-DIFF is "the request loop and the session fields survive a heap restore".

What the switch-over batch then does (unchanged plan, in order): overrides.toml: drop lines 66-69 and 72-79 (the session methods'
exclusions), the GGRSFFI rules 402-416 except IsInInit (404) and ping_measurement_stats (406, MatchmakingService.cpp's hand ping
measurement still uses net.cpp's: ping_measurement_start/stop need explicit noop rules, else the direct C call binds to net.cpp's
other signature), the interface rules that stand in for the session (INetplayManager IsSynchronized / IsDisconnected / IsInit /
Poll / UpdateNetplayRequests / HaveRequestToHandle / CanAdvanceFrame / Save / AdvanceGameState ... -> the generated object, as
svc::HasFailedInitialConnection forwards today), NetplayManager.IsSynchronized/IsDisconnected (452-453); construct 3's rule lines
in place of 408-415, plus Inputs/NetplayRequests ToModel rules (InputsImpl._inputs and NetplayRequestsImpl._requests need runtime
functions that make the managed lists: tfexgen_ffi has InputsData only). Then NetplayGate: TF.EX's NetplayLogic order on the
generated object (AdvanceFrame, UpdateNetplayRequests, the request loop) with rollback.cpp's save/load and the snapshot handle.
Note also rb::kWindow = 48 frames against ggrs_ffi's max_rollback_window 45 (at Fps 60): enough, with 3 frames for SafeTarget.

## Checks for the switch-over (the user, 2026-10-18: "prove the game boots"; the console test covers the match)
Emulator, after the switch-over and the deletions: one boot, `tools/run-vs.sh -s 120 "vsstop 300"`: no guest exception, and
hwlog has `ffisock: socket thread started` (frame 180) and no `[ggrsffi] ERROR`.

Console, next test day (a lobby match against the browser, at least one death, then REMATCH and QUIT). The log must show:
1. The config: one `[ffi]` line with the GGRSConfig JSON netplay_init got (`"Fps":60`, Players in seat order, LocalPeerId =
   our RoomPeerId, ServerConf.RoomUrl = the lobby's room, InputDelay); no `AppConfig deserialization failed` panic.
2. Connect on the worker: `[ffi] RunWorker` and TF.EX's "Netplay initialization succeeded" from the worker thread while the game
   keeps drawing (no freeze up to 30 s); never `a worker already runs`.
3. The transport: `ffisock: socket <h> open: /room/...`, `ffisock <h>:` tfx-rtc lines (peer connected), `[ggrsffi] Starting
   matchbox loop (pumped)`, then ggrs-ffi's Synchronized event (TF.EX's "Synchronized!" notification on screen).
4. Rollback: rb lines as today (`[rb] rollback #n`, cost lines) and no `[rb] FATAL`; with the blocker's option 1, no change in the
   NetplayManager's session fields across a load (a log line per load the first 10 times: requests left, frames to resimulate).
5. Sync: no DesyncDetected event (TF.EX logs it and marks the match report), digests equal on both sides at every 120-frame check.
6. The end: REMATCH starts a new session (netplay_reset, then init again: one `socket <h> closed` and a new `open`); QUIT
   closes the socket; `ffisock` slots in use go back to 0 (peak <= 2).
7. Pools: no `ffisock: all 4 slots in use`, no `ggrs-ffi: N log lines dropped`, no `matchbox-switch: packet queue full`.

## Step 2 status (2026-10-18, stopped at 77% weekly usage)
Done (build-checked, not run). No setting (the user, 2026-10-18: the ggrs_ffi path becomes THE path, no off-by-default
flag); until step 1 below lands nothing opens a ggrs_ffi session, so the pump and the socket thread idle:
- **The pump**: `ggrsffi::Frame()` in xplay's FrameHook (after DrainGameThreadActions, where TF.EX's TFGame update starts) calls
  `netplay_switch_pump()` and copies ggrs-ffi's log lines into hwlog (`[ggrsffi] ...`, at most 64 a frame).
- **The sockets**: net.cpp `ffisock`, a thread started at frame 180 (after xplay mounts the SD) (NetplayPreferences' rtcbind, stun, ip).
  It opens every socket ggrs-ffi asks for (`ws[s]://host[:port]/path`, the server address `ip` when name lookup fails), one
  UDP socket each, and runs RunRtc's pass on it: WebSocket in, UDP in, drive, signaling out, UDP out, log, 10 s keepalive;
  a socket ggrs-ffi dropped is closed and released. 4 slots (the match's and the ping measurement's + 2), usage and peak
  logged, a full pool is a logged refusal (`ffisock: all 4 slots in use`). Log lines: `ffisock: socket thread started`,
  `ffisock: socket <h> open: <path> (UDP <port>)`, `ffisock <h>: <tfx-rtc line>`, `ffisock: socket <h> closed`.

Not done, in order (the next session starts at 1):
1. **The session: TF.EX's NetplayManager session methods and its TFGame update loop.** They are excluded from translation
   today (overrides.toml lines 66-79: Init, ApplyRelay, NativeInit, Connect, Poll, NativePoll, UpdateNetworkStats,
   AdvanceFrame, UpdateNetplayRequests, SaveGameState, AdvanceGameState, LoadGameState) and the loop
   (TFGame.cs TFGameUpdate_Patch + NetplayLogic, manifest.toml 74/76) is xplay.cpp's FrameHook/NetplayGate. Translating
   them needs from tfex-translator: P/Invoke externs as direct calls (`GGRSFFI.netplay_*` -> `::netplay_*` with the
   st:: structs, which already match ggrs-ffi's `#[repr(C)]` layouts: Status is int16 + pointer on both sides in effect,
   little endian), `SafeBytes<GGRSConfig>` (the AppConfig's serialization: check AppConfig::new in
   ggrs-ffi config/app_config.rs for the format), `using` handles (Events/Inputs/NetplayRequests `ToModel()` + free),
   `Task.Run(() => Connect(...))` (TFEX-DIFF: on the Switch run Connect on a worker thread or inline: netplay_init
   blocks up to the connection timeout while ffisock does the I/O, so inline freezes the game for that time, as the
   thread on PC doesn't), and Regex in AdvanceFrame's desync branch.
   With those: TFGameUpdate_Patch / NetplayLogic translated and called from FrameHook in place of NetplayGate, and
   NetplayManager's GGRSFFI calls 1:1 (no wildcard). No flag: the switch-over lands in one batch with the hand session
   deleted (below), once the lobby run against the browser synchronizes and stays in sync.
   Translator status (tfex-translator, 2026-10-18, not started): DllImport calls none (~half a day); SafeBytes<GGRSConfig>
   JSON partial (small); using/Dispose exists, but EventsImpl/StatusImpl/NetplayRequestsImpl/Handle are excluded and ruled
   (small-medium); Task.Run none (medium, needs a pooled thread); Regex none (one-off rule); Status.Info with 3. Agreed
   order: Poll/NativePoll/GetEventAndUpdate/UpdateNetworkStats; then AdvanceFrame, UpdateNetplayRequests,
   AdvanceGameState, Save/LoadGameState; then Init/ApplyRelay/NativeInit/Connect; then TFGame.cs TFGameUpdate_Patch +
   NetplayLogic (their StateApi capture/restore: rules onto rollback.cpp, ours to provide).
2. **Save/load on rollback.cpp** (NetplayLogic's SaveGameState / LoadGameState cases; TFEX-DIFF): instead of
   `StateApi.CaptureGameState()` / `RestoreGameStateBytes`, rollback.cpp's snapshot into a ring slot and the 16-byte
   handle (section "save/load contract" below), `netplay_set_pending_checksum(npdigest)` before each save.
   rollback.cpp is Integration B's: ask it for `Snapshot(slot)` / `Restore(slot)` entry points if NetplayGate's aren't reusable.
3. **The lobby's AppConfig**: TF.EX builds GGRSConfig from the lobby (MatchmakingService, NetplayManager.Init); compare it
   with lobbymatch.cpp's mapping (seats = players order, local_peer_id = RoomPeerId, room_url, input delay).
   Checked (2026-10-18): the JSON matches field for field. ggrs-ffi's AppConfig is `serde(rename_all = "PascalCase")` (InputDelay,
   Name, Netplay {NumPlayers, Spectators, Players, LocalPeerId, LocalConf, ServerConf {RoomUrl, IsHost, RelayUrl,
   RelayPeers, ...}, SpectatorConf}, Test, Fps, MaxInputDelay, ConnectionTimeoutMs), the same names as TF.EX's GGRSConfig.cs
   serialized by Newtonsoft (SafeBytes<GGRSConfig>(config, useJson: true)). Two things to settle in that batch:
   **Fps**: TF.EX's default is NETPLAY_FPS = 240 and ggrs-ffi scales every window by it (prediction 15 -> 60 frames,
   desync interval 120 -> 480); the Switch runs at 60 (overrides.toml GetSessionFps = 60, S-60hz), and the browser's
   Switch-compatible build too. The AppConfig the Switch sends must say Fps 60 (TFEX-DIFF, 60 Hz), and its peers must
   agree on it, or the frame numbers don't line up. **ConnectionTimeoutMs** 30000: netplay_init blocks that long at most.
4. **Check**: `tools/lobby-vs-browser.sh` short run (synchronizes, plays, 0 differing ticks), then in the same branch delete
   the hand session (wiring plan below): xplay's GGRS session, lobbymatch's session setup, the tfx_* API, the svc::
   session-state rules and the GGRSFFI wildcard.

## Status (2026-10-18, step 1)
- **Built and linked, not called.** TF.EX's own native netplay library (ggrs_ffi) builds for the Switch target
  (aarch64 musl, no TLS) as part of ggrs-switch's staticlib (`ggrs-switch/build.sh` -> `rustlib/libggrs_spike.a`), and the
  module keeps its whole C API (`hw-probe/exlaunch/config.mk` GGRS_FFI_KEEP, 45 symbols as GC roots). `tools/build-probe.sh`
  passes; the module's unresolved symbols are exactly what they were before (57, none new: no pthread_create, epoll, ...).
- All **29 functions of TF.EX's GGRSFFI.cs** are exported with GGRSFFI.cs's signatures (table below). 26 under their own
  names; the 3 `ping_measurement_*` are exported as `ggrs_ffi_ping_measurement_*` for now, because net.cpp still defines
  hand-written functions with those names (another signature) that the generated code calls. Step 2 deletes net.cpp's
  and the rename.
- build-probe now also fails on unresolved `netplay_*` / `ggrs_ffi_*` symbols (beside `tfx_*`).
- Not run: nothing calls it yet (no behaviour change). Not tested in Ryujinx or on hardware (step 2's job).
- Commits on this branch: f06faea (TF.EX's ggrs-ffi vendored unchanged, as the browser builds it), then the Switch changes.

## What built, and what had to change
Source: `ggrs-ffi-switch/ggrs-ffi/` = the browser's `vendor/netplay-src/ggrs-ffi` (Fcornaire/ggrs-ffi **2fe64d9**
"strict routing", 2026-10-15, plus the browser's changes: `relay_browser.rs`, `netplay_set_pending_checksum`, the
current-thread runtime). The browser's `tools/build-netplay.sh` still names cf60182; its vendored tree is at 2fe64d9.
Open: check which ggrs-ffi TF.EX v0.21.1 ships with and pin that.
GGRS: the same fork and copy ggrs-switch already uses (the browser's patched `vendor/netplay-src/ggrs`), via
`[patch."https://github.com/Fcornaire/ggrs"]` in ggrs-switch/Cargo.toml.

Every Switch change is behind the cargo feature `switch` (the browser and PC builds are unchanged) and marked TFEX-DIFF:

| change | where | reason |
|---|---|---|
| matchbox_socket -> `matchbox-switch` (tfx-rtc's Mesh) | ggrs-ffi-switch/matchbox-switch, `[patch.crates-io]` | The Switch's sockets are nn::socket via net.cpp; std's are stubs. Same move as the browser's matchbox-browser. Signaling, roles and the unreliable channel are matchbox's (tfx-rtc already talks to PC/browser peers). |
| relay.rs -> relay_browser.rs (never connects; peers stay direct) | lib.rs, Cargo feature `relay` (off) | relay.rs opens a WebSocket + UDP socket through std/async-std/rustls. Same as the browser. TF.EX's relay is a follow-up (a relay channel over net.cpp, like matchbox-switch). |
| no threads: the poller, the matchbox loop and the ping measurements are stepped by `netplay_switch_pump()` | switch.rs, lib.rs, neplay.rs, ping_measurement.rs | The Switch's Rust std has no working threads (shim.c has no pthread_create; futex waits fail). Same bodies, one pass per call. `netplay_init` steps the matchbox loop itself while it waits for the players. |
| tokio current-thread runtime | lib.rs | Same as the browser; no worker threads. |
| tracing -> a 512-line queue read by `netplay_switch_log()` | switch.rs | No stdout; the module writes the lines into its log (hwlog), dropped lines counted. |
| crate-type += rlib | Cargo.toml | Linked into ggrs-switch's staticlib: one Rust std in the module (two staticlibs would both define std). |
| `ping_measurement_*` exported as `ggrs_ffi_ping_measurement_*` | ffi.rs | Temporary: name clash with net.cpp's hand versions (see Status). |

### matchbox-switch: the module's side (C API, `netplay_switch_socket_*`)
ggrs-ffi creates a socket with `WebRtcSocket::new_unreliable(room_url)`; it only queues. The module (net.cpp's network
thread) does the I/O, like it does today for `tfx_rtc_*`:
1. `next_open(url, cap) -> h` (0: none): a socket to open; connect the room's WebSocket to `url`, bind a UDP socket.
2. `bind(h, ip, port)`: the UDP socket's address (the host candidate); `stun(h, ip, port, txid)` as in rtc.rs.
3. Each pass: `signal_in` (WebSocket text in), `signal_out` (text out), `keepalive` (every 10 s), `udp_in`, `udp_out`,
   `drive` (returns ms until next); `log(h, buf, cap)` for its lines.
4. `closed(h, failed, msg, len)` when the WebSocket ends (failed = it never connected).
5. `wants_close(h)` turns 1 when ggrs-ffi dropped the socket: close both sockets, then `release(h)`.
The packet queue holds 1024 packets per socket (overflow counted and logged).

## The 29 functions (GGRSFFI.cs -> ggrs-ffi ffi.rs)
All `extern "C"`, same order and types (Status/Events/Inputs/NetplayRequests/ActionResult/SafeBytes/NetworkStats/
PingStats are ggrs-ffi's own `#[repr(C)]` structs, which GGRSFFI.cs's structs mirror):
netplay_init(SafeBytes) -> Status · netplay_poll() -> Status · netplay_is_synchronized() -> Status ·
netplay_is_disconnected() -> Status · status_info_free(char*) · netplay_events() -> Events · netplay_events_free(Events) ·
netplay_advance_frame(Input) -> Status · netplay_set_test_inputs(const Input*, i32) -> Status ·
netplay_get_requests() -> NetplayRequests · netplay_requests_free(NetplayRequests) · netplay_save_game_state(SafeBytes) -> Status ·
netplay_advance_game_state() -> Inputs · netplay_load_game_state() -> ActionResult · netplay_inputs_free(Inputs) ·
netplay_network_stats(i32, NetworkStats*) -> Status · netplay_frames_ahead() -> i32 · netplay_free_game_state(SafeBytes) ·
netplay_current_frame() -> i32 · netplay_reset() -> Status · netplay_local_player_handle() -> i32 ·
netplay_remote_player_handle() -> i32 · netplay_remote_player_handle_count() -> i32 · netplay_remote_player_handle_at(i32) -> i32 ·
netplay_add_spectator(const char*) -> Status · netplay_frames_behind() -> i32 ·
ping_measurement_start(const char*, bool) -> Status · ping_measurement_stop(bool) · ping_measurement_stats(const char*, bool, PingStats*).
Extra (not in GGRSFFI.cs): `netplay_set_pending_checksum(u64)` (the browser's), `netplay_switch_pump`, `netplay_switch_log`,
`netplay_switch_socket_*`.
Checked: the 29 names in GGRSFFI.cs against the archive's exports (`nm`), and each parameter list against ffi.rs by hand
(bool = `UnmanagedType.U1`, strings = UTF-8 `char*`, `out` = pointer).

## The save/load contract on the Switch
On PC, TF.EX serializes TF.State (msgpack) and hands the bytes to `netplay_save_game_state(SafeBytes)`; ggrs-ffi
**copies** them (UnmanagedBytes) into GGRS's cell for that frame and checksums them (fletcher16) for desync detection.
`netplay_load_game_state()` returns a fresh copy of the cell's bytes (ActionResult with SafeBytes), TF.EX deserializes
them, then calls `netplay_free_game_state(that SafeBytes)`, which frees the copy and resets ggrs-ffi's current state.

On the Switch the state is rollback.cpp's memory snapshot (write tracking), too big and too slow to copy per frame
(TFEX-DIFF, performance). So the bytes TF.EX passes are a **snapshot handle**, not the state:
- **save**: rollback.cpp snapshots into its ring, then the Switch calls `netplay_set_pending_checksum(digest)` (the state
  digest, npdigest.cpp: the browser added this call for the same reason) and `netplay_save_game_state` with a small
  record, e.g. `{u32 magic, i32 frame, u32 slot, u32 generation}` (16 bytes). ggrs-ffi copies 16 bytes per frame.
- **load**: `netplay_load_game_state` returns that record; the Switch checks magic, frame and generation (a slot reused
  by a newer save is a bug: log it and fail loudly, never load a wrong slot), restores the slot, and calls
  `netplay_free_game_state` with the returned SafeBytes as TF.EX does.
- **ring size**: GGRS keeps a cell per frame up to the rollback window: ggrs-ffi's session uses
  `max_prediction_window 15`, `max_rollback_window 45` (at 60 fps; neplay.rs base_session_builder). rollback.cpp's ring
  must hold at least max_rollback_window + 2 snapshots, else a load can name an overwritten slot (the generation check
  catches it). Log ring usage/headroom (the pools rule).
- **desync detection**: ggrs-ffi compares the checksums of confirmed frames between peers (every 120 frames). With the
  pending checksum, a Switch peer sends its digest; a PC peer sends fletcher16 of TF.State. Mixed Switch/PC matches
  would therefore report false desyncs unless both sides use the same digest. Open for step 2: what the browser does
  in mixed matches (it uses the same pending-checksum path), and whether the Switch should only enable desync detection
  between digest peers.
- Where it plugs in: TF.EX's `NetplayManager` save/load (`TFGame`-level SaveState/LoadState, through the rollback
  requests from `netplay_get_requests`) maps onto rollback.cpp's existing snapshot/restore; that call site is the
  single TFEX-DIFF of the netplay core.

## Reusing the working Switch code (what moves where)
The Switch already plays online against the browser on hardware with ggrs-switch + net.cpp + xplay/lobbymatch +
rollback.cpp. Step 1 reuses its Switch-specific pieces; TF.EX's logic replaces ours. Where the two differ, ggrs-ffi's
behaviour wins (it is TF.EX's); the Switch's way stays only where the console needs it (TFEX-DIFF).

| working piece | in step 1 | in step 2 |
|---|---|---|
| ggrs-switch build.sh, target JSON (no TLS, panic=abort, PIC), nightly + build-std, `[patch]`es | **reused as is**: ggrs-ffi builds inside ggrs-switch's build | stays |
| shim.c (getrandom, clock_gettime/nanosleep, pthread keys, futex stubs) | **reused unchanged**: ggrs-ffi needed no new libc shims (57 imports before and after) | stays |
| GGRS fork (the browser's patched `vendor/netplay-src/ggrs`) | **the same copy** for both crates | stays |
| tfx-rtc + rtc.rs (matchbox-compatible WebRTC, talks to browsers through TF.EX's server) | **matchbox-switch is rtc.rs's C API keyed by socket handle**, over the same tfx-rtc Mesh | net.cpp's loop that drives `tfx_rtc_*` (np::RunRtc) drives `netplay_switch_socket_*` instead; rtc.rs goes |
| net.cpp: nn::socket, UDP, the room WebSocket (TLS), the DNS fallback | untouched | **stays as the transport under matchbox-switch** (TFEX-DIFF: the console's sockets) |
| net.cpp's WebSocket relay (`/relay/<room>`) | not used: ggrs-ffi's relay is the never-connecting stand-in | candidate backing for ggrs-ffi's relay channel (a RelayChannel over net.cpp's relay), if it matches TF.EX's relay protocol; else TF.EX's UDP relay over net.cpp's UDP |
| ggrs-switch lib.rs `tfx_*` session (create, poll, advance, requests, checksums, desync) | unchanged, still the live path | **replaced by ggrs-ffi's neplay.rs**. Differences settled for ggrs-ffi: session config from `base_session_builder` (prediction 15, rollback 45, disconnect 15 s, max frames behind 50, catch-up 4, desync every 120 frames, all scaled by fps) instead of per-call parameters; ggrs-ffi's `Input` and player handles (seat order = TF.EX's lobby players list); no lockstep mode (max_prediction 0), TF.EX always rolls back. `tfx_set_checksum` (checksum after the save) becomes `netplay_set_pending_checksum` (before the save): same digest, other order |
| ggrs-switch ping_measurement.rs + net.cpp `net::pm` | unchanged (hence the temporary `ggrs_ffi_` rename) | **replaced by ggrs-ffi's ping_measurement** (stepped by the pump); its WebSocket is a matchbox-switch socket opened from `ping_measurement_start`'s URL |
| xplay.cpp + lobbymatch.cpp: the session built from TF.EX's lobby (seats, RoomPeerIds, input delay, room URL) | unchanged | **TF.EX's NetplayManager builds the AppConfig and calls `netplay_init`**; lobbymatch's mapping is the reference to check the generated AppConfig against (players order, local_peer_id, room_url, input delay), then goes |
| rollback.cpp: memory snapshot with write tracking | unchanged | **stays**, behind save/load as a snapshot handle (above): the one TFEX-DIFF of the netplay core (performance) |

## Wiring plan for step 2
Prerequisite: the module calls `netplay_switch_pump()` every frame (before TF.EX's NetplayManager update, never from
inside another netplay_* call) and drains `netplay_switch_log()` into hwlog; net.cpp drives `netplay_switch_socket_*`
on its network thread (the same loop it runs for `tfx_rtc_*` today, keyed by handle instead of one Rtc). Note that
`netplay_init` blocks (TF.EX waits up to its connection timeout for all players, as on PC), so the socket I/O must run on
net.cpp's thread, not the game thread.

1. **overrides.toml: GGRSFFI becomes 1:1.** Delete the wildcard `"TF.EX.Domain.Externals.GGRSFFI.*" = { noop = true }`
   (line 416) and the hand rules: `netplay_current_frame` (svc::CurrentFrame), `netplay_local_player_handle`
   (svc::LocalHandle), `netplay_frames_behind` (value 0), `ping_measurement_stats` (svc::PingMeasurementStats),
   `netplay_reset` (xplay::EndLobbyMatch), `netplay_events` (empty Events), and the NetplayManager interface rules
   `IsSynchronized` / `IsDisconnected` (lines 452-453): their bodies then call the real FFI. `IsInInit` (404) is TF.EX
   state, not FFI: check it against TF.EX's NetplayManager. Bind every `GGRSFFI.*` extern to the C function of the same
   name (the translator emits a direct call; the structs are ggrs-ffi's `#[repr(C)]` layouts). Regenerate gen/ and run
   check-gen.sh. Remove the `S-transport` rows (lines 277-278, 551) that point at ggrs-switch.
2. **ping measurement**: drop net.cpp's `ping_measurement_*` and `net::pm` (and ggrs-switch's ping_measurement.rs /
   `tfx_pm_*`), and the `ggrs_ffi_` rename in ffi.rs: TF.EX's ping measurement runs as on PC (direct path).
3. **retire the custom session driver**: xplay.cpp's GGRS session (tfx_session_new / tfx_advance(_requests) / its
   event loop, svc::AddNetplayEvent), lobbymatch.cpp's session setup, netplay_gate.cpp's tfx_* calls, the svc:: session
   rules in tfex/runtime/tfexgen_services.cpp (CurrentFrame, LocalHandle, IsSynchronized, IsDisconnected,
   PingMeasurementStats), ggrs-switch's `tfx_*` session API (lib.rs) and rtc.rs once net.cpp drives matchbox-switch
   instead. TF.EX's NetplayManager then creates and drives the session (`netplay_init` with its AppConfig).
4. **kept (TFEX-DIFF)**: rollback.cpp's snapshot/restore behind save/load (above), 60 Hz, net.cpp's sockets.
5. **smoke**: lobby, join, quickplay, rematch, quit (all session paths change), then a console run whose checklist
   names the ggrs-ffi log lines ("Starting matchbox loop (pumped)", the synchronized/disconnected events).

## Notes
- Building in a worktree: ggrs-switch's paths to the browser repo are relative (`../../../Towerfall Browser`), which in a
  worktree is `switch-online/.claude/Towerfall Browser`; a symlink there to `~/Code/Towerfall Browser` was added by hand
  (2026-10-18). Without it, cargo can't build ggrs-switch in any worktree.
- `cargo run --bin selftest` (the Mac self-test) was not rerun with ggrs-ffi in the build.

### Translator construct 1: P/Invoke externs (tfex-translator, 2026-10-18; build-checked, not exercised yet)
A `[DllImport]` static extern call with no [service] rule now translates to a direct C call:
- `::<EntryPoint or name>(args)`, with an `extern "C"` prototype per unit, written before its namespace from the C# signature;
- st:: structs by value; ref / out as pointers (`&lvalue`);
- a string as a UTF-8 copy (a 512-byte stack buffer per call), a string result back as a managed string;
- a managed array or class parameter / result is reported unsupported.

The `GGRSFFI.*` no-op wildcard (overrides.toml) still matches first, so no generated code calls ggrs_ffi yet (check-gen: gen/
unchanged). It takes effect when the port session replaces the wildcard with its 1:1 mappings / deletions. Check then: the generated
prototypes against ggrs-ffi's exported signatures (the build links them), and `out` structs passed as `&local`.
Next constructs (agreed order): 3 (Events / Inputs / NetplayRequests / Status handles + their *_free, Status.Info), then 2 (GGRSConfig
JSON, Fps 60), 5 (the AdvanceFrame regex: one-off rule), 4 (Task.Run on a pooled worker thread).

### Translator construct 3: the FFI handles (tfex-translator, 2026-10-18; build-checked, not wired)
Runtime `tfex/runtime/tfexgen_ffi.{hpp,cpp}` is the C side of TF.EX's wrappers:
- `ffi::EventStrings(events)`: EventsImpl._events, the C strings as a managed string[].
- `ffi::InputsData(inputs)`: an `st::Input*` view of the native array.
- `ffi::IsOk(status)` / `ffi::StatusInfo(status)`: StatusImpl.IsOk / InfoHandle.AsString.
- `ffi::FreeEvents` / `FreeInputs` / `FreeRequests` / `FreeStatus`: the ggrs_ffi *_free calls TF.EX's SafeHandles make.

At the switch-over, replace today's overrides.toml [service] lines for these (they serve xplay's hand session now: EventsImpl._events =
svc::NetplayEvents, ToModelGGrsFFI = null, StatusImpl.IsOk = true, InfoHandle.AsString = "") with:
```
"TF.EX.Domain.Models.EventsStructExtension.ToModel" = { cpp = "(&($0))", include = "tfexgen_ffi.hpp", key = "S-native", why = "EventsImpl over the native struct" }
"TF.EX.Domain.Models.EventsImpl._events" = { cpp = "tfex::gen::ffi::EventStrings(*reinterpret_cast<const tfex::gen::st::Events*>($self))", include = "tfexgen_ffi.hpp", key = "S-native" }
"TF.EX.Domain.Models.EventsImpl.Dispose" = { cpp = "tfex::gen::ffi::FreeEvents(*reinterpret_cast<const tfex::gen::st::Events*>($self))", include = "tfexgen_ffi.hpp", key = "S-native" }
"TF.EX.Domain.Extensions.StatusExtension.ToModelGGrsFFI" = { cpp = "(&($0))", include = "tfexgen_ffi.hpp", key = "S-native" }
"TF.EX.Domain.Models.StatusImpl.IsOk" = { cpp = "tfex::gen::ffi::IsOk(*reinterpret_cast<const tfex::gen::st::Status*>($self))", include = "tfexgen_ffi.hpp", key = "S-native" }
"TF.EX.Domain.Models.InfoHandle.AsString" = { cpp = "tfex::gen::ffi::StatusInfo(*reinterpret_cast<const tfex::gen::st::Status*>($self))", include = "tfexgen_ffi.hpp", key = "S-native" }
"TF.EX.Domain.Models.StatusImpl.Dispose" = { cpp = "tfex::gen::ffi::FreeStatus(*reinterpret_cast<const tfex::gen::st::Status*>($self))", include = "tfexgen_ffi.hpp", key = "S-native" }
```
- `$self` is the wrapper the ToModel rule returned: the address of the native struct, which TF.EX's `using` keeps alive.
- The st::Status.info that StatusImpl.Info returns is the same struct (`StatusImpl.Info` -> `$self`).
- Inputs / NetplayRequets follow the same pattern: ToModel -> `&($0)`, element access through `ffi::InputsData`, Dispose -> FreeInputs /
  FreeRequests. Check their element reads (InputsImpl's indexer) in the generated code when the port switches over.

Stopped here at 79% weekly usage. Next: construct 2 (the GGRSConfig JSON: ggrs-ffi's AppConfig is serde PascalCase; Fps 60), then 5
(AdvanceFrame's regex, a one-off rule) and 4 (Task.Run on a pooled worker thread).

### Construct 5 (not done): AdvanceFrame's regex
NetplayManager.cs:511-520: `Regex.Matches(info, @"\b(\d+)\b")`, then `matches.Count` and `matches[^1].Value` -> int.Parse -> the frame
for `_syncTestUtilsService.Compare(frame)`: the desync message's detail for TF.EX's sync-test tooling. Plan: rules for
`Regex.Matches` -> a runtime `ffi::Numbers(info)` (a managed string[] of the decimal runs), `MatchCollection.Count` -> `.len`,
`MatchCollection.get_Item` -> `[i]`, `Match.Value` -> the string. Or rule `SyncTestUtilsService.Compare` to "" (S-tools) and let the regex
go with the rest of the branch. Check first how the translator keys an indexer rule (get_Item).

### Construct 5: done (673b877)
Regex.Matches -> null and MatchCollection.Count -> 0 (S-tools: the regex only finds the frame for SyncTestUtilsService.Compare, TF.EX's
sync-test tooling). The desync message keeps frame -1, with no detail.

### Construct 2 (not done; plan): SafeBytes<GGRSConfig>(GGRSConfig, true).ToBytesFFI() in NativeInit (NetplayManager.cs:217)
GGRSConfig is a class graph (GGRSConfig -> NetplayConfig -> ServerConf / LocalConf / SpectatorConf, TestConfig), generated as
runtime-made classes (tfexgen_*: `Self` structs, auto-properties as fields).
- Plan: a rule `TF.EX.Common.Handle.SafeBytes<T>..ctor` / `ToBytesFFI` -> runtime `ffi::ConfigJson(void* ggrsConfig)`. It is a hand JSON
  writer (runtime Json.hpp Writer) over the generated classes' getters: serde PascalCase = the C# property names; null members omitted
  or null, as System.Text.Json writes them.
- It returns an `st::SafeBytesFFI` {data, len} in a fixed buffer (pool-logged), freed at the end of the `using`.
- TFEX-DIFF S-60hz: `Fps` is written as 60, not Constants.NETPLAY_FPS (240). Only here: TF.EX's 240-based delay units stay
  everywhere else.
- Check against ggrs-ffi config/app_config.rs (AppConfig::new = serde_json::from_slice) and lobbymatch.cpp's mapping (the doc's
  section 3 says the fields match).

### Construct 4 (not done): Task.Run(() => Connect(roundLogic), token)
A runtime worker thread from a fixed pool (1 slot: Connect runs once per session; a second request while busy is logged). The token is
ignored (TF.EX cancels on Reset; on the Switch the session's teardown ends netplay_init's wait).

Stopped at 80% weekly usage, after construct 5.

### Constructs 2 and 4: done (tfex-translator, 2026-10-18; build-checked, not exercised)
- **2, the config bytes:** `new SafeBytes<GGRSConfig>(config, true)` in NetplayManager -> glue `Ffi.GgrsConfigBytes` (translate.toml
  [[rewrite]] new). It builds TF.EX's contractless JSON (property names in declaration order, null as null, enums as numbers) with
  `"Fps":60` (TFEX-DIFF S-60hz), into a runtime buffer (tfexgen_ffi BytesOfString: 2 pooled 4 KB slots, logged when full or oversize).
  ToBytesFFI -> `ffi::BytesFFI` ({ptr, size}, parked until the GGRSFFI.* wildcard goes). The `using`'s Dispose -> `ffi::FreeBytes`.
  Check at the switch-over: the JSON against ggrs-ffi's AppConfig (`serde_json::from_slice`) with one real lobby's config logged.
- **4, the worker:** `Task.Run(() => Connect(roundLogic), token)` in NetplayManager -> glue `Ffi.RunWorker` (translate.toml
  [[rewrite]] call). It runs on a game `System.Threading.Thread` (managed, so the GC knows the thread: the translated Connect allocates), one
  at a time (a second while one runs is refused and logged: "[ffi] RunWorker: a worker already runs"); the token isn't watched.
  Thread.Start() was stripped by AOT: pinned to its body StartInternal (0x3f6990, which takes only `this`; translate.toml [pin], evidence there).
  Check at the switch-over: Connect's log lines come from the worker, and the game keeps running while netplay_init waits.
All of the port's constructs (1-5) are now in. Next: the port session's switch-over (the GGRSFFI.* wildcard -> 1:1, the excluded
NetplayManager methods un-excluded, the construct 3 rule lines swapped in).
