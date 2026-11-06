# Viability tests: the smallest test per risk

Written 2026-10-14. Read [`STATUS.md`](STATUS.md), [`PLAN.md`](PLAN.md) and
[`TFEX-LESSONS.md`](TFEX-LESSONS.md) first.

**Goals.**
- **Minimum:** the Switch build plays the browser port online, with rollback, on content both
  have in common (the same restriction TF.EX applies).
- **Stretch:** the Switch plays real desktop TF.EX players.

**Decided:** the 1.3.3.3 vs 1.4.1.6 version gap isn't a concern, because only perfectly compatible
levels and features are allowed on both sides.

## What the setup looks like

- The browser port already runs real TF.EX, including its Rust netcode library `ggrs_ffi` built
  for WebAssembly. It has its own matchmaking and signaling server
  (`../../Towerfall Browser/cloudflare/tfex-server`, documented in the browser repo's
  `docs/MULTIPLAYER.md`).
- So for the Switch to join, four things have to be solved. Each has tests below:
  1. **Transport.** The browser speaks WebRTC (an unordered, unreliable data channel). The Switch
     has no WebRTC. The service permissions in the probe's `application.json` already include
     `bsd:u`, `nifm:u`, `sfdnsres` and `ssl`, so sockets and TLS from inside the game look
     plausible. Not yet tried.
  2. **GGRS on the Switch.** Either build the Rust library for aarch64, or port its wire protocol
     to C++.
  3. **Checksums.** What TF.EX's desync checksum covers is not yet known (test 6).
  4. **Game semantics.** The Switch must match whatever the browser runs: 60 Hz for the minimum
     (if the browser can be set to it), or TF.EX's exact 240 Hz behaviour for the stretch.

## Open decision: which tick rate for the minimum goal

We control both ends for the minimum, so 240 Hz, TF.EX's state format and its `DeterministicMath`
only matter for the stretch. A 60 Hz mode would cut the CPU cost by 4×, but it needs the browser's
TF.EX to run at 60 Hz (`Constants.NETPLAY_FPS`), and that hasn't been tried. Test 1 measures both
rates so the decision can rest on numbers.

## The tests

Where a test says **hardware**, it can't be answered in Ryujinx. Ryujinx on Apple Silicon is much
faster than the Switch's 1 GHz A57 cores, and the FPCR value is set by the OS.

### 1. CPU budget: does an update, plus rollback, fit in a frame? (hardware)

*Passive part.* In a 4-player versus match started by hand, the probe times each call of
`Orig(game, gameTime)` in `FrameHook` (`TFGame.Update`, the update only, no drawing) with the
system tick (19.2 MHz) and logs p50, p99 and max to a file on the SD card.

*Active part.* Run the update 4 times per frame (240 Hz) and then 12 extra times (a resimulation),
with a save and restore around the window, and time the whole frame.

**Pass:** (updates per frame + resimulated ticks) × update cost + saves + restore ≤ about 14 ms
at 12 resimulated ticks (roughly 50 ms of ping at 240 Hz). The 60 Hz variant must pass at 8.
Run it docked and handheld.

### 2. Snapshot cost: the hot set of changed pages (hardware)

In the same run, record which pages change over a whole versus match. Measure copy time for just
that set.

**Pass:** the set stops growing after the first minute or so, and a save plus restore of it takes
under about 1 ms per tick. If it keeps growing, fall back to field snapshots (PLAN step 4, option 3).

### 3. Hardware determinism and FPCR (hardware)

Run the existing 700-frame Trials script on the Switch and diff it against
`test-scripts/results/result-switch-700.txt`. Hand-start the level, with the script keyed to the
level start, because the menu part of the script is timed in controller polls and was flaky.
This needs the probe to log to a file on the SD card (shared with test 1).

**Pass:** FPCR reads flush-to-zero off, and all 700 frames are identical.

### 4. `System.Random` matches (Ryujinx)

At boot, the probe calls the game's own `Random(seed).Next()` 1,000 times for a few seeds. Do the
same in the browser and compare hashes. (This is PLAN step 2d, which was left to the determinism
test.)

**Pass:** the hashes match.

### 5. Versus determinism, the real gate (Ryujinx, then hardware)

A scripted 4-player versus match with a fixed map and the same seed on both sides. Log the
position and speed of every player and arrow each frame. Script a last kill, so the end-of-round
slow motion is covered.

**Pass:** per-frame values are bit-identical, slow motion included. Covers what PLAN step 3 left
open: random numbers, arrows, several players, slow motion.

### Tick rate: TF.EX's rollback sync test passes at 60 Hz in the browser build (agent experiment, 2026-10-14)

Run by a background agent in a throwaway worktree of the browser repo (branch `exp/tfex-60hz`, not
merged; the main checkout was left alone). **These are the agent's reported results; I haven't re-run
them.** Headless Chrome, the sync test with checkDistance 2 unless noted:

| Run | Result |
|---|---|
| 240 Hz control, 2 players | about 25k ticks over two round starts and a round end: no desync, no exception |
| 240 Hz, 4 players | about 70 s clean |
| **60 Hz, 2 players** (three runs) | about 20 windows each, 2 to 3 round changes: **no desync, no exception** |
| **60 Hz, 4 players** | 120 s clean |
| 60 Hz, 2 players, checkDistance 7 | 14 windows clean |
| TF.EX's own `scenario` sweep (scripted input, hard-coded 60 Hz) | 71 of 73 pass; 45 and 48 crash with `Scheduled action not found` in `PlayerExtensions.ToActions` during a rollback load. Scenario 45 crashes the same at 240 Hz, so not rate-related; whether they crash on desktop is unknown |

