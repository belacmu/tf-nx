# Working on switch-online

## The goal and the rules for the code
- **Target: the latest TF.EX release** on both the Switch port (translated by tools/tfex; tools/tfex/update.sh for the diff between
  tags) and the browser port. The Switch mod is TF.EX's own code wherever possible. Bugs seen in play are noted (STATUS.md backlog,
  with evidence); they may just be TF.EX not fully implemented yet.
- **Switch over, don't just prepare:** a change that brings TF.EX's code ends with it ON by default and the hand code it replaces
  DELETED, checked by the emulator scenario of that flow. No generated code parked behind an off flag; a flag may stay only as a
  short-lived way back (`X 0`).
- **Reuse existing Switch solutions:** where the Switch really differs from the PC and a working solution exists (rollback, the
  native lobby-match start, the native transport, wide mode, the keyboard...), TF.EX's code takes over the logic and the Switch
  solution stays underneath (marked S / TFEX-DIFF).
- **Custom rules are the exception:** every TFEX-DIFF and Switch-only behaviour must be truly needed (rollback, the console's
  own systems), with a one-line reason. A workaround for a crash is not a reason: fix the crash and do what TF.EX does.
- **No patchwork fixes:** a bug in a flow TF.EX owns is fixed by adopting TF.EX's code for it, not by a hand patch on the Switch's
  stand-in. Infrastructure (logs, pools, harness) is ours to fix directly.
- **The console's logs are crucial:** the match log covers the whole session (boot, lobby, every match to its end). No lines that
  repeat without limit: log the first few, then a count or a periodic summary. A hardware checklist names the log lines that prove
  it worked. The log running full is a pools error.
- **Fixed-size pools** (threads, connection slots, hooks/trampolines, TLS contexts, GC roots, rings): log usage and headroom, and
  make running out a clear error, never a silent failure.
- **Progress is measured by TF.EX code running on the console**, not by the coverage percentage.

## Testing
- **Test sparingly:** deleting code nothing calls: build only. Switching a flow to TF.EX's code: ONE run of that flow's scenario.
  Run only the scenarios a change can affect; no reruns of passing scenarios after unrelated changes.
- **Test runs: `tools/smoke.sh <scenario>`** (the scenario list is at the top of the script). It prints one line,
  `SMOKE <scenario> PASS|FAIL: <reason> | shot <png> | logs <dir>`. PASS means: no guest exception, the flow's log lines seen, the
  GGRS checksum clean on every page, and for the one-match scenarios TF.State's saved bytes identical on both sides on every dumped frame after 0 (every match runs at
  TF.EX's 240 Hz, the Switch and the browser alike; vsstop counts 240 ticks a second). A desync
  FAIL names the first differing frame and field (both sides dump TF.State's state; tools/tfex/statediff.py; SMOKE_STATEDUMP=0 turns
  it off). Extra hw.txt
  lines go after the scenario (`tools/smoke.sh lobby "genpatch PlayerPatch"`; same key = replaced). `XPLAY_VISIBLE=0` keeps the
  Ryujinx window hidden. `LVB_SERVER=real` runs TF.EX's own matchmaking server (our fork, docs/SERVER-DIFF-2026-10-26.md) instead
  of the browser repo's local.mjs.
- An issue known to be hardware-only (seen on the console, not reproduced in one or two emulator runs) isn't chased in Ryujinx: add
  logging that settles it on the next console run.
- **Ryujinx** only runs through the scripts (`tools/smoke.sh`, `tools/run-vs.sh`, `tools/lobby-vs-browser.sh`; they take
  `/tmp/xplay-ryujinx.lock`, so runs queue). Never kill Ryujinx blindly (the user may be playing; the scripts end their own run),
  never run its binary with arguments. The emulator's save data may change
  (`tools/rebaseline-save.sh`). Don't edit a script while a run of it is going (bash reads it as it runs).
- **Private Ryujinx slots:** `XPLAY_RJSLOT=<name>` in front of those scripts runs a private copy beside the shared one, with no queue
  (tools/rj-slot.sh; make one once with `tools/rj-slot-setup.sh <name>`; existing: update, subclass, B, b2, lockstep, eight). It has its own
  data folder, lock, harness ports and out dir. A slot uses the checkout's `hw-probe/exlaunch/deploy`, so don't build while a run of
  this checkout may start. Don't start a run when the 1-minute load average is above ~20.
- A run dying in Apple's Metal shader compiler is a host bug: retry. It shows as `~/Library/Logs/DiagnosticReports/Ryujinx-*.ips`
  with AGXMetal frames, or as run-probe's "run ended: Ryujinx ... exited by itself" with a normal guest log.
