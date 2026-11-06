# ws/match: Integration B, "match & after"

## HANDOVER (2026-10-18, for a fresh session): state, next rows, parked
**State (master + ws/match):** TF.EX's NetplayManager is generated; its match start (genstart) and disconnect handling (genend) are the
default; the hand disconnect path, the scripted test host (lobbyhost / LobbyTest.cpp), the hand narrow tower pick, QuitToNetplayEntry and
OnInitialConnectionFailed are deleted; the end-of-match menu is TF.EX's generated PauseMenuPatch / HUDPatch / MatchEndChoices /
LightPauseMenu; the match start reads TF.EX's own lobby (gen::svc::GetOwnLobby). Smoke scenarios (slot B, BROWSER_LOBBY_WT=
towerfall-gamefiles-upload-42ca5d): lobby, join, rematch, quit, disconnect, team all PASS. Rule of the move (AGENTS.md "Operating mode"):
each batch ends with TF.EX's code ON by default and the hand code DELETED; no investigations; ask A / the translator directly.

**Batch 4 (2026-10-18, B's successor, branch claude/ws-match2-f9e559): items 1-3 done.** Rematch / archer select run TF.EX's own
generated RematchLobby / ArcherSelectLobby handlers (A's GenDispatch intercept and the Consume* flags deleted): RematchLobby's
`Engine.Scene = new MapScene(Versus)` -> MapScenePatch's BeginHook sees a MapScene begin while the lobby awaits the vote: its buttons
are made with the lobby's unlocks, then EndOfMatch::RematchMapScene runs the native start underneath (NetplayManagerStartLobbyMatch),
whose Enter selects on that MapScene (no second GotoVersusLevelSelect). ArcherSelectLobby -> TF.EX's ReturnToMenu(Rollcall); the
Switch's teardown is QuitTeardownFrame's (it now covers QUIT and ARCHER SELECT; archer select keeps TFGame.Players, as TF.EX).
INetplayManager.Reset -> the generated NetplayManager.Reset (svc::NetplayReset); GetEndGameStatus -> TF.EX's generated one (rule and
the services' hand copy deleted); HasFailedInitialConnection / GetFailedConnectionMessage / ConsumeAbortToVersusOptions -> the generated
NetplayManager object (svc::GenNM); NetplayManager.cpp's hand flags and NetplayManagerAbortToVersusOptions & co deleted.
Item 4 waits on A (asked). Item 5: NetplayManagerOnEvent needs a generated GetEventAndUpdate event reaction (translator), the tower
code left is WiderSet's (S-wide).

**The rbwrite 1 desync of 2026-10-20 06:37 (B2, code reading; build-checked only).** The log: digests agree at 120-480, both players die
at 513-514 (remote p0 cause 10, the Switch's p1; TF.State's generated PlayerStatePatch LoseHat twice), cold hits on game globals at
513-518, the round-2 level load (line 508), then "[rb] demotion at frame 600" and the first DESYNC at 600. Findings: (1) demotion
can't change the game: Demote only drops never-changed UNTRACKED pages (globals / Boehm metadata) from the hot list; it neither
writes game memory nor (re)protects anything, and a demoted page is verified by the cold sweep; it runs every 30 frames, GGRS checks
every 120, so 600 is where both fall; the divergence lies in 481-600 (the double death and the round change). (2) Without a rollback
the undo log is never applied, so lost / late undo entries can't explain it either. (3) Write tracking can change a run that never
rolls back only through the fault handler's register state (checked 2026-10-19: exact with the kernel's copy learned, as here) or
through kernel writes into protected pages: those don't fault, they fail. The two nn::fs::ReadFile overloads the game imports are
exposed first, but a failed permission change in Expose and a failed read were never noticed, and faults on other threads /
overlapping handler entries (one handler stack, one info buffer) were only counted for the whole boot, at the match end (this
match crashed first). New, decisive lines at every GGRS check frame (first-time ticks): "[wt] frame F: faults +N (other threads +a,
overlapping handler entries +b, entry/kernel mismatches +c), fs read pages exposed +e (not made writable +x, failed reads +r)", one
"[wt]   <event>: pc main+X / page / thread" line per odd event, and "[rb] frame F audit: N tracked pages changed without a fault" (the
first 5 checks, then every 10th; non-zero = writes the handler didn't see, with the first pages described). Console check: one lobby
match with rbwrite 1 (and one with rbwrite 0 on the same build): at the first DESYNC, read the "[wt] frame" lines of the checks before
it: an "other thread" / "overlapping" / "not made writable" / "failed read" event, or a non-zero audit, in the window before the first
bad check names the mechanism; all zero with the desync still there means write tracking's handler and I/O path are cleared and the
difference is elsewhere (its timing).

**The early copy is the answer; the residual faults (2026-10-20, B2; build-checked only).** Console 16:16 (master b2fda65, the
early copy on): the first long write-tracked match in sync (3547 frames, 21 rollbacks), and at float stores the early copy held clean
floats where the stub's copy held zeros / garbage (430.0 vs 98.0, -80.0 vs 0x00000640200208e3, 7.0 vs 0 ...): the stub's kernel calls
change v0-v7 before its copy, and the old resume wrote those values into game memory. Now the early copy is the only resume path
(the A/B "wtvec" and the info / entry resume paths removed; a fault without an early copy keeps the entry copy and is counted).
The residual faults of that boot (a stretched sprite once; match 2 crashed at the next round's level load reading 0x412cccdc, a float
with the same kind of off tail, as a pointer: TowerFall +0x3b06f0 `ldr w0, [x0,#0x10]`, a count getter called from +0xbae964 on an
object read from the level loader's [x19+0x38]): what the code and that log rule out: (1) other-thread faults, overlapping handler
entries, failed fs exposures / reads, pages changed without a fault: all 0 at every check of both matches; (2) the GPRs: x9-x29 come
from the stub's pushes made before any call (sdk+0x32fa3c-0x32fa64), x0-x8 / lr / sp / pc / pstate (NZCV) from the kernel's context,
x16 / x17 used by the hook only after the stub saved them; (3) a rollback restore of pages written through a fault: OpenDirty marks then
opens each restored tracked page, its next take compares and reprotects it; (4) the runtime's in-window allocations: 4 warnings, all at
frame 0 of match 1, and no rollback reached frame 0. Left, counted from now on at every check: "early copy overwritten before its resume
+N" (an exception while another is being handled: the second one's hook would replace the first one's copy) and "resumes WITHOUT the early
copy +N". If both stay 0 and a float lands in a pointer slot again, the remaining candidate is the resume's own kernel call
(svcReturnFromException): the stub's kernel calls changed the vector registers, so the one after our restore could too, rarely; that
needs a resume that restores the vector state after the kernel call (not possible from user mode without the kernel's help: a design
question, not a quick fix). Console check: boot with rbwrite 1: "[wt] self-test: early register copy (sdk+0x32fa68): hooked; 2 probe
faults passed it, vector registers changed by a resume from it 0" and "the resume restores v0-v31, FPCR and FPSR from the early copy";
two lobby matches past frame 1200 in sync; in each "[wt] frame F" line "early copy overwritten before its resume +0" and "resumes
WITHOUT the early copy +0"; no stretched sprite and no crash at a level load. Any non-zero count names the fault path.

**The early register copy (2026-10-20, B2; build-checked only, coordinator-approved).** wt_EarlyEntry is hooked at sdk+0x32fa68 (right
after the SDK stub's GPR pushes, before its kernel calls; the hook checks nine instruction words of game 1.4.1.6's sdk first and
refuses another SDK): it stores q0-q31, FPCR and FPSR into wt_Early and continues the stub. The resume restores v0-v31 and FPCR / FPSR
from that early copy (wt_VecSrc 2). Boot safety: the hook is installed and proven inside the self-test, on its own probe faults (all
32 vector registers and FPCR / FPSR set to known values across a tracked write, resumed from the early copy): if the hook can't be
installed, no probe fault passed it, or any register comes back changed, "[wt] ERROR: self-test: the early register copy isn't
proven: write tracking off for this boot"; a crash there is caught by the existing self-test guard (the next boot skips the self-test).
A/B: hw.txt "wtvec early|entry|info" chooses the copy (default early). Each mismatch now logs all three: "[wt] mismatch #N: pc main+X
(insn) STORES vR: early <64 bits>, info <64 bits>, entry <64 bits> (the resume used ...); vectors entry/info M1, early/info M2", or for
other instructions both masks. Console check: boot with rbwrite 1: "[wt] self-test: early register copy (sdk+0x32fa68): hooked; 2 probe
faults passed it, vector registers changed by a resume from it 0, FPCR/FPSR 0xc00000/0x800009f" and "the resume restores v0-v31 from the
early copy"; then a lobby match past frame 1200: in sync = the cause was the late copy (the vectors changed during the stub's kernel
calls) and the fix holds; still desyncing = read the "STORES vR" lines: early == info at those stores clears the stub window too, and
one match each with "wtvec info" / "wtvec entry" compares.

**Which register copy is right at a write-tracking fault? The SDK's own code (2026-10-20, B2).** Source: the game dump's sdk NSO
(~/Roms/Switch/Towerfall ExeFS/sdk, decompressed with tools/re/nso.py; text 0..0x52d880). nn::os's user-exception entry is
sdk+0x32fa3c (crash reports: UserExceptionHandler + 0x28c = sdk+0x32fcc8, the return address after the call into our handler):
- 0x32fa3c-0x32fa64: pushes x8-x29 on the faulting thread's stack (x0-x7 and x30, sp, pc, pstate stay in the kernel's context, passed
  in x1). No SIMD / FP register is written anywhere in the stub.