- **Cost:** about 3 ms per tick at 60 Hz against about 2.5 ms at 240 (TF.EX's state copying
  dominates), so roughly 190 ms of work per second at 60 Hz against 600 ms at 240. Round length in
  wall time is the same at both rates (about 50 s).
- **How it was forced:** [`browser-60hz/tfex-60hz-experiment.diff`](browser-60hz/tfex-60hz-experiment.diff)
  (apply to the browser repo at commit 6028684): a new `web/Netplay/TfexFps.cs` with three Harmony
  patches (the `GGRSConfig.Fps` getter, `NetplayPreferences.ToFrames`, and a transpiler for the
  `InputDelayAdvisor` constant), enabled by `?tfexfps=60`. TF.EX's own `ManageTimeStep` already sets
  the engine's tick length once the session rate isn't 240. The worktree and branch were removed.
- **Still hard-coded to 240 (for 1v1):** instant replay never fires at 60 Hz (cosmetic: its threshold
  is 60 ticks, round-end age maxes at 55; scale it or disable it); the browser's `AheadThreshold`
  should be 1 tick at 60 Hz (it is tuned for 240); the browser's `TickCatchUp` cap drops a few tens
  of ms after hitches (acceptable). Spectator thresholds don't matter for 1v1. GGRS scales its
  windows from fps already; per-tick gameplay patches act as vanilla at scale 1.
- **Both peers must run the same rate.** It isn't negotiated, so a 60 Hz peer can't play stock
  desktop TF.EX at 240. That makes 60 Hz the plan for Switch↔browser (and browser↔browser), and 240
  Hz the stretch goal.
- **Not verified:** a real two-peer session at either rate (the sync test is single-process), and
  whether `TargetElapsedTime` is restored after a 60 Hz session.
- **Cleaned up:** the worktree, its branch and the 718 MB scratch Chrome profile are deleted; the main
  browser checkout was never touched.

**Decision (proposed): the first version runs at 60 Hz.** That removes the 4 updates per frame and
the per-entity tick patches from the Switch's job; what remains is `Random`, the gameplay RNG, and
`DeterministicMath`. The 240 Hz path stays the stretch goal.

### Gameplay RNG whitelist: the first 14 hooks (2026-10-14)

Spec: [`GAMEPLAY-RNG-PORT.md`](GAMEPLAY-RNG-PORT.md) (a desk inventory by a background agent, checked
against the vendored v0.19.2 DLLs: no RNG-relevant difference from the source clone). Its key facts:
one generator for the whole match (reseeded at `Session.StartGame` from the lobby seed, never per
round); every draw consumes exactly one `NextULong`; the quickplay variants are `NoAutobalance` +
`SymmetricalTreasure` (not all off); `GetRandomArrowType` draws **twice** per round (the original's
`Next(N)` plus `Next(1)` from FortRise/TF.EX's `Choose`); Lava's `BubbleComplete` needs an *Ignore*;
Cataclysm is a separate milestone (procedural, needs a bit-exact seeded `Random`).

**Built (in `rnd.cpp`):** bracket hooks (register on entry, unregister on exit; typed signatures so float
and bool arguments are forwarded intact) for `ArrowCushion.AddArrow` `+0x98a2e0`,
`PlayerCorpse.DropNextArrow` `+0xafb3c0`, `Player.LoseHat` `+0xaf0620`, `OrbLogic.Update` `+0xada080`,
`DoSpaceOrb` `+0xadabf0`, `DoDarkOrb` `+0xadb370`, `DoTimeOrb` `+0xadb4d0`, `LavaControl.Extend`
`+0xa7ae30` and `TreasureSpawner.GetChestSpawnsForLevel` `+0xb862e0`; an Ignore hook on
`Lava.BubbleComplete` `+0xa77d50`; and the cases TF.EX *replaces*, done by routing instead of
re-implementing: `GetRandomArrowType` `+0xb879f0` (extra `Next(1)`), `SpawnPlayersFFA` `+0xb39070` (the
unseeded `new Random()` it makes draws from the gameplay stream: only that object, not the player
constructors inside), `VersusLevelSystem.GenLevels` `+0xbae7a0` (the unseeded `new Random()` becomes
`new Random(lobby seed)`), and `Session.StartGame` `+0xb44330` (reset the swap, reseed, forget old
sentinels). The treasure spawner's `Random` field (`+0x28`) is bound to an object that always draws from
the stream (TF.EX's `UseDeterministRandom`). Options in `hw.txt`: `rngcapture 1`, `rngseed N`.
In Ryujinx with the stream on, the scripted Trials run is unaffected (700-frame log still identical).

**Lesson: the metadata resolver can return the neighbouring method.** `AddArrow` came back as
`ReleaseArrows`, the two spawn methods were swapped, and `Arrow.EnforceLimit` resolved to
`Arrow.Initialize` (hooking it crashed arrow creation). Every address is now verified against constants
from the PC source (`tools/re-verify/`). `EnforceLimit` is left out (it only matters with more than 12
arrows alive).

**Added after that: three more, found from live objects and draws** (all verified in Ryujinx):
- **`Arrow.EnterFallMode`** (virtual, vtable slot `0x200`, three bool arguments): when `Arrow.Create`
  (`+0x985350`) returns an arrow, its class name and vtable are read from the object and the bracket
  goes on whatever body that arrow's slot holds (one hook per distinct body; the first arrow reported
  `DefaultArrow`, body `+0x988ea0`, which matches the PC source line for line).
- **Crown, SteelHat and WoodenHat `Update`**: found the same way when `Hat.CreateHat` (`+0xa56130`)
  returns one (the hat constructors' own draws are already inside the `LoseHat` bracket). Not exercised
  by the scripted run (nobody dies); the install path is the arrow's.
- **`Chain(Vector2, int)` constructor** (`+0x9cc380`): found by logging which function called the
  `Next(max)` draws at level load: a group of exactly 8 draws of `Next(4)` (the Trials level's three
  chains of height 20, 20 and 40). With the stream on, its draw count went from 4 to 12.

**How objects find their way to bodies (reusable):** a managed object's first word points to a class
descriptor; word 1 of that is the vtable, word 0 leads to a record with the type's name (UTF-16 name
token at word 4, namespace at 5) and a method table (word 39: records of `0x60` bytes, name at `+0`,
**concrete body at `+0x18`**, reflection thunk at `+0x10`). Class records and method tables appear
lazily (a class that has not been used yet has none), so this finds virtuals from an instance but
can't give a constructor before its first call; constructors are found from their draws instead.

**Still to do for the whitelist**
- `Orb`, `Spikeball` and `LavaControl` constructors, `LavaControl.Added` (not on Sacred Ground except
  for a picked-up lava orb: needed for the other maps; find them the same way from a level that has them),
- `SpawnPlayersTeams` (`+0xb38950`, which TF.EX replaces with a different draw pattern, so TDM needs
  more than routing), `LocalSettingsGuard` (neutralise unlock state around the spawner), the
  `UnlockData` entities the local save can add to six Sacred Ground levels, and `Arrow.EnforceLimit`
  (its real address was never found).

### Test 5 plan: the scripted 2-player versus match (2026-10-14, work in progress)

**Reference:** TF.EX's own test mode in the browser at 60 ticks/s (`test lms 0 0 42 2 #NONE# 2`: last man
standing, map 0, seed 42, no variants, 2 players), driven by scripted inputs for both archers. A
background agent is building that harness and writing `TEST5-MATCH-SETUP.md` (exactly how TF.EX sets the
match up); another is writing the whitelist inventory (`GAMEPLAY-RNG-PORT.md`). **Per-tick log line**
(both sides): tick, a fold of the gameplay RNG state, then each player's position and speed as float
bits and whether it is alive. The RNG state per tick is the key diagnostic: the first tick where it
differs names the first place the two sides drew a different number of randoms.

