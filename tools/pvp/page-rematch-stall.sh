#!/bin/bash
# Page vs page TF.EX lobby rematch with a main-thread stall on page B during the match results.
# Both pages are plain TF.EX (no tfexplatform=Switch): the reference for the Switch lobby's end of match (docs/ws-netplay-loop.md).
# Usage: tools/pvp/page-rematch-stall.sh <tag> [stall seconds, default 3]   (logs: $OUT/<tag>, default .pvp-out/<tag>)
# Ports 18191 (page), 13391 (server), 19461/19561 (drivers); Chrome profiles netplay-PA/PB-profile (delete after).
HERE="$(cd "$(dirname "$0")" && pwd)"
SO="$(cd "$HERE/../.." && pwd)"
WT="$HOME/Code/Towerfall Browser/.claude/worktrees/harness-main"
TAG=$1; STALL=${2:-3}; OUT=${OUT:-$SO/.pvp-out}/$TAG; mkdir -p "$OUT"
PAGE=18191; SRV=13391; DA=19461; DB=19561
S="$SO/test-scripts/versus"; S0=$(tr -d '\n' < "$S/script0.txt"); S1=$(tr -d '\n' < "$S/script1.txt")
enc() { python3 -c "import urllib.parse,sys; print(urllib.parse.quote(sys.argv[1], safe=''))" "$1"; }
CMD=$(enc "xlobby script0=$S0 script1=$S1 maxticks=4400 origin=266")
BASE="http://localhost:$PAGE/?mute&nointro&autoplay&mods=TF.EX&fortrise&tfexserver=ws://127.0.0.1:$SRV&mode=online&command=$CMD"
pids=()
cleanup() { curl -s -m 3 localhost:$DA/quit >/dev/null; curl -s -m 3 localhost:$DB/quit >/dev/null; kill ${pids[@]} 2>/dev/null; }
trap cleanup EXIT
cd "$WT"
python3 tools/serve.py $PAGE > "$OUT/serve.log" 2>&1 & pids+=($!)
node cloudflare/tfex-server/local.mjs --port $SRV --verbose > "$OUT/server.log" 2>&1 & pids+=($!)
sleep 2
CONSOLE_TEE="$OUT/A.txt" node "$SO/browser-60hz/xnet-tools/driver-quiet.mjs" --url "$BASE" --players PA --port $DA > "$OUT/drvA.log" 2>&1 & pids+=($!)
CONSOLE_TEE="$OUT/B.txt" node "$SO/browser-60hz/xnet-tools/driver-quiet.mjs" --url "$BASE" --players PB --port $DB > "$OUT/drvB.log" 2>&1 & pids+=($!)
k() { local u=$1; shift; for x in "$@"; do curl -s -m 5 "localhost:$u/key?k=$x" >/dev/null; sleep 0.6; done; }
waitfor() { local f=$1 pat=$2 max=$3; for _ in $(seq 1 $max); do grep -aq "$pat" "$f" 2>/dev/null && return 0; sleep 1; done; echo "timeout waiting for '$pat' in $(basename $f)"; return 1; }
log() { echo "t+${SECONDS}s $*" | tee -a "$OUT/steps.txt"; }
for s in A:$DA/PA B:$DB/PB; do f="$OUT/${s%%:*}.txt"; u=${s#*:}
  waitfor "$f" 'xlobby: armed' 180 || exit 1
  for n in 1 2 3 4; do waitfor "$f" '\[mode\] online: entering netplay' 25 >/dev/null && break; log "nudge ${s%%:*} ($n)"; k $u c; done
  grep -aq '\[mode\] online: entering netplay' "$f" || { log "${s%%:*} never reached the online menu"; exit 1; }
done
sleep 10; log "both online menus"
k $DA/PA ArrowRight ArrowRight c; sleep 3; k $DA/PA Enter; sleep 2; k $DA/PA ArrowDown ArrowRight ArrowDown ArrowDown ArrowLeft ArrowLeft; sleep 1; k $DA/PA Enter; sleep 3
curl -s "localhost:$DA/PA/shot" -o "$OUT/A-lobby.png"; log "A created the lobby"
k $DB/PB ArrowRight ArrowRight c; sleep 3; k $DB/PB c; sleep 4; curl -s "localhost:$DB/PB/shot" -o "$OUT/B-join.png"; log "B joined"
k $DA/PA c; k $DB/PB c; sleep 3; k $DA/PA c; log "archers picked, FIGHT"
waitfor "$OUT/A.txt" 'Event: Synchronized' 90 || exit 1; log "synchronized"
# the stall: STALLAT=round (when B's first round ends, the ~4 s results before it: a check that a stall makes GGRS interrupt) or
# results (default: as soon as B's match results show; they last ~4 s)
if [ "${STALLAT:-results}" = round ]; then waitfor "$OUT/B.txt" 'CreateResults\|round results\|RoundResults' 400 || exit 1
else for _ in $(seq 1 4000); do grep -aq 'match results shown' "$OUT/B.txt" && break; sleep 0.1; done; fi; log "B: stall point"
for i in 1 2; do sleep 0.3; log "stall B's main thread ${STALL}s ($i)"; curl -s -m 30 "localhost:$DB/PB/eval" --data "(()=>{const t=Date.now();while(Date.now()-t<${STALL}000){};return 'stalled'})()" >/dev/null; done
waitfor "$OUT/A.txt" 'sending MatchEnded' 60; waitfor "$OUT/B.txt" 'sending MatchEnded' 60; log "both MatchEnded (or timed out)"
for p in "$DA/PA" "$DB/PB"; do for i in 1 2 3; do sleep 2; k $p c; done; done; log "both voted REMATCH!"
for _ in $(seq 1 60); do [ "$(grep -ac 'Event: Synchronized ' "$OUT/A.txt")" -ge 2 ] && [ "$(grep -ac 'Event: Synchronized ' "$OUT/B.txt")" -ge 2 ] && { log "RESULT: the rematch synchronized"; break; }; sleep 1; done
[ "$(grep -ac 'Event: Synchronized ' "$OUT/A.txt")" -ge 2 ] || log "RESULT: no rematch sync within 60 s"
for s in A B; do echo "== $s"; grep -aE "match results shown|neutral from here|NetworkInterrupted|Disconnected from|GGRS session reset|sending MatchEnded|Event: Synchronized " "$OUT/$s.txt" | sed 's/page log: //' | cut -c1-140; done | tee "$OUT/summary.txt"
