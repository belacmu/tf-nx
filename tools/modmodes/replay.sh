#!/usr/bin/env bash
# A replay of a mod mode (docs/PLAN-MOD-MODES-ONLINE-2026-10-31.md): pages A and B (one driver each) play the test mod's mode online;
# after round 0, B's page closes, A exports the replay (TF.EX: the session lost); A then plays it back (TF.EX's `replay` command).
# Logs only, one line: MODREPLAY PASS|FAIL: <reason> | logs <dir>
#   PASS: the replay recorded the mode (TF.Replay's CustomMode) and its playback ran the mod's round logic (a "[ModTest] round" line
#   after the replay command: playback restores the players from the recorded state, so no spawn line), no exception.
#   tools/modmodes/replay.sh
# Env: as two-pages.sh (OUT default .devserve/modreplay).
set -uo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)
MM_SERVER=${MM_SERVER:-$HOME/Code/Towerfall Switch Mods/tf-ex-mm-mod-modes/target/debug/tf-ex-matchmaking-server}
BROWSER_WT=${BROWSER_WT:-$HOME/Code/Towerfall Browser/.claude/worktrees/tfex-0281}
OUT=${OUT:-$REPO/.devserve/modreplay}
SERVER_PORT=13475 PAGE_PORT=18475 DRV_A=19775 DRV_B=19776
[ -x "$MM_SERVER" ] || { echo "MODREPLAY FAIL: no server at $MM_SERVER"; exit 1; }
"$HERE/build-page.sh" "$OUT" || { echo "MODREPLAY FAIL: the page's build | logs $OUT"; exit 1; }

cleanup() {
  for d in $DRV_A $DRV_B; do curl -s "localhost:$d/quit" > /dev/null; done; sleep 2
  for p in $SERVER_PORT $PAGE_PORT; do lsof -ti tcp:$p -sTCP:LISTEN 2>/dev/null | xargs kill 2>/dev/null; done
  rm -rf ~/.towerfall-browser/netplay-RA-profile ~/.towerfall-browser/netplay-RB-profile
}
trap cleanup EXIT
(PORT=$SERVER_PORT METRICS_PORT=$((SERVER_PORT + 11000)) RELAY_PORT=$((SERVER_PORT + 12000)) "$MM_SERVER" 2>&1 \
  | perl -pe 'BEGIN { $| = 1 } s/\e\[[0-9;]*m//g' > "$OUT/server.log" &)
(cd "$BROWSER_WT" && TOWERFALL_WWW="$OUT/www" python3 -u tools/serve.py $PAGE_PORT > "$OUT/serve.log" 2>&1 &)
sleep 2
url="http://localhost:$PAGE_PORT/?mute&nointro&autoplay&mods=TF.EX,NetplayModeTest&fortrise&tfexserver=ws://127.0.0.1:$SERVER_PORT&mode=online"
(cd "$BROWSER_WT" && node tools/netplay-driver.mjs --url "$url&debug" --players RA --port $DRV_A --no-cache > "$OUT/driver-a.log" 2>&1 &)
(cd "$BROWSER_WT" && node tools/netplay-driver.mjs --url "$url" --players RB --port $DRV_B --no-cache > "$OUT/driver-b.log" 2>&1 &)

log() { curl -s "localhost:$1/$2/log?from=0"; }
k() { curl -s "localhost:$1/$2/key?k=$3" > /dev/null; sleep "${4:-0.6}"; }
wait_log() { for _ in $(seq 1 "$4"); do log "$1" "$2" | grep -q "$3" && return 0; sleep 1; done; return 1; }
fail() { log $DRV_A RA > "$OUT/RA.log"; log $DRV_B RB > "$OUT/RB.log" 2>/dev/null; echo "MODREPLAY FAIL: $1 | logs $OUT"; exit 1; }

for d in "$DRV_A RA" "$DRV_B RB"; do
  set -- $d
  wait_log $1 $2 "\[ModTest\] ready" 180 || fail "$2 didn't load the test mod"
done
sleep 10
k $DRV_A RA x; k $DRV_B RB x
wait_log $DRV_A RA "entering netplay" 40 && wait_log $DRV_B RB "entering netplay" 40 || fail "the pages didn't reach the online menu"
sleep 3
k $DRV_A RA ArrowRight; k $DRV_A RA ArrowRight; k $DRV_A RA c 3; k $DRV_A RA Enter 3
k $DRV_A RA ArrowRight; k $DRV_A RA ArrowRight; k $DRV_A RA ArrowRight
k $DRV_A RA ArrowDown; k $DRV_A RA ArrowDown; k $DRV_A RA ArrowLeft; k $DRV_A RA Enter 4
k $DRV_B RB ArrowRight; k $DRV_B RB ArrowRight; k $DRV_B RB c 4; k $DRV_B RB c 5
k $DRV_A RA c; k $DRV_B RB c 3; k $DRV_A RA c
wait_log $DRV_A RA "\[ModTest\] round 0: " 60 || fail "round 0 didn't spawn"
sleep 6
curl -s "localhost:$DRV_B/quit" > /dev/null   # B leaves: A's session is lost, A exports the replay
wait_log $DRV_A RA "Exported replay" 90 || fail "A didn't export a replay"
name=$(log $DRV_A RA | grep -o "Exported replay [^ ]*" | tail -1 | sed 's/Exported replay //; s/\.tow$//')
before=$(log $DRV_A RA | grep -c "\[ModTest\] round")
sleep 8; k $DRV_A RA x; sleep 2; k $DRV_A RA x; sleep 2   # (past the lost-session notice, back to the menus)
curl -s -d "towerfallCommand('replay $name')" "localhost:$DRV_A/RA/eval" > /dev/null
for _ in $(seq 1 90); do [ "$(log $DRV_A RA | grep -c "\[ModTest\] round")" -gt "$before" ] && break; sleep 1; done
log $DRV_A RA > "$OUT/RA.log"
grep -q "IS MISSING\|You must specify" "$OUT/RA.log" && fail "the replay didn't start: $(grep -m1 'IS MISSING' "$OUT/RA.log" | cut -c1-120)"
[ "$(grep -c "\[ModTest\] round" "$OUT/RA.log")" -gt "$before" ] || fail "the replay $name didn't run the mod's round logic"
grep -q "An exception occurs\|Unhandled exception\|Error in MainLoop" "$OUT/RA.log" && fail "an exception (RA.log)"
echo "MODREPLAY PASS: replay $name played the mod's mode (its round logic ran again), no exception | logs $OUT"