- Headless Chrome slows Ryujinx: keep at most one browser page of your own running, and close it (`curl localhost:<driver port>/quit`).
- **A test that runs long is a wrong test** (2026-11-03): estimate a run's length before starting it; past ~10 minutes, make the flow
  end sooner instead of raising the harness's limits. Rollback matches with 6+ players run 10-80x slower than real time here, so
  never wait for a timer (the miasma: ~11000 ticks a round); force the event (wide8end: START WITH BOMB ARROWS, the Switch's players
  jump and shoot down, the round ends a second into play). Seat scripts only act after the round intro (players are frozen until
  script tick ~810 at 240 Hz).
- **More players, not more pages:** a page seats up to 3 (TF.EX's local players: `LVB_PAGEGUESTS=2`, virtual gamepads), so 8
  players are the Switch's 2 and 2 pages. Pages start from a template profile (patched game, tutorial seen; `LVB_FRESHPROFILE=1`
  for a fresh one) and join all at once.
- **Wait for the load in the command, don't check it once:** start a run behind a loop that waits for a 1-minute load under ~12 and
  a 5-minute one under ~15 (Microsoft Defender's scans take it past 40; pages then miss their boot limits).
- A scenario's `until` line goes in its `expect` list too, or a run that never got there passes.

## Building, generating, publishing
- **Build** the Switch module with `tools/build-probe.sh` (never `exlaunch.sh` in place: make breaks on the spaces in the path). Each
  checkout builds in its own mirror folder; output lands in `hw-probe/exlaunch/deploy/`.
- **Private settings stay out of the repo:** our server (TFNX_SERVER), the console's FTP_IP and RYU_PATH live in
  `~/.config/tfnx/local.mk` (config.mk includes it; without it a build uses TF.EX's official server). No home paths, LAN addresses
  or server hosts in tracked files: write `~/`, `<console-ip>`, `<your-server>`.
- **Regenerate gen/** after editing overrides.toml / translate.toml rules or the hand runtime: `tools/tfex/translate.sh all <tag>`,
  and commit gen/ (and tools/tfex/binding.json) with the rule change, build-checked. The hand runtime (tfexgen_services.*, code_rt,
  glue, msgpack) lives in `hw-probe/exlaunch/source/program/tfex/runtime/` and is compiled in place; gen/ holds only the
  translator's output. gen/ merges with merge=ours (.gitattributes): after a merge run `tools/tfex/check-gen.sh` and regenerate
  when it differs.
- **Hardware runs master.** Land work on master, then `../switch-online-hw/tools/publish-hw.sh` (master's copy): it builds master in
  `../switch-online-hw` and publishes it to `.devserve/channel/` (tools/devserver.py's channel). Never publish another checkout's
  build. **Boot it before publishing:** one Ryujinx launch of that build to the title screen (171f0de compiled but aborted at every
  console boot). A bad build: put the last good one back from its kept ELF (`elf2nso .devserve/elf/<id>.elf subsdk9`) into
  `.devserve/channel/`. Merges touching ggrs-switch need a fresh Rust library first (ggrs-switch/build.sh).
- **Crash reports:** symbolize with the ELF of that build: `.devserve/elf/<Module Id, lowercase>-<commit>.elf`.
- **GitHub (belacmu/tf-nx, private):** `tools/publish-github.sh` commits master's current tree as one snapshot on top of GitHub's
  history (this repo's own history keeps private details in old commits and never goes there). It refuses a tree with a home path, a
  server host, a work address or a game copy's file name. Running it pushes: the user's OK first.
- Pushing, deploying (Cloudflare, the site) and publishing to the console need the user's OK. The SD card and `hw-probe/make-sd.sh`
  only when the user asks.

## Where things are
Docs: README.md, STATUS.md (the overview and backlog), PLAN.md, docs/CORE-FLOW.md, ROLLBACK.md, LOBBY-PROTOCOL.md, TFEX-UI-SPEC.md,
SWITCH-UI-RE.md, BROWSER-RELAY.md, BROWSER-LOBBY-RELAY.md, GAMEPLAY-RNG-PORT.md, TFEX-LESSONS.md. Write findings into the repo
(the doc the work belongs in), not into Claude memory.
Switch code: `hw-probe/exlaunch/source/program/` (xplay.cpp = hooks, netplay gate, rollback glue, lobby match start, NETPLAY
button; rollback.cpp; net.cpp = sockets, WebRTC; tfex/ = TF.EX's code, generated (gen/) and hand-written; rnd.cpp = .NET Random +
TF.EX gameplay RNG).
RE: `tools/re/` (main.flat = the game's code, disasm.py, xref.py, metaresolve.py; verify offsets with tools/re-verify).
The lobby harness (tools/lobby-vs-browser.sh, lobby-auto.sh) picks its ports, page player and Chrome profile per checkout
(tools/harness-slot.sh). Browser harness (xnet, xlobby, local tfex-server): `~/Code/Towerfall Browser/.claude/worktrees/harness-main`
(see the hardware-test notes there); rebuild with `tools/build.sh` there.
TF.EX's matchmaking server, our fork: `~/Code/Towerfall Switch Mods/tf-ex-matchmaking-server`, branch `switch-online`.
Don't keep harnesses or tool inputs in /tmp: a restart wipes it.
