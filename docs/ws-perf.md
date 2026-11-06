# Undo-log cost for the real console (workstream D, 2026-10-15)

Task: get rollback.cpp's per-frame cost ready for the Switch's A57 cores (budget: a normal frame well under 16.6 ms including the
game's ~1.2 ms update and Draw). Measure first, keep exact correctness.

## What was there (measured in Ryujinx, M-series Mac, `tools/switch-vs-browser.sh`, rollback on, full match)
- 25,044 tracked pages (~100 MB: heap 23,900, Boehm metadata ~120, main-module globals ~1,000); hot set ~3,400-3,600 pages, of
  which only ~65 change per frame.
- EndTick compares every hot page plus a cold slice of >= (pages - hot) / 12 (every cold page verified within kWindow/4 = 12
  frames): **~5,300 pages per tick = 21 MB of live memory against 21 MB of shadow (42 MB read).**
- PendingScan before each rollback compares every non-hot page: **~21,500 pages = 170 MB read**, ~230 times per 4,380-frame match.
- The restore itself is cheap (only the logged pages).

## Telemetry added (rb::GetTiming; telemetry keys and the `[rb] timing` line at vsstop)
p50 / p99 / max (ms, x100 in telemetry) and sample counts of: first-time EndTick (`rbEndTick*`), resimulated EndTick
(`rbEndTickResim*`; an EndTick at or below the highest frame seen), PendingScan (`rbPendingScan*`), the restore inside Rollback
(`rbRestore*`), and pages compared per first-time EndTick (`rbComparedP50`, `rbComparedMax`). Plus, at Init, one log line with the
compare throughput over 16 MB (`[rb] compare bench ...`: SDK memcmp, a word loop, and the workers), so the first console run
reports the bandwidth that decides everything below.

## Changes (exact: only *where* the comparing happens changed; logging and shadow updates stay on the game thread, in order)
1. **Word-loop compare**: 8 x 64-bit XOR/OR per 64 bytes with an early exit. 2.4x the SDK's memcmp on cached data in Ryujinx
   (46 vs 19 GB/s); chosen at Init by the benchmark (falls back to memcmp if slower on the console).
2. **Worker threads** (hw.txt `rbthreads n`, default 2, 0 = game thread only): the page compares of EndTick (hot pages + cold
   slice, one batch) and PendingScan are split between the game thread and two workers on cores 1 and 2 (nn::os threads,
   priority 12, sleeping on events between jobs); the game thread waits for them.

| Ryujinx, ms p50 / p99 / max | game thread only | 2 workers |
|---|---|---|
| EndTick, first time | 2.54 / 6.48 / 8.84 | 1.88 / 6.30 / 8.44 |
| EndTick, resimulated | 0.84 / 3.70 / 5.52 | 1.35 / 4.57 / 8.71 |
| PendingScan | 6.62 / 10.91 / 11.63 | 5.38 / 8.20 / 13.78 |
| restore | 0.24 / 0.98 / 1.27 | 0.36 / 1.29 / 1.44 |

Logs identical to the browser in every run (only the known T 2153). In Ryujinx the gain is modest (-25% EndTick p50, -20%
PendingScan p50): the host's memory system is fast and the emulator's cores share it. On the console the compare is DRAM-bound
on slow cores, where more cores pulling memory should help more; the Init benchmark and the telemetry will show it.

Final check with the defaults: switch-vs-browser 1 of 4,376 ticks differs (T 2153; EndTick 1.84 / 6.53, PendingScan 5.31 / 9.12,
222 rollbacks); `tools/lobby-auto.sh` (lobby match, 110 rollbacks) 2 of 4,377 differ, T 2153 and T 4107, both round-change
frames with equal rng (the known logging artefact).

## Tried and dropped
- **Demoting hot pages that stopped changing** back to the cold sweep (hw.txt `rbdemote n`): fewer compares per tick, but every
  later change of such a page becomes a late discovery, and with rollbacks resetting every page's verification frame those
  started landing below the window's floor ("a late page wanted frame ... below the window's floor", 4 times) and the match
  desynced from tick 851, then crashed. Not exact with a 48-frame window: removed.
- **Skipping pages in free Boehm blocks** (hw.txt `rbpark`): only 4 of 25,044 pages are in free blocks (the heap is expanded and
  used), and the verify mode caught 1 parked page changing without an allocation hook seeing it. No gain, not exact: removed.

## What the console probably needs (projection, to be confirmed by the telemetry)
An A57 core streams perhaps 3-6 GB/s; two streams per compare. EndTick (42 MB read) would take roughly 8-14 ms on one core and
perhaps 4-7 ms on three; PendingScan (170 MB) roughly 30-55 ms on one core, 15-25 ms on three, on every rollback frame. That is
over budget: the full-compare design has to read ~4x fewer bytes on the console. Options, best first:
1. **Write tracking instead of comparing** (exact, removes most of the cost and PendingScan entirely): keep the tracked pages
   read-only between ticks (svcSetMemoryPermission on the heap sections, if their memory state allows it), mark a page dirty in a
   user exception handler on its first write and make it writable again; at EndTick compare/log only the dirty pages and protect
   them again. Known now (Init logs it: `[rb] memory state of ...`): Boehm's heap sections and its bottom index are in
   Horizon's normal heap (svcQueryMemory type 0x5 = Heap, perm RW), a state svcSetMemoryPermission accepts; the main module's
   globals are type 0x4 (the module's writable data: may not be reprotectable, but they are only ~1,000 pages and can keep being
   compared). Still to find out: nn::os::SetUserExceptionHandler and returning to the faulting instruction
   (svcReturnFromException), and whether Ryujinx emulates both. ~65-300 faults per frame are cheap; a full experiment is the
   next step.
2. **A larger window** for a smaller cold slice: the slice is (cold pages) / (kWindow / 4); kWindow 96 halves it (~1,800 -> ~900
   pages per tick, about -17% of EndTick) for ~4.6 MB more side-state memory. Does not help PendingScan.
3. **No cold slice during resimulated ticks** (all pages were just verified by PendingScan and reset to the rollback frame): about
   a fifth of all EndTick cold work, but the "verified within 12 frames" bound then depends on the rollback depth; needs a proof
   against the window before it is safe.
4. Hashing the shadow (compare live pages against a stored 64-bit hash) halves the bytes read but is not exact (2^-64 per page per
   tick); only acceptable as a pre-filter if exactness is relaxed, which the project rule does not allow.

## Seen on the way (coordinator)
- One run had a 47-frame rollback and `late entries ... (beyond window 21)`: a late discovery placed in the oldest log because its
  real change frame had fallen out of the window, which can restore the wrong content. Logs stayed identical that time; with the
  window at 48 and rollbacks this deep it is a correctness risk worth a guard (refuse / resync instead of a silent wrong restore).
- One run started Ryujinx with `combined-mod` as the live native mod (a mod switch by another session between activation and
  launch?); the next runs were fine.

## Write tracking (built 2026-10-15; hw.txt `rbwrite`, default 0 = compares as before)
`wtrack.cpp` + rollback.cpp. Only Boehm's heap sections (Horizon heap, type 0x5) are tracked; Boehm's metadata and the module's
globals (type 0x4) stay on compares.
- **`rbwrite 1` (live, for the console).** Init runs a self-test first: it registers a handler with
  nn::os::SetUserExceptionHandler, makes one cold heap page read-only, writes its own first word back into it and checks that
  the handler fired once and the write landed (`[wt] self-test: handler fired N time(s) ... write tracking WORKS / does NOT
  work`). Only if it passes:
  - the heap sections are made read-only after every log close (EndTick, PendingScan, the restore, the rebaseline after a
    collection);
  - the handler takes the fault address as the u64 after the ESR word (data abort, EC 0x24/0x25: libnx's ThreadExceptionDump
    layout), accepts it only inside a protected range, pushes the page into a lock-free ring, makes it writable and resumes the
    faulting instruction with svcReturnFromException(0);
  - EndTick and PendingScan compare only those dirty pages among the heap pages (no cold slice, no late finds for them), plus the
    untracked pages as before;
  - kernel writes don't fault (an IPC into a read-only buffer fails instead): nn::fs::ReadFile (both overloads) is hooked to make
    its destination writable and report it dirty first; a collection runs with the heap writable (rb::BeforeCollect);
  - any failure (a protection call, the ring overflowing) turns it off and falls back to compares, exactly: every tracked page is
    marked verified at that frame (no fault since the last reprotect = unchanged).
  Telemetry: `rbWriteMode`, `rbFaultsP50/P99/Max` (faults per tick), and the `[rb] timing` line.
- **`rbwrite 2` (simulated, for Ryujinx):** the same bookkeeping with the dirty set taken from comparing every tracked page (no
  protection, no faults): checks the integration, not the speed.
- Validation in Ryujinx: `rbwrite 2` over a full rollback match (236 rollbacks): 0 differing ticks, no late finds at all (none of
  the 236 rollbacks had to move earlier), pages compared per tick 5,200 -> ~1,220 (untracked + dirty; the simulated dirty set
  itself still costs a full compare, so its timing says nothing). Default (`rbwrite 0`) rerun: 0 differing ticks.
- **Ryujinx doesn't enforce guest page permissions**: svcSetMemoryPermission(R) succeeds, the write lands, no fault (self-test:
  "handler fired 0 time(s), page perm now 0x1, content kept 1: write tracking does NOT work"; the run continues on compares, logs
  identical). So the live path can only be tried on the console.

