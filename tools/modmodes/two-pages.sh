#!/usr/bin/env bash
# Two browser pages play the test mod's versus mode online (docs/PLAN-MOD-MODES-ONLINE-2026-10-31.md, step 1): TF.EX built
# from a local source folder, tools/modmodes/NetplayModeTest, and the matchmaking server fork's mod-modes branch. Host A creates
# a lobby with the 4th game mode (the first mod mode), B joins, both ready, A starts, A shoots left to end round 0. Checks the
# logs only (no screenshots):
#   PASS: both pages log the mod's match start, its round 0 spawn after Synchronized, round 0 over and round 1 spawned, and no
#   GGRS desync event.
# Prints one line: MODMODES PASS|FAIL: <reason> | logs <dir>
#   tools/modmodes/two-pages.sh
# Env: TFEX_SRC (default ~/Code/TF.EX-mod-modes), MM_SERVER (the server binary, default the tf-ex-mm-mod-modes debug build),
#      BROWSER_WT (the browser worktree, default tfex-0281), OUT (the log folder, default .devserve/modmodes).
set -uo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)
TFEX_SRC=$(cd "${TFEX_SRC:-$HOME/Code/TF.EX-mod-modes}" && pwd -P)
MM_SERVER=${MM_SERVER:-$HOME/Code/Towerfall Switch Mods/tf-ex-mm-mod-modes/target/debug/tf-ex-matchmaking-server}
BROWSER_WT=${BROWSER_WT:-$HOME/Code/Towerfall Browser/.claude/worktrees/tfex-0281}
OUT=${OUT:-$REPO/.devserve/modmodes}
SERVER_PORT=13471 PAGE_PORT=18471 DRV=19771
[ -x "$MM_SERVER" ] || { echo "MODMODES FAIL: no server at $MM_SERVER (cargo build in the fork's mod-modes-online worktree)"; exit 1; }
"$HERE/build-page.sh" "$OUT" || { echo "MODMODES FAIL: the page's build | logs $OUT"; exit 1; }

cleanup() {
  curl -s "localhost:$DRV/quit" > /dev/null; sleep 2
  for p in $SERVER_PORT $PAGE_PORT; do lsof -ti tcp:$p -sTCP:LISTEN 2>/dev/null | xargs kill 2>/dev/null; done
  rm -rf ~/.towerfall-browser/netplay-MA-profile ~/.towerfall-browser/netplay-MB-profile
}
trap cleanup EXIT
(PORT=$SERVER_PORT METRICS_PORT=$((SERVER_PORT + 11000)) RELAY_PORT=$((SERVER_PORT + 12000)) "$MM_SERVER" 2>&1 \
  | perl -pe 'BEGIN { $| = 1 } s/\e\[[0-9;]*m//g' > "$OUT/server.log" &)
(cd "$BROWSER_WT" && TOWERFALL_WWW="$OUT/www" python3 -u tools/serve.py $PAGE_PORT > "$OUT/serve.log" 2>&1 &)
sleep 2
(cd "$BROWSER_WT" && node tools/netplay-driver.mjs --url "http://localhost:$PAGE_PORT/?mute&nointro&autoplay&mods=TF.EX,NetplayModeTest&fortrise&tfexserver=ws://127.0.0.1:$SERVER_PORT&mode=online" \
  --players MA,MB --port $DRV --no-cache > "$OUT/driver.log" 2>&1 &)

log() { curl -s "localhost:$DRV/$1/log?from=0"; }
k() { curl -s "localhost:$DRV/$1/key?k=$2" > /dev/null; sleep "${3:-0.6}"; }
wait_both() { # <pattern> <seconds>
  for _ in $(seq 1 "$2"); do
    log MA | grep -q "$1" && log MB | grep -q "$1" && return 0
    sleep 1
  done
  return 1
}
fail() { for P in MA MB; do log $P > "$OUT/$P.log"; done; echo "MODMODES FAIL: $1 | logs $OUT"; exit 1; }

wait_both "\[ModTest\] ready" 180 || fail "the pages didn't load the test mod"
log MA | grep -q "TF.EX mod modes found" || fail "the test mod didn't find TF.EX's RegisterNetplayVersusMode"
sleep 8
for P in MA MB; do k $P x; done                     # TF.EX's first-run tutorial
wait_both "entering netplay" 30 || fail "the pages didn't reach TF.EX's online menu"
sleep 2
k MA ArrowRight; k MA ArrowRight; k MA c 3; k MA Enter 3       # LOBBIES, create: the lobby builder, GAME MODE selected
k MA ArrowRight; k MA ArrowRight; k MA ArrowRight             # LMS -> HH -> TDM -> the first mod mode
k MA ArrowDown; k MA ArrowDown; k MA ArrowLeft; k MA Enter 4   # lobby size back to 2 (TDM raised it), create
k MB ArrowRight; k MB ArrowRight; k MB c 4; k MB c 5          # LOBBIES, join the first lobby
k MA c; k MB c 3; k MA c                                       # both ready, A starts
wait_both "\[ModTest\] round 0: " 60 || fail "round 0 didn't spawn through the mod on both pages"
for _ in 1 2 3 4 5; do curl -s "localhost:$DRV/MA/key?k=ArrowLeft&hold=150" > /dev/null; k MA x 1.5; done
wait_both "\[ModTest\] round 0 over" 30 || fail "round 0 didn't end on both pages"
wait_both "\[ModTest\] round 1: " 30 || fail "round 1 didn't spawn through TF.EX's round-load helper"
for P in MA MB; do log $P > "$OUT/$P.log"; done
grep -qiE "DesyncDetected|Desync detected" "$OUT/MA.log" "$OUT/MB.log" && fail "GGRS desync"
echo "MODMODES PASS: mod mode played online, rounds 0 and 1 on both pages, no desync | logs $OUT"
