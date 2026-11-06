#!/bin/bash
# run-vs.sh [-b] [-s <seconds>] [hw.txt lines...]: one test-5 cycle in Ryujinx (hidden), then the comparison.
#   -b   build the probe first (tools/build-probe.sh)      -s   run-probe seconds (default 300; the run ends early at vsstop)
#   -p "<delay> <frames> [predict]"   netplay: (re)start the scripted Mac peer (seat 1, room npt1) on the local relay :8811 first
# Always: re-baseline the save (the user OK'd save changes), write hw.txt = "vstart 1 / rngseed 42 / rngcapture 1" +
# the given lines (default: "vsstop 5300"), run, extract sd:/xplay/vs-log-3m.txt, run tools/compare-vs.py and print a summary.
set -u
here=$(cd "$(dirname "$0")/.." && pwd); cd "$here"
out=${XPLAY_OUT:-/tmp/xplay-runs-$(printf '%s' "$here" | shasum | cut -c1-8)${XPLAY_RJSLOT:+-$XPLAY_RJSLOT}}; mkdir -p "$out"   # this checkout's run outputs (space-free; other agents run the same scripts)
secs=300; build=0; peer=
while getopts "bs:p:" o; do case $o in b) build=1;; s) secs=$OPTARG;; p) peer=$OPTARG;; esac; done; shift $((OPTIND-1))
[ $build = 1 ] && { tools/build-probe.sh || exit 1; }
source tools/ryujinx-lock.sh
source tools/rj-slot.sh; sd="$RJ/sdcard/xplay"
{ echo "vstart 1"; echo "rngseed 42"; echo "rngcapture 1"; if [ $# -gt 0 ]; then printf '%s\n' "$@"; else echo "vsstop 4500"; echo "vsshift 2155 1"; fi; } > "$sd/hw.txt"
cp test-scripts/versus/script0.txt "$sd/vs0.txt"; cp test-scripts/versus/script1.txt "$sd/vs1.txt"; cp test-scripts/menu-to-versus.txt "$sd/script.txt"
tools/rebaseline-save.sh >/dev/null || exit 1
rm -f "$sd/vs-log-3m.txt"   # (a run that ends before the Switch writes its tick log compared the last run's)
if [ -n "$peer" ]; then
  pgrep -f "relay-server.mjs" >/dev/null || { echo "no relay on :8811 (node tools/relay-server.mjs --port 8811)" >&2; exit 1; }
  pkill -f "release/peer"; sleep 0.5
  ggrs-switch/target/release/peer 127.0.0.1 8811 npt1 1 test-scripts/versus/script1.txt $peer > $out/peer.log 2>&1 &
  sleep 1
fi
XPLAY_UNTIL="vsstop: parking" XPLAY_EXLAUNCH="$here/hw-probe/exlaunch" XPLAY_MOD=hw-probe XPLAY_TAG=vs tools/run-probe.sh "$secs" > $out/run-vs.out 2>&1
[ -f "$sd/vs-log-3m.txt" ] || { echo "run-vs: no Switch tick log from this run ($(grep -a 'run ended' $out/run-vs.out | tail -1))"; exit 1; }
tr -d '\000' < "$sd/vs-log-3m.txt" | sed 's/ *$//' | grep '^\[vs\]\|^\[cr\]' > $out/vs-sd.txt
echo "log lines: $(grep -c '^\[vs\] [0-9-]' $out/vs-sd.txt); events:"; grep "event" $out/vs-sd.txt | head -8
python3 tools/compare-vs.py test-scripts/versus/browser-reference-log.txt $out/vs-sd.txt | head -9 | cut -c1-230
