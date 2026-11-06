#!/bin/bash
# hw-peer.sh: the Mac side of a console session (HARDWARE-SESSION.md). The browser peer is the same harness page the Ryujinx scripts
# use (lobby-vs-browser.sh), but against the DEPLOYED server, and no Ryujinx.
#   tools/hw-peer.sh xnet <room>          seat 1 of `make-sd.sh install <SD> N|W <host> <room>`: xnet over wss://$HOST/relay/<room>,
#                                         script1, seed 42; ends by itself after the match (or Ctrl-C)
#   tools/hw-peer.sh lobby                joins the lobby the Switch hosts (`make-sd.sh install <SD> L <host>`): xlobby page against
#                                         wss://$HOST, LOBBIES -> first lobby -> ready; then the Switch's player starts the match
#   tools/hw-peer.sh compare <xplay dir> [browser log]   the per-tick comparison of a peer log (default: the last one) with the
#                                         Switch's (sd:/xplay copied off the card, or one of its runs/<...> folders)
# Env: HOST (default: TFNX_SERVER in ~/.config/tfnx/local.mk), BROWSER_WT (xnet build), BROWSER_LOBBY_WT (xlobby build, as
# lobby-vs-browser.sh), PEER_PORT (page, 18480), PEER_DRV (driver, 19480). Logs: $out/hw-browser.txt (also kept as
# $out/hw-browser-<room|lobby>-<time>.txt), $out/hw-driver.log, $out/hw-peer.png; $out is printed at the end.
set -u
here=$(cd "$(dirname "$0")/.." && pwd); cd "$here"
out=${XPLAY_OUT:-/tmp/xplay-runs-$(printf '%s' "$here" | shasum | cut -c1-8)}; mkdir -p "$out"
HOST=${HOST:-$(sed -n 's#^TFNX_SERVER *[:?]*= *wss://##p' ~/.config/tfnx/local.mk 2>/dev/null)}
[ -n "$HOST" ] || { echo "hw-peer: no server: set HOST or TFNX_SERVER in ~/.config/tfnx/local.mk" >&2; exit 1; }
PORT=${PEER_PORT:-18480}; DRV=${PEER_DRV:-19480}
USW=${SWITCH_PEER:-33333333-3333-4333-8333-333333333333}   # make-sd.sh's default Switch peer
UBR=22222222-2222-4222-8222-222222222222
mode=${1:?usage: hw-peer.sh xnet <room> | lobby | compare <xplay dir>}

start_page() {   # $1 worktree, $2 URL
  if ! curl -s -m 2 -o /dev/null "localhost:$PORT/"; then (cd "$1" && python3 tools/serve.py $PORT > $out/hw-serve.log 2>&1 &); sleep 2; fi
  rm -f $out/hw-browser.txt
  (cd "$1" && CONSOLE_TEE=$out/hw-browser.txt node "$here/browser-60hz/xnet-tools/driver-quiet.mjs" --url "$2" --players HWP --port $DRV > $out/hw-driver.log 2>&1 &)
}
stop_page() {
  curl -s "localhost:$DRV/HWP/shot" -o $out/hw-peer.png
  curl -s "localhost:$DRV/quit" >/dev/null; sleep 2; pkill -f "driver-quiet.mjs.*$DRV"; pkill -f "serve.py $PORT"
  rm -rf "$HOME/.towerfall-browser/netplay-HWP-profile"
}
trap 'stop_page; exit 130' INT TERM
keep_log() { local k="$out/hw-browser-$1-$(date +%H%M%S).txt"; cp $out/hw-browser.txt "$k" 2>/dev/null && echo "browser log kept: $k"; }
wait_end() {     # until the page logged $1 ticks, or nothing new for 90 s after the match started
  local last=-1 idle=0 t=0
  while :; do
    local n; n=$(grep -c '\[xplay2\] T ' $out/hw-browser.txt 2>/dev/null); n=${n:-0}
    [ "$n" -ge "$1" ] && break
    if [ "$n" = "$last" ]; then idle=$((idle + 5)); [ "$n" -gt 0 ] && [ $idle -ge 90 ] && break; else idle=0; fi
    last=$n; sleep 5; t=$((t + 5))
    [ $((t % 30)) = 0 ] && echo "  page: $n ticks logged"
  done
}