- 0x32fa98: a call through a function pointer (sdk data +0xc12680, resolved at load; its arguments, x0 = &out, x1 = 8, x2 = x3 = 0,
  have the shape of an svc wrapper), and 0x32fabc `bl 0x32fd58` (finds the registered info buffer and stack; itself two calls through
  pointers at 0x32fdf0 / 0x32fe18). These are the only code before the copy.
- 0x32fad4-0x32fbe0: UserExceptionInfo: type at +0, then (from +0x10) x0-x8 and lr / sp / pc from the kernel's context, x9-x28 from the
  stack pushes. **0x32fbe8-0x32fc24: `stp q0,q1 ... q30,q31` into info+0x120: the LIVE vector registers** (the kernel's context has
  none), then FPCR / FPSR (+0x324 / +0x328) by mrs, ESR / FAR from the kernel's context.
- 0x32fc64-0x32fcb0: on the handler stack, q0-q31 and FPCR / FPSR again, then `blr` into our handler (0x32fcc4); after it returns it
  reloads them (0x32fcc8-0x32fd10) and the GPRs: the SDK's own return path, which our handler bypasses with svcReturnFromException.
Verdict: neither copy is "the kernel's". UserExceptionInfo's vectors are the stub's read of the live registers at 0x32fbe8; our entry
copy reads the same registers at our first instruction; between the two the stub executes no instruction that writes a SIMD / FP
register (stores, mrs, scalar moves, one blr). So the two copies can only differ if the registers change underneath the thread inside
the exception window (an asynchronous change: preemption / the kernel's FP context handling while the thread runs the handler), not
through any code path. Of the two, UserExceptionInfo's is the earlier and the closer to the fault, and it is what the resume uses: the
resume does not pick the wrong copy, so there is nothing to switch in wtrack.cpp. But the same reasoning says UserExceptionInfo's copy
is only right if nothing changed the registers in the window BEFORE 0x32fbe8, and that window contains kernel entries (the calls at
0x32fa98 and in 0x32fd58). The console's mismatches at float stores (#97 `str s0` with v0 differing ...) show such changes do happen in
the later window; one in the earlier window would corrupt the stored float exactly as the 0x3e9999ad drift looks, with both copies
agreeing. That isn't settled by code: the next step is to capture q0-q31 / FPCR / FPSR before any kernel entry (a hook at sdk+0x32fa68,
after the GPR pushes, that stores them and continues) and resume from that copy, logging the three copies per fault; or the console's
"STORES vR" lines and the wtvec A/B. Proposed, not done (the instruction was to fix only a wrong copy choice).

**Write-tracking desync: bookkeeping or fault/resume? (2026-10-20, B2).** Test tool: hw.txt "rbwrite 2" (simulated write tracking,
rollback.cpp, already there): every bit of the bookkeeping (the tracked / untracked split, the hot / cold lists, demotion, the undo
log's tracked-page path) with the written pages found by comparing every tracked page with its shadow each tick: only the protection,
the faults and the handler's resume are absent (Ryujinx doesn't enforce page permissions, so rbwrite 1 falls back to compares there).
Runs (slot b2, lobby against the browser, scripted movement, to vsstop 1300, rbdemote 120): rbwrite 2 in sync, 0 differing ticks of
1298, digest clean, rollbacks and demotion included (runs/070918 of /tmp/xplay-runs-87c3cfc3-b2); rbwrite 0 the same (runs/071136).
**Verdict: the bookkeeping is clean; the fault/resume is the suspect.** And the console's own list points at it: the mismatches of
06:50 (entry copy != the kernel's copy) include faults AT float stores whose stored register is itself in the differing mask, e.g.
#97 main+0x94c8bc `str s0, [x19,#0x34]` with v0 differing, #95 main+0x92ddd0 `str s0, [x0,#0x14]`, #93 main+0xaaf60 `str s8, [x8]`.
The resume writes the stored float from the kernel's copy; if that copy isn't the faulting code's live value (stale), the store
writes an older float: the digests' tiny speed drift (0x3e9999ad where 0.3 is 0x3e99999a, 0x3fa666b2, 0xbfd99980) with no RNG draw
in between fits exactly. FPCR (FZ / DN / RMode) and FPSR: equal in both copies on every console fault so far; the exclusive monitor
(an exception return clears it: STXR fails and the loop retries) and partially done pair stores (restarted whole, idempotent) can't
change values. New log: a mismatch at a SIMD/FP store now reads "[wt] mismatch #N: pc main+X (insn) STORES vR: entry copy <64 bits>,
kernel's copy <64 bits> (the resume used ...)". New test switch (off by default): hw.txt "wtvec entry": the resume restores v0-v31
from the entry copy instead of the kernel's. Console check: two lobby matches with rbwrite 1 on the same build, one plain, one with
"wtvec entry". In the "STORES vR" lines, the copy whose value is the clean one (e.g. 0x3e99999a, not ...ad) is the faulting code's;
the match that resumes from it stays in sync past frame 1200 and the other desyncs. If both desync, the vector copies are cleared and
the remaining difference is outside the registers.

**The crash after "every remote gone" (console 2026-10-20 06:40, fixed by B2).** At tick 1532 the remote left the lobby: TF.EX's
HandleLobbyUpdate ran InputService.EnsureLocalControllerSeat mid-match, which re-seated the local controller and left seat 1's
PlayerInputs slot null ("PlayerInputs non-null mask now 01"); after the session's end and "Lobby left!" the vanilla Level.HandlePausing
read the null slot (+0xa7c0dc, far 0). On the PC TF.EX's Level_HandlePausing prefix runs InputService.EnsureEveryControllerSlot every
frame while the session is up (an empty seat gets a FakeController), so no joined seat is ever left without one; the Switch's rule for it
was a no-op ("xplay fills every seat at the match start"). Now the rule is xplay::EnsureEveryControllerSlot (lobbymatch.cpp): an empty
seat gets the local controller object (TFEX-DIFF S-input, emptied when the Level is left), from TF.EX's three call sites. Checked: smoke
disconnect PASS (the level runs on to vsstop 2400, no exception). Console check: a lobby match the other player leaves mid-match: "every
remote gone: TF.EX's IsDisconnected, the level runs on locally", no CRASH after "Lobby left!", and any "TF.EX's EnsureEveryControllerSlot:
seat N had no controller object" line names a slot it refilled.

The full log of that match (.devserve/results/20261020-064046-prev-hw-log.txt, the build before the per-check lines) clears more:
0 faults on other threads, 0 overlapping handler entries, 155 entry/kernel mismatches with 0 touching d8-d15 and 0 FPCR, every resume
from the kernel's copy; no write-tracking fallback until the match end. Time is pinned per tick (TimeMult, DeltaTime, DeltaTicks: TF.EX's
Level_Update prefix, xplay.cpp), so slower frames can't change the step. The remote checksum stays the same at 600 and 720 while the
Switch's changes: the round's outcome differs (the double death at 513-514). Left: kernel writes into protected pages failing, and
writes the handler never saw, which the new per-check lines and audit show.

**Does write tracking's fault handler corrupt registers? (2026-10-19, B2, code reading; build-checked only)** The resume
(wtrack.cpp wt_HandlerEntry -> svcReturnFromException(0)): the kernel restores x0-x8, lr, sp, pc and pstate (NZCV); the handler reloads
x9-x29 (x1-x29) from UserExceptionInfo; v0-v31 (full 128 bits) from the kernel's copy in UserExceptionInfo when the self-test learned
its offset (q0 at +0x120 on the console), else from the handler's entry copy; FPCR / FPSR from the entry copy (the console: 0 of
83,878 faults differ from the kernel's). "Entry copy != kernel" = registers the SDK's exception stub used after the kernel saved them:
the kernel's copy is right, and with the offset learned the resume uses it, so the 81 mismatches of the 21:06 boot (v0-v7, v20-v31)
were overwritten, not seen by the game. The handler's own C++ / SIMD use is undone the same way (everything is reloaded after it);
it can't fault inside itself (it writes only module globals, never protected); faults on other threads and overlapping entries were
0 on the console (one handler stack / info buffer: both would be a hazard, and both are counted). Restarted stores (STP across a page,
exclusives, writeback) are architecturally restartable. **Verdict: no, on a boot where the self-test learned the kernel's vector copy
(as the 21:06 one did), write tracking resumes the exact register state; it can't explain that boot's desync.** One real hazard,
fixed: if the offset was NOT learned but the self-test's single probe saw no change, tracking stayed on and every resume reloaded
v0-v31 from the entry copy (wrong in ~0.1% of faults): now write tracking stays off then ("[wt] self-test: the kernel's vector-register
copy wasn't found ... write tracking off"). New log for the next console test: at each match end, "[wt] mismatch #N: pc main+X (insn
XXXXXXXX): vectors MASK[, CALLEE-SAVED d8-d15][, FPCR][, FPSR] (the resume used the kernel's copy)" (the ring's last 32, 200 per boot),
then "[wt] this match: N faults' entry copy differed ... vector restore from the kernel's copy ON|OFF". To settle the desync itself:
one console lobby match with rbwrite 0 (compares only) against one with rbwrite 1, same build: if both desync at 360, write tracking
is cleared.

