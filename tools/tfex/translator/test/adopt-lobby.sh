#!/bin/bash
# adopt-lobby.sh [-w regex] [-d secs] [hw.txt lines...]: one lobby match against the browser (tools/lobby-auto.sh: the Switch hosts,
# the page joins through TF.EX's menus), Ryujinx visible; once the guest log matches -w, waits -d s, takes $out/adopt-<n>.png and ends
# the run. For docs/ws-tfex-adopt.md switch-overs. Default: a short match (lobbylength 0), digest on both sides, the Switch QUITs from
# the end-of-match menu 3 s after the results (lobbyvote 3: new MainMenu(NetplaySelect)); shots once the netplay menu is up.
set -u
here=$(cd "$(dirname "$0")/../../../.." && pwd); cd "$here"
out=${XPLAY_OUT:-/tmp/xplay-runs-$(printf '%s' "$here" | shasum | cut -c1-8)${XPLAY_RJSLOT:+-$XPLAY_RJSLOT}}; mkdir -p "$out"
want='\[menu\] state 62'; delay=3
while getopts "w:d:" o; do case $o in w) want=$OPTARG;; d) delay=$OPTARG;; esac; done; shift $((OPTIND-1))
[ $# -gt 0 ] || set -- "rngcapture 1" "lobbyhost 1" "lobbyscript 1" "npdelay 2" "lobbypred 8" "npdigest 1" "rbwrite 1" "lobbylength 0" "lobbyvote 3"
source tools/rj-slot.sh
wb=${TMPDIR:-/tmp}/xplay-winid
[ "$wb" -nt "$here/tools/winid.swift" ] || swiftc -O -o "$wb" "$here/tools/winid.swift"
rm -f "$out"/adopt-*.png "$out/lvb-phases.txt"
XPLAY_VISIBLE=1 LVB_UNTIL="adopt-lobby: never" LVB_XLOBBY_EXTRA="${LVB_XLOBBY_EXTRA:-digest=1}" tools/lobby-auto.sh "$@" > "$out/adopt-run.txt" 2>&1 &
la=$!
# this run's own Ryujinx log: only after lobby-vs-browser has the lock and removed the previous run's stop file
until [ -f "$out/lvb-phases.txt" ] || ! kill -0 $la 2>/dev/null; do sleep 2; done
log=; for _ in $(seq 1 120); do
  pid=$(pgrep -n -x "$RJ_NAME"); [ -n "$pid" ] && log=$(lsof -p "$pid" -Fn 2>/dev/null | sed -n 's/^n\(.*\/Ryujinx_[^/]*\.log\)$/\1/p' | head -1)
  [ -n "$log" ] && break; sleep 1
done
ok=
while kill -0 $la 2>/dev/null; do
  [ -n "$log" ] && /usr/bin/grep -aqE "$want" "$log" && { ok=1; break; }
  [ -f "$out/lvb-stop" ] && break
  sleep 0.5
done
if [ -n "$ok" ]; then
  sleep "$delay"
  for n in 1 2; do w=$("$wb" "$(pgrep -n -x "$RJ_NAME")"); screencapture -x -o -l "$w" "$out/adopt-$n.png" && echo "adopt-lobby: $out/adopt-$n.png"; sleep 1; done
  sleep 2; echo "adopt-lobby: shots taken" > "$out/lvb-stop"
else echo "adopt-lobby: '$want' never logged"; fi
wait $la
cat "$out/adopt-run.txt" | tail -25
[ -n "$log" ] && echo "log: $log"
