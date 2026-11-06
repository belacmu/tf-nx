#!/bin/bash
# Runs the `xnet` harness (browser branch exp/xplay-versus, web/Netplay/XplayNet.cs): two headless Chromes, seat 0 and seat 1, play the scripted
# 2-player match against each other through the Node WebSocket relay on this Mac. Details: ../../BROWSER-RELAY.md.
#
#   xnet-run.sh <label> <maxticks> [relay options ...] [-- xnet options for both seats ...]
#   env: X0OPTS / X1OPTS      extra xnet options for one seat only (e.g. X0OPTS="delay=2" X1OPTS="delay=6")
#        BROWSER_WT           the browser repo worktree on branch exp/xplay-versus (default: the scratch one of the session that wrote this)
#        OUT                  where logs go (default: ./xnet-out); <label>-0.txt / -1.txt are the two browsers' console logs,
#                             <label>-relay.log the relay's, <label>-relaystats{0,1}.txt the pages' tfnet counters
#        SEED_SCRIPTS         directory with script0.txt / script1.txt (default: ../../test-scripts/versus)
#        SEED                 the match seed (default 42)
#        WAITLOOPS            give up after this many 3 s polls (default 80)
# Needs: node, python3, Google Chrome; the page served by `python3 tools/serve.py 18080` from $BROWSER_WT (started here if not running), a build of
# the worktree (tools/build.sh). Uses ports 18080 (page), 18800 (relay), 19400/19500 (drivers) and the Chrome profiles
# ~/.towerfall-browser/netplay-XN0-profile and -XN1-profile, which it deletes at the end.
#
# Afterwards: python3 cmp.py $OUT/<label>-0.txt $OUT/<label>-1.txt <reference-log-with-R-lines>
HERE="$(cd "$(dirname "$0")" && pwd)"
WT=${BROWSER_WT:-/private/tmp/claude-501/-Users-crab-Code-Towerfall-Switch-Mods/a86873af-5865-44d3-bb29-d370464237eb/scratchpad/wt}
OUT=${OUT:-$PWD/xnet-out}
SCRIPTS=${SEED_SCRIPTS:-$HERE/../../test-scripts/versus}
RELAY=${RELAY_SERVER:-$HERE/../../tools/relay-server.mjs}
mkdir -p "$OUT"
LABEL=$1; MAX=$2; shift 2
RELAYOPTS=(); XOPTS=()
while [ $# -gt 0 ] && [ "$1" != "--" ]; do RELAYOPTS+=("$1"); shift; done
[ "$1" == "--" ] && shift
XOPTS=("$@")
U0=11111111-1111-4111-8111-111111111111
U1=22222222-2222-4222-8222-222222222222
ROOM=${ROOM:-room-$LABEL}   # (tfex-server: an xnet-* name, e.g. ROOM=xnet-test RELAY_SERVER=<browser repo>/cloudflare/tfex-server/local.mjs)
S0=$(tr -d '\n' < "$SCRIPTS/script0.txt")
S1=$(tr -d '\n' < "$SCRIPTS/script1.txt")
enc() { python3 -c "import urllib.parse,sys; print(urllib.parse.quote(sys.argv[1], safe=''))" "$1"; }
CMD0=$(enc "xnet ws://localhost:18800/relay/$ROOM 0 $U0 $U1 script=$S0 seed=${SEED:-42} maxticks=$MAX ${XOPTS[*]} $X0OPTS")
CMD1=$(enc "xnet ws://localhost:18800/relay/$ROOM 1 $U1 $U0 script=$S1 seed=${SEED:-42} maxticks=$MAX ${XOPTS[*]} $X1OPTS")
STARTED_SERVER=
if ! curl -s -m 2 -o /dev/null localhost:18080/; then
  (cd "$WT" && python3 tools/serve.py 18080 > "$OUT/serve.log" 2>&1 &)
  STARTED_SERVER=1
  sleep 2
fi
node "$RELAY" --port 18800 "${RELAYOPTS[@]}" > "$OUT/$LABEL-relay.log" 2>&1 &
RPID=$!
sleep 1
BASE="http://localhost:18080/?mute&nointro&autoplay&mods=TF.EX&fortrise&tfexfps=60"
cd "$WT"
CONSOLE_TEE="$OUT/$LABEL-0.txt" node "$HERE/driver-quiet.mjs" --url "$BASE&command=$CMD0" --players XN0 --port 19400 > "$OUT/$LABEL-drv0.log" 2>&1 &
D0=$!
CONSOLE_TEE="$OUT/$LABEL-1.txt" node "$HERE/driver-quiet.mjs" --url "$BASE&command=$CMD1" --players XN1 --port 19500 > "$OUT/$LABEL-drv1.log" 2>&1 &
D1=$!
STATUS=timeout
for i in $(seq 1 ${WAITLOOPS:-80}); do
  sleep 3
  if [ "$(grep -l "xplay2\] stopped logging after" "$OUT/$LABEL-0.txt" "$OUT/$LABEL-1.txt" 2>/dev/null | wc -l | tr -d ' ')" = 2 ]; then STATUS=done; break; fi
  if grep -q "FAILED\|Sync test desync\|Cross-peer desync\|DesyncDetected" "$OUT/$LABEL-0.txt" "$OUT/$LABEL-1.txt" 2>/dev/null; then STATUS=failed; break; fi
done
curl -s localhost:19400/XN0/eval --data 'JSON.stringify({relay: globalThis.tfnetRelayStats ?? null, calls: globalThis.tfnetCalls ?? null})' > "$OUT/$LABEL-relaystats0.txt"
curl -s localhost:19500/XN1/eval --data 'JSON.stringify({relay: globalThis.tfnetRelayStats ?? null, calls: globalThis.tfnetCalls ?? null})' > "$OUT/$LABEL-relaystats1.txt"
curl -s localhost:19400/quit >/dev/null; curl -s localhost:19500/quit >/dev/null
sleep 2
kill $D0 $D1 2>/dev/null
kill -INT $RPID 2>/dev/null; sleep 1; kill $RPID 2>/dev/null
[ -n "$STARTED_SERVER" ] && pkill -f "serve.py 18080"
rm -rf "$HOME/.towerfall-browser/netplay-XN0-profile" "$HOME/.towerfall-browser/netplay-XN1-profile"
echo "$LABEL: $STATUS"
