# hw-crash: console crashes, hangs and desyncs (2026-10-16/17)

## Status (2026-10-17 ~01:20)
**Fixed and confirmed on the console**
- Write-tracking hang at frame 1 (wrong fault address from scanning UserExceptionInfo): the self-test learns the ESR offset (+0x334).
- Leaked nn::ssl contexts (TLS failed after 16 connections: telemetry silent, the 3rd match never connected).
- QUIT from the end-of-match menu crashed (ScreenTitle built for TF.EX's netplay state 62).
- Console-only desyncs from the fault resume's vector registers: q0-q31 now come from the kernel's copy (info +0x120). Measured: 49 / 101 /
  38 faults per boot where the old path would have changed q0-q6/q16-q29; 0 faults on other threads, 0 concurrent.
- FPCR/FPSR cleared: the old resume keeps them (self-test: set 0xc00000/0x800009f, kept; info at +0x324/+0x328).
- A browser that quits mid-match: the console ends the lobby match at once when the room session ends (every other player left), or
  when every remote disconnected (a 3-4 player FFA goes on when one leaves); then NETPLAY.
- Lobby map default RANDOM (-1) as TF.EX.

**Open**
- The first-to-5 (goal 5 / length 2) RNG-only desyncs (22:42 round 4, 00:35 round 1, both before any rollback): not reproduced since
  (00:53: a goal 5 match with the browser side played and 62 rollbacks stayed in sync). The draw ring is dumped at the first DESYNC
  (`[vs] draw N frame F site +X`, `tools/draw-sites.py`); a hand-played page would need a `?rngtrace` switch for the other side.
- Round 5's whole level drawn shifted on the console (00:53, no desync): Engine.Screen.Offset left non-zero (space orb scroll or shake)
  across a level change; TF.State's LevelLoaderXMLStatePatch.Reset zeroes it, our port skipped it. With TF.EX-base.
- The console's network thread slowing within a WebRTC session: its own session (webrtc-slowdown); findings below.
- tools/lobby-vs-browser.sh (dad2ab7): the page's keys wait for its online menu ('[netplay] connected to ws', a nudge key for a box in
  front of it), LVB_MENUMAX / LVB_MENUSETTLE / LVB_MENUNUDGE / LVB_MENUKEY, and a stale-log guard (vs-log-3m.txt removed at the start;
  no compare when missing, older than the run or for another seed). Syntax-checked; NOT yet verified by a full run (the test runs were
  overlapped by another of this session's runs on the same output folder or queued behind the shared lock).

**hw.txt switches added (defaults)**
- `wtfpcr 1`: FPCR/FPSR from the kernel's copy on resume (default 0; not needed: the entry copy keeps them).
- `telemetry 0`: no telemetry reports (default 1).
- `lobbyvote 3`: a test console QUITs 3 s after the results (besides 1 REMATCH!, 2 ARCHER SELECT; default 0).
- Logged always (no switch): `[wt] this boot so far …` and `[wt] FPCR/FPSR …` at each match end; `[np] lobby match #N over …`
  (ticks, remote input changes, rollbacks); the last 256 gameplay draws at a match's first DESYNC.

---

# (history) hw-crash: the console test of master 4142e4b (2026-10-16, 20:05) never got past frame 1

Branch `claude/hw-crash-e63c2e`. One commit, in `hw-probe/exlaunch/source/program/wtrack.cpp`.

## What happened (it was a hang, not three crashes)
- **The three crash reports are not from this test.** `results/20261016-200537-`, `200627-` and `200817-crash-01792170015_…log` are
  byte-identical (md5 892bc34e…) to the 18:39 and 18:56 uploads. Telemetry.cpp uploads the *newest* file in
  sd:/atmosphere/crash_reports to the dev server at every boot (`devupdate::UploadFile`, ~5 s after boot), so one old report comes
  back at each launch. That report is the 17:00-build crash already analysed in docs/ws-wt-resume.md (write-tracking resume clobbered
  x19/x29; fixed there). Its module id D0CC67F5… is not 4142e4b's build, so symbolizing it against 4142e4b gives nonsense.
  The three uploads just mark three boots: 20:05:36 (old build; it downloaded the new subsdk9), 20:06:27 and 20:08:17 (4142e4b).
- **The console's telemetry** (`tools/switch-reports.py`, reports #503-#517) shows the 20:06:27 session (47fb7c69) **did start the
  match**: lobby_joined 20:06:58, lobby_match_start 20:07:01, `[np] lobby match #1 synchronized` 20:07:06. Then:
  - `[rb] undo log ready at frame 0`, `[wt] self-test: … write tracking WORKS`, `[wt] fault cost: 32 … 9.7 us per fault`,
    `[rb] write tracking (live): 14 heap sections, 23897 pages tracked`, the memory-state lines (heap sections now perm 0x1: the
    first protect succeeded). After that **not one more game-thread line.**
  - The network thread kept logging (`np: rtc: sent 13 received 180 …` every 10 s, unchanged). Stats at 20:07:36: lvTick 1,
    npFrame 1, ggrsFrame 1, npSent 13 (frozen since the sync). The game thread was stuck inside frame 1. No new crash report.
  - The user restarted at 20:08:17 (session 626686ac); its boot report carries 47fb7c69's log tail, which ends the same way.
- Compare 18:59 (earlier build, `20261016-185942-match-hw-log.txt`): there the first *reprotect* failed with 0xd401 and tracking
  turned itself off at frame 0 ("back to compares"), so the match ran. 55a1a38 / 4142e4b fixed that reprotect, so 20:07 was the
  **first console run with write tracking live during gameplay**, and it hung at the first frame.

## Cause
`wt_Track` (the fault handler) found the fault address by scanning nn::os::UserExceptionInfo **from offset 0** for any u32 that looks
like a data-abort ESR (EC 0x24/0x25) followed by an 8-byte-aligned u64 inside a tracked range. The structure starts with the saved
registers (x0-x28 at +0x10, fp/lr/sp/pc, pstate/afsr0/afsr1, **esr at +0x124, far at +0x128**: the crash logger's decoded layout), so
the registers are scanned first. Any register whose low half is 0x90000000-0x97ffffff (e.g. a heap pointer 0x3_9xxx_xxxx: this run's
heap spans 0x30b4… to at least 0x38cc…) followed by a register holding a tracked heap pointer is taken as "ESR + FAR". The handler then
makes **that** page writable and resumes; the faulting page is still read-only, the instruction faults again with the same registers,
the same wrong page is picked: an endless fault loop on the game thread, silent (no log, no abort). The self-test can't see it: its
registers hold values outside the ranges. A second way to the same loop: `svcSetMemoryPermission(RW)` in the handler had its result
ignored, so a page it can't make writable would also loop forever.

## Fix (wtrack.cpp)
- The self-test **learns** the ESR offset: during its write, the only handled fault is one whose ESR word is followed by the exact
  address written; that offset is kept (`g_esrOff`) and logged (`[wt] self-test: … esr at +0x124: write tracking WORKS`). The test
  fails if nothing was learned. Live faults read the ESR and the fault address at that offset only (EC must be 0x24/0x25, address in
  a range); nothing is scanned any more.
- If `svcSetMemoryPermission(RW)` fails and the page still isn't writable, the fault is not handled (crash logger + abort, with the
  decoded `CRASH: … esr … far …` line) instead of a silent loop.
- Loop guard: the same page faulting more than 1000 times between two protects is not handled either (a crash report beats a hang).
  The counter resets at every ProtectAll.

## Verified / not verified
- Builds (tools/build-probe.sh); the disassembly of wt_Track reads the learned offset and only scans while the self-test learns.
- Ryujinx: **the fix was not exercised there.** Ryujinx doesn't enforce guest page permissions, so even a rollback run would only show
  the self-test failing ("handler fired 0") and the compare fallback. And none of the runs reached the undo log:
  - `tools/run-vs.sh -s 300 "vsstop 1500" "rbwrite 1"` (offline versus, no rollback): 0 differing ticks of 1499;
  - `PEER_FPS=12 tools/run-vs.sh -p "2 1500 8" … "rbwrite 1" "rbdemote 120"` (peer binary copied from the main checkout): the peer
    joined npt1, but the Switch's hw.txt had no netplay line, so it played offline as well: 0 differing ticks of 1499, no `[rb]`/`[wt]` lines;
  - `tools/lobby-vs-browser.sh … "rbwrite 1"` twice: the page connected to the local server but never joined the lobby (harness
    problem, "the page didn't join the lobby in 150 s"; also with LVB_JOINMAX=300).
  So only the build and the unchanged non-rollback path are checked; the live fault path needs the console.
- **Needs a console run.** Expected log: `[wt] self-test: handler fired 1 time(s), … registers changed by the resume 0, esr at
  +0x124: write tracking WORKS` (a different offset is fine as long as it's learned), then frames advancing with tracking live, and
  at the end the `[rb] timing` line with `rbFaults…`. If the game thread still stops: a `CRASH:` report now (far / pc decoded), not a
  hang. If it hangs anyway, the remaining suspect is two threads faulting at once on the one shared handler stack / info buffer
  (`SetUserExceptionHandler(…, g_handlerStack, …, g_info)`), not covered by this change.

## hw.txt for the retest
- To test the fix: keep the current hw.txt (`rbwrite 1`, `rbdemote 120`, `lobbypred 8`, `npdelay 2`, `rngcapture 1`). sd:/xplay/
  wt-selftest.txt says "passed" from the 20:07 run, so the self-test runs again (delete it only if it says "pending"/"died").
- **`rbwrite 0` is a safe fallback** (the default): no handler is installed, no page is protected; that is the configuration that
  played full matches before (and the 18:59 run, which fell back to compares at frame 0, played its match). Use it if the goal of the
  session is playing, not testing write tracking.

## Retest of f8dac8e (20:37-20:43): a transport mismatch, write tracking never ran
- `results/20261016-203808-crash-…log` is the same old report again (md5 892bc34e…).
- Console session e25b2f98 (telemetry #552-#574): joined lobby 235230bc at 20:41:59, StartLobby 20:42:02, `[np] session created`,
  `np: rtc: UDP <lan-ip>:54279, room 235230bc… (seat 1 of 2)`, connected to `…/room/235230bc…?peer=6bb47db9…`, then for a
  minute `np: rtc: sent 0 received 0 …, seats up 0 of 0x1`: no `[rtc] offer`, no ICE, never synchronized. So no undo log, no
  self-test, no write tracking: the match never got to frame 0. (The user restarted at 20:43:53.)
- The page: `Switch lobby: this match runs at 60 Hz over the server's relay (wss://…/relay/235230bc…)`, then TF.EX's "Failed to
  etablish a connection to the opponent". Before the page server's restart at 20:40:55 every match said "… over TF.EX's room
  (wss://…/room/…)", including the 20:07 one that connected (ICE Connected).
- Cause: the page server on :18300 (pid 38865, `tools/serve.py 18300 --diag`, started 20:40:55) runs from the browser worktree
  **switch-harness (58a0b5f)**. That build still has 2dd2757 `SwitchLobby.cs`: a 2-player lobby with a Switch player plays over the
  server's `/relay/`. Since 5a564af (10-16 14:41) the console plays lobby matches over TF.EX's WebRTC room only, so the two sides
  waited in different places. The build that matches the console is the **switch-rtc** worktree (0d43d04 "Switch lobbies (any size)
  over TF.EX's own WebRTC room", HEAD 4f10eae, publish built 19:19); tools/lobby-vs-browser.sh and `hw-peer.sh lobby` use it.
- Fix (not on this branch, nothing to change on the console): serve :18300 from switch-rtc
  (`cd ~/Code/Towerfall Browser/.claude/worktrees/switch-rtc && python3 -u tools/serve.py 18300 --diag`), reload the page, retest
  with the same hw.txt (`rbwrite 1`). `rbwrite 0` would tell nothing here: the failure is before the first frame, where write
  tracking isn't involved; keep it as the fallback only if the match then stops at frame 1 again.

## Retest with the page from switch-rtc (20:47-20:52): write tracking works; a crash on QUIT; stutter under heavy latency
### Write tracking on the console: confirmed
`results/20261016-204840-match-hw-log.txt` (session bcc05c9e): `[wt] self-test: handler fired 1 time(s), … registers changed by the
resume 0, esr at +0x334: write tracking WORKS`. That is libnx's ThreadExceptionDump layout (x0-x28, fp lr sp pc, padding, 32 q
registers, then pstate afsr0 afsr1 esr, as learned: esr +0x334, far +0x338); the crash logger's decode at +0x124/+0x128 was
wrong for the console: it now uses the learned offset (`wt::EsrOffset()`, `esr at +0x…` in its CRASH line) once the self-test ran. Tracking stayed live for the whole match (frames 0-2077), turned off cleanly at
the end (`write tracking OFF at frame 2077 (the match ended)`). Game 1: GGRS ping 116 ms, 29 rollbacks loaded, frame period p99 24.5 ms,
undo log end tick p50 1.3 ms.

### Crash after QUIT in the end-of-match menu (`results/20261016-204915-crash-01792177863_…log`, new)
- Module id 14CC1F70… = the published f8dac8e build (../switch-online-hw ELF, build id 14cc1f70). User Break from `__cxa_throw` ->
  `_Unwind_RaiseException` (no handler) via our `xplay::CxaThrowHook` (+0x32370). Game frames (tools/re callmap): `Monocle.Engine::Update`
  -> `TowerFall.MainMenu::Begin` (+0xa9edbc) -> ScreenTitle's constructor (+0xb3ee20) -> the title-texture dictionary's get_Item
  (+0xb3f8e0), whose default case allocates and throws.
- The log tail (telemetry #586's previousSessionLog): `[lobby] QUIT from the end-of-match menu: left the lobby`, `[lobby] back to the
  lobby screen (MainMenu 62)`. QUIT does `ReturnToMenu(NetplaySelect)` = `new MainMenu(62)`; MainMenu.Begin builds `ScreenTitle(62)`,
  and 62 (a TF.EX state) isn't in the vanilla dictionary. NetplayScreens mapped the netplay states to the roll call (5) only in
  `ScreenTitle.ChangeState`. Rematch / ARCHER SELECT go back with Rollcall (5), so the Ryujinx rematch test never hit it.
- Reproduced in Ryujinx with the new hw.txt test switch `lobbyvote 3` (QUIT 3 s after the results; tools/lobby-auto.sh with
  `lobbyhost 1 lobbyscript 1 lobbylength 0 lobbyvote 3`): the same abort, guest stack through MainMenu.Begin.
- Fix: `ScreenTitleCtorHook` (+0xb3ee20) maps the netplay states to the roll call like ChangeState (logs `[screens] ScreenTitle for
  netplay state 62 built with the roll call's title`). With it, the Ryujinx run got past that and threw next in
  `CreateNetplaySelect` -> `MakeButton` -> `Atlas.get_Item(null)`: the Ryujinx test host never builds the main menu's NETPLAY
  button, whose text gives the screens their managed-string class (`LiveString`), so every `Str()` was null. On the console the user
  always comes through NETPLAY, so this second throw is test-only; the end-of-match menu's item names now seed that class when it is
  missing, so the test takes the console's path.

### Stutter in game 2 (session e46a047a, `205110`/`205120-match-hw-log.txt`)
- Not the console's rollback work: update 2-3 ms, undo log end tick ~1.3 ms, **0 rollbacks loaded**, write tracking live.
- GGRS ping **851-918 ms** (game 1: 116 ms). The console sits 8 frames past the last confirmed remote input (ggrsFrame 555,
  confirmed 547 = max prediction) and its frame period spikes to 300-600 ms every ~10 frames (`[spike] … frame period 333 ms, update
  3 ms`). The page shows the same: 60 fps drawn, `online 13-21 ticks/s` from the first second of the match.
- That is GGRS waiting at its prediction limit: play stays smooth while the one-way latency in frames is below input delay + max
  prediction (npdelay 2 + lobbypred 8 = 10 frames, ~170 ms one way); ~450 ms one way is ~27 frames, so each side can only run ~10
  frames per round trip (~20 ticks/s), exactly what both logged. TF.EX on PC has the same limit (same GGRS, max prediction 8) and would
  stall the same way. Game 2's page must have run with a much larger netdelay than game 1's 80 ms.
- To tolerate more latency: raise the input delay (`npdelay`, both sides should agree with TF.EX's own delay) before raising
  `lobbypred` (deeper rollbacks, each one restoring more frames).
- Telemetry fix in passing: `rbFaultsP50/P99/Max` measured faults only inside EndTick (always 0); now faults per first-time tick
  (from one EndTick to the next).
- Seeding: the first try (taking the class from the end-of-match menu's item names) never ran, because the auto-QUIT comes before that
  menu is built. `LiveString()` now falls back to the class of a string made by the game's own `Int32.ToString` (+0x21b160, as
  levelitems.cpp does). Order in MainMenu.Begin: `CallStateFunc("Create", 62)` (our NetplaySelect) first, then `ScreenTitle(62)`.
- **Verified in Ryujinx** (tools/lobby-auto.sh, `lobbyhost 1 lobbyscript 1 lobbylength 0 lobbyvote 3 rbwrite 1`, run 210923): match vs
  the page with 0 differing ticks of 2261, then `[lobby] test: QUIT`, `back to the lobby screen (MainMenu 62)`, `[screens] NetplaySelect`,
  `[screens] ScreenTitle for netplay state 62 built with the roll call's title`, `[menu] state 62`, and the game kept running. Before the
  fix the same run aborted in MainMenu.Begin (runs 205833, 210424).
- Next console test: same hw.txt (rbwrite 1, rbdemote 120, lobbypred 8, npdelay 2, rngcapture 1); after a match, QUIT from the end menu
  should land on the NETPLAY screen. The page at netdelay ≤ 100 to judge smoothness.

## Console test of 140ea2d (21:13-21:19): the rematch never ended on the console
- Session a92c602a: match 1 played and finished (52 rollbacks, max depth 7, write-tracking faults per tick p50 63 / p99 381 / max 400),
  REMATCH voted, match #2 synchronized at 21:16:19 with a fresh undo log and write tracking live.
- The user: the browser finished the rematch normally (a kill ended it); the console kept playing. On the console the page "left (seat 0)"
  at frame 1874 (GGRS interrupted, then disconnected 15 s later); it then **simulated the level alone** (frames 2190+) until the reboot
  at 21:19:36. No match end, no crash report.
- **Match 2 had zero rollbacks**: telemetry rbLoads 52 after match 1 and still 52 at frame 1574 of match 2 (ping 283 ms, confirmed 6 frames
  behind). Against a human with that latency GGRS should load states all the time; none means the remote inputs the console's GGRS got
  never differed from its predictions: on the console the remote archer did not do what the page's player did, so the page's kill never
  happened there. GGRS desync detection (`npdigest 1` + the page's `digest=1`) was off, so no desync event was possible.
- **Fallback (done)**: a GGRS disconnect before the results now ends the lobby match and leaves the lobby for the netplay menu (TF.EX:
  15 s without the remote, no winner -> leave the lobby; LOBBY-PROTOCOL.md). Log `[vs] the remote disconnected at frame N, before the
  results: the lobby match ends here`, telemetry `lobby_remote_left`. Verified in Ryujinx (run 212427: the page navigated away at frame
  ~73; `left (seat 1)`, disconnected at frame 544, the lobby left, NetplaySelect, `[menu] state 62`, running).
- Per-match counters (new): `[np] lobby match #N over at frame F: T first-time ticks, remote input changes C, rollbacks R` at every
  lobby match's end; telemetry `npRemoteChanges`, `rbLoadsMatch`.
- Ryujinx rematch (run 213147: lobby-auto, page scripted, both vote REMATCH, the page navigated away ~1176 frames into match 2):
  match 1 `2263 first-time ticks, remote input changes 110, rollbacks 60` (0 differing ticks of 2262 vs the page); match 2
  `1176 first-time ticks, remote input changes 58, rollbacks 34`, a death in match 2, then the disconnect fallback. So in Ryujinx the
  rematch rolls back as match 1 does: the console's 0 rollbacks in its rematch is not reproduced. Not compared: match 2 tick by tick
  (the harness compares the first match only).
- What the console's rematch looked like (user): both screens matched until the kill; the page's archer moved the same on the console
  (its inputs arrived). 0 load requests over 1574 frames with inputs changing means the console was never ahead of the page's inputs
  when they changed (or GGRS didn't report it); a kill only on the page is then a desync, and only the digest can locate it.
- Needed for the next console test: GGRS desync detection on both sides. Console: hw.txt `npdigest 1`. Page: only xlobby/xnet's
  `digest=1` enables it today, and xlobby replaces the hand-played input with its (empty = neutral) script; the browser needs a
  digest-only switch (e.g. `?digest=1` calling GameplayDigestHook.Enable at load) for a hand-played page.

## Console test of 694d6c6 with the digest (22:35-22:38): match 3 never started -- TLS contexts leaked
- Session 44787170: match 1 (1300 ticks, remote input changes 0, rollbacks 0), REMATCH, match 2 (882 ticks, 42 changes, 4 rollbacks),
  no DESYNC line in either; d12be00's buffer fixes applied at boot. Match 3: the console froze at its start, the page timed out.
- Telemetry went silent at 22:37:13 (match 2's round end): `ws: TLS setup with tfex-server… failed 0xe47b`, `[tele] report
  'lobby_match_finished' not delivered`. The same error silenced telemetry at 21:16:49 (session a92c602a).
- Cause: net.cpp created an `nn::ssl::Context` for every TLS connection (telemetry posts every 30 s and per event, the matchmaking
  socket, each match's room socket) and never destroyed one (`Connection::Destroy` only; `Context::Destroy` wasn't even bound). Both
  failing sessions got exactly 16 TLS contexts through (13 telemetry posts + the matchmaking socket + 2 room sockets) before every
  further setup failed with 0xe47b: in 44787170 the 17th+ were telemetry and then match 3's room socket, so match 3 never connected
  (console waiting at the match start, the page's sync timeout).
- Fix: `nn::ssl::Context::Destroy` bound and called after `Connection::Destroy` on every close and on every failed setup (ws Conn: telemetry,
  matchmaking, room; the relay conn; the 7b probe).
- "faults 103166" in match 2's self-test line is the cumulative fault count (match 1's ~79 per tick x 1300), not a problem.
- Match 1's "remote input changes 0" means the remote archer's input never changed on the console in that match (an idle browser player
  would do that); to check with the user.
- Workaround on 694d6c6 (no hw.txt switch for telemetry): restart the game before each match (each boot gets the full budget).
  rbwrite 0 doesn't help; write tracking isn't involved.

## Console desyncs with write tracking live (22:42 RNG, 22:56 the Switch archer): the resume's vector registers
- Ruled out in Ryujinx: the in-match GC (run 230003: seed 97086193, `rbgcmb 1`, 6 collections inside the compared window, 74 rollbacks,
  0 differing ticks of 2554); input conversion (InputState at player+0x148 = TF.EX's ToTFInput layout, lossless f32 on the wire,
  moveAxis from the integer MoveX/MoveY on the Switch as on the PC: analog sticks don't change running).
- Every desync so far was on the console with `rbwrite 1`; Ryujinx (no faults: write tracking falls back to compares) never desyncs.
  wt_HandlerEntry reloads x1-x29 from UserExceptionInfo but the vector registers from its own copy taken on entry, after the SDK's
  exception stub has already run (it copies the context: memcpy uses q registers). The self-test only checked d0 and d8.
- Change: the self-test runs two more probes with all 32 vector registers (both halves) set to known values across a tracked write:
  first resumed with the entry copy (the old behaviour: the changed mask is logged), then with learning on: the handler finds q0..q31 in
  the info (q0's known 128 bits followed by q1's; learned offset `wt_FpuOff`) and the resume reloads every vector register from there
  (the kernel's saved values). Log: `[wt] self-test: vector registers changed by the resume: <old mask> with the entry copy, <new mask>
  restored from the info (q0 at +0x…)`. If the info's copy can't be found or is wrong and the old path changes registers, write
  tracking turns itself off for the match.
- Not testable in Ryujinx (no faults). Console check: that self-test line (expected: old mask non-zero, new mask 0), then matches with
  `npdigest 1` and no DESYNC. Meanwhile `rbwrite 0` avoids the handler entirely.

## Match 3's higher ping (22:55): the console's network thread slowed down
- All four matches: direct WebRTC (host candidates, ICE Connected in ~2 s), no relay/TURN; GGRS ping 101 / 16 / 217 / 100-167 ms.
- `np: rtc` windows (10 s): matches 1-2 ~10,000 loops, udp in/out ~0.3-0.6 s; match 3's second window 926 loops, udp in 4875 ms, udp out
  4225 ms (~5 ms per loop); match 4 starts at 10212 loops, then 3126 and 847. Each match starts fast and slows the longer and busier it
  runs; the next match's new WebRTC session starts fast again.
- Not telemetry: during matches only event reports are posted, and match 3's slowest window (~22:55:03-13) had none. Not a socket leak
  (RunRtc closes and frees everything at a match's end), not the GC (one collection per match, at frame 1).
- Open: something growing within a match's WebRTC session (or GGRS packets growing with the ping), or write-tracking faults on the
  network thread (tracked ranges are Boehm's heap sections rounded out to whole pages). The `rbwrite 0` test separates the last one.
- New: hw.txt `telemetry 0` (default on) turns the reports off, for a network test without them.

## For the slowdown session: the console's network thread slows within a WebRTC session
- Evidence: `np: rtc: … N loops in 10 s` / `ms in ws …, udp in …, udp out …` (net.cpp RunRtc, every 10 s). Matches start at ~10,000 loops
  (udp in/out ~0.3-0.6 s per 10 s); in longer/busier matches it falls to 926 / 847 / 205 loops with udp in 4.9-6.0 s, udp out 3.7-4.2 s
  (~5-10 ms per loop). The next match's new session starts fast again. GGRS ping rises with it (match 3: 217 ms vs 16-101).
- Ruled out: write-tracking faults (still there with `rbwrite 0`, 23:07-23:09), telemetry posts (none in the slowest windows; there are
  no periodic posts during a match), socket/rtc reuse (RunRtc makes a new UDP socket, tfx_rtc and room socket per match and frees them;
  only the sent/received counters are cumulative), the in-match GC (one collection per match, at frame 1), the route (direct host
  candidates every time, no TURN/relay).
- Where the time goes: "udp in" = poll + recvfrom + `tfx_rtc_udp_in` (str0m: DTLS decrypt, SCTP); "udp out" = `tfx_rtc_udp_out`
  (encrypt) + sendto. The data channel is already unordered with no retransmits (tfx-rtc src/lib.rs channel_config). The cost per loop
  grows within one str0m session.
- Build: ggrs-switch/tfx-rtc release with opt-level 2, pure-Rust crypto (str0m-rust-crypto patch; target spec cortex-a57: check whether
  the AES/PMULL instructions are enabled, else AES-GCM runs in software).
- Next steps: per-window datagram/byte counts in and out; str0m's channel `buffered_amount` and SCTP queue sizes per window; time
  `tfx_rtc_udp_in` / `udp_out` separately from the socket calls; a Mac self-test of tfx-rtc with a long busy session to see if the cost
  grows there too.

## Browser quits mid-match (23:09): the console waited 15 s; lobby map default
- Match #5: the page quit through TF.EX's menu at frame 528; the console's room session logged `left (seat 1)` / `ended` at once, but the
  game stood at frame 528 until GGRS's disconnect 15 s later (interrupted 23:09:20, disconnected 23:09:35), then the fallback went to
  NETPLAY. Fix: when the room session has ended (net.cpp ends it once every other player has left the room) a lobby match before its
  results ends at once (`[vs] the room connection ended at frame N: every other player left`); also covers a room that never connects.
  Ryujinx (page navigated away): ended at frame 350 right after `left (seat 1)`, NETPLAY, state 62.
- Same map every match: our lobby builder defaulted to MapId 0 (Sacred Ground); TF.EX's GameData.MapId defaults to -1 (RANDOM) and its
  MAP button starts there. Default now -1 (MainMenuPatch g_mapId, NetplayScreens g_bMap); hw.txt `lobbymap N` still overrides.

## The vector-register fix on the console (3684728, 23:14-23:16): desyncs gone, but the self-test saw no difference
- `[wt] self-test: vector registers changed by the resume: 0 with the entry copy, 0 restored from the info (q0 at +0x120)`; 4 lobby
  matches with write tracking live and npdigest 1: 0 DESYNC (with rbwrite 0, 23:07-23:09: 0 of 5; before the fix with rbwrite 1: desyncs
  in 3 of 5 matches at 22:52-22:56, and at 22:42).
- The probe (game thread, a small known register set, a write right after the setup) didn't see the old path change a register, so the
  mechanism isn't proven: other threads' faults, two faults at once on the one handler stack / info buffer, FPCR/FPSR, or chance (0/4
  after 3/5 is suggestive, not conclusive).
- Measured from now on, on every real fault (no behaviour change): the entry copy of q0-q31 against the kernel's (info +0x120), faults on
  threads other than the self-test's (tpidrro_el0), handler entries while another ran. At each match end: `[wt] this boot so far: N
  faults, A on other threads, B handler entries while another ran, C with the entry copy's vector registers != the kernel's (first:
  mask .. at pc .. = main+..)`. C > 0 proves the theory and names the instruction; C = 0 over many matches with other threads or
  concurrent entries > 0 points at those instead.

## Measured on the console (03ac730, 2026-10-17 ~00:14-00:30): the old resume path did change vector registers
- `[wt] this boot so far: 279860 faults, 0 on other threads, 0 handler entries while another ran, 49 with the entry copy's vector
  registers != the kernel's (first: mask 0x3fff007f at pc … = main+0xc54ac)` (20 after match 1, 34 after match 2, 49 after match 3);
  3 lobby matches, 0 DESYNC.
- So: the copy wt_HandlerEntry took on entry (after the SDK's exception stub ran) differed from the kernel's saved registers in 49 faults,
  in q0-q6 and q16-q29 (the caller-saved vector registers: the stub's own use). The old resume restored those values into the faulting
  code; since 3684728 the resume reloads the kernel's copy (info +0x120). No concurrency: every fault was on the game thread, one at a
  time.
- The first one: main+0xc54ac `str x23, [x0, #0x10]` in a MonoGame SpriteBatch draw (the overload with eight float arguments, frame at
  +0xc5448), writing into a SpriteBatchItem the batcher (+0xc8800) just handed out. That function had already moved its floats into
  v8-v15 (callee-saved, not in the mask), so that site was harmless; any code holding floats in q0-q7 / q16-q31 across a first write
  to a tracked page got them changed, which is how the console-only desyncs could happen (a speed of 0.1 vs 0.32, the RNG).
- The 49 are rare (0.02% of faults): the stub's effect on those registers only shows when they held other values.
- Cost of the measurement: a 512-byte compare per fault (~0.1 us next to ~9-13 us per fault). Kept: it guards the fix (a non-zero count
  is harmless now, it only reports what the old path would have done) and catches concurrency if other threads ever fault.

## Desync in lobby match #4 (03ac730, 00:35): the RNG at the first round's start, before any rollback
- `4209 first-time ticks, remote input changes 151, rollbacks 57`; the first DESYNC at frame 360, the first rollback at 559. At 360 both
  players are identical on both sides (console p0=43870000,43540000 / p1=42a40000,43540000,bf866677; page the same) and only the
  gameplay RNG differs (console 0252515e…, page 32b4ace4…); 240 matched. So a draw in frames 241-360 (the first round starts at 266)
  happened differently, with no rollback involved.
- Matches #1-#3 of the same boot: also map -1 (random tower), but the browser archer idle (0 remote input changes): no desync. Match #4 is
  the first with the browser side played. Same signature as 22:42 (RNG only).
- Console-only candidate left in the fault handler: FPCR/FPSR are restored from the copy taken after the SDK stub (the q registers come
  from the kernel's since 3684728). Now measured too: `[wt] FPCR/FPSR: info +0x320/+0x324 vs the entry copy differ in N / M faults
  (first fault: info … , entry …)`. A non-zero FPCR count (if +0x320 is the kernel's FPCR) would explain float-gated draws differing.
- Split test: `rbwrite 0` with the browser side played. Desyncs gone -> write tracking (FPCR or another register path); still there ->
  the simulation itself (a draw depending on the remote seat's input path).

## Prepared for the next split (00:40)
- If `rbwrite 0` makes the desyncs go away: 5608d3e restores FPCR/FPSR from the kernel's copy as well. The self-test sets FPCR 0xc00000
  (round toward zero) and FPSR 0x0800009f across a tracked write, learns where the info keeps them (scanning the words after q31; libnx's
  ThreadExceptionDump has pstate/afsr0/afsr1 there, the console 5 words before ESR, so the offsets are learned, not assumed) and logs
  `[wt] self-test: FPCR/FPSR set …/… across the resume: …/… with the entry copy, …/… from the info (at +…/+…)`.
- If they stay: RNG traces. Console: at the first DESYNC of each lobby match the last 256 gameplay draws are dumped (`[np] the last 256
  gameplay draws …`, then `[vs] draw N frame F site +X`; always recorded, no tracing cost); hw.txt `rnggametrace N F` logs draws live
  from frame F (`[rnd] gdraw …`). `tools/draw-sites.py <log> [F0 F1]` names the sites (callmap). Page: `[xplay2rng] draw … ggrs_frame=F
  <callers>` exists only under xlobby/xplay2 (`rngtrace=N rngfrom=F`), and xlobby replaces the hand-played input; a hand-played page
  needs a URL switch like 2736735's `?digest=1` (e.g. `?rngtrace=N&rngfrom=F` setting rngTrace/rngFrom and installing XplayVersus's
  RngDrawPrefix without xlobby's input hook) — a browser-repo change.

## ab126ab on the console (00:50-00:53): goal 5 in sync; FPCR cleared; a drawing offset in round 5
- Lobby match #2: goal 5 (length 2), tower 7 (King's Court), 4524 ticks, 211 remote input changes, 62 rollbacks, 0 DESYNC.
- FPCR/FPSR: the self-test set 0xc00000/0x800009f across a tracked write and the old resume (entry copy) kept them; the info holds them at
  +0x324/+0x328 (learned). So FPCR is cleared as a desync cause; `wtfpcr` can stay off. The per-fault comparison had read hard-coded
  +0x320/+0x324 (pstate 0x60000000 and FPCR): its "differ in 394956" was meaningless; it now uses the learned offsets.
- Vector registers: 38 mismatches this boot, all handled by the q restore.
- Round 5's map drawn at an offset on the console only: most likely Engine.Screen.Offset (the screen-shake offset) left non-zero from
  round 4's final kill at 2498 into round 5's level. TF.State's LevelLoaderXMLStatePatch.Reset zeroes it at every level load; our port
  skipped it as cosmetic (docs/ws-tfex-update.md). Drawing only (the digest stayed equal). A TF.EX-base parity item.