**Switch side, what is known:**
- The game polls controller ids 0 to 5 every frame and Ryujinx reports all six as connected, so a
  second scripted player (id 1) is possible in the emulator.
- **Starting a versus match natively (found 2026-10-14):** `MapScene.StartSession` is at `+0xab54e0` (18
  instructions): it loads the static `MainMenu.CurrentMatchSettings` (slot `+0x4793818`), calls the
  `Session` constructor at `+0xb43b70` (`this` = null: it allocates; second argument the settings), then
  tail-calls `Session.StartGame` (`+0xb44330`), and never touches `this`, so it can be called from the game
  thread with any receiver. The recipe: set the static settings (`MainMenu.VersusMatchSettings`, slot
  `+0x4793398`, is the object the menus edit: `Mode` at `+0x30`, 3 = last man standing; its defaults come from
  `MatchSettings.GetDefaultVersus`: tower 0, standard length, no variants), point `CurrentMatchSettings` at
  it, enable the players in `TFGame.Players`, and call it. `Session.StartGame` also increments save-data
  stats, so this is an emulator-only tool. What remains is the player setup (the `TFGame.Players`,
  `Characters` and `PlayerInputs` statics) as TF.EX's test mode does it: that is in
  `TEST5-MATCH-SETUP.md` (a background agent is writing it).
- Everything else the test needs is in place: fixed timestep, scripted input in `Player.Update`,
  `Random`, the gameplay stream, `DeterministicMath`. Still to do: the whitelist hooks, the match
  start, per-player scripts and logging for all players, and logging the gameplay RNG state per tick.

### Gameplay RNG machinery and DeterministicMath (steps 2 and 3, built 2026-10-14)

Both are the Switch halves of what TF.EX does to make gameplay identical across peers, each checked
against TF.EX's own code running on the real .NET runtime.

- **xoshiro256** (`xoshiro.hpp`)**: TF.EX's `DeterministicRandom`, seeded through SplitMix64 from
  `(ulong)seed` (an `int` seed is sign-extended). The C++ port matches the real class on 208 reference
  lines (9 seeds as `int` and `long`: raw words, `Next()`, `Next(max)`, `Next(min,max)` including the
  large range, `NextDouble`, `NextBytes`, snapshot and restore, the 5000th value).