### Console session plan
1. `rbwrite 1` with the usual rollback lines; read `[wt] self-test ...`. If the process stops right after "writing to it", the
   handler can't resume (or the info layout differs): note it and run without `rbwrite`.
2. If it passes: a full match; compare `[rb] timing` (EndTick, PendingScan, faults per tick) with a run without it, and the
   per-tick log with the browser's.
3. If faults per tick are high (every hot page written every frame faults once per tick; maybe a few hundred, each a kernel round
   trip), the next step is a hybrid: pages written in most frames stay writable and are compared, the rest stay protected.

## Native state in the module's globals (2026-10-15)
Question (coordinator): the main module's .data / .bss (0x450e000..0x4904000) are restored wholesale, native engine state included;
which words that change during a match belong to native layers (allocator, GPU, audio, nn::fs, NVN), and keep those out of restores
with exact byte ranges, game statics still restored.

**Measured** (hw.txt `rbglobstat 1`, rollback.cpp `GlobCount` / `GlobalsReport`): at every first-time EndTick, at the cold slice and
in PendingScan, each changed globals page is diffed word by word against its shadow (its content at the previous log close) and every
changed 8-byte word is counted by module offset; at vsstop the words are merged into ranges of adjacent words and logged, most
frequent first (`[rb] glob +lo..+hi (n words) changed in up to N ticks ...`). Two full rollback matches (switch-vs-browser, ~225
rollbacks each, logs identical to the browser): **~390 words in ~175 ranges change, every one of them Boehm or managed state**.
Attributed with `tools/re-verify/globref.py` (ADRP references and the R_AARCH64_RELATIVE data slots through which the BRUTE code
reaches statics; users classified by code address):
| words | changed in | users | what |
|---|---|---|---|
| +0x48e2af0.. (GC_arrays; slot +0x483b648) incl. +0x48e2b10..b38, +0x48e2fb0..+0x48efc48 | every tick .. once | Boehm (+0x2856000..) | allocation counters, free lists, mark state, block index; consistent with the restored heap: must stay restored |
| +0x48e25f0..+0x48e2a58 | 120-1,100 ticks | Boehm (GC_alloc_large, GC_expand_hp, finalization) | large-block / heap-growth state |
| +0x49033d0, +0x4903430 | 2,000 / 24 ticks | Boehm | GC_clear_stack's call counter (every 13th call clears), GC statistics |
| +0x483f6c8, +0x4840550 | every tick | managed static get/set pairs (+0xa3f00/10, +0xee560/70) | managed static fields: game state |
| +0x484e218..+0x488d6d0 (~100 ranges) | every tick .. once | managed (slots in +0x4791xxx..) | managed static fields |
| ~40 single words in .data +0x45168d8..+0x471fe30 | once (frames 0, 32, 44, 171, 264, 1764, 2207, 2298: level loads) | managed type descriptors | lazily filled type caches (0 or -1 before) |

