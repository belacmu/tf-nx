#!/bin/bash
# notif-test.sh [-f] [-s secs] [hw.txt lines...]: a menu run in Ryujinx (a normal window behind the front app: a hidden one draws nothing
# to capture) with screenshots of TF.EX's Notification (tfex/Notification.cpp, docs/ws-tfex-subclass.md).
#   default: hw.txt "gennotiftest 200" (the generated Notification: a test toast on the main menu 200 frames in; again every 420 frames with the other text)
#   -f:      the real create-failed path: hw.txt "notif 1", "lobbyui 1" (NETPLAY -> LOBBIES -> START -> START) with a server that refuses
#            (netplay.txt server ws://127.0.0.1:9), so MatchmakingService's CreateLobby fails and MainMenuPatch calls Notification.Create
# Shots: $out/notif-<n>.png at 0.3, 1, 2.5 and 4.5 s after the toast was made (the [notif] / [tfex] log line). XPLAY_RJSLOT as the other
# scripts (tools/rj-slot.sh). Needs the screen-recording permission (screencapture -l).
set -u
here=$(cd "$(dirname "$0")/../.." && pwd); cd "$here"
out=${XPLAY_OUT:-/tmp/xplay-runs-$(printf '%s' "$here" | shasum | cut -c1-8)${XPLAY_RJSLOT:+-$XPLAY_RJSLOT}}; mkdir -p "$out"
fail=; secs=150
while getopts "fs:" o; do case $o in f) fail=1;; s) secs=$OPTARG;; esac; done; shift $((OPTIND-1))
if [ $# -eq 0 ]; then if [ -n "$fail" ]; then set -- "notif 1" "lobbyui 1"; else set -- "gennotiftest 200"; fi; fi
source tools/rj-slot.sh; sd="$RJ/sdcard/xplay"
source tools/ryujinx-lock.sh
for f in hw.txt script.txt net.txt netplay.txt; do cp "$sd/$f" "$out/nt-$f.bak" 2>/dev/null || : > "$out/nt-$f.bak"; done
trap 'for f in hw.txt script.txt net.txt netplay.txt; do cp "$out/nt-$f.bak" "$sd/$f"; done; ryujinx_unlock' EXIT
printf '%s\n' "$@" > "$sd/hw.txt"; : > "$sd/script.txt"; : > "$sd/net.txt"
[ -n "$fail" ] && printf 'server ws://127.0.0.1:9\nname RYUJINX\ninputdelay 2\nrtcbind 127.0.0.1\nstun off\n' > "$sd/netplay.txt"
tools/rebaseline-save.sh >/dev/null || exit 1
wb=${TMPDIR:-/tmp}/xplay-winid
[ "$wb" -nt "$here/tools/winid.swift" ] || swiftc -O -o "$wb" "$here/tools/winid.swift"
rm -f "$out"/notif-*.png "$out/nt-stop"
XPLAY_VISIBLE=1 XPLAY_STOPFILE=$out/nt-stop XPLAY_UNTIL="notif-test: never" XPLAY_EXLAUNCH="$here/hw-probe/exlaunch" XPLAY_MOD=hw-probe XPLAY_TAG=notif \
  tools/run-probe.sh "$secs" > "$out/nt-run.out" 2>&1 &
rp=$!
pid=; log=
for _ in $(seq 1 120); do
  pid=$(pgrep -n -x "$RJ_NAME"); [ -n "$pid" ] && log=$(lsof -p "$pid" -Fn 2>/dev/null | sed -n 's/^n\(.*\/Ryujinx_[^/]*\.log\)$/\1/p' | head -1)
  [ -n "$log" ] && break; sleep 1
done
shot() { w=$("$wb" "$(pgrep -n -x "$RJ_NAME")"); screencapture -x -o -l "$w" "$out/$1" && echo "notif-test: $out/$1"; }
want='\[notif\] test: created'; [ -n "$fail" ] && want='creating the lobby failed'; want=${NT_WANT:-$want}   # NT_WANT: another log pattern to shoot at
ok=
for _ in $(seq 1 $((secs - 10))); do
  kill -0 $rp 2>/dev/null || break
  if [ -n "$log" ] && /usr/bin/grep -aqE "$want" "$log"; then ok=1; break; fi
  sleep 1
done
if [ -n "$ok" ]; then
  sleep 0.3; shot notif-1.png; sleep 0.7; shot notif-2.png; sleep 1.5; shot notif-3.png; sleep 2; shot notif-4.png
  [ -z "$fail" ] && { sleep 3; shot notif-5.png; }
else echo "notif-test: no toast made (nt-run.out)"; fi
echo "${ok:+shots taken}${ok:-no toast}" > "$out/nt-stop"
wait $rp
[ -n "$log" ] && /usr/bin/grep -a "\[notif\]\|\[tfex\]\|\[ui\]\|Unhandled\|Exception" "$log" | sed 's/.*OutputDebugString: //' | tail -40
tail -3 "$out/nt-run.out"