**genstate (2026-10-18, build-checked only; the translator's 37949a7).** hw.txt "genstate 1" (off by default) installs TF.State's generated
patches (chained hooks) and these hand ports step aside (only Orig): ArrowCushionPatch (rnd.cpp AddArrowHook), LevelEntityPatch
(tfstate_ports LevelEntityRenderHook), ChainPatch (ChainCtorHook), Crown / SteelHat / WoodenHat RngPatch (HatUpdateHook0-2), LavaPatch
(BubbleCompleteHook), LavaControlPatch (LavaCtorHook, LavaAddedHook, LavaExtendHook), PickupPatch (PickupCtorHook's target record; the hand
PickupTweenHook then finds no target and runs the vanilla tween), PlayerStatePatch (LoseHatHook). New under genstate 1 only: Bat, PlayerGhost,
Slime. Hand ports that stay either way: Miasma, Icicle, SensorBlock, BrambleArrow, corpse, LevelRandomGeometry, MoonGlass, OrbPickup,
WhiteArcherPrison, rnd.cpp's EnterFallMode / arrow drop / RNG registration. Test-day check: the same lobby match (fixed seed) with
genstate 0 and 1 against the browser: 0 differing frames in both (tick log compare), on towers with lava (Sunken City / The Amaranth),
chains, hats (crown) and pickups; genstate stays 0 until both are 0.

**Digest version 2 (evaluation 2026-10-18 rec 6): spec, not implemented (off by default; both sides switch together)

Why: version 1 (digest.hpp) covers the rng, each seat's alive / Position / Speed and the arrows. The state that decides a hit can
differ unseen until a death (the recurring "the Switch kills a remote archer" desync). Version 2 adds, per alive seat, after Speed:
- u32 Player.State (the StateMachine's current state index: Normal, Ducking, Dodging, LedgeGrab, Dying, Frozen ...)
- u32 the dodge timer / dodge cooldown counters (Player's dodge cooldown counter and the Dodging state's end timer, float bits)
- u8 Invincible (Player's invincibility flag) and u32 its timer (float bits)
- u8 HasShield, u8 the hat state (HatState: Normal / Crown / None), u8 Aiming, u8 Ducking flag
(field names as TowerFall's Player; the Switch's offsets come from tools/re fields.py / classmeta.py at implementation time; the
browser reads the same fields by name in web/Netplay/GameplayDigest.cs)
Bytes: u32 version (2), then as version 1 with the per-seat block extended in the order above, little-endian, FNV-1a 64.

Switching: off by default. The Switch: hw.txt "digestv 2" (npdigest.cpp Compute fills the new fields; digest.hpp Compute writes them
when version 2). The browser: a page URL parameter `digestv=2` (the same place as `digest=1`), GameplayDigest.cs writes the same bytes.
Both sides must use the same version in a match, or every check reports a desync. Tests: hw-probe/tests/refgen/Digest.cs and
digest-test.cpp get version-2 reference vectors (tests/digest-ref.txt), so both twins are checked byte for byte before a test day.
Test day: one match with digestv 2 on both sides: "[digest] frame N" lines show the new fields, 0 desyncs in a clean match; then the
next hardware kill desync shows at which frame the remote archer's state first differs.

**Wave 3 (2026-10-18, build-checked only).** Rollback cost: hw.txt "rbfastrestore 1" (off by default) opens only the restored tracked
pages instead of unprotecting / reprotecting every heap section per rollback; write-tracking the globals and metadata not done (new
protected classes need a console fault-handler run). Hooks: exlaunch chains trampoline hooks per address (the entry rewritten, Orig =
the previous callback); Miasma.Dissipate's double install removed; the hand glue around generated prefixes (PlayerPatch,
SessionPatch, PauseMenuPatch, HandlePausing) is not retired yet (call order: a test day). Test-day checks: (1) one lobby match with
rbfastrestore 0 and one with 1: "[rb] cost" avg and "[hw] timing split" rollback bookkeeping lower with 1, 0 desyncs (digest clean)
in both, no "[rb] rbfastrestore: a page couldn't be opened"; (2) the boot log's "[pools]" trampoline count is one lower (the
duplicate Miasma hook gone) and Frostfang's miasma still matches the browser.

**Evaluation Wave 2 (2026-10-18, B2; build-checked only).** Input delay: TF.EX's GetEffectiveInputDelay (the settings' InputDelayMs as
240 Hz frames, or the session override) rounded up to the 60 Hz session (S-60hz); hw.txt npdelay is a test override; Lobby.SeriesInputDelay
not yet (not generated). Desync safety: the silent rollback pools are [pools] EXHAUSTED errors (page table, side state, allocation
records, late-page intervals), SideAll static_assert, 48 sfx snapshots; the digest's extra fields need the browser's GameplayDigest in
the same step (not started); the in-window allocation warnings asked of the translator. Every remote gone: TF.EX's GetEventAndUpdate
handles GGRS's Disconnected (LeaveLobby if no winner, the level runs on; a team match: EndMatch); the crash that kept the TFEX-DIFF (run
235306: Level.HandlePausing on a null PlayerInputs[i] at +0xa7c0dc, the filled controller slots emptied at the teardown) fixed: the slots
are emptied once the Level is left. xplay.cpp: the feasibility probes deleted (552 lines), the rest split into hwconfig.cpp,
lobbymatch.cpp, netplay_gate.cpp, tfstate_ports.cpp, xplay_internal.hpp. Test-day checks: (1) smoke disconnect: "every remote gone:
TF.EX's IsDisconnected, the level runs on locally", no guest exception to vsstop 2400, the menu after QUIT; a team match where one
player leaves: "A PLAYER LEFT"; (2) "[np] input delay 2 frames at 60 Hz (TF.EX's; ... 5 at 240 Hz)" with the default 20 ms, and a
changed InputDelayMs in the settings changes it; (3) the full smoke set once (the split touches every match path); (4) no
"[pools] EXHAUSTED rollback ..." lines in a normal match. Variants: the hand copy is deleted (TF.EX's generated ApplyVariants /
ApplyNetplayVariantRules apply them; the per-player store is GC-scanned, the translator checked): (5) a quick play and a lobby with
variants on: the same variants as the browser (its match settings), and no "[tfexgen] POOL FULL" line.

**Evaluation Wave 1 (2026-10-18, B2; build-checked only).** Rec 1: the frame cost (g_fc) reset at the start of FrameHook, the netplay
gate timed on its own ("netplay gate" column in "[hw] timing split"), frames over 250 ms kept, 700-byte log lines, "[rb] cost": the
first 200 then "[rb] cost summary" every 200, digest check frames logged once simulated here. Rec 5: net::Log doesn't flush per line;
the hw log's writer thread flushes once a second; the tail is mirrored incrementally under the append lock and the file written
outside it; "[pause]" button lines: changes only, 30, then a count. Rec 4: genend / genstart / sessionpatch / g_hudPatch out; 25
research hw.txt keys and the glue test keys out with their code (969 lines); hw.txt 8 KB, an over-long file is a [pools] EXHAUSTED
error. Waiting: the hand variants copy (needs the translator's GC-safe rule for NormalizeForNetplay's playerValues store).
Test-day checks: (1) a console match log has "[hw] timing split" lines whole (not cut), non-zero "ggrs" / "netplay gate" columns,
no "[digest] frame N: no longer kept"; (2) the RTC loop's "poll ... max" under ~30 ms and no GGRS "interrupted" without a real stall;
the log file on the SD still grows during a match (the writer thread flushes) and ends within a second of a force-quit; (3) smoke lobby,
rematch, disconnect (the flags' removal; genend / genstart log labels unchanged); (4) a long hw.txt (> 8 KB) logs
"[pools] EXHAUSTED hw.txt".

**Archers through TF.EX's ArcherService (2026-10-18, build-checked only).** The generated MapScenePatch.StartSession_Prefix runs in the
Switch's StartSession hook (before xplay::LobbyStartSession, TF.EX's prefix order): RestoreArchersFromLobbyIfNeeded, CompactSeatsToHandles,
ApplyToGame (TFGame.Players / Characters / AltSelect from GameContext's archer store, the translator's 0c7f268), ApplyTeamsToMatchSettings.
Deleted: ResolveArchers / ResolveArcherForSeat, xplay's Characters write, LobbyMatch.archer/alt. Alts now apply (the hand path played
normal archers). Kept (S-start): SortedBySeat for the GGRS handle, teams and room peers, needed before the map screen. Checks for the next
test day: smoke lobby, join, quickplay, team, rematch, archer: the log line "[map] TF.EX's StartSession_Prefix: N archers in the store;
Players ..., Characters ..." with N = the match's players and the lobby's archers; the archers on screen match the browser's (an alt too).

