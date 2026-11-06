#!/bin/bash
# tools/re/browser-test.sh [hw.txt lines...]: tools/panel-check.sh with several PC lobbies listed (BT_HOSTS, default "wide narrow wide": a
# lobby-host.mjs per word, named PCHOST1..), for TF.EX's generated LobbyInfos/LobbyPanel (docs/ws-tfex-subclass.md). The auto-UI joins the
# first row it confirms (lobbyjoin 1). Copied from panel-check.sh (only the hosts differ).
# panel-check.sh's own notes:
# A local tfex-server and tools/lobby-host.mjs (a TF.EX-like host; default --wide: a WiderSet wide lobby, which a standard Switch refuses);
# the Switch (Ryujinx, a normal window behind the front app: a hidden one draws nothing to capture) walks NETPLAY -> STANDARD -> LOBBIES
# (hw.txt lobbyui 1, lobbyjoin 1), selects the first lobby and presses A. Shots: $out/panel-1.png just after A,
# $out/panel-2.png 6 s later. -n: a narrow host lobby (joinable) instead. XPLAY_RJSLOT (tools/rj-slot.sh) as the other scripts.
# Needs the screen-recording permission for the shell that runs it (screencapture -l).
set -u
here=$(cd "$(dirname "$0")/../.." && pwd); cd "$here"
out=${XPLAY_OUT:-/tmp/xplay-runs-$(printf '%s' "$here" | shasum | cut -c1-8)${XPLAY_RJSLOT:+-$XPLAY_RJSLOT}}; mkdir -p "$out"
WT=${BROWSER_LOBBY_WT:-$HOME/Code/Towerfall Browser/.claude/worktrees/test-page}
[ $# -gt 0 ] || set -- "lobbyui 1" "lobbyjoin 1"
source tools/harness-slot.sh
[ -n "${XPLAY_RJSLOT:-}" ] && [ -z "${XPLAY_SLOT:-}" ] && XS=$(( XS + 1 + $(printf '%s' "$XPLAY_RJSLOT" | cksum | cut -d' ' -f1) % 9 ))   # as lobby-vs-browser.sh
SERVER_PORT=${SERVER_PORT:-$((13300 + XS))}
source tools/rj-slot.sh; sd="$RJ/sdcard/xplay"
source tools/ryujinx-lock.sh
cp "$sd/netplay.txt" $out/pc-netplay.txt.bak; cp "$sd/net.txt" $out/pc-net.txt.bak; cp "$sd/script.txt" $out/pc-script.txt.bak
cleanup() {
  cp $out/pc-netplay.txt.bak "$sd/netplay.txt"; cp $out/pc-net.txt.bak "$sd/net.txt"; cp $out/pc-script.txt.bak "$sd/script.txt"
  pkill -f "local.mjs --port $SERVER_PORT( |$)" 2>/dev/null; pkill -f "lobby-host.mjs --server ws://127.0.0.1:$SERVER_PORT " 2>/dev/null
  ryujinx_unlock
}
trap cleanup EXIT
printf 'server ws://127.0.0.1:%s\nname RYUJINX\ninputdelay 2\nrtcbind 127.0.0.1\nstun off\n' $SERVER_PORT > "$sd/netplay.txt"
: > "$sd/net.txt"; : > "$sd/script.txt"
printf '%s\n' "$@" > "$sd/hw.txt"
tools/rebaseline-save.sh >/dev/null || exit 1
pkill -f "local.mjs --port $SERVER_PORT( |$)" 2>/dev/null; sleep 1
(cd "$WT" && node cloudflare/tfex-server/local.mjs --port $SERVER_PORT --verbose > $out/pc-server.log 2>&1 &)
sleep 2
k=0; for h in ${BT_HOSTS:-wide narrow wide}; do k=$((k+1)); a=; [ $h = wide ] && a=--wide
  node tools/lobby-host.mjs --server ws://127.0.0.1:$SERVER_PORT --timeout 300 --name PCHOST$k $a > $out/pc-host$k.log 2>&1 &
  sleep 1; done
wb=${TMPDIR:-/tmp}/xplay-winid
[ "$wb" -nt "$here/tools/winid.swift" ] || swiftc -O -o "$wb" "$here/tools/winid.swift"
rm -f $out/panel-*.png $out/pc-stop
XPLAY_VISIBLE=1 XPLAY_STOPFILE=$out/pc-stop XPLAY_UNTIL="panel-check: never" XPLAY_EXLAUNCH="$here/hw-probe/exlaunch" XPLAY_MOD=hw-probe XPLAY_TAG=ui \
  tools/run-probe.sh 200 > $out/pc-run.out 2>&1 &
rp=$!
# the guest log: this Ryujinx's open log file (as run-probe finds it); then the shots
pid=; log=
for _ in $(seq 1 120); do
  pid=$(pgrep -n -x "$RJ_NAME"); [ -n "$pid" ] && log=$(lsof -p "$pid" -Fn 2>/dev/null | sed -n 's/^n\(.*\/Ryujinx_[^/]*\.log\)$/\1/p' | head -1)
  [ -n "$log" ] && break; sleep 1
done
shot() { w=$("$wb" "$(pgrep -n -x "$RJ_NAME")"); screencapture -x -o -l "$w" "$out/$1" && echo "panel-check: $out/$1"; }
ok=
for _ in $(seq 1 150); do
  kill -0 $rp 2>/dev/null || break
  if [ -n "$log" ] && /usr/bin/grep -aq "press A (join the first lobby)" "$log"; then ok=1; break; fi
  sleep 1
done
if [ -n "$ok" ]; then
  shot panel-1.png   # just after A (a refused lobby: its "CAN'T JOIN - ..." message)
  sleep 6; shot panel-2.png   # 6 s later
else echo "panel-check: the Switch never selected a listed lobby (pc-run.out, pc-host.log)"; fi
echo "${ok:+shots taken}${ok:-no lobby}" > $out/pc-stop
wait $rp
[ -n "$log" ] && /usr/bin/grep -a "\[ui\]\|\[tfex\]\|\[lobby\]\|browser" "$log" | sed 's/.*OutputDebugString: //' | tail -15
tail -3 $out/pc-run.out
