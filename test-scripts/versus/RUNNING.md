# Running test 5 (scripted 2-player versus, Switch in Ryujinx vs real TF.EX in the browser)

Reference: `browser-reference-log.txt` (5,000 ticks, from `TEST5-MATCH-SETUP.md`'s harness; scripts `script0.txt`, `script1.txt`).

1. Build the probe in `~/Code/tfx-hw-exlaunch` (`DEVKITPRO=/opt/devkitpro ./exlaunch.sh build`; sources are kept in
   `hw-probe/exlaunch-overlay/source/program/`).
2. SD card (`~/Library/Application Support/Ryujinx/sdcard/xplay/`):
   - `script.txt` = `test-scripts/menu-to-versus.txt` (joins seats 0 and 1 in the rollcall; the Switch's rollcall does not
     start a match by itself, so the probe starts it natively);
   - `vs0.txt`, `vs1.txt` = `script0.txt`, `script1.txt` here;
   - `hw.txt`: `vstart 1`, `rngseed 42`, `rngcapture 1`, and **`vsstop <tick>`** (see below).
3. `XPLAY_EXLAUNCH=~/Code/tfx-hw-exlaunch XPLAY_MOD=hw-probe XPLAY_TAG=vs tools/run-probe.sh <seconds>` (hidden window).
4. `tr -d '\000' < sdcard/xplay/vs-log-3m.txt | sed 's/ *$//' | grep '^\[vs\]' > /tmp/vs-sd.txt`, then
   `python3 tools/compare-vs.py test-scripts/versus/browser-reference-log.txt /tmp/vs-sd.txt`.

**Save data:** a match that reaches its end writes the game's save data (stats), which `run-probe.sh` treats as a failure.
The browser match ends at T=4016 (Level.Update tick 4282); always set `vsstop` below the Switch's own match end
(use 4250 at most) so the game thread is parked before that. The log is flushed every 300 ticks and at round events.

Known differences still open: `Coroutine.OneFrameDelay` (round 1's intro is one tick longer), the remaining RNG whitelist hooks,
unlock neutralisation. The first tick of each round prints alive=0 on the Switch (the new Player objects aren't registered yet).


## One-command cycle (2026-10-14, later)

`tools/run-vs.sh [-b] [-s <seconds>] [hw.txt lines...]` does everything: (-b: build the probe), re-baselines the emulator save
(`tools/rebaseline-save.sh`; test runs may change the save), writes `hw.txt` (`vstart 1`, `rngseed 42`, `rngcapture 1` + your lines,
default `vsstop 5300`), runs hidden at full speed (~2.5 min for 5,300 ticks), extracts `sd:/xplay/vs-log-3m.txt` to `/tmp/vs-sd.txt`
and runs `tools/compare-vs.py`. Arrow lists: `python3 tools/compare-arrows.py test-scripts/versus/browser-reference-log-arrows.txt /tmp/vs-sd.txt`.

hw.txt options added: `vsstop <tick>`, `vsshift <fromT> <n>` (re-time scripts/labels/FrameCounter by n ticks from T>=fromT: round 1's intro
is one tick longer on the Switch than in the sync-test reference).

State of the comparison: with `vsshift 2155 1` the Switch matches the browser from T=-265 to T=4016 (players + RNG). The first real
divergence is the miasma (TF.State replaces its coroutine with `MiasmaSequenceController`; see MIASMA-PORT.md once written).
