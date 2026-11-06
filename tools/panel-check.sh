#!/bin/bash
# TF.EX's own settings for the run (its settings store, sd:/xplay/TF.EX.settings.json: name, the local server, input delay), padded to
# the store's fixed 1024 bytes; netplay.txt keeps only the Switch's connection keys
tfex_settings() { python3 -c 'import json,sys; d={"InputDelayMs":33,"AcceptedRollbackFrames":2,"Name":"RYUJINX","Server":sys.argv[1],"AutoAdjustInputDelay":"PROPOSE","CustomSkins":"FULL","AutoUpdate":False,"PlayerId":"","LastVersionSeen":"0.21.1"}; s=json.dumps(d,separators=(",",":")); sys.stdout.write(s+" "*(1024-len(s)))' "$1"; }
# panel-check.sh [-n] [hw.txt lines...]: a screenshot of the Switch's lobby browser (NetplayScreens) showing a PC lobby, no browser page.
# A local tfex-server and tools/lobby-host.mjs (a TF.EX-like host; default --wide: a WiderSet wide lobby, which a standard Switch refuses);
# the Switch (Ryujinx, a normal window behind the front app: a hidden one draws nothing to capture) walks NETPLAY -> STANDARD -> LOBBIES
# (hw.txt lobbyui 1, lobbyjoin 1), selects the first lobby and presses A. Shots: $out/panel-1.png just after A,
# $out/panel-2.png 6 s later. -n: a narrow host lobby (joinable) instead. XPLAY_RJSLOT (tools/rj-slot.sh) as the other scripts.
# Needs the screen-recording permission for the shell that runs it (screencapture -l).
set -u
here=$(cd "$(dirname "$0")/.." && pwd); cd "$here"
out=${XPLAY_OUT:-/tmp/xplay-runs-$(printf '%s' "$here" | shasum | cut -c1-8)${XPLAY_RJSLOT:+-$XPLAY_RJSLOT}}; mkdir -p "$out"
WT=${BROWSER_LOBBY_WT:-$HOME/Code/Towerfall Browser/.claude/worktrees/test-page}
hostarg=--wide; [ "${1:-}" = -n ] && { hostarg=; shift; }
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
printf 'rtcbind 127.0.0.1\nstun off\n' > "$sd/netplay.txt"
tfex_settings "ws://127.0.0.1:$SERVER_PORT" > "$sd/TF.EX.settings.json"
: > "$sd/net.txt"; : > "$sd/script.txt"
printf '%s\n' "$@" > "$sd/hw.txt"
tools/rebaseline-save.sh >/dev/null || exit 1
pkill -f "local.mjs --port $SERVER_PORT( |$)" 2>/dev/null; sleep 1
(cd "$WT" && node cloudflare/tfex-server/local.mjs --port $SERVER_PORT --verbose > $out/pc-server.log 2>&1 &)
sleep 2
node tools/lobby-host.mjs --server ws://127.0.0.1:$SERVER_PORT --timeout 300 $hostarg > $out/pc-host.log 2>&1 &
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