- **Routing and counters (`rnd.cpp`, `rnd.hpp`)**: `CalcPatch`'s depth and ignore counters
  (`RegisterRng`, `UnregisterRng`, `IgnoreRng`, `UnignoreRng`, `ResetRng`, `SetCapture`), the same
  condition for the override (`depth > 0 && ignore == 0 && capture`), and the gameplay stream
  save/load (`SaveRng`, `LoadRng`: the seed and four words, TF.State's `Rng`). While the override is on,
  calls on the object that was in `Monocle.Calc.Random` when it began draw from the stream; every other
  `Random` (a seeded one pushed inside, a local `new Random()`) is untouched, as in TF.EX. The
  `Calc.PushRandom()` hook does TF.EX's prefix (draw a seed from `Calc.Random`, call `PushRandom(int)`).
  `Monocle.Calc.Random` is the static whose address is in the relocated slot at `+0x47919d0`;
  `PushRandom()` is at `+0x908fc0`, `PushRandom(int)` at `+0x908e40`, `PopRandom` at `+0x909050`.
  Boot self-tests (in Ryujinx): the stream equals the port for seed 1234 while another object stays on the
  .NET algorithm, and the counters behave (including no underflow). **Not built yet: the whitelist
  itself** (the game methods that call `RegisterRng`); that is the inventory in `GAMEPLAY-RNG-PORT.md`.
- **DeterministicMath (`dmath.hpp`, `dm.cpp`)**: the C++ port matches the real class bit for bit: 1,936
  special-value lines (NaN, ±0, infinities, subnormals, huge angles, `pow` edge cases) and four hashes
  over 200,000 game-like inputs per function, at `-O0` and `-O2`. On the Switch it is wired in by
  redirecting the four managed `Math` wrappers (single `b` instructions at `+0xad50`, `+0xad60`,
  `+0xade0`, `+0xae20`) for every caller. That is a superset of TF.EX's method list on purpose: every
  gameplay use of those functions in the PC source is on TF.EX's list; the rest are menus, drawing and
  exact integer powers. On-device self-test through the game's own wrapper addresses: all four hashes
  equal the .NET reference (58 ms), so this toolchain emits no fused multiply-adds either. The scripted
  Trials run is unchanged.
- **Caveat for the whitelist port:** the hooks are compiled without unwind tables, so a managed
  exception thrown *through* a hooked frame can't be unwound; TF.EX's Harmony postfixes also don't run
  on an exception, so exceptions inside gameplay sections are not expected, but it is a risk to keep in
  mind (and, if needed, a reason to build the hooks with unwind tables).

### 7c. WebSocket relay: works, with a stall every few minutes on lossy Wi-Fi (2026-10-14)

**What exists:**
- `tools/relay-server.mjs`: a dependency-free WebSocket relay (`/relay/<room>?peer=<id>`) with an
  option to inject stalls (hold everything for N ms every S s, release in order, which is what a
  lost packet does on TCP). It also serves `/echo.html`, a page that turns a browser into the
  echoing peer.
- `tools/relay-peer.mjs`: a Node echo peer. `tools/relay-probe.mjs`: the Node twin of the Switch's
  test, with the same statistics and pass line.
- The probe's `relay <host> <port> <tls 0|1> <seconds> [<ipv4>]` stage: from inside the game, 60
  packets a second of 100 bytes (plain TCP or TLS, `TCP_NODELAY` on, non-blocking reads), the
  round trip of every packet, and the gaps between echoes.
- `relay-worker/`: the same relay as a Cloudflare Worker with one Durable Object per room (4 peers
  per room, 1 KB messages, 150 messages/s per connection). **Deployed 2026-10-14 as
  `tf-relay-probe`** at `https://<relay-probe-worker>` (a separate Worker from
  `tfex-server`). Delete it when the tests are done: `cd relay-worker && npx wrangler delete`.

**Method.** The echo peer sends every packet straight back, so each measured round trip crosses
the relay **twice** (out and back, two legs each way): per-leg jitter is roughly half of what the
table shows. The pass line stays as set: p99 within 50 ms of the median, no gap over 150 ms.

| Run | p50 | p99 | p99.9 | Longest gap | Verdict |
|---|---|---|---|---|---|
| Node twin, local Node relay, loopback, 20 s | 0.52 ms | 3.50 ms | 5.4 ms | 21 ms | pass (relay and peer add almost nothing) |
| Node twin, local relay injecting 250 ms stalls every 5 s | 0.75 ms | 202.59 ms | 249.5 ms | 269 ms | fail, as it should (4 gaps over 150 ms) |
| **Node twin through Cloudflare, Node peer, 60 s** (Mac on Wi-Fi) | 32.1 ms | 83.6 ms | 157 ms | 153 ms | marginal fail (p99 − p50 = 51.5) |
| **Node twin through Cloudflare, Node peer, 300 s** | 30.8 ms | 79.9 ms | 297 ms | **539 ms** | fail (p99 − p50 = 49.1, but one stall) |
| **Node twin through Cloudflare, a browser tab as the peer, 60 s** | 30.3 ms | 58.3 ms | 129 ms | 148 ms | **pass** (p99 − p50 = 28.1) |
| Probe in Ryujinx, TLS, through Cloudflare, browser tab as peer, 40 s | 33.1 ms | 186 ms | 535 ms | 520 ms | not a network result (Ryujinx scheduling adds noise) |

- **Nothing was lost** in any run: 18,000 of 18,000 echoed over five minutes. TCP delivers; the
  cost is delay.
- **The relay itself is fast.** About 31 ms median for a round trip that crosses it twice (the
  Mac is in Norway, on the Oslo edge), so roughly 15 ms added per one-way leg.
- **The tail is TCP waiting for lost packets.** A concurrent ping from the same Mac on Wi-Fi to
  Cloudflare's edge showed 5 to 7 ms typical, but 1% loss and a 28 ms maximum. The 539 ms
  stall is a TCP recovery after a loss: everything behind the lost packet waits. In 300 s there
  was one stall that long, and p99.9 was 297 ms.
- **What it means for play:** about one half-second hitch every few minutes on lossy Wi-Fi, none
  on a clean link. GGRS keeps the game in step across it (it predicts up to its window, then waits
  and catches up), but the player feels it. A browser's WebRTC channel is unreliable by design
  and would just drop the packet. Not tested: GGRS itself over a real stall.
- **The Switch side works:** the probe in the game, over TLS with non-blocking reads, joined the
  Cloudflare room and played against a real browser tab with nothing lost. Its numbers are
  Ryujinx's, not the console's.
- **Verdict by the written criteria: fail on this Wi-Fi, marginal.** Viable as a first transport,
  with hitches. To remove them, the Switch needs an unreliable transport: WebRTC on the console
  (large), or UDP from the Switch to a small bridge that speaks WebRTC to the browser (needs a
  host with UDP, so not a Cloudflare Worker). To measure how much the console's own Wi-Fi adds,
  run the probe on the console against the same relay.
- **Still open for 7c:** the console numbers (hardware checklist), a wired Mac for comparison, and
  GGRS behaviour over a stall.

### 8. GGRS on the Switch: pass in Ryujinx (2026-10-14)

**The real GGRS fork TF.EX uses runs inside the TowerFall process.** Two P2P sessions in one
process over an in-memory socket, driving a small deterministic game, with delay and packet loss:

| Delay (polls) | Loss | Result | Rollbacks |
|---|---|---|---|
| 0 | none | pass | 0 |
| 2 | none | pass | 2 |
| 6 | none | pass | 1,169 |

Every run reached frame 600, synchronized both sessions, got no desync event, and compared
identical saved state for all 591 frames up to the prediction window. The Mac run gives the same
numbers, including the rollback counts, so the Switch build behaves like the native one. The Mac
run also passed with 1-in-11 and 1-in-5 packet loss (up to about 970 rollbacks).

- **Sources:** [`ggrs-spike/`](ggrs-spike/) (the Rust test and a build script), the probe's
  `ggrs <ticks> <delay>` stage, and [`net-probe/exlaunch-overlay/source/program/shim.c`](net-probe/exlaunch-overlay/source/program/shim.c).
- **How it builds:**
  - Rust has no `std` for the Switch target (`aarch64-nintendo-switch-freestanding` fails to
    build it). Instead the staticlib is built for **aarch64 Linux musl with `std` rebuilt from
    source**, using a custom target that turns off native thread-locals (so `std` keeps them behind
    pthread keys), uses PIC and aborts on panic.
  - The exlaunch module links with `-nodefaultlibs`, so every libc call binds to the **game's own
    libc at load time** (its `malloc` family is the game's allocator, set up by `nninitStartup`).
    The module is 340 KB.
- **`shim.c` supplies the rest.** `std` references 165 libc symbols; the game's main module and
  the SDK export 78 of them. Of the other 87, stubs return -1 (sockets, processes, signals, files:
  never reached). The ones that do matter are real: `getrandom` (also reached through
  `syscall(278)`), `clock_gettime` and `nanosleep` from the system tick (the SDK's
  `clock_gettime` rejects Linux clock ids), `sysconf`, `pthread_atfork`, and single-thread
  `pthread` keys, once and mutex (the SDK's `pthread_getspecific` aborts on Rust's keys). `write`
  and `writev` go to the probe's log, so Rust panic messages are readable.
- **What this means:** no C++ port of GGRS is needed. The Switch can run the same session code,
  protocol, timing and desync logic as the browser build. Remaining pieces are a socket for it
  (a `NonBlockingSocket` over the transport from test 7c) and the game glue.
- **Limits:** Ryujinx only, in-memory transport, a toy game. Not yet: the real transport, the
  game's save and load, or hardware. Everything runs on one thread, so the pthread shims
  would need real locking before any second thread calls into Rust.

### 6. What TF.EX's checksum covers (desk check)

Read the `ggrs-ffi` source and TF.EX's save path.

**Result is one of:**
- **Serialized state:** the Switch can't match it without serializing identically, so the minimum
  needs the browser side changed to checksum only gameplay values (positions, arrows).
- **A gameplay-only subset:** feasible to reproduce.
- **Can be turned off:** easiest.

### 7. Networking from inside the game (both)

In stages:
- **7a.** Plain TCP echo to the Mac on the LAN.
  **Pass:** the round trip works on Ryujinx and on hardware.
- **7b.** TLS WebSocket to our own signaling server (`/room/<uuid>?peer=<uuid>`).
  **Pass:** the handshake completes and the Switch receives `IdAssigned`.
- **7c.** A relay between a Switch and a browser through the server: 60 packets a second of about
  100 bytes for 5 minutes. This needs a relay route added to the server and a WebSocket path in
  the browser's `tfnet.js`.
  **Pass:** p99 latency under about 50 ms beyond the base round-trip time, and no stall over
  150 ms.

**The expected problem:** WebSocket is TCP, so a lost packet stalls everything behind it, whereas
the browser's WebRTC channel is unreliable and unordered by design. Rollback tolerates loss and
tolerates stalls badly. If 7c fails, the fallback is WebRTC on the Switch, which is a much bigger
project.

### 8. GGRS on the Switch (Ryujinx)

A go/no-go spike: cross-compile the `ggrs` core (or `ggrs_ffi` with a stub socket) for aarch64,
link it into the probe and run a two-session loopback handshake in-process.

**Pass:** it builds, links and syncs two sessions. **If not:** port the GGRS wire protocol to C++.

### 9. Real TF.EX interop (stretch, deferred)

Needs WebRTC on the Switch, or a server bridge for desktop peers, plus TF.EX's exact semantics
(240 Hz, `DeterministicMath`, the gameplay RNG whitelist). Don't start until the minimum works.

## Order

1. **Tests 7a and 7b.** If the game process can't reach the network on hardware, nothing else
   matters.
2. **Tests 1, 2 and 3,** in one hardware session, since they share the SD-card logging.
3. **Tests 4 and 6.** Cheap, no hardware.
4. **Test 8.** The biggest technical unknown. It decides whether GGRS gets ported to C++.
5. **Tests 5 and 7c.** The main gates.

## Results

### 7a and 7b in Ryujinx: pass (2026-10-14)

Probe: [`net-probe/`](net-probe/README.md). It runs on its own thread inside the game process and
uses the game's own SDK (`nn::nifm`, `nn::socket`, `nn::ssl`), looked up by name in the SDK
module's exports. No new service permissions were needed.

- **7a, plain TCP:** connects to a local echo server, 20 of 20 echoes. Round trip 0.07 to 0.28 ms
  minimum, 0.4 to 2.6 ms average (host loopback, so not a network figure).
- **7b, TLS WebSocket:** TCP connect, TLS handshake (69 to 87 ms) and WebSocket upgrade to
  `wss://<your-server>` all work. The reply was `HTTP/1.1 101 Switching
  Protocols`, then `{"IdAssigned":"<uuid>"}` as the first frame.
- **Findings for later steps:**
  - `nn::ssl` connections need `SetIoMode(blocking)` (value 1) before the handshake. The default
    returned would-block (`0x1987b`) on reads.
  - `nn::socket::GetAddrInfo` returned an error (7) in Ryujinx, so the test used the server's IP.
    `nifm::IsNetworkAvailable` also reads false there. Both are probably Ryujinx's networking
    stubs, but only hardware can say.
  - Ryujinx doesn't allow reading `cntvct_el0`; use `cntpct_el0` (`nn::os::GetSystemTick` does).
  - No Origin header is sent, so the server treats the Switch like desktop TF.EX.

**Not yet answered: the same on real hardware.** That is what 7a and 7b are for: Ryujinx forwards
to the Mac's network stack, so it says nothing about the console's Wi-Fi, DNS, certificate store
or clock. Steps are in [`net-probe/README.md`](net-probe/README.md).

### 6. What TF.EX's checksum covers: the serialized state (desk check, 2026-10-14)

From `ggrs-ffi` (`core/src/neplay.rs`, the pinned commit the browser builds): when GGRS asks the
game to save a frame, the checksum is `fletcher16(bincode(game_state.data()))`, where
`game_state.data()` is TF.EX's serialized state buffer (the C# side's MessagePack plus LZ4 bytes).
Desync detection is on, every `120 × fps/60` ticks.

- **What crosses the wire:** only GGRS's own protocol (inputs, acks, quality reports, the
  checksums), on one data channel. Nothing else rides on it.
