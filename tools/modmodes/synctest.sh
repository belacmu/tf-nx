#!/usr/bin/env bash
# TF.EX's sync test on the test mod's mode (docs/PLAN-MOD-MODES-ONLINE-2026-10-31.md): one page runs `test NetplayModeTest/ModTest`,
# GGRS rolling back CHECK frames every frame and comparing the checksums, player 0 on the driver's random keys. The mode makes and
# removes entities of its own (markers) and keeps a counter of its own, in TF.State's state through RegisterEntityStateEvents and
# RegisterVariantStateEvents: a state not rolled back shows at once ("Sync test desync detected").
# Logs only, one line: MODSYNC PASS|FAIL: <reason> | logs <dir>
#   tools/modmodes/synctest.sh            PASS: no desync, no exception, markers made
#   MOD_DEFINES=NO_STATE tools/modmodes/synctest.sh   the mode without its state: must FAIL (the test sees what isn't rolled back)
# Env: as two-pages.sh (OUT default .devserve/modsync), PLAY (seconds, default 40), CHECK (frames, default 2).
set -uo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)
BROWSER_WT=${BROWSER_WT:-$HOME/Code/Towerfall Browser/.claude/worktrees/tfex-0281}
OUT=${OUT:-$REPO/.devserve/modsync}
PLAY=${PLAY:-40}
PAGE_PORT=18477 DRV=19778
"$HERE/build-page.sh" "$OUT" || { echo "MODSYNC FAIL: the page's build | logs $OUT"; exit 1; }
cleanup() {
  curl -s "localhost:$DRV/quit" > /dev/null; sleep 2
  lsof -ti tcp:$PAGE_PORT -sTCP:LISTEN 2>/dev/null | xargs kill 2>/dev/null
  rm -rf ~/.towerfall-browser/netplay-MS-profile
}
trap cleanup EXIT
(cd "$BROWSER_WT" && TOWERFALL_WWW="$OUT/www" python3 -u tools/serve.py $PAGE_PORT > "$OUT/serve.log" 2>&1 &)
sleep 2
cmd=$(python3 -c "import urllib.parse; print(urllib.parse.quote('test NetplayModeTest/ModTest 0 ${MAP:-0} 42 ${CHECK:-2}'))")
(cd "$BROWSER_WT" && node tools/netplay-driver.mjs --url "http://localhost:$PAGE_PORT/?mute&nointro&autoplay&mods=TF.EX,NetplayModeTest&fortrise&command=$cmd" \
  --players MS --port $DRV --no-cache > "$OUT/driver.log" 2>&1 &)
log() { curl -s "localhost:$DRV/MS/log?from=0"; }
for _ in $(seq 1 240); do log | grep -q "Starting test session" && break; sleep 1; done
log | grep -q "Starting test session" || { log > "$OUT/MS.log"; echo "MODSYNC FAIL: the sync test didn't start | logs $OUT"; exit 1; }
sleep 8
end=$((SECONDS + PLAY)); keys=(ArrowLeft ArrowRight c x ArrowUp z)
while [ $SECONDS -lt $end ]; do curl -s "localhost:$DRV/MS/key?k=${keys[$((RANDOM % ${#keys[@]}))]}&hold=200" > /dev/null; sleep 0.25; done
log > "$OUT/MS.log"
n=$(grep -c "Sync test desync detected" "$OUT/MS.log"); x=$(grep -c "An exception occurs\|Unhandled exception\|Error in MainLoop" "$OUT/MS.log")
[ "$n" -gt 0 ] && { echo "MODSYNC FAIL: $n desync(s), first: $(grep -m1 -A4 'Sync test desync detected' "$OUT/MS.log" | sed 's/^[0-9]*: \[error\] *//' | tr '\n' ' ' | cut -c1-400) | logs $OUT"; exit 1; }
[ "$x" -gt 0 ] && { echo "MODSYNC FAIL: $x exception(s) | logs $OUT"; exit 1; }
grep -q "\[ModTest\] round 0: " "$OUT/MS.log" || { echo "MODSYNC FAIL: the mode's round never started | logs $OUT"; exit 1; }
echo "MODSYNC PASS: sync test (check distance ${CHECK:-2}), $PLAY s, the mode's own entities and counter rolled back, no desync | logs $OUT"
