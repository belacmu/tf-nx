# Console session checklist (netplay + rollback on real hardware)

One sitting, four steps, no hand edits: each test is one `hw-probe/make-sd.sh install` variant, the Mac side is one
`tools/hw-peer.sh` command, and the console's logs come back to the Mac's dev server (`tools/devserver.py`). Everything below runs
from the switch-online checkout (master). `<SD>` = the card's mount point on the Mac (e.g. `/Volumes/SWITCH SD`), quoted.
`H` = `<your-server>`.

## Before the session (Mac)
- [ ] `git pull` on master; `tools/build-probe.sh` (the module lands in `hw-probe/exlaunch/deploy/`).
- [ ] The browser harness exists and is built: `~/Code/Towerfall Browser/.claude/worktrees/switch-harness` (branch `switch-harness`
      = the browser repo's main + a local debug commit; `tools/build.sh` there). Quick check: `tools/hw-peer.sh xnet xnet-check`
      joins the relay ("relay: joined ... other peer 3333...") within ~20 s; Ctrl-C.
- [ ] The PC game files the browser plays from exist (`~/Code/Towerfall Browser/gamefiles/...`, else the Steam install):
      `make-sd.sh install` copies their **versus levels** onto the card (it prints `pc-levels: 141 PC versus levels -> ...`). The
      Switch's own versus levels differ from the PC's in 40 of 141 levels (chests, spawners, orbs, jump pads, chain lengths), and a
      round on one of those desyncs (found 2026-10-15: seed 109144726 -> round 1 on Sacred Ground 06). Not verified in Ryujinx (its
      GPU crashes): the lobby test (3) is the first proof; a desync that starts exactly at a round change points here.
- [ ] `~/.config/towerfall-tfex.env` has `STATUS_KEY` (for the reports).
- [ ] Pick a room name for the session: `xnet-<something unique>` (below: `xnet-hw1`, `xnet-hw2`).
- [ ] The console is online (Wi-Fi or dock LAN) and Atmosphere boots with the card.

Keep `python3 tools/devserver.py` running the whole time (see "Dev updates" below). The console uploads to `.devserve/results/`:
the previous session's log at every boot (`prev-hw-log.txt`), the newest Atmosphere crash report for TowerFall (`crash-<name>`), the
live log every 45 s in a lobby or a match (`live-hw-log.txt`) and the test logs at vsstop. Match reports are TF.EX's own
(MatchReport, sent to the TF.EX server as on PC); our earlier `/report` telemetry is retired (2026-10-20).

## 1. xnet vs the browser, rollback on, sound on, scripted (make-sd variant N)
1. Card in the Mac: `hw-probe/make-sd.sh install "<SD>" N H xnet-hw1`
   (backs up the current exefs, moves the mod's romfs aside, writes hw.txt = vstart, rngcapture, np, nppred 8, rb 1, rbprehot 1,
   vsstop 4380; sound is on by default). Eject, card into the console.
2. Mac: `tools/hw-peer.sh xnet xnet-hw1` (browser = seat 1, script1, seed 42; waits for the Switch).
3. Console: boot, start TowerFall, **don't touch the controller**: the menu script starts a versus match, seat 0 plays the
   reference script against the browser. About 75 s of match; at tick 4380 the game **freezes on purpose** (`vsstop`).
4. Watch in the reports: `boot` (build, previous log), log lines `[rb] undo log ready`, `[rb] compare bench ...` (memcmp / word loop /
   workers MB/s on the console), `[rb] 2 compare worker thread(s) started`, `netplay_synchronized`, `round_end`, then `vsstop` with
   the `[rb] timing ms p50/p99/max: EndTick ... PendingScan ... restore ...` and `[rb] rollbacks ...` lines.
   Stats: `frameMsP50x100/P90x100/P99x100` (frame period), `updMsP50x100` (the game's update), `rbEndTickP50x100/P99x100`,
   `rbEndTickResimP50x100`, `rbPendingScanP50x100/P99x100`, `rbRestoreP50x100`, `rbComparedP50`, `rbLoads`, `rbMaxDepth`,
   `pingMs`, `npEvents`.
5. The Mac peer ends by itself after the match ("page done: ~4380 ticks"). Quit the game on the console.
- **Pass:** match to `vsstop`; no crash report at the next boot; `tools/hw-peer.sh compare` (after step 4 of section 4, or now
  with the card) shows only T 2153 differing (the known round-change logging artefact) or nothing; no `netplay_desync_detected`.
- **Record:** the `[rb] timing` line and the stats above (the console's real costs: the decision on write tracking / budgets).
- **Fail signs:** a freeze before 4380 (look at the last report's log), `rollback_failed`, a crash report.

## 2. The same with write tracking (make-sd variant W = N + `rbwrite 1`)
1. Card in the Mac: `hw-probe/make-sd.sh install "<SD>" W H xnet-hw2` (keeps run 1's logs in `sd:/xplay/runs/<time>-runN/`).
2. Mac: `tools/hw-peer.sh xnet xnet-hw2`.
3. Console: boot, TowerFall, hands off, as in 1.
4. Watch: right after `[rb] undo log ready`: `[wt] self-test: page ... is read-only; writing to it` and then
   `[wt] self-test: handler fired 1 time(s), page perm now 0x3, content kept 1: write tracking WORKS`, `[wt] nn::fs::ReadFile hooks:
   on, on`, `[rb] write tracking (live): N heap sections, M pages tracked`. Stats: `rbWriteMode` 1, `rbFaultsP50/P99/Max` (faults
   per tick), and the timing keys of 1 to compare.
- **If the console stops right after "writing to it"** (no further reports): the handler can't resume the faulting instruction (or
  the exception info layout differs). Note it, power-cycle; the next boot's report carries the log tail and the crash report. Go on
  with 3 (it reinstalls without `rbwrite`).
- **If the self-test says "does NOT work"**: the run continues on compares (like 1); note it.
- **Pass:** self-test WORKS, match to `vsstop`, logs identical as in 1, `[rb] write tracking OFF` never logged.
- **Record:** EndTick / PendingScan p50/p99 against run 1, faults per tick (many hundreds per tick -> the hybrid in docs/ws-perf.md).

## 3. A lobby the Switch hosts, real controller, rollback on (make-sd variant L)
1. Card in the Mac: `hw-probe/make-sd.sh install "<SD>" L H` (hw.txt: rngcapture 1, lobbyhost 0, lobbyui 0, lobbyscript 0 = the
   real controller, npdelay 2, lobbypred 8 = rollback; netplay.txt -> the deployed server). Card into the console.
2. Console: boot, TowerFall, main menu -> **NETPLAY** (the lobby is created on the server: report `lobby_created`), **A** to ready
   (seat 0).
3. Mac: `tools/hw-peer.sh lobby`; when it says "page armed", press Enter: it waits 20 s, then LOBBIES -> joins the first lobby ->
   ready (screenshot in the printed folder: `hw-peer.png`). If it didn't join, drive it with the printed `curl` keys.
   (Or a person on the browser instead: `https://belacmu.github.io/towerfall-browser/?tfexserver=wss://H` -> ONLINE -> LOBBIES ->
   join; that browser's log isn't captured, so no per-tick comparison.)
4. Console: when the ready banner shows, start (**+** or **A**) and play the match with the controller against the browser's
   scripted archer. After the results: back to the lobby screen (`lobby_match_finished`); **B** leaves to the main menu
   (`lobby_left`). Ctrl-C the Mac peer.
- **Watch:** `lobby_created`, `lobby_start`, `lobby_match_start`, `netplay_synchronized`, `round_end`s, `lobby_match_finished`;
  stats as in 1 (`rbLoads`, timing keys, `pingMs`).
- **Pass:** the match is played to its end with the real controller, the archers move the same on both screens, back to the
  lobby and to the main menu; no desync event, no crash; the comparison of the page's log with the Switch's (`tools/hw-peer.sh
  compare <copy>/xplay <the kept hw-browser-lobby-*.txt>`) shows no differing ticks except round-change frames.
- **Record:** whether the controller felt right (input delay 2 + rollback), any stutter.

## 4. Restore and collect
1. Card in the Mac. First copy everything off it:
   `mkdir -p ~/hw-session && cp -R "<SD>/xplay" ~/hw-session/ && cp -R "<SD>/atmosphere/crash_reports" ~/hw-session/ 2>/dev/null`
   (`xplay/` = the last run's logs (run 3), `xplay/runs/<time>-runN/` and `<time>-runW/` = runs 1 and 2: each with `hw-log.txt`,
   `vs-log-3m.txt` and that run's `hw.txt`).
2. `hw-probe/make-sd.sh restore "<SD>"` (combined-mod's exefs and romfs back; `sd:/xplay` stays).
3. Card into the console, boot TowerFall once: the usual modded game, no netplay.
4. Mac: the comparisons: `tools/hw-peer.sh compare ~/hw-session/xplay/runs/<...>-runN <the kept hw-browser-xnet-hw1-*.txt>`, the
   same for runW / xnet-hw2, and `tools/hw-peer.sh compare ~/hw-session/xplay <hw-browser-lobby-*.txt>` for the lobby; the reports
   are in `~/hw-session/reports` (`--save`); the Mac peer's logs in the folder `hw-peer.sh` printed.
5. Put the numbers into ROLLBACK.md / docs/ws-perf.md (console columns).

## Files this kit uses
- `hw-probe/make-sd.sh` (install A|B|N|W|P|L, restore), `hw-probe/sd-template/run{N,P,L}/xplay/` (W = N + `rbwrite 1`).
- `tools/hw-peer.sh` (xnet, lobby, compare), `tools/devserver.py`, `tools/compare-vs.py`.
- Dry-run of every variant against a fake card folder (2026-10-15): install N / W / L, a refused room name changes nothing,
  logs of the previous run kept under `xplay/runs/`, restore puts the exefs and romfs back.

## Fast loop over Wi-Fi (no SD card swaps)

The probe can keep itself up to date from the Mac (hw-probe/exlaunch/source/program/devupdate.cpp, tools/devserver.py):

1. Once, with the card in the Mac: `DEVSERVER="<Mac IP> 8765" hw-probe/make-sd.sh install <SD mount> <run> ...` (writes
   sd:/xplay/devserver.txt). The Mac's IP: `ipconfig getifaddr en0`.
2. On the Mac: `python3 tools/devserver.py` (port 8765). It serves what tools/devserve.list names: the probe build
   (hw-probe/exlaunch/deploy) and test files under `.devserve/` (hw.txt, netplay.txt, vs0/vs1.txt). The manifest is computed on each
   request, so `tools/build-probe.sh` (or editing `.devserve/hw.txt`) is all it takes to publish.
3. On the console: launch TowerFall. Every 4 s it compares its files with the manifest (size + CRC-32), downloads what differs to
   `<file>.dl`, checks it and swaps it in; the main menu shows "DEV UPDATE: DOWNLOADING n/m", then "DEV UPDATE READY (n) - RESTART THE
   GAME". Restart the game to run the new build / settings.
4. Results: at vsstop the probe uploads vs-log.txt and hw-log.txt; they land in `.devserve/results/<time>-<name>`.

Recovery from a build that crashes at launch: hold L while starting TowerFall (Atmosphère loads it without mods), then put the previous
build back with an FTP app (or the card). Without devserver.txt nothing of this runs.