- **What a checksum mismatch does:** GGRS reports a mismatch, and TF.EX answers "Cross-peer desync
  detected", ends the match and shows "DESYNC DETECTED - match ended"
  (`NetplayManager.cs`).
- **So** a Switch peer can't pass the check without serializing the state byte for byte like
  TF.EX, which is a stretch-target job. For the minimum, the browser build's `ggrs-ffi.patch`
  has to change the checksum: turn desync detection off, or checksum a small gameplay digest
  that the Switch also computes. Off is the cheap start; it also means a real desync goes
  unnoticed.
- **Input format** (for any GGRS port): 14 fields, 11 `i32`s then two `Vector2f`s (aim and
  right-stick aim) and a disconnected flag, serialized with bincode.
- **Size of the job if GGRS is ported to C++:** `ggrs` is about 6,300 lines of Rust. The wire
  side (`network/protocol.rs`, `messages.rs`, `compression.rs`) is about 1,400. Its dependencies
  are `serde`, `bincode`, `rand`, `bitfield-rle`, `parking_lot` and `instant`, all of which need
  `std`, so a Rust build for the Switch needs a Horizon `std` (test 8).

### 4. `System.Random`: the Switch's is a different algorithm (static analysis, 2026-10-14)

PLAN step 2d assumed that the Switch's `System.Random` is Mono's, because of a seed-constant
claim. **That was wrong.** From the `main` NSO:

