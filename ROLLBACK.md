# Rollback on the Switch (design and measurements)

Status 2026-10-14: lockstep works (no rollback; PLAN.md). This is the plan for real rollback: the Switch predicts the remote
player's input (GGRS `max_prediction_window` > 0) and, when a prediction was wrong, restores an earlier frame and resimulates.

## Approach: raw memory + an undo log

Snapshots of fields (TF.State's way) mean knowing every class's layout and restoring coroutines by replay: too much and too fragile.
Instead the game's whole mutable memory is restored: Boehm's heap sections (about 61 MB), its block headers and index tables
(about 0.4 MB), and the main module's globals (4 MB). The earlier raw-memory drill (VIABILITY-TESTS 5, 6) proved that this restores
the simulation exactly (with the GPU buffer slots kept, audio muted, and our own side tables saved separately).

Copying 65 MB per frame is far too slow, so: **an undo log of changed pages**. After each tick, compare the candidate pages with a
shadow copy (the previous frame's content); for each page that changed, push its *old* content into that frame's undo list and
update the shadow. Rolling back from frame n to frame f = apply the undo lists n-1 ... f, newest first. A GGRS `SaveGameState`
is then free (the log is continuous); `LoadGameState(f)` = that roll-back; then the `AdvanceFrame` requests resimulate.

## Measurements (rbstudy; Sacred Ground, 2 rounds, 4,400 frames, full 67 MB compare every frame; test-scripts/versus/rb-study-sacred-ground.txt)

- Pages changed per frame: mean 63, p95 80, max 457 (the round change, when the next level is built).
- The ever-changed set grows 46 -> 232 (300 frames) -> 468 (1,000) -> 1,371 (3,000) -> 1,405 pages (4,400): about 5.6 MB, so
  comparing it every frame costs well under 1 ms of memory traffic. Growth is bursts of pages that are consecutive in memory.
- The managed allocator is `+0x610` (new object: `GC_malloc` `+0x285c4e0` / `GC_malloc_atomic` `+0x285c5c0`, zeroed by a memset in
  the wrapper `+0xe00`) with the large-object path `+0x285c990`. Hooking those three notes every allocation range; allocations
  average 5 per frame (max 4,114 at the round change). **89 % of the pages that change for the first time are inside an allocation of
  that frame** (1,074 of 1,204 heap pages).
- The other 11 % (130 pages, mostly in the first 300 frames) are objects that were allocated earlier and written for the first time
  later: struct arrays being filled page by page (floats: particles, i.e. cosmetic state) and pooled objects used for the first time.

## What a missed first change costs

A page that was cold (never changed since the baseline copy) has, until its first change, exactly its baseline content. So if its
first change is only noticed a few frames late (a sliced scan of the cold pages), the undo entry for the right frame can still be
filled from the baseline: exact if the page changed once in that window, otherwise it is stale by the changes in between. That
is harmless for cosmetic memory and for pooled objects that are fully reinitialised, and wrong for anything else; the drill test
(below) finds such cases, and TF.EX's own state checksum or a gameplay digest is the safety net.

## Pieces

1. `rollback.cpp` (module `rb`): page table + shadow (done: study mode), hot set (learned + allocation-marked), sliced cold scan,
   per-frame undo lists in a ring pool, `Rollback(frame)`, the GPU-slot keep list, a side-state record per frame (our RNG slot table,
   xoshiro state, miasma state, counters), a GC barrier (a collection rewrites free lists everywhere: take a new baseline and set a
   rollback floor, or keep the collector off during a match).
2. A drill (`rbdrill <every> <frames>`): every N frames roll back M and resimulate with the recorded inputs; the per-frame log line
   of the resimulation must equal the first pass. Run it in the scripted netplay match; any mismatch names the first frame.
3. GGRS: `ggrs-switch` returns the request list (save/load/advance) instead of lockstep-only; the frame driver resimulates by calling
   the Level's Update directly (like TF.EX) and runs the final tick through the normal game update.
4. Audio: gameplay sounds are muted in the probe; a TF.EX-style sound reconciler comes later.

## Status 2026-10-14 (evening): real rollback works in Ryujinx

`hw.txt`: `np 1 / npseat 0 / npdelay 2 / nppred 8 / rb 1 / rbprehot 1` (+ `rbcheck 1` for the free-list checker). Mac peer:
`PEER_FPS=12 tools/run-vs.sh -b -p "2 5400 8" ...` (a 12 fps peer makes the emulated Switch, which runs ~16 fps hidden, the side that predicts).
Result: 3,000 ticks across the round change, results and the next level load: **194 rollbacks (deepest 22 frames), 1,479 resimulated
ticks, 0 of 2,990 ticks differ from the browser reference**, no crash.

How it works now (`RbGate` in xplay.cpp, `tfx_advance_requests` in ggrs-switch):
- GGRS request list: save f = BeginTick(f) (side state); load f = roll back; advance = resimulate (Level.Update called directly) except the
  last, which is the normal game update. A frame's log closes right after its Level.Update (EndTick in LevelUpdateHook).
- Log lines are written only for frames confirmed before the previous advance (GGRS can roll back to a frame at or below its confirmed frame).
- Everything is logged, Draw included: Draw allocates from the same Boehm free lists, so its changes must roll back with the simulation's
  (`rbabsorb 1`, which kept Draw changes out, corrupts the free lists). Kept out of restores: the GPU buffer slots (only when the slot holds
  a known buffer both before and after: a dead object's slot is reused memory), the managed GraphicsDevice (0x300 bytes) and its native
  state block (device +0x238, 0x6000 bytes, in the GC heap).
- Before a rollback: the changes since the last log closed (the last Draw) go into a log of their own, and every non-hot page is compared
  (`PendingScan`, ~10 ms in Ryujinx).
- Late discoveries are exact: each page remembers when it was last verified (`lastScan`); a change found at F on a page verified at S goes
  into frame S+1's log, and a rollback to a frame in S+2..F starts at S+1 instead and resimulates with the recorded inputs (`SafeTarget`).
  In the run above 34 of 194 rollbacks were moved earlier (163 extra frames).
- Allocator: hooks on GC_malloc / GC_malloc_atomic / the large path, plus GC_generic_malloc_inner +0x285c0c0 (free-list refills: the new
  list is walked and its pages noted). Free lists: kind 0 PTRFREE GC_arrays+0x4b0, kind 1 NORMAL +0x8b8, size map +0x14d0.
  A new heap section is picked up the moment its first allocation is noted.
- What the late-found pages hold (census): SpriteBatchItem pools, vertex arrays, sound objects, XML DOM nodes / strings (level load),
  dictionaries. Lazily filled caches in old objects are why late finds can't simply be absorbed.

Open: cost on hardware (EndTick 4-6 ms and PendingScan ~10 ms in Ryujinx on an M-series Mac; measure on the console, then a CRC/
multi-core scan), GC collections inside the window, audio during resimulation (port TF.EX's sound reconciliation), desync checksums,
the browser as the peer.

## Status 2026-10-15: Switch vs the real browser (TF.EX xnet), rollback on both sides

`tools/switch-vs-browser.sh [-b] [hw lines]` (relay `node tools/relay-server.mjs --port 8811 --delay 40 --jitter 15`): a full match,
**4,394 ticks identical to the browser's own P2P log** (only T 2153 differs: the round-change frame, where the Switch logs the nested
first update before the new round's archers are known to the log, so it shows them dead; rng equal), 249 Switch rollbacks (deepest 20),
1,512 resimulated ticks, one garbage collection at a safe point, 60 fps in Ryujinx.

Fixed on the way:
- **Speed**: Ryujinx's vsync against the hidden window throttled the game to ~16 fps. Test runs set `vsync_mode` 1 (Unbounded) in
  Config.json for the run only (run-probe.sh `XPLAY_RJCONFIG`, restored on exit). **Since 2026-10-15 evening:** that plus the hidden
  window coincided with the MoltenVK/AGX crashes; runs now use a visible background window with vsync on (60 Hz), run-probe's default. The netplay gate paces
  itself at 60 Hz (TF.EX's accumulator; `nppace 0` turns it off) and **sleeps** while waiting: a spinning main thread starves same-core
  threads (Horizon does not time-slice equal priorities); the network thread now runs at priority 12.
- **Garbage collection**: off for the match (Boehm `GC_dont_gc`, slot +0x483b668); it runs at a safe point (every simulated frame
  confirmed; held up to 30 frames otherwise) once 8 MB were allocated (`GC_bytes_allocd`, GC_arrays+0x40), via `GC_gcollect` +0x2857ea0,
  then `rb::Rebaseline`. The heap must not grow inside the rollback window (a rollback across a growth breaks Boehm's block headers): +32 MB
  at the start (`GC_expand_hp_inner` +0x2858040 under the allocator lock +0x28600f0/+0x2860110) and `GC_free_space_divisor` (+0x483b650) 1,
  so a level load's allocation burst doesn't make Boehm grow instead of splitting free blocks. A growth inside the window is logged
  (WARNING) and answered with +16 MB at the next safe point.
- **Pausing**: `Level.HandlePausing` (+0xa7bfb0) is skipped during the match (the hidden window flaps Engine.IsActive -> pause menu;
  TF.EX disables pausing in netplay too).
- **Socket**: transient send/receive errors are retried; the 5 s give-up counts only time spent failing (and tolerates the tick counter
  read on another core being slightly behind).
- Never log from inside the allocation hooks (they run inside a collection, with threads suspended that may hold the log's lock).

## Status 2026-10-15 (later): the hardware blocker (nn::diag assertion) fixed; Switch vs browser through the DEPLOYED relay

`tools/switch-vs-browser.sh -r xnet-<room>` runs the same test through the live tfex-server relay (wss) instead of the local Node relay.
Runs 8 and 9 (`nppred 8 / rb 1 / rbprehot 1`, Ryujinx vs headless-Chrome TF.EX, full match to vsstop 4380): **4,377 / 4,375 ticks identical
to the browser's own log** (only T 2153, the known round-change logging artefact), 200+ rollbacks, a collection at a safe point, no crash.

What the console's assertion was (`nn::diag::OnAssertionFailure` <- +0x20354 <- +0x20604 (buffer SetData) <- ... <- Level.Update):
- `+0x20310` retires a GPU buffer **record** (0x60 bytes: +8 link, +0x48 size, +0x50 used, +0x54 GPU frame, +0x58 device block; created
  by `+0x201e0` with the SDK allocator, GPU memory from the pool manager `+0x1e030`) onto the device's retired list (device block
  +0x55e0; free list +0x55f0) and asserts the record isn't listed already.
- Records and the device block live in the **SDK allocator's arena, which the undo log doesn't track** (page class -1). A managed
  buffer's slot (GC heap, rolled back) went back to a record retired since; the replay retired it again -> assertion.
- Fix: the slot keep (`KeepRestore`) now accepts every record ever met (listed on either device list, or seen through a slot;
  `GfxRecScan` every frame) as a buffer value, not only the buffers seen by the ring hook.

Second bug found on the way (intermittent abort in `GC_freehblk` / hang in `GC_gcollect` at the first collection after rollbacks):
- Boehm's block headers come from its scratch chunk (GC_arrays +0x68 = scratch end; 0x40000 chunks in the SDK arena). A header page was
  tracked only once found (with headers already written), so a rollback across that left headers / the header free list out of step
  with the restored free lists.
- Fix (`CollectAll`): the chunk's unused tail (highest known header page .. scratch end) is tracked ahead of use.

Still rolled back although it is engine state (watch for symptoms): the SDK allocator object (+0x4903660) and the GPU pool manager's
list (+0x483cee8), both in the main module's globals. A Ryujinx run also died once in Apple's Metal shader compiler (host GPU driver,
not the game).
Hardware: not yet run with rollback (the card is back on combined-mod).

## Status 2026-10-15 (afternoon): lobby matches with rollback hold for a full match

Switch-hosted TF.EX **lobby** match (tools/lobby-vs-browser.sh: Ryujinx hosts on a local tfex-server, a headless TF.EX page joins through
its menus, scripted inputs, 60 Hz relay, `lobbypred 8`): 4,373 ticks, only T 2153 and T 4107 differ (both round-change frames, rng
equal: the known logging artefact), 100+ rollbacks, a collection, no crash (rehearsal 18).

Fixed on the way (all in xplay.cpp unless noted):
- **Native graphics vs rollback.** Buffer records, textures and GPU memory live in the SDK allocator's arena (not undone). A rolled-back
  managed slot can point at a record/texture that the undone frames retired, disposed or destroyed. Now: Retire (+0x20310) and Dispose
  (+0x20420, and two inline paths +0x210dc/+0x218a8) take the record off whatever device list holds it first; no buffer record or texture
  is destroyed while simulating with rollback on (nvnBufferFinalize wrapped via its proc variable [+0x47232e0]: graveyard; texture
  Dispose +0x21520 deferred); they are destroyed once the window's floor has passed the frame they were disposed in, unless their slot
  holds them again or SetData/Retire used them; GPU pool frees (+0x1e2f0) of blocks not allocated are skipped (double free after a
  rollback, +0x1e380 assertion); the pool manager's globals (+0x483cee0, 0x80) are kept out of restores.
  Pitfall: the native free wrapper +0x2861840 starts with a PC-relative cbz: its trampoline Orig is broken, never call it.
- **Safe-point collection.** GGRS reads new inputs in its poll and handles a misprediction found there only in the next advance, so
  "confirmed >= state frame" before the advance isn't safe (it collected, then GGRS loaded an older frame -> refused rollback -> desync).
  The collection now runs after the tick's requests are applied (RbSafeCollect, before the last frame's update).
- A late page that wants a frame below the window's floor clamps to the floor (refusing = certain desync).

## Status 2026-10-15 (evening): what changed since the afternoon, and what is still unverified

Rollback fixes (all in Ryujinx; the console has not run rollback yet):
- **Graphics destruction bound = the window's floor.** Graveyard records and deferred textures are destroyed only once `rb::Floor()` has
  passed the frame they were disposed in (a late page can move a rollback below the confirmed frame, so "confirmed" was too early).
  Textures whose memory block is no longer allocated are never destroyed twice (`PoolBlockAllocated`), GPU pool double frees are skipped.
- **Window 48 frames; no page staler than half the window.** Repeated deep rollbacks reset every page's lastScan to their target, so a
  page could go unverified past the floor and a late find landed beyond the window (a silent wrong restore). EndTick now also verifies
  every non-hot page with lastScan < F - kWindow/2 (cold sweep >= pages/12 per tick besides).
- **Safe-point collection after the tick's GGRS requests** (`RbSafeCollect`), with confirmed >= state frame.
- **Native globals guard** (D, docs/ws-perf.md): the platform layer's globals (+0x483be30..+0x483d258: audio, NVN, hid) and the SDK
  allocator (+0x4903650..+0x4904000) are kept out of restores; `rbglobstat 1` showed nothing native changing between ticks, so it is a
  guard. D's write tracking (`rbwrite 1`) and compare split over 2 worker cores are in; write tracking only works on hardware.
- **Not a rollback bug:** the seed-dependent round-1 desync of lobby matches was the versus level data (40 of 141 levels differ between
  the Switch 1.4.1.6 and PC); netplay now plays the PC levels (tools/pc-levels.sh). Unverified: Ryujinx runs kept dying in the GPU
  stack (MoltenVK / AGX; this Ryujinx ignores `graphics_backend: Metal`), so the proof is the console session (HARDWARE-SESSION.md 3).
- Results before the level fix: xnet vs browser full matches 0 differing ticks (deployed relay), Switch-hosted lobby matches 0 differing
  ticks for seeds whose rounds avoid the differing levels.

## Status 2026-10-16 (night): two restore gaps found by 4-player wide matches (branch claude/wide-teams-test)
Wide TDM, 4 players (Switch + 3 browser pages), `lobbypred 8`, Ryujinx: 3 of 4 runs crashed; after these two fixes 2 of 2 ran to the
harness's time limit (10 rollbacks each, frames 1136 and 894) without one. Both gaps exist in any mode; wide and 4 players made them likely.
- **Boehm's header free list** (rollback.cpp CollectAll): headers on GC_hdr_free_list (main +0x48e2a48, linked through hb_next at +0)
  are referenced by no bottom index, so their pages joined the undo log only once alloc_hdr (+0x285b814) had popped one and written it.
  A rollback across that restored the list head but not the popped header's first word: the head led to its new contents (0x28;
  `ldr x9, [x0]` at +0x285b820 faulted under GcInnerHook). The list's pages are now tracked from the start (meta pages 115 -> 132).
- **MonoGame's user buffers vs the kept-out graphics device** (xplay.cpp ApplyFixes): DrawUserIndexedPrimitives replaces the device's
  user index buffer (+0x260 16-bit, +0x268 32-bit; made with max(count, 6000) at +0xd6a40) and vertex buffers (dictionary +0x258,
  max(count, 2000) at +0xd6860) when a draw needs more. The device object is kept out of restores, so after a rollback to before a
  replacement it pointed at a buffer the restored heap no longer had (virtual call through garbage at +0xba744, from
  LightingLayer.RenderLightCanvas in a resimulated Level.Update). A wide level's 1008 tiles pass 6000 indices. The minimums are now the
  SpriteBatcher's cap (Math.Min(n, 5461) sprites per draw, +0xc8ca4: 32766 indices, 21844 vertices), patched at boot, so the buffers
  made at the first draw never grow.
- Still slow in Ryujinx: a 4-player match runs at ~3-7 ticks/s, the network thread spending ~5 ms per UDP call in Ryujinx's socket
  service (the host's own loopback UDP: 11 us per send). Not a console concern; telemetry no longer adds to it (repeated events within
  10 s are counted into the next report: 534 netplay_interrupted reports -> 14 connections in one match).