**No word of the native layers changed between ticks in either match**, the allocator object included. Their globals, found
statically (every global a function below 0x48000, the platform layer, uses):
- +0x483be30..+0x483d258: nn::audio layer pointers (+0x483be30..+0x483bea8; users +0x17b00..+0x19430: the native sound instance
  create / play / stop / destroy functions), the NVN device / queue / memory-pool globals (+0x483bec8..+0x483cf68; +0x19600,
  +0x1e8f0, +0x1eff0, +0x1e030; includes the already kept GPU pool manager +0x483cee0..+0x80), hid (+0x483d088..; +0x305e0,
  +0x30700). 935 code references, all from the platform layer; the next used word (+0x483d258) is also used by managed code.
- +0x4903650..+0x4904000: the runtime's malloc arena (base, size at +0x4903650; written once by +0x2861730) and the
  nn::mem::StandardAllocator (+0x4903660: initialized + thread cache flags, arena pointers; nonzero up to +0x4903698), the last
  object of .bss; only the runtime's malloc / free / realloc wrappers (+0x2861790..) use it.
- The rest of the native layers' mutable state (allocator heap, voices, pools, nn::fs cache) lives in the SDK allocator's arena and
  the audio / GPU work buffers, which the undo log does not track: never rewound.
The words in between (+0x483b870..+0x483be28: C++ guard variables, nn::os mutexes / events, used by both sides) stay restored.