- The class has the fields `x`, `y`, `z`, `c` and a method `JKiss` in its metadata. The core is
  at `+0x271ef0`: it is **JKISS32** (Marsaglia), with the constants 314527869, 1234567,
  4294584393, 987654321 and 6543217 in the code.
- `.NET`'s seed constant (161803398, from the Knuth subtractive generator) appears **nowhere** in
  the binary, as a literal or as an instruction pair.
- Derived methods differ too: `Next(min, max)` (`+0x271f40`) is `x % range + min`, where .NET
  multiplies a double.

So `new Random(seed)` gives different sequences on the Switch and in the browser. In the PC
game that matters here:
- `Calc.Random = new Random(Level.LoadSeed)` (`LevelLoaderXML.cs:62`) and
  `Calc.PushRandom(seed)` (`LevelTiles`, `LevelBGTiles`, `LevelLoaderXML`): seeded, so the
  results are meant to be the same everywhere.
- TF.EX's own gameplay path, `CalcPushRandomPatch`: `PushRandom(Calc.Random.Next())` while its
  gameplay generator is active, which makes a seeded `System.Random` run inside gameplay (for
  example `MatchVariants.Randomize`).
- Unseeded `new Random()` in gameplay (`Spikeball`, `TreasureSpawner`, spawn shuffles in
  `RoundLogic`) is replaced by TF.EX with its xoshiro256** `DeterministicRandom`, which
  overrides every `Random` method with its own arithmetic. That part only needs the same
  algorithm and the same patch sites on the Switch.

