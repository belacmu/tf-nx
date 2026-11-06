#!/bin/bash
# testmode.sh [-s <seconds>] [tfextest args...]: TF.EX's test mode (its dev console's `test`: ggrs_ffi's sync test, no peers, no
# browser) in Ryujinx, hidden, at TF.EX's 240 Hz. Default test: LMS 0 1 42 2 (mode, start level, map, seed,
# check distance). Writes hw.txt = "tfextest ...", runs until the test match ends (or a guest exception / the time limit),
# keeps the Ryujinx log, and prints one line:
#   TESTMODE PASS|FAIL: <reason> | mismatches N | logs <dir>
# plus the session's timing summaries (per drawn frame: game ticks, the time split with rollback save / load / resim, the worst frame).
# PASS: the session started and ran (timing windows logged), no sync-test mismatch, no guest exception, for the run's length (the test match
# needn't end: seat 0 plays the vs script, the other seats stand; if it ends, its teardown is in the log too).
# TESTMODE_HW: more hw.txt lines (e.g. "statedump 1": every save's state, for tools/tfex/statediff.py on a mismatch).
# XPLAY_RJSLOT works as everywhere (a private Ryujinx); the run is hidden unless XPLAY_VISIBLE=1.
set -u
here=$(cd "$(dirname "$0")/.." && pwd); cd "$here"
secs=300
while getopts "s:" o; do case $o in s) secs=$OPTARG;; esac; done; shift $((OPTIND-1))
args=${*:-LMS 0 1 42 2}
out=${XPLAY_OUT:-/tmp/xplay-runs-$(printf '%s' "$here" | shasum | cut -c1-8)${XPLAY_RJSLOT:+-$XPLAY_RJSLOT}}; mkdir -p "$out"
keep="$out/runs/testmode-$(date +%H%M%S)"; mkdir -p "$keep"
source tools/ryujinx-lock.sh
source tools/rj-slot.sh; sd="$RJ/sdcard/xplay"
printf '%s\n' "tfextest $args" > "$sd/hw.txt"
[ -n "${TESTMODE_HW:-}" ] && printf '%s\n' "$TESTMODE_HW" >> "$sd/hw.txt"   # TESTMODE_HW="statedump 1": more hw.txt lines (one per line)
tools/rebaseline-save.sh >/dev/null || exit 1
for i in 0 1 2 3; do cp test-scripts/versus/script$i.txt "$sd/vs$i.txt"; done   # seat 0 plays the vs script (BeginTestSession)
XPLAY_LOGKEEP="$keep/ryujinx.log" XPLAY_UNTIL='\[test\] the test match is over|Sync test desync detected|Session.CreateResults at frame [0-9]+ \(tick [0-9]+\): match results' \
  XPLAY_EXLAUNCH="$here/hw-probe/exlaunch" XPLAY_MOD=hw-probe XPLAY_TAG=test tools/run-probe.sh "$secs" > "$keep/run.out" 2>&1
log="$keep/ryujinx.log"
g() { grep -a "OutputDebugString" "$log" 2>/dev/null | sed 's/.*OutputDebugString: //' | grep -a "$@"; }
mism=$(g -c 'Sync test desync detected\|\[np\] AdvanceFrame: ' || true)
res() { echo "TESTMODE $1: $2 | mismatches ${mism:-0} | logs $keep"; g -E '\[hw\] timing|\[test\] sync-test mismatches' | tail -6 | cut -c1-400; [ "$1" = PASS ]; exit $?; }
grep -aq 'Unhandled exception' "$log" 2>/dev/null && res FAIL "the guest crashed ($(grep -a -m1 'Unhandled exception' "$log" | cut -c1-120))"
g -q "\[test\] TF.EX's test-mode session" || res FAIL "the test-mode session never started ($(grep -a 'run ended' "$keep/run.out" | tail -1))"
[ "${mism:-0}" -gt 0 ] && res FAIL "the sync test reported mismatches"
g -q '\[hw\] timing split' || res FAIL "no timing window was logged (the session didn't run long enough: $(grep -a 'run ended' "$keep/run.out" | tail -1))"
res PASS "the test session ran ($(g -c '\[hw\] timing split') timing windows$(g -q '\[test\] the test match is over' && echo ', to the match end'))"