case "$mode" in
xnet)
  room=${2:?room (xnet-...)}
  case "$room" in xnet-*) ;; *) echo "the room must start with xnet-" >&2; exit 1;; esac
  WT=${BROWSER_WT:-$HOME/Code/Towerfall Browser/.claude/worktrees/test-page}
  S1=$(tr -d '\n' < test-scripts/versus/script1.txt)
  CMD=$(python3 -c "import urllib.parse,sys; print(urllib.parse.quote(sys.argv[1], safe=''))" \
    "xnet wss://$HOST/relay/$room 1 $UBR $USW script=$S1 seed=42 maxticks=4400 synctimeout=900")
  start_page "$WT" "http://localhost:$PORT/?mute&nointro&autoplay&mods=TF.EX&fortrise&tfexfps=60&command=$CMD"
  echo "browser peer (seat 1) waiting in room $room for the Switch ($USW); boot the console and start TowerFall now"
  wait_end 4380
  stop_page
  keep_log "$room"
  echo "page done: $(grep -c '\[xplay2\] T ' $out/hw-browser.txt) ticks; rollbacks: $(grep -o 'rollbacks=[0-9]*' $out/hw-browser.txt | tail -1)"
  echo "after copying sd:/xplay off the card: tools/hw-peer.sh compare <copy>/xplay"
  ;;
lobby)
  WT=${BROWSER_LOBBY_WT:-$HOME/Code/Towerfall Browser/.claude/worktrees/test-page}
  S0=$(tr -d '\n' < test-scripts/versus/script0.txt); S1=$(tr -d '\n' < test-scripts/versus/script1.txt)
  CMD=$(python3 -c "import urllib.parse,sys; print(urllib.parse.quote(sys.argv[1], safe=''))" "xlobby script0=$S0 script1=$S1 maxticks=20000 origin=266")
  start_page "$WT" "http://localhost:$PORT/?mute&nointro&autoplay&mods=TF.EX&fortrise&tfexserver=wss://$HOST&mode=online&command=$CMD"
  key() { for k in "$@"; do curl -s "localhost:$DRV/HWP/key?k=$k" >/dev/null; sleep 0.6; done; curl -s "localhost:$DRV/HWP/shot" -o $out/hw-peer.png; }
  for _ in $(seq 1 120); do grep -q 'xlobby.*cfg\|\[command\] xlobby' $out/hw-browser.txt 2>/dev/null && break; sleep 1; done
  echo "page armed. On the Switch: NETPLAY (the lobby is created), A to ready. Then press Enter here."
  read -r _
  sleep 20   # keys pressed while the online menu still builds land on QUICK PLAY
  key ArrowRight c; sleep 3; key c; sleep 4; key c
  echo "keys sent (LOBBIES, join the first lobby, ready; screenshot $out/hw-peer.png). If it didn't join, drive it:"
  echo "  curl localhost:$DRV/HWP/key?k=<key>   (x = back, c = confirm, arrows), screenshot: curl localhost:$DRV/HWP/shot -o shot.png"
  echo "Start the match on the Switch (A / + when the ready banner shows). Ctrl-C here when the match is over."
  wait_end 999999
  stop_page
  keep_log lobby
  ;;
compare)
  dir=${2:?the copied sd:/xplay folder}; blog=${3:-$out/hw-browser.txt}
  [ -f "$dir/vs-log-3m.txt" ] || { echo "no vs-log-3m.txt in $dir" >&2; exit 1; }
  tr -d '\000' < "$dir/vs-log-3m.txt" | sed 's/ *$//' | grep '^\[vs\]\|^\[cr\]' > $out/hw-sd.txt
  echo "switch: $(grep -c '^\[vs\] [0-9-]' $out/hw-sd.txt) lines; browser: $(grep -c '\[xplay2\] T ' "$blog") ticks ($blog)"
  python3 tools/compare-vs.py "$blog" $out/hw-sd.txt | head -9 | cut -c1-230
  ;;
*) echo "unknown mode $mode" >&2; exit 1 ;;
esac