**What it means:** the Switch needs a native `System.Random` that behaves like .NET's, for
seeded instances at least. The bodies are findable (`+0x271d00` ctor(int), `+0x271e30` ctor(),
`+0x271ef0` JKiss, `+0x271f40` Next(min,max); the others are beside them), but .NET's state is
228 bytes against JKISS's 16, so the state lives outside the object (a side table). It only
needs to survive inside one `PushRandom` bracket, which is within a frame, so rollback doesn't
have to snapshot it. Gameplay randomness (xoshiro) is separate and does need to be snapshotted.

A dynamic check (1,000 values for several seeds, against desktop .NET) belongs after that
replacement exists; run it in Ryujinx.


**Fix built and verified (2026-10-14).**
- **`dnrandom.hpp`** is .NET's seeded generator (Knuth's subtractive method, "Net5CompatSeedImpl") as plain
  C++. Checked against the **real .NET runtime** (10 seeds including `int.MinValue`, `int.MaxValue` and
  −2147483647; `Next()`, `Next(max)`, `Next(min,max)` including the large-range path, `NextDouble`,
  `NextBytes`, and the 2000th value): `hw-probe/tests/dnrandom-test.cpp` against
  `dnrandom-ref.txt`, all 120 lines identical. (The first version differed for the extreme seeds:
  C# int arithmetic wraps and signed overflow in C++ doesn't; every overflowing step now wraps
  explicitly.)
- **`rnd.cpp`** replaces all the method bodies of the Switch's `System.Random` (`.ctor()`,
  `.ctor(int)`, `Next()`, `Next(max)`, `Next(min,max)`, `NextBytes`, `Sample`; `NextDouble` calls
  `Sample`). The 228-byte state lives in a 1,024-slot table found through an index kept in the object;
  invalid arguments still go to the original body, which throws. Each hook checks the first
  instruction of its target and refuses to install on a mismatch.
- **Verified in Ryujinx:**
  - a boot self-test through the game's own function addresses (a fake object, seed 42) gives
    1434747710, 302596119, `Next(100)` 12, `Next(5,10)` 7 and the right `Sample`, equal to .NET;
  - **2,825 of the game's own calls on 45 `Random` instances** (menus and Trials: 1,694 `Sample`, 1,091
    `Next(max)`, 40 `NextBytes`) replayed by `tools/check-random-trace.py`: **all match**;
  - the scripted Trials run is unaffected (trial log still identical to both result files).
- **Not exercised by the game's own traffic yet:** `Next()` and `Next(min,max)` (covered by the
  self-test only), the large-range path, and a full `NextBytes` array (only its first byte is traced).
- **To do for rollback:** the slot table must go into the snapshot, or an object restored to an older
  state would find its stream further along. (Gameplay will use the separate xoshiro generator, step 2.)
## Console probe: built and validated in Ryujinx (2026-10-14)

Everything in the hardware checklist below is now one module, [`hw-probe/`](hw-probe/README.md):
install it for run A (network and the scripted Trials run) and run B (versus play with the rollback
drill and the changed-page tracker), then read the logs with `tools/hw-check.py`. **The console visit
is the last step** (user, 2026-10-14); nothing has to change in the probe before it.

Validated in Ryujinx:
- It builds as one 387 KB module (game hooks, network thread, Rust GGRS, shared log) and every
  unresolved libc symbol is one the game exports.
- Scripted Trials run: FPCR 0, the five math hashes as before, and the 700-frame log it writes to
  `trial-log.txt` is **identical** to both `result-switch-700.txt` and `result-browser-700.txt`.
- The network thread runs beside the game hooks (the GGRS stage passes while the game plays).
- The drill (save, 4 or 12 extra updates, restore) ran every 120 frames for 2,400 frames with no
  crash and no refused restore, and the game carried on. The changed-page tracker ran too.
- The installer (`make-sd.sh install/restore`, tested against a stand-in folder) and the checker work.

What Ryujinx can already say, and what it can't:
- **Timing: nothing.** An update costs 0.16 to 0.4 ms when the emulated thread gets the CPU and 4 to
  14 ms when it doesn't, so p90 and p99 are scheduler noise. The uncontended 0.16 to 0.4 ms (an
  M-series core, a one-player Trials level) is in line with the browser's measured 0.26 ms per tick
  (docs/MULTIPLAYER.md of the browser repo), so a Switch core being several times slower is the
  open question, not whether the simulation is cheap.
- **Changed pages (test 2): the set doesn't settle quickly.** About 60 pages (240 KB) change between
  scans 30 frames apart, but the ever-changed set grew 339, 570, 802, 1,065 pages over 2,400 frames
  (+230 to +260 per 600 frames): the allocator keeps touching new pages. Copying the whole set is
  cheap (0.1 to 0.4 ms), but a fixed "hot set" won't do on its own; it needs a dirty-page
  mechanism or periodic re-learning. A full fingerprint scan of the 16,816 pages takes about 20 ms on
  the Mac, so detecting changes by scanning is out. Longer play and the console will show whether
  the growth levels off.

## Hardware checklist (to run once, last)

Everything that can only be answered on a console, collected so the SD-card session happens once.
Prerequisites: the console online (Wi-Fi or dock LAN) before the game starts; one native mod at a
time in `exefs/`; a probe build that logs to `sdmc:/xplay/` (the network probe already does).

