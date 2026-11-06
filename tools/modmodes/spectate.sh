#!/usr/bin/env bash
# A spectator of a mod mode (docs/PLAN-MOD-MODES-ONLINE-2026-10-31.md): page A hosts a lobby in the test mod's mode, B joins, C spectates
# it from the lobby list (TAB): SPECTATE=early (default: before the match starts) or late (once it started).
# Logs only, one line: MODSPECTATE PASS|FAIL: <reason> | logs <dir>
#   PASS: C's game ran the mod's round logic ("[ModTest] round" on C), no GGRS desync on any page, no exception.
#   tools/modmodes/spectate.sh
# Env: as two-pages.sh (OUT default .devserve/modspectate), SPECTATE=early|late.
set -uo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)
MM_SERVER=${MM_SERVER:-$HOME/Code/Towerfall Switch Mods/tf-ex-mm-mod-modes/target/debug/tf-ex-matchmaking-server}
BROWSER_WT=${BROWSER_WT:-$HOME/Code/Towerfall Browser/.claude/worktrees/tfex-0281}
OUT=${OUT:-$REPO/.devserve/modspectate}
WHEN=${SPECTATE:-early}
SERVER_PORT=13476 PAGE_PORT=18476 DRV=19777
[ -x "$MM_SERVER" ] || { echo "MODSPECTATE FAIL: no server at $MM_SERVER"; exit 1; }
"$HERE/build-page.sh" "$OUT" || { echo "MODSPECTATE FAIL: the page's build | logs $OUT"; exit 1; }

cleanup() {
  curl -s "localhost:$DRV/quit" > /dev/null; sleep 2
  for p in $SERVER_PORT $PAGE_PORT; do lsof -ti tcp:$p -sTCP:LISTEN 2>/dev/null | xargs kill 2>/dev/null; done
  rm -rf ~/.towerfall-browser/netplay-SA-profile ~/.towerfall-browser/netplay-SB-profile ~/.towerfall-browser/netplay-SC-profile
}
trap cleanup EXIT
(PORT=$SERVER_PORT METRICS_PORT=$((SERVER_PORT + 11000)) RELAY_PORT=$((SERVER_PORT + 12000)) "$MM_SERVER" 2>&1 \
  | perl -pe 'BEGIN { $| = 1 } s/\e\[[0-9;]*m//g' > "$OUT/server.log" &)
(cd "$BROWSER_WT" && TOWERFALL_WWW="$OUT/www" python3 -u tools/serve.py $PAGE_PORT > "$OUT/serve.log" 2>&1 &)
sleep 2
(cd "$BROWSER_WT" && node tools/netplay-driver.mjs --url "http://localhost:$PAGE_PORT/?mute&nointro&autoplay&mods=TF.EX,NetplayModeTest&fortrise&tfexserver=ws://127.0.0.1:$SERVER_PORT&mode=online" \
  --players SA,SB,SC --port $DRV --no-cache > "$OUT/driver.log" 2>&1 &)

log() { curl -s "localhost:$DRV/$1/log?from=0"; }
k() { curl -s "localhost:$DRV/$1/key?k=$2" > /dev/null; sleep "${3:-0.6}"; }
wait_log() { for _ in $(seq 1 "$3"); do log "$1" | grep -q "$2" && return 0; sleep 1; done; return 1; }
fail() { for P in SA SB SC; do log $P > "$OUT/$P.log"; curl -s "localhost:$DRV/$P/shot" -o "$OUT/$P.png"; done; echo "MODSPECTATE FAIL ($WHEN): $1 | logs $OUT"; exit 1; }
spectate() { k SC ArrowRight; k SC ArrowRight; k SC c 4; k SC ShiftLeft 3; k SC Tab 5; }   # LOBBIES, refresh, spectate the first lobby

for P in SA SB SC; do wait_log $P "\[ModTest\] ready" 240 || fail "$P didn't load the test mod"; done
sleep 10
for P in SA SB SC; do k $P x; done
for P in SA SB SC; do wait_log $P "entering netplay" 40 || fail "$P didn't reach the online menu"; done
sleep 3
k SA ArrowRight; k SA ArrowRight; k SA c 3; k SA Enter 3
k SA ArrowRight; k SA ArrowRight; k SA ArrowRight
k SA ArrowDown; k SA ArrowDown; k SA ArrowLeft; k SA Enter 4
[ "$WHEN" = early ] && spectate   # (before B: a full lobby isn't listed until its match starts)
k SB ArrowRight; k SB ArrowRight; k SB c 4; k SB c 5
k SA c; k SB c 3; k SA c
wait_log SA "\[ModTest\] round 0: " 60 || fail "round 0 didn't spawn on A"
[ "$WHEN" = late ] && { sleep 3; spectate; }
wait_log SC "\[ModTest\] round" 90 || fail "C (spectating) didn't run the mod's round logic"
sleep 15
for P in SA SB SC; do log $P > "$OUT/$P.log"; done
d=$(cat "$OUT"/S?.log | grep -c "DesyncDetected"); x=$(cat "$OUT"/S?.log | grep -c "An exception occurs\|Unhandled exception\|Error in MainLoop")
[ "$d" -gt 0 ] && fail "$d GGRS desync report(s)"
[ "$x" -gt 0 ] && fail "$x exception(s)"
echo "MODSPECTATE PASS ($WHEN): C spectated the mod's mode (its round logic ran on C), no desync, no exception | logs $OUT"