**Change** (xplay.cpp PresentHook, next to the GPU pool manager): both native blocks are kept out of restores like the other
KeepObject ranges (`[xplay] rollback: keeping native platform globals (audio, NVN, hid) at ... (5160 bytes)` and `... SDK allocator
(arena, StandardAllocator) ... (2480 bytes)`). Since nothing in them changed between ticks in Ryujinx this is a guard, not a fix for
an observed failure: a word the console's SDK writes inside a tick (the allocator's thread cache, an NVN pool made mid-match as with
the pool manager before, an audio voice slot) can no longer be rewound against its untracked arena. Game statics (managed fields,
type caches) and all of Boehm's state stay restored.

Verification: switch-vs-browser rollback matches with the keeps: run 1 0 of 4,376 ticks differ (229 rollbacks), run 2 and 3 below.

## Console findings and demotion, take 2 (coordinator, 2026-10-16)
First console matches with rollback (WebRTC vs the browser on a LAN, ping ~17 ms, input delay 2):
- **The console couldn't hold 60 fps:** EndTick p50 13.4 ms / p99 ~21 ms (~5,600 hot pages + the cold slice, ~7,000 pages compared; ~1.9 us
  per page compared), update p50 14.7 ms, frame period p50 17 ms / p99 25-30 ms. The browser then ran ahead and GGRS made it wait
  (WaitRecommendation up to 14 frames, NetworkInterrupted): the "lag spikes". No rollback at all happened (rbLoads 0: every frame was
  simulated with confirmed inputs on that LAN), so the whole cost bought nothing there; on the internet it also pays PendingScan
  (~35 ms per rollback projected) and resimulation.
- Ryujinx is ~4.8x faster at this work (EndTick 2.8 ms for the same page count): compare page counts, not milliseconds.
- `rbchangestat N`: only ~56 pages change per tick (35% render data: SpriteBatchItem 12.5, VertexPositionColorTexture[] 6.9; Boehm free
  lists 7.9, globals 6.4, Particle[] 2.9, Entity[] 2.3). Write tracking would take ~50 faults per tick.

**Demotion again (`rbdemote D`), now exact:** a hot page that has never been seen changing and has been hot for D ticks leaves the hot set
(the cold sweep verifies it; a later change is a late find that moves a rollback earlier if needed, which is exact). The earlier attempt
demoted any page unchanged for D ticks and failed below the window's floor; the "no page staler than kWindow/2" rule added since and
"never changed" make it hold. With write tracking (`rbwrite`) it demotes the untracked globals / metadata, the cold minimum is dropped and
the untracked pages are verified every kWindow/8 frames. Ryujinx, netdelay 40 + jitter 20, 1,800 ticks, all 0 differing ticks:

| mode | pages compared per tick | rollbacks moved earlier (extra frames) | deepest |
|---|---|---|---|
| compares (default) | ~6,850 | 25 (124) | 17 |
| compares + `rbdemote 120` | ~2,760 | 33 (245) | 26 |
| `rbwrite 2` (simulated) | ~1,180 | 0 | 7 |
| `rbwrite 2` + `rbdemote 120` | **~340** | 2 (7) | 9 |

Console plan: hw.txt `rbwrite 1` + `rbdemote 120`: if the self-test passes, ~340 compares + ~50 faults per tick; if not, compares with
demotion (~5 ms projected instead of 13). The self-test has a crash guard (sd:/xplay/wt-selftest.txt: pending -> died = not tried again)
and logs the fault cost and whether globals / metadata pages can be made read-only too.