**Sync timeout after a rematch (2026-10-18, hardware 11:19; build-checked only, no tests by the user's rule).** The rematch's new
session never synchronized (the browsers had crashed) and the console waited at "genstart: ... waits for EstablishSessionIfSynchronized"
forever: the generated NetplayManager.IsSynchronized was a constant true (GGRSFFI.netplay_is_synchronized a no-op, IsOk constant), so
TF.EX's AbortIfSynchronizationFailed always returned at its first test. Fixed by the translator (c792253: the member rule = svc::IsSynchronized,
xplay's tfx_state == 1). The abort's menu step is TF.EX's own (TFGame.Update: DisconnectFromServer, ResetPeer, an empty own lobby,
LevelExtensions.GoToNetplayEntryMenu generated, EnableAllControllers, the failed-connection notification); the hand ReturnToMenu is deleted
and QuitTeardownFrame does the Switch's teardown when that menu appears. TF.EX has no pause / quit while it waits (its Level.Update prefix
skips the level until synchronized): the 30 s timeout is its way out. Checks for the next test day: a lobby match (or a rematch) whose
peer never connects -> about 30 s after "genstart: the first round's spawn waits" the log has "[lobby] TF.EX's AbortIfSynchronizationFailed:
the match's connection failed, back to the netplay menu", then "the match's Level was left for the menu (TF.EX's QUIT / GoToNetplayEntryMenu)",
the NETPLAY menu with "Connection to the other players failed"; smoke quit and archer (shared teardown).

**The recurring hardware desync (2026-10-18, B2; the user approved the job).** The Switch kills a REMOTE archer the browsers keep alive.
What the three console logs (.devserve/results) settle:
- **No rollback crosses any kill.** 12:55 (narrow, Switch seat 1): 0 rollbacks the whole match; the remote (seat 0) died at frame 1067
  (rumble line), first bad check 1080. 17:44 (narrow, Switch seat 0): remote p1 died at 387 (an arrow of the Switch's p0 created ~380,
  type 0); the only rollback before the bad check, #1 411 -> 409, starts after the kill; 360 agreed. 08:27 (wide 3-player TDM, Switch
  p0): last rollback #2 362 -> 357; remote p2 died at 469; 360 agreed. So the kill was simulated once, on the first pass, and never
  resimulated: a wrong restore is ruled out for all three.
- **Input misprediction is ruled out for 12:55:** "remote input changes 0" for the whole match (the remote's input, as fed to the
  simulation, never changed: an idle browser player), yet it was killed at 1067 (the Switch's p1 had shot Bramble-type arrows, type 4).
  Same inputs on both sides, different outcome.
- **08:27 had no arrow in flight** (arrows=0 at 360 and 480, no Arrow.Create): p2 died at 469 at least 160 px from both other archers
  (p0 (120,72), p1 (241,112), p2 (400,132) at 360; the browsers have p2 alive at (334,202) at 480). Not an arrow, not a stomp: an
  environmental death or a Die() call from code. Its 3 rng draws at 469 come from Calc's random helpers (+0x9091c4..+0x909258: the
  death's effects, not its cause).
- No GGRS Disconnected event before any kill (TF.EX's PlayerPatch Curse kill needs an input with the disconnected flag: ggrs-switch sets
  it only for status Disconnected).
- Oddity, not the cause (it's after the kill): 17:44 rollback #1 "411 back to 409" with 0 resimulated ticks.
Theory (labelled, not shown): a simulation difference that touches only remote archers on the Switch (something the Switch computes
for a non-local Player differently: e.g. a remote-only path, RightStickShot aim, or the wide level's hazards) rather than rollback.
**Logging added (xplay.cpp, `[desync]` lines, at most 8 deaths per match):** every remote death in a lobby match: frame, cause, killer,
brambled/laser, position, GGRS confirmed/current frame, and that seat's inputs for the 8 frames before with their status (C confirmed,
P predicted, D disconnected) and the disconnected flag; any rollback that crosses such a death ("crosses remote pN's death") and the
outcome ("died again" / "did NOT die again in the resimulation"); the first 4 remote inputs carrying the disconnected flag.
Hardware checklist: after a desync, the console log has `[desync] remote pN died at frame F` near the first bad check; read its cause
(0 arrow, 10 in the emulator run = a fall/environment cause; compare with the browser's ptrace of that seat) and whether F's inputs were C.

**Batch 5 (2026-10-18): item 5.** TF.EX's NetplayManager.GetEventAndUpdate is generated (translator 5d40a92) and runs once per frame after
xplay's GGRS event loop (events queued with svc::AddNetplayEvent: Synchronized, NetworkInterrupted, NetworkResumed, DesyncDetected;
TFEX-DIFF parked: Disconnected not passed, GenRemoteLeft keeps it). The hand NetplayManagerOnEvent is deleted (it still ended the match
on a desync; TF.EX 0.21.1 only logs it). ShowMatchStartWarnings stays at the Synchronized event (LevelPatch's
ClearWaitingNotificationOnceSynchronized, not ported). B's other rows stay: glue around generated code or S with reasons (see below).

**Next (in order, before batch 4):**
1. (b) Rematch / archer select through TF.EX's own handlers (coordinator-approved; the user: reuse the Switch's existing native start
   underneath, adapt Enter only as far as the hand-off needs, no rewrite). A's MatchmakingService GenDispatch intercepts
   RematchLobby / ArcherSelectLobby and raises EndOfMatch's Consume* flags; A drops the intercept in the batch that lands this:
   - INetplayManager.Reset (overrides.toml:449, a no-op) -> the generated NetplayManager.Reset on EndOfMatch's object (A asked; TF.EX
     only calls it `if (IsInit())`, false by then on the Switch, so it rarely runs, but it should be TF.EX's).
   - RematchLobby: TF.EX does `Engine.Scene = new MapScene(Versus)`. Switch side: in MapScenePatch's BeginHook, a MapScene while
     !g_active and EndOfMatch::AwaitingVote() is TF.EX's rematch: call NetplayManagerStartLobbyMatch() (the native start: session,
     players, archers, xplay::StartLobbyMatch -> MaybeNativeStart -> Enter a frame or two later). In Enter's else-branch, when the
     current scene already is a MapScene, don't GotoVersusLevelSelect: activate and run gen::MapScenePatch::Begin_Postfix(scene) on it
     (the selection the BeginHook would have made). StartSession is ~100+ frames later (confirm, zoom), so the order holds.
   - ArcherSelectLobby: TF.EX's ReturnToMenu(Rollcall) runs; clear EndOfMatch's g_endgame / g_awaitingVote / g_matchStarted when the
     main menu appears (EndgameFrame) instead of in the Consume path. Then delete EndgameFrame's ConsumeArcherSelectLobby /
     ConsumeRematchLobby blocks. Check: rematch, archer (the `archer` scenario ends at the roll call), quit.
