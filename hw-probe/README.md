# Console probe: every hardware-only check in one visit

One native module (the game takes one exefs mod at a time), built and validated in Ryujinx on
2026-10-14. The console visit is meant to be the **last** thing: run it when the emulator and desk
work is done. Nothing here needs to change before then.

| Part | File | What it does |
|---|---|---|
| Game hooks | `xplay.cpp` | The math, rounding and fixed-timestep fixes; the scripted Trials run (`script.txt`) with its 700-frame log; timing of the game's update on every in-level frame; the **rollback drill** and the **changed-page tracker** (`hw.txt`) |
| Network thread | `net.cpp` | The stages in `net.txt`: TCP echo, TLS WebSocket, GGRS loopback, relay test (VIABILITY-TESTS 7a, 7b, 8, 7c) |
| Log | `hwlog.cpp` | `sd:/xplay/hw-log.txt`, written in the background by the network thread so the game thread never waits for the SD card |
| .NET `Random` | `rnd.cpp`, `dnrandom.hpp` | Replaces the Switch's `System.Random` (JKISS32) with .NET's seeded algorithm; boot self-test; traces the game's first calls for `tools/check-random-trace.py` |
| Gameplay RNG | `rnd.cpp` (+ `rnd.hpp`) | .NET's System.Random algorithm (the runtime's is JKISS32); calls on TF.State's DeterministicRandom (generated, with StateContext / RngService / CalcPatch) routed to its generated methods |
| DeterministicMath | `dm.cpp` | TFEX-DIFF (no IL transpilers): redirects `Math.Sin/Cos/Atan2/Pow` to TF.State's generated DeterministicMath (unit State_DeterministicMath); on-device self-test against the .NET reference |
| Libc shims | `shim.c` | What the Rust GGRS library needs that the game doesn't export |

The sources are in `exlaunch-overlay/` (its own checkout, `~/Code/tfx-hw-exlaunch`; build with
`DEVKITPRO=/opt/devkitpro ./exlaunch.sh build`, and `../ggrs-spike/build.sh` first if the Rust
changed). **While it is installed the game has no sound** (gameplay sounds are muted so the rollback
drill can't break the audio layer; see PLAN step 6).

## What it measures (and which VIABILITY-TESTS item that answers)

- **3. FPCR and math:** the FPCR value and the hashes of 100,000 `sin`, `cos`, `atan2`, `pow` calls,
  against what Ryujinx and the browser give. And the 700-frame scripted Trials run, against
  `test-scripts/results/`.
- **1. Update cost:** per in-level frame, the time of `TFGame.Update` (input and `Engine.Update`, no
  drawing) and the frame period, as p50/p90/p99/max per 600 frames.
- **1. Rollback cost, the drill:** every `drill <period>` in-level frames: save the game's memory
  (the GC heap, Boehm's block headers and the module's globals, about 67 MB), run `n` extra updates,
  restore, and log each part. The game carries on unchanged. `n` alternates between two values.
- **2. Changed pages:** every `hotset <every>` in-level frames, fingerprint all of that memory and
  track which pages ever change: how large the set a rollback would have to copy is, and whether it
  stops growing.
- **7a/7b:** TCP to the Mac, TLS and a WebSocket upgrade to the Cloudflare relay, on the console's
  Wi-Fi/dock, with its own DNS, certificate store and clock. **7c:** 60 packets/s through the relay
  for 2 minutes, with a peer echoing. **8:** the GGRS loopback on the console's CPU.

## The visit

Needs: the console online before the game starts; the Mac on the same network as the console (for the
echo server); the SD card readable on the Mac (hekate UMS, or removed). The relay Worker
(`../relay-worker`, `tf-relay-probe`) must still be deployed for the relay stage.

**Install and run A (network + scripted Trials), about 10 minutes**

1. On the Mac: `hw-probe/make-sd.sh install <SD mount> A $(ipconfig getifaddr en0)`. It backs up the
   current exefs (combined-mod's) to `<SD>/xplay-backup/exefs`, puts the probe in the exefs slot and
   writes this run's `xplay/net.txt` and `xplay/script.txt`.
2. Before booting the console, on the Mac:
   - `python3 tools/echo-server.py 9000` (allow incoming connections if macOS asks);
   - the peer for the relay test, in room `relaytest`: either
     `node tools/relay-peer.mjs wss://<relay-probe-worker> relaytest` or a browser
     tab at `https://<relay-probe-worker>/echo.html?room=relaytest`.
3. Boot the console and start TowerFall. The network stages start by themselves 15 s after boot and
   take about 4 minutes (the relay test is 2 of them). They run in the background while you play.
4. Go to **Trials, Sacred Ground I**, start it, and **don't touch the controller** for about 20 seconds:
   the script plays player 1 for 700 frames (it also runs the rollback test windows at frames 85,
   198 and so on, which time a full save and restore).
5. After about 5 minutes from boot, quit the game.

**Run B (update cost, drill, changed pages), about 10 minutes**

6. `hw-probe/make-sd.sh install <SD mount> B` (no network files, no script; `xplay/hw.txt` turns on a
   drill every 300 in-level frames and the changed-page tracker every 30).
7. Boot, and play **a versus match with as many archers as you can** (arrows, chests, a round end:
   the heaviest scenes) for 3 to 5 minutes. Docked and handheld if you can; note which. Every 5 s
   there is a short hitch (the drill); that's expected.
8. Quit.

**Collect**

9. `tools/hw-check.py <SD mount>/xplay` prints a summary of every item. The raw logs are
   `hw-log.txt` and `trial-log.txt`. Copy them somewhere before anything else.
10. `hw-probe/make-sd.sh restore <SD mount>` puts the original exefs back.

## How to read the result

- **FPCR 0 and all five hashes ok, trial-log IDENTICAL:** the console computes like Ryujinx and the
  browser (flush-to-zero off, same `sin`/`cos`/`atan2`, same rounding).
- **Budget, from the drill's per-update time `u`:**
  - 60 Hz: (1 + 8 resimulated) × `u` + snapshot + restore should be under about 14 ms.
  - 240 Hz: (4 + 12) × `u` the same, which needs `u` under about 0.85 ms before any snapshot cost.
  - The drill's save is a *full* copy (about 67 MB); a real design copies only changed pages, so
    read the save time as an upper bound. The changed-page set says whether that works.
- **Changed pages:** about 60 pages per scan in Ryujinx, but the ever-changed set kept growing there
  (see VIABILITY-TESTS, test 2). If it stops growing on the console, a hot-set snapshot works; if
  not, a dirty-page mechanism (or field snapshots) is needed.
- **Network:** `net-log` lines `7a PASS`, `7b PASS` (and whether name resolution worked), `7c`
  percentiles and gaps, `8 PASS`. 7c is the first number from the console's own Wi-Fi.