| Check | From | What to read off |
|---|---|---|
| Network on hardware: DNS, `nifm::IsNetworkAvailable`, TLS with the real certificate store and clock, TCP to the Mac | 7a, 7b (done in Ryujinx) | `net-log.txt` from `net-probe` |
| Update cost per tick, 4-player versus, docked and handheld | 1 | p50/p99/max of the update call |
| Rollback window cost: 4 and 12 extra updates plus save and restore | 1 | frame time |
| Changed-page set over a whole match, and its copy time | 2 | set size over time, copy ms |
| FPCR value, and the 700-frame Trials replay against `result-switch-700.txt` | 3 | FPCR bits, per-frame diff |
| Versus determinism against the browser, repeated on hardware | 5 | per-frame diff |
| Relay latency and stalls to a browser through the server: the probe's `relay` stage over Wi-Fi and docked, against the Cloudflare relay | 7c | p99, longest gap (and the Node twin from the Mac for comparison) |
| The GGRS spike (`ggrs 600 0/2/6` in `net.txt`): no crash, same numbers as the Mac | 8 | `net-log.txt`; also the time of one `advance_frame` |

Add to this list as new hardware-only questions come up.

## Test 5 — first results (emulator, 2026-10-14)

Switch (Ryujinx, native LMS 2-player match, seed 42, TF.EX's timing overrides and script) vs real TF.EX in the browser
(`test-scripts/versus/browser-reference-log.txt`, compared with `tools/compare-vs.py`):

- Ticks -265..2153 (the whole of round 0, including the kill, the end counter and EndRound at T=2033) are bit-identical:
  RNG fold, both archers' position and speed.
- Round 1 starts identically (level 05, spawns, RNG c1276bbf) but its intro is one tick longer on the Switch:
  `Coroutine.OneFrameDelay` (TF.EX sets it true) is not set yet; its static's address is not found.
- Bugs found on the way: the FFA spawn function is +0xb38950 (not +0xb39070, which is Teams); forcing MenuConfirm before the
  scene change made idle rollcall seats join (4 players, no round end); Level.ReplayRecorder is at +0x70.
- Still to do: OneFrameDelay, the rest of the whitelist, then compare round 1 and the match end (stop the run before the
  match ends: a finished match writes the save data).


## Test 5 — PASS in the emulator for the scripted 2-player match (2026-10-14, later)

`tools/run-vs.sh` (default hw lines `vsstop 4500`, `vsshift 2155 1`) against the browser reference
(`test-scripts/versus/browser-reference-log.txt`, real TF.EX 0.19.2, 60 ticks/s, seed 42): the Switch build in Ryujinx is **bit-identical
for T=-265..4532**: the whole of round 0, the whole of round 1 including the miasma, the final kill with its slow motion, the match end;
RNG fold, both archers' position and speed every tick (first tick of each round excepted: the new Player objects are not registered yet).
After T=4533 the Switch has already started a new session (results screens auto-confirm) while the browser sits on its results screen.

What it took beyond round 0: the FFA spawn hook at the right address; no extra archers (forced MenuConfirm only once the level runs);
`Arrow.EnforceLimit` off; TF.EX's TimeMult formula *with* Engine.TimeRate; `OrbLogic.DoSlowMoKill` patched 0.25 -> 0.5 (a Switch 1.4.1.6 vs PC
1.3.3.3 difference); the TF.State miasma port (`miasma_seq.hpp`, hooks on `Miasma.Update` found from a live instance, `Miasma.Dissipate`
+0xacb7f0, the Miasma's coroutine skipped); a bigger hook pool (`setting.hpp` JitSize 0x6000).

The `vsshift 2155 1` is explained: TF.EX's `SkipLevelLoaderIfNeeded` (`TF.EX.Patchs/Scene/LevelPatch.cs:110-146, 251-276`) runs the next round's
loader to completion and the new Level's first `Update()` **nested inside the previous Level.Update tail, at the same GGRS frame**, so from
round 1 on the browser's tick number is the Level.Update count minus 1 (`T=2153` is never logged). The Switch's round-1 intro length (167
Level.Update calls) is right. A real Switch frame driver must do the same nesting so that its GGRS frame numbers and FrameCounter agree.
(Agent evidence: `test-scripts/versus/browser-distance-*.txt`, `browser-60hz/xplay-versus-distance.diff`: sync-test distance 0-7 and no-restore
runs all give the same numbers, so it is not a restore artefact.)

Still not covered by this test: modes other than LMS, Orb/Spikeball/LavaControl levels, unlock neutralisation, Cataclysm; and everything
needing hardware (see the checklist).

## Test 5b — the Switch plays the scripted match over the network (emulator + local relay, 2026-10-14)

Switch build in Ryujinx (seat 0, script0) + `ggrs-switch/target/release/peer` on the Mac (seat 1, script1, same lockstep GGRS session code),
GGRS packets as WebSocket binary frames through `tools/relay-server.mjs --port 8811`, input delay 4 on both. `tools/run-vs.sh -b -s 400
"np 1" "npseat 0" "npdelay 4" "vsstop 4500"` (net.txt: `np 127.0.0.1 8811 0 npt1 -`; peer: `peer 127.0.0.1 8811 npt1 1
test-scripts/versus/script1.txt 4 5400`):

- GGRS synchronized (5 round trips), the Switch advanced only on confirmed frames (no rollback, no saves), the frame driver ran the
  next round's loader and first update nested like TF.EX, and the per-tick log is **bit-identical to the real-TF.EX browser reference for
  T=-265..4233 (0 differing ticks of 4497)** with no `vsshift`: two rounds, the miasma, the final kill.
- Script mapping used by both peers: the input added at GGRS frame F (0-based, before the advance) is the script step at
  T = F + delay + 1 - 266, i.e. it is applied by tick T.
- The Mac peer plays seat 1 only through the same FFI the Switch uses, so this proves the Switch side (transport, session, driver),
  not yet compatibility with TF.EX's own `ggrs_ffi`: that is the browser agent's job (BROWSER-RELAY.md).
