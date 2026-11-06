#!/bin/bash
# Lobby rehearsal (BROWSER-LOBBY-RELAY.md): two headless Chromes on the lobby menu of a local tfex-server, page A normal,
# page B claiming the Switch (?tfexplatform=Switch), both with `xlobby <options>` armed.
#   lobby-start.sh <tag> [xlobby options...]      e.g. lobby-start.sh run1 "script0=$S0" "script1=$S1" maxticks=4400 origin=266
# Needs, already running: the page `python3 tools/serve.py $PAGE_PORT` from $BROWSER_WT (branch switch-lobby, built) and
# `node cloudflare/tfex-server/local.mjs --port $SERVER_PORT`. Logs: $OUT/<tag>-A.txt, -B.txt. Drive with lobby-keys.sh.
# Stop: curl localhost:19400/quit; curl localhost:19500/quit; then delete ~/.towerfall-browser/netplay-LA-profile and -LB-profile.
HERE="$(cd "$(dirname "$0")" && pwd)"
: "${BROWSER_WT:?set BROWSER_WT}"; OUT=${OUT:-$PWD/lobby-out}; PAGE_PORT=${PAGE_PORT:-18180}; SERVER_PORT=${SERVER_PORT:-13300}
mkdir -p "$OUT"
TAG=$1; shift
enc() { python3 -c "import urllib.parse,sys; print(urllib.parse.quote(sys.argv[1], safe=''))" "$1"; }
CMD=$(enc "xlobby $*")
BASE="http://localhost:$PAGE_PORT/?mute&nointro&autoplay&mods=TF.EX&fortrise&tfexserver=ws://127.0.0.1:$SERVER_PORT&mode=online&command=$CMD"
cd "$BROWSER_WT"
rm -f "$OUT/$TAG-A.txt" "$OUT/$TAG-B.txt"
(CONSOLE_TEE="$OUT/$TAG-A.txt" node "$HERE/driver-quiet.mjs" --url "$BASE" --players LA --port 19400 > "$OUT/$TAG-drvA.log" 2>&1 &)
(CONSOLE_TEE="$OUT/$TAG-B.txt" node "$HERE/driver-quiet.mjs" --url "$BASE&tfexplatform=Switch" --players LB --port 19500 > "$OUT/$TAG-drvB.log" 2>&1 &)