2. GetEndGameStatus: generated by the translator in ws/tfex-update ce35cd0 (TF.EX's body, List<EndGameStatus, 8>, a ring of 4
   temporaries: read at once). Once on master: delete the overrides rule IMatchmakingService.GetEndGameStatus (the interface -> TF.EX
   implementation default, 037c5f5, then calls the generated one on ResolveMatchmakingService's object) or point it at
   tfex::gen::MatchmakingService::GetEndGameStatus(svc::GenMM()); delete runtime/tfexgen_services.cpp's hand copy; regenerate; smoke
   rematch / archer / quit (the vote panel reads it).
3. The INetplayManager service rules (HasFailedInitialConnection, ConsumeAbortToVersusOptions, GetFailedConnectionMessage, ...) still
   read NetplayManager.cpp's hand state (g_failedConnectionMessage ...): route them to the generated NetplayManager object, then delete
   that state and NetplayManagerAbortToVersusOptions & co.
4. The two TEMPORARY models::StrList wrappers in NetplayManager.cpp (FirstUnported / HasAll): delete once A says its WarnUnportedVariants
   uses VariantTitles.
5. The rest of docs/HAND-CODE-INVENTORY.md's B rows (most remaining ones are glue / S with reasons, see "B's remaining L rows verified").

**Parked (the user):** the genend "every remote gone" TFEX-DIFF (LeaveLobby + EndMatch instead of running the level on alone; removing it
needs the 0.3 s-after-teardown crash fixed: "do not worry about the crash issues"); the wide desyncs ("not yet"); the Switch-mid-match
question (no investigations). Replays: not supported yet (setting Off).

## Status (2026-10-17)
Owner: Integration B (branch `ws/match`). Scope: the match start and GGRS session, the match itself, end of match, rematch, archer
select, the pause menu and quit, as TF.EX's own (generated) code. Files: xplay.cpp's match parts, tfex/NetplayManager.cpp,
tfex/PlayerPatch.cpp, tfex/EndOfMatch.cpp (moved out of MainMenuPatch.cpp, e7f80ac), the match-side `[service]` rules.

Queue: (1) rematch, (2) wide, (3) end-of-match menu -> generated PauseMenuPatch / HUDPatch / MatchEndChoices, (4) SessionPatch,
PlayerPatch, MatchSettingsPatch.

Batch 1 (e7f80ac, 72070b1, e596def), merge-ready: the EndOfMatch.cpp move; rematch fixed (room connection closes); wide from WiderSet's
own setting; TF.EX's 30 s sync timeout; the SeriesContinueChoice service rule. Tested: smoke rematch PASS (runs/110745 of
/tmp/xplay-runs-21a542c9: match 2 synchronized, digest clean; the server log shows the Switch's peer leaving each room). Not tested: wide
quick play (no harness scenario), the 30 s timeout path (needs a peer that never connects), quit/lobby scenarios on this build.
Batch 2 (9a479eb..HEAD; contains the merge of ws/tfex-update 0b42843), merge-ready: the end-of-match menu, results and vote are TF.EX's
generated PauseMenuPatch (all six methods) + HUDPatch (+ MatchEndChoices); PlayerPatch's postfix generated; SessionPatch wired but off
(hw.txt sessionpatch 1); the test vote presses the real menu. Tested: smoke quit PASS (120535), smoke rematch PASS (120939).
Batch 3 (after 4a21435): SessionPatch on by default in lobby matches (smoke rematch with it PASS, runs/121400: CreateResults made
round and match results as before, match 2 synchronized); MatchSettingsPatch on by default (genpatch; its hooks were never called in
the lobby run, as expected: CleanSettingsVersus is the offline VersusOptions screen's). Smoke lobby PASS on the batch-3 build with every
switched class at its default: 0 differing ticks of 594, digest clean (runs/121658).
Open (next): the input lock (IInputService Disable/EnableAllControllers, todo), ARCHER SELECT through the generated path (lobbyvote 2),
a mid-match QUIT, the vote panel seen on screen, wide quick play on hardware.
Item 4: likely root cause sent to tfex-translator: generated units hold managed objects in unscanned C++ statics (votedMenu,
_reassignTarget, UnauthorizedVariant's List...), so the GC can free them and later writes corrupt reused memory.

## 1. Rematch: match 2 never connected
Evidence: smoke rematch FAIL /tmp/xplay-runs-eb755020/runs/104826 (server log: after match 1 only the page leaves the room; the
Switch's reconnect replaces its own stale connection, and the page then lands in a new room). Two causes:
- The Switch: the server never saw the Switch's room WebSocket close (no "left" for its peer in any run that day, though net.cpp RunRtc
  logged "rtc: ended"). Fix: netws::Close sends a WebSocket close frame first; EndLobbyMatch calls `np_stop()` (the WebRTC loop leaves
  the room when the match ends here, as TF.EX's session dispose does).
- The local test server (browser repo cloudflare/tfex-server/local.mjs, not ours): replacing a peer's connection closes the old one
  synchronously, the room is empty for that instant and is dropped from the room map; the new connection lives in an orphaned room.
  Reported (side-session notes, integration-b-rematch-local-server.md). The deployed Durable Object (one room per object) is not
  affected that way.
Why it worked on 2026-10-16 (TF.EX 0.19.2): probably order/timing; the stale room connection existed then too.

## 2. Wide quick play played narrow
Console 2026-10-17 10:42 (telemetry session c761ffe1, build dc84bd0): WIDE -> QUICK PLAY, matched with the browser (wide): the Switch
started with "standard set" and a 16-tower random pick (narrow), the browser wide. Cause: StartLobby took wide from the lobby's
Teuria.WiderSet entry; a quick-play lobby has `Mods: []` (the server matches equal `is_wide` instead). On the PC, WiderSet plays from
the player's own `WiderSetModApi.IsWide`; the lobby entry only gates joining. Fix: `m.wide = MainMenuPatch::IsWide()`.
The same match then never connected and the console stood in the black level ("froze"): nothing on the Switch implemented TF.EX's
`AbortIfSynchronizationFailed`. Now: 30 s (CONNECTION_TIMEOUT_MS) without the first synchronization -> TFGame.Update's failed-connection
branch (DisconnectFromServer, ResetPeer, empty lobby, the NETPLAY menu, "Connection to the other players failed"). Why that match's
peers never connected is not known (the page's side isn't in the telemetry).

## 4. The hook-class combination crash (ws-tfex-update.md "Open")
Every generated hook target (tfexgen_hooks.cpp Enable, bind:: offsets) against every hand InstallAtOffset: no two generated classes
share an address, but three generated hooks land on addresses the hand code always hooks:
| address | generated | hand |
|---|---|---|
| 0xb44b40 Session.EndRound | SessionPatch (SkipWhileBaking, EndRound prefix/postfix) | xplay.cpp EndRoundHook |
| 0xb45290 Session.CreateResults | SessionPatch.Session_CreateResults | xplay.cpp CreateResultsHook |
| 0xaef280 Player.Update | PlayerPatch.Player_Update_Postfix | tfex/PlayerPatch.cpp PlayerUpdateHook |
A second exlaunch trampoline on an address relocates the first one's branch; that fits "PlayerPatch + SessionPatch together crash".
Plan: at those three addresses the hand hook calls the generated prefix/postfix itself; the class's other methods get the generated
hooks (needs a per-method enable from tfex-translator; asked). Also noted to tfex-translator: generated units keep managed objects in
unscanned C++ statics (PauseMenuPatch votedMenu, HUDPatch _reassignTarget, MatchVariantsPatchs' UnauthorizedVariant List): a GC hazard.

PlayerPatch generated vs hand: same kill rule (input `disconnected` -> Die(Curse, -1), Die = 0xaee3d0 both), plus RightStickShot
(a side-table write per player per tick) and SkinSlot around the ctor; the hand hook also runs rnd::CheckScreenCovered (diagnostic,
moves elsewhere). Capture flag: hand rnd::Capture(), generated IsServerMode && !(InSession && IsDisconnected).
MatchSettingsPatch: PlayerGoals never applies (NetplayMode Local doesn't exist here); CleanSettingsVersus clears netplay mode at 0 players.
HUDPatch: blocked: Traverse.Field("finished") translated as false, so the match-end branch is dead (asked tfex-translator for the mapping).

## 3. End of match: TF.EX's generated PauseMenuPatch, HUDPatch (batch 2)
- **PauseMenuPatch** (all six methods, incl. VersusRematch from ws/tfex-update 9d9780a): hooks installed by hand in tfex/EndOfMatch.cpp,
  each calling the generated function with tfexgen_hooks.cpp's prefix/postfix semantics (genpatch PauseMenuPatch stays off). Two Switch
  steps around it: (1) PauseMenu's constructor **inlines AddItem** (the only BL to AddItem +0xae2550 is a wrapper at +0x27c1e4c), so
  TF.EX's AddItem prefix never sees the menu's own items; MATCH SETTINGS is removed on the menu's first Update instead (same rule).
  Before this, the Switch's end-of-match menu always showed MATCH SETTINGS (the hand AddItem hook never fired either). (2) TF.EX's QUIT
  (GoToNetplayEntryMenu) has no Switch teardown: QuitTeardownFrame runs it when the match's Level gave way to a MainMenu none of our paths
  made (EndLobbyMatch, TFGame.Players, RollcallMode, CurrentMatchSettings; logs "back to the lobby screen (MainMenu 62)").
- **HUDPatch** (genpatch default): NotifyMatchEnded + MatchEndChoices (the vote panel: "PICK IN n", P1/P2 choices) on the finished
  VersusMatchResults. The hand OnLobbyResultsFinished keeps the Switch flags and sends MatchEnded only for a test host (no netplay
  mode). TFEX-DIFF S-input: HUDPatch's PlayerInput.AssignInputs + MenuInput.UpdateInputs are skipped on the lobby's results screen
  (xplay owns and restores the controller slots). Whether running them would harm is untested (the "results deaf" failures I first
  blamed on them were the test driver's single Confirm, below).
- **Test vote** (hw.txt lobbyvote 1|2|3): presses controller 1's buttons like a player: Confirm on the results once a second until
  the menu opens (the first Confirm only tweens the unfocused results in; VersusMatchResults' coroutine), then Down to the item, Confirm.
  Holds are counted per frame (the game polls the controller several times a frame). The menu's own hooks take the vote.
- Tested: smoke quit PASS (runs/120535, HUDPatch on): MatchEnded from HUDPatch, MATCH SETTINGS removed, TF.EX's QUIT, the Switch
  teardown, the NETPLAY menu (MainMenu 62). Smoke rematch PASS (runs/120939, default build a94ea26): MatchEnded from HUDPatch, the
  vote by TF.EX's generated PauseMenu_VersusRematch, match 2 synchronized, digest clean.
- **PlayerPatch**: the hand Player.Update hook calls the generated Player_Update_Postfix (one trampoline per address).
- **SessionPatch**: through xplay's EndRoundHook / CreateResultsHook / a new GotoNextRoundHook, behind hw.txt `sessionpatch 1`
  (off until its own test).

## Batch 4 (2026-10-17 afternoon): input lock, ARCHER SELECT, mid-match pause, map screen, hook pool
- **Hook pool** (top priority, 0b09328 / 4b52d4e): the console's trigger-arrow crash was exlaunch's trampoline pool (JitSize 0x8000 = 163)
  overflowing at rnd's lazy Arrow.EnterFallMode install, plus an allocator off-by-one that handed trampoline #164 out past the pool.
  JitSize 0x10000 (327), `>=`, lazy installs skip when full; counts logged (boot 105, after late installs 173, after a lazy one 180).
- **Input lock** (tfex/InputLock.cpp): TF.EX's IInputService lock as gates on JoyConInput's menu getters (slots 0x38-0xc0, bodies from the
  live vtable, one trampoline each; xplay's MenuConfirm rule folded in). In a lobby's MainMenu the flag gates (IsForeignSeat's lock
  part); ResultsVoteLock (a pending vote) anywhere; a lock taken outside a lobby gates every getter (the PC empties the inputs).
  TFEX-DIFFs: no "foreign seat" by controller (local guest / couch), DisableAllControllerExceptLocal is a no-op.
- **ARCHER SELECT** (lobbyvote 2): through TF.EX's generated VersusArcherSelect; the lock taken and released; back to the roll call.
  Screenshot: REMATCH! / ARCHER SELECT / QUIT, "WAITING FOR OTHER PLAYERS..." (/tmp/xplay-runs-21a542c9-B/runs/124233).
- **Mid-match pause** (tfex/MatchPause.cpp): TF.EX v0.21.1's LightPauseMenu (RESUME / QUIT, the game keeps running) and LevelPatch's two
  HandlePausing prefixes, generated, called from xplay's HandlePausingHook in lobby matches. TFEX-DIFF S-rollback: the overlay's buttons
  are recorded per simulated frame and replayed in resimulations. Neutral local input while open; ForceClose at the match end.
  hw.txt `lobbypause N`. Tested: Start at frame 400 -> overlay open -> Down -> Confirm -> TF.EX's QUIT -> the Switch teardown -> NETPLAY
  menu (runs/125418). Runtime classes (MatchEndChoices, LightPauseMenu) are made on the main menu (outside any rollback window).
- **Map screen**: TF.EX's generated MapScenePatch from tfex/MapScenePatch.cpp's hooks in standard netplay lobbies; wide lobbies and the
  test host keep the hand port (WiderSet's towers are WiderSet's code). [service] ITfStateApi.GetSeed = the lobby seed (as a no-op it
  read 0: the random tower would have differed from the browser's). StartSession_Prefix waits for IArcherService.ApplyToGame /
  CompactSeatsToHandles rules (xplay::LobbyStartSession is the Switch's). Smoke lobby PASS 0/599, TF.EX's tower = the hand model's.
- Open: TF.EX's vote panel (MatchEndChoices) isn't seen on screen; a diagnostic line runs with the next vote (lobbyvote).

## Batch 5 (2026-10-17 13:25): batch 4 on master 605094b, the console crashes, the desync's evidence, pools (part 1)
- The two console crashes reported as "tfex::InputLock::DisableAllControllers" (20261017-125234/-125435) ran the build published
  before 9ed5846: both reports carry module id 177EFDB8; the second session (f84e091c) booted at 12:52:33, before 9ed5846 was published
  at 12:53. That build has no InputLock and still had the 163-trampoline pool with the allocator off-by-one; the crash frames are an
  exception inside exlaunch's hook install reached from rnd's lazy arrow hook: the trigger-arrow crash, fixed in 9ed5846. The
  InputLock attribution came from symbolizing with a later ELF. (Caveat: that build's ELF is gone, frame names are from a later one.)
  The telemetry "build" stamp is stale (11:50:52 on every build since): __DATE__/__TIME__ only recompile with their file.
- Hardware desync at 1080 (match 12:56, build 9ed5846): not root-caused (findings and two theories in the coordinator thread);
  the console now logs its digest at every GGRS check frame. Ryujinx sanity runs with the same map / seed / Instant, the real-controller
  path and the console as seat 1 stay in sync.
- Pools: pools.hpp registry (exlaunch trampolines and inline hooks, rnd's lazy hook slots), one line at boot, 15 s and every lobby match
  end; Exhausted = error + telemetry. InputLock's 18 late gates check the pool first; a refused trampoline no longer counts as used.
  Inventory of every other pool: in the session notes (part 2 to wire).
- Vote panel: MatchEndChoices is made and is in the HUD layer (1 of 1) but not seen on screen: a drawing problem, open.
- Tested: smoke rematch on master 605094b + this, slot B: PASS (match 2 synchronized, digest clean; digest per check; pool lines).

## Batch 6 (2026-10-17 15:10): the in-match sound crash, the rollback floor on a late overflow
- **Crash at PC +0x47320** (the game's per-frame sweep +0x48360 of its static List of sound instances, slot +0x4724770, met a null
  element right after a rollback; repro runs/143738 of /tmp/xplay-runs-21a542c9-B, frame 432; Integration A's at tick 572): a restore
  brings back the List object (its count) while the element block of its items array can hold a later state (List.RemoveAt shifts the
  entries down and nulls the vacated tail slot). xplay.cpp `RepairSoundLists()` runs in RbGate's load path right after `sfx::OnLoad`:
  both lists (active +0x4724770, finished pool +0x4724788) get their nulls compacted out in order and the count lowered (also a count
  above the array length), logged as "[sfx] rollback: N null sound instance(s) removed ..." (first 20).
- **rollback.cpp PendingScan**: a late-found change of the closed frame with no room in the undo pool only counted poolOverflows; now it
  raises g_floor to that frame + 1 as EndTick's overflow does (no rollback may restore across a page it couldn't log), and both report
  through pools::Exhausted("rollback undo pages").
- Tested (slot B): smoke lobby PASS, runs/150522: 0 differing ticks of 593, digest clean, 9 Switch rollbacks (deepest 37), no crash; no null
  entry turned up in that run, so the repair's path ran but had nothing to remove. Not seen: an actual repair line, a pool overflow.
- Harness finding (not this batch): `tools/smoke.sh quickplay` fails on its own: the page never recognizes the quick-play lobby as a
  Switch lobby (no "[netplay] Switch lobby: ... 60 Hz" line from the browser's SwitchLobby.UpdatePlayersPostfix), so it runs TF.EX's
  240 Hz session (`session init: fps=240 input_delay=5`) against the Switch's 60 Hz: ~58-frame page rollbacks and a desync at frame 480
  (runs/145547, 145947; the Switch had 0 rollbacks). Browser side (SwitchLobby's platform check in quick play).

## Batch 7 (2026-10-17 15:45): the vote panel and the pause overlay on screen, the pause across rollbacks, three smoke scenarios
- **Vote panel not drawn (row 13):** TF.EX's MatchEndChoices rendered every frame, and the drawing call's ABI was fine (tfex-translator
  checked it). A probe drawn at (120, 120) from its Render showed at the level's x 70. The Switch's Level HUD layer (layer 4, a
  CameraLayer) draws in the console's 420-wide canvas; the side art over canvas x 0..50 / 370..420 is drawn on top. TF.EX's (2, 32) was
  under the left art. `tfex/SwitchHud.hpp` Shift (TFEX-DIFF S-hud) ends the SpriteBatch and begins it again with the same state and the
  transform moved 50 right, around MatchEndChoices' and LightPauseMenu's Render (vtable overrides in EndOfMatch.cpp). Seen in Ryujinx:
  "PICK IN 25 / P1 ARCHERS / P2 ..." at the top left of the level, and the pause overlay centred. A's in-level Notification has the same
  50 px shift (told A).
- **Pause across rollbacks (row 11):** Level.Update calls HandlePausing before Scene.Update, so BetweenTicks::InTick was false for TF.EX's
  HandleStartPress. The Start that opens the overlay was read live, never recorded, and a resimulation didn't repeat it: a rollback across
  the opening frame left it closed. MatchPause::Pressed now records and replays per simulated frame of a lobby match. A press on a real
  frame where the game didn't tick is latched for the next simulated frame (only when no tick read it then: FrameHook runs before the
  game's Update). Opening and closing are logged with the frame and "(resimulated)".
- **Smoke scenarios (tools/smoke.sh, lobby-auto.sh; A's files, told A):** `archer` (both vote ARCHER SELECT; ends at "[lobby] ARCHER
  SELECT won the vote: back to the roll call", a new log line), `disconnect` (the page navigates away 8 s after it synchronized),
  `pause` (hw.txt `lobbypause 300 1`: Start at frame 300, RESUME 4 s later, vsstop 600 with the tick compare).
- Tested (slot B): archer PASS (runs/151951), disconnect PASS (152221: the room closed at frame 568, the Switch left for NetplaySelect),
  pause PASS (153917: 0 of 596 ticks differ; opened at 301, a rollback took it back, the resimulation reopened it at 301, open through 7
  rollbacks, closed by RESUME at 549), quit PASS (154611), rematch PASS (155300).

## NetplayManager split: B's corrections to tfex-translator's draft (2026-10-17, docs/ws-tfex-update.md "NetplayManager split")
The draft's grouping stands (TF.EX generated: flags and modes, match start, disconnect / end, players, input delay; xplay native: session
creation, polling, advance / save / load, resimulation counts, network stats, fps; out: spectators, GetArchersInfo). What xplay changes:
1. **Where the state lives (the lockstep rule).** The generated object, its fields and statics are in the GC heap, which a rollback
   restores. NetplayManager's state is the session's, not the game's (TF.EX never saves it in TF.State). A field written during a match
   would be taken back by the next restore: `_isRollbackFrame` set for a resimulation would read false again right after the load. So
   every field that changes between Init and Reset is a get/`set` [service] rule into xplay's native state: `_isInit`, `_netplayMode`,
   `_isRollbackFrame` (= xplay's g_resim; SetIsRollbackFrame a no-op), `_isUpdating`, `_framesToReSimulate`, `_framesAhead`,
   IsSynchronized / IsDisconnected (from the native session). Fields only written outside a match (`_player2Name`,
   `_failedConnectionMessage`, `_hasFailedInitialConnection`, `_pendingAbortToVersusOptions`, `_pendingSessionRoundLogic`, GGRSConfig)
   stay the generated object's own.
2. **Requests move to the xplay group:** UpdateNetplayRequests / HaveRequestToHandle / ConsumeNetplayRequest, `_netplayRequests`,
   `_events`, `_networkStats`. xplay's RbGate drives GGRS's requests itself (TF.EX's TFGame.Update loop isn't the Switch's).
3. **Disconnect (row 12):** EndMatchOnNetplayError / EndMatch / IsUnfinishedTeamMatch are generated, called from xplay's event loop
   where it now calls OnRemoteLeftMidMatch (after the events, outside a tick). Reset is generated. Its native parts are rules into
   xplay's teardown, in TF.EX's order (Reset before GoToNetplayEntryMenu, which already fits xplay):
   `GGRSFFI.netplay_reset` -> xplay::EndLobbyMatch (np_stop, session free, rollback log end, GC back on), `StateApi.ResetSession` /
   `ResetSfx` -> rb / sfx resets, `StateApi.SetFrameDriver(null)` no-op. The match then ends as TF.EX does ("CONNECTION LOST" /
   "NETPLAY ERROR" notification on the netplay menu) instead of the hand QuitToNetplayEntry. Gate: smoke disconnect.
4. **Match start (row 9):** EstablishSessionIfSynchronized / OnSessionEstablished / SpawnFirstRound are generated and called where xplay
   spawns today (synchronized, at the origin tick the page's xlobby also uses). Rules: IGameContext ResetPlayersIndex / GetPlayers /
   Reset, IArcherService.ApplyToGame -> the lobby's archers (A's roll call data), IRoundPlayerSpawner -> none (the vanilla
   SpawnPlayersFFA path). Behind hw.txt `genstart 1` first. Gate: smoke lobby, join, private, privatejoin with 0 differing ticks; then
   on by default.
5. **Order:** (a) the flag rules and the generated class with the xplay group as rules: no behaviour change, every scenario PASS;
   (b) disconnect; (c) match start behind genstart, then default. Spectators after priority 1.
Owners: the unit and translator support: tfex-translator. The [service] rules (overrides.toml, runtime/tfexgen_services.*), the xplay
side, the wiring and the tests: B.

## Batch 9 notes (2026-10-17 17:00): archer to match 2 (open), the team scenario
- **archer through match 2: not done.** The page side can't be driven reliably by keys in TF.EX's end-of-match menu: Confirm on the
  results opens the menu after a varying delay (one run at once, another after more than 6 s), and keys sent before it opens are lost. Runs
  162632 / 163558 / 164454: the page never voted ("P2 ..."); the Switch's lone ARCHER SELECT won at the deadline, and the page ended outside
  the lobby. The `rematch` scenario passes the same way (the Switch's vote alone decides). The scenario stays ending at the roll call. Next:
  a page-side hook (driver eval or an xlobby `vote=` parameter in the browser harness) to vote when the menu opens.
- **team** (for A, row 8): hw.txt `lobbymode 5` + `lobbyteam 0`, page LVB_TEAMKEY=ArrowDown; shot at the Switch's "seat N: ... joined"
  (the roll call with the BLUE/RED labels). First run (165410): the lobby is TDM ("LobbyBuilder START: mode 5, map -1, length 2,
  size 3"), the page joined on the server, but the Switch never saw seat 1 and the page ended on NetplaySelect. Handed to A.

## NetplayManager step (b): disconnects through TF.EX's generated code (2026-10-18, hw.txt `genend 1`, off by default)
- EndOfMatch.cpp makes the generated NetplayManager once on a menu (an uncollectable root) when `genend 1`. xplay's NetplayGate, after
  GGRS's events, on remote(s) gone mid-match: TF.EX's Disconnected branch: every remote gone (IsDisconnected) -> LeaveLobby, then
  (TFEX-DIFF S-rollback) TF.EX's EndMatch("A PLAYER LEFT") instead of running the level on alone (TF.EX does that; on the Switch the
  guest crashed 0.3 s after the session's teardown, run 235306); an unfinished team match -> LeaveLobby + EndMatch("A PLAYER LEFT").
  RbGate's advance error -> EndMatchOnNetplayError unless "Peer Disconnected!" / "No session found" (TF.EX leaves those to the events).
- Rules fixed on the way (overrides.toml): StatusExtension.ToModelGGrsFFI evaluates its receiver (the value rule had dropped
  netplay_reset(), so Reset never tore the match down and EndMatch ran every frame); NetplayManager._cancellationTokenSource /
  _cancellationToken read null / 0 and CancellationTokenSource.Cancel is a no-op (Init, which makes it, is xplay's: Reset crashed on it).
- smoke `disconnect` accepts both paths' lines. Tested (slot B): disconnect + genend 1 PASS (runs/001347: EndMatch -> Reset ->
  EndLobbyMatch -> the netplay menu), lobby (defaults) PASS 0/594 and 0/593.
- To retire once genend is the default: xplay.cpp NetplayGate's hand branch (`else if (leftMidMatch)` -> OnRemoteLeftMidMatch),
  EndOfMatch.cpp OnRemoteLeftMidMatch, smoke's hand-path alternatives.

## NetplayManager step (c): the match start through TF.EX's generated code (2026-10-18, hw.txt `genstart 1`, off by default; FFA modes)
- xplay.cpp hooks the vanilla FFA round logics' OnLevelLoadFinish (LastManStanding +0xa76050, Headhunters +0xa56440, Warlord +0xbca880:
  each is base + VersusStart + SpawnPlayersFFA). For a lobby match's first round (Session.RoundIndex 0) with genstart they run only the
  base (RoundLogic.OnLevelLoadFinish +0xb38070) and hand the round logic to the generated NetplayManager as TF.EX's Init would
  (_pendingSessionRoundLogic). NetplayGate calls the generated EstablishSessionIfSynchronized before GGRS's first advance (before
  rb::Init, so no restore can repeat it) -> OnSessionEstablished -> SpawnFirstRound: VersusStart + SpawnPlayersFFA. Later rounds: vanilla.
- IGameContext.GetPlayers (overrides.toml, B): the own lobby's players as (seat, player), TF.EX GameContext.ArcherSelections.
- Tested (slot B): lobby / join / privatejoin with genstart 1: PASS 0/597, 0/599, 0/596 (one "OnSessionEstablished spawned the first
  round" each; origin tick 266 as before); lobby with defaults PASS 0/595.
- **Team mode (open, for tfex-translator): TeamDeathmatchRoundLogic isn't hooked, so a team match keeps the hand (vanilla) start.**
  TF.EX's SpawnFirstRound does `if (roundLogic is IRoundPlayerSpawner spawner) { spawner.SpawnRoundPlayers(); return; }`, then the FFA
  fallback (DynamicData: VersusStart + SpawnPlayersFFA). On the Switch the round logics are the vanilla ones; the test is generated as a
  definite false (`gen::As(roundLogic, nullptr)`), so a team match would spawn as FFA. Needed: a [service] rule form for a type test
  against a TF.EX interface, e.g. `"TF.EX.Domain.Ports.IRoundPlayerSpawner" = { is = "tfex::gen::svc::IsRoundPlayerSpawner($0)" }`, so the
  test calls a function (true for any of the four versus round logics), and `IRoundPlayerSpawner.SpawnRoundPlayers` -> a function doing
  the mode's own spawn (VersusStart + SpawnPlayersFFA, or VersusStart + SpawnPlayersTeams + Players = TFGame.PlayerAmount for
  TeamDeathmatch, as TeamDeathmatchRoundLogic.OnLevelLoadFinish +0xb685c0). Then B hooks TeamDeathmatch's OnLevelLoadFinish too.
- Retires once genstart is the default: nothing hand-written (the first spawn was the vanilla OnLevelLoadFinish's); the IGameContext
  "todo" rule is replaced.
- Update (merge of master 80216b8): tfex-translator delivered the type-test rule (`"TF.EX.Domain.Ports.IRoundPlayerSpawner" = { is = ... }`,
  tfexgen_RoundSpawner: SpawnRoundPlayers per mode, teams included), so SpawnFirstRound now spawns through it for every mode. Smoke lobby with
  genstart 1 on the merged tree PASS 0/596 (runs/004202). Left for team mode: hook TeamDeathmatchRoundLogic.OnLevelLoadFinish (+0xb685c0)
  like the FFA three, and run the `team` scenario with genstart 1.
- Option (A), team mode (2026-10-18 00:50): TeamDeathmatchRoundLogic.OnLevelLoadFinish (+0xb685c0) hooked like the FFA three, so every
  versus mode starts through TF.EX's SpawnFirstRound (teams through the translator's RoundSpawner). NOT tested on a TDM match: `team`
  on master creates a mode-3 lobby ("[vs] mode 3": hw.txt lobbymode 5 doesn't reach the generated lobby builder; Integration A's side).
  Runs of `team` (an LMS match): genstart 1 1/588 differing (p0 at tick 323, runs/004512), again 0/599 (later run); defaults 0/597. The
  one-tick difference didn't repeat in 5 other genstart runs: watch it.
- (ws/match, 2026-10-18) The narrow map screen: TF.EX's generated map screen (Begin_Postfix, GetRandomVersusTower) already ran for a
  standard netplay lobby, but xplay::LobbyStartSession then set the level system from the hand pick (tfex::Tower::Select). Now TF.EX's
  pick stands there (vanilla StartSession takes the selected button's level system); the hand pick only serves a wide lobby (a Switch
  difference, stays) and the scripted test host (`lobbyhost 1`, no netplay mode: TF.EX's code is inert; tools/rematch-test.sh,
  lobby-vs-browser.sh's defaults). So no narrow hand lines can be deleted while the test host exists: Tower::SelectStandard and the hand
  Begin / GetRandomVersusTower branches are the test host's (S-tools). The hand model is still logged beside TF.EX's pick
  ("tower = the hand model's"). Smoke: lobby 0/599, join 0/592, rematch, quit, disconnect, team 0/597: all PASS.
- smoke `quickplay`: expects 'server message QuickPlayMatchFound' (the generated screens don't log the hand "OPPONENT FOUND").
- (ws/match, 2026-10-18) Hand models::Lobby readers in B's files moved to TF.EX's own lobby (gen::svc::GetOwnLobby, ws::Lobby):
  NetplayManagerStartLobbyMatch (seed, length, mode, map, variants, session players by seat, archers, teams, peers, room; SortBySeat /
  ResolveArchers are templates over either player type), the "in a lobby" checks (NetplayManager's resync notice, EndOfMatch's three
  LeaveLobby guards and the rematch seed log, InputLock.InLobby, xplay's live-log flag). Left: ResolveArcherForSeat(const models::Lobby&),
  A's roll call caller (goes with A's readers). Dropped: the StartLobby log comparing WiderSet's setting with the lobby's Mods (diagnostic
  only). Smoke: lobby 0/598, join 0/599, rematch, quit, disconnect, team 0/595: all PASS.
- (ws/match, 2026-10-18) The 30 s sync timeout through TF.EX's NetplayManager.AbortIfSynchronizationFailed (genstart: TF.EX's pending round
  logic): rules NetplayManager._synchronizationTimer (non-null while waiting) and Stopwatch.ElapsedMilliseconds -> svc::SyncWaitMs (xplay's
  sync-wait clock; the translator's 9c09a45 made property rules work). After its abort, TF.EX's TFGame.Update part (DisconnectFromServer,
  ResetPeer, empty lobby, netplay menu, TF.EX's notification) is EndOfMatch::GenSyncAbort. With genstart off, xplay's own 30 s timeout and
  OnInitialConnectionFailed stay (they retire with the genstart default). Smoke lobby with genstart 1: 0/595 (the check runs every waiting
  frame), defaults 0/599. NOT tested: an actual 30 s failure (no scenario with a peer that never connects).
- (ws/match, 2026-10-18) The scripted test host retired (the user: "more issues than gains"): tfex/LobbyTest.cpp (85 lines: hw.txt
  lobbyhost 1, its CreateLobby / ready / start driver, the dead RequestHost), xplay's lobbyhost key (TF.EX's DrainGameThreadActions now
  called directly), the hand narrow tower pick (NetplayManager.cpp SelectStandard, Safe, the narrow self-test: TF.EX's map screen picks;
  Tower::Select is wide-only), the test-host-only hand NotifyMatchEnded and the dead OnLobbyMatchFinished (EndOfMatch.cpp), the map
  screen's hand-model log comparison. Tools: lobby-vs-browser.sh's defaults and rematch-test.sh use the real menus (lobbyui 1); smoke's
  base drops lobbyhost 0; smoke `private` expects TF.EX's CreateLobbyResponse. Code: +19 -161 (net -142 lines).
- (same batch) NetplayManager.cpp off the hand model types for A's Models.hpp removal: VariantsExtensions FirstUnported / HasAll /
  ApplyVariants take TF.EX's GameData.Variants (two TEMPORARY models::StrList wrappers until A's MainMenuPatch switches);
  ResolveArcherForSeat and the hand SortedBySeat removed (no callers). Smoke: lobby 0/599, join 0/593, rematch, quit, disconnect,
  team 0/595 (before the variant change), lobby 0/599 after it.
- Running total retired in B's files this push (excluding docs): ~160 lines (plus the gendefault branch's 16, not on master).
- (2026-10-18) B's remaining L rows verified (docs/HAND-CODE-INVENTORY.md); none retire today, each kept for a reason:
  - MatchPause.cpp: Pressed = S-rollback (the pause buttons recorded per simulated frame and replayed in resimulations); HandlePausing =
    glue (xplay's HandlePausing hook owns the address: one trampoline per address, it calls TF.EX's two generated prefixes); IsOpen /
    ForceClose = glue (xplay's neutral input while open, EndLobbyMatch's OnSceneTransition); Frame = S-tools (hw.txt lobbypause, the
    `pause` smoke).
  - PlayerPatch.cpp: glue: xplay hooks Player.Update; the hook calls TF.EX's generated Player_Update_Postfix (one trampoline per address).
  - InputLock.cpp: S-input: TF.EX's InputService drives PC XGamePad / keyboard objects; on the Switch the same lock gates JoyConInput's
    menu getters, and [service] rules route TF.EX's Disable/EnableAllControllers / IsInputLocked calls here.
  - EndOfMatch.cpp: the end-of-match menu is already TF.EX's generated PauseMenuPatch (the hand hooks chain the addresses).
    QuitToNetplayEntry and OnRemoteLeftMidMatch retire with the genend default (ws/match-gendefault: its note that the vote's QUIT used
    QuitToNetplayEntry was wrong: the QUIT is the generated PauseMenu_Quit; remove it there too). OnInitialConnectionFailed retires
    with the genstart default. ReturnToMenu and the ArcherSelectLobby / RematchLobby handling belong to TF.EX's MatchmakingService
    (ReturnToMenu, RematchLobby): with genmm the only path they should move to the generated service; coordinate with A, whose hand
    MatchmakingService state (ConsumeArcherSelectLobby / ConsumeRematchLobby) is being retired. QuitTeardownFrame = S (the Switch's
    teardown when the Level gives way to the menu: EndLobbyMatch, rollback, sound). OnLobbyResultsFinished = glue (xplay's results-
    finished detection into TF.EX's HUDPatch path). DropMatchSettings = S (the Switch's PauseMenu ctor inlines AddItem, so TF.EX's
    AddItem prefix never sees MATCH SETTINGS).

## Retired hand code (running list; the move to TF.EX's code, docs/HAND-CODE-INVENTORY.md)
Branch ws/match-gendefault (NOT for master until a hardware session with the new logs clears the 3-player TDM freeze, which happened with
genstart / genend on):
- genend / genstart on by default (hw.txt `genend 0` / `genstart 0` turn them off).
- Retired: EndOfMatch.cpp `OnRemoteLeftMidMatch` (11 lines, with its comment) and xplay.cpp NetplayGate's hand disconnect branch (5 lines:
  OnRemoteLeftMidMatch -> QuitToNetplayEntry); its declaration in xplay.cpp; smoke `disconnect`'s hand-path alternatives. The `else` that
  remains only covers genend 0 or a NetplayManager that wasn't made: it logs and ends the session (EndLobbyMatch), no menu change.
- Kept, verified still used: QuitToNetplayEntry (the vote's QUIT), ReturnToMenu / OnLobbyResultsFinished / OnLobbyMatchFinished /
  DropMatchSettings / QuitTeardownFrame (the results and vote flow, not the disconnect path: the inventory's "genend" grouping is too coarse
  for these rows), OnInitialConnectionFailed (the 30 s sync timeout: TF.EX's AbortIfSynchronizationFailed is the next candidate).
- genstart retires no hand code (the first spawn was the vanilla OnLevelLoadFinish's own).
