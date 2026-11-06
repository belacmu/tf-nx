#!/usr/bin/env bash
# Two browser pages in one TF.EX lobby on a local matchmaking server, with screenshots of each step (docs/SCREENSHOTS.md). Both pages
# run TF.EX built from one commit (a throwaway worktree, as shot.sh). The host page picks STANDARD or WIDER, adds local guests on
# virtual gamepads, opens the lobby builder at its largest LOBBY SIZE and creates the lobby; the second page joins it; everyone
# readies, the host starts, the match plays.
#   tools/pageshot/lobby.sh <name> <TF.EX commit> <server binary>
# Shots in .devserve/pageshot/<name>-*.png: panel (the host's local players), builder (LOBBY SIZE at its largest), host-lobby,
# host-joined-2s/-4s (the host's lobby right after the join: its update feed), join-lobby (both roll calls, everyone ready), match (once it draws). Logs: <name>-host.log, <name>-join.log, <name>-server.log.
# Prints one line: PAGESHOT-LOBBY <name> OK|FAIL: <reason> | shots <dir>
# Env: LOBBY_WIDE=1 (WIDER), LOBBY_SIZE_KEYS=<n> (presses right on LOBBY SIZE, default 8: its largest), PAGESHOT_PATCHES (patches/<name>.patch,
#      space-separated, as shot.sh), HOST_GUESTS=<n> (local guests on the host page, default 0), NO_JOIN=1 (stop after the builder: no
#      second page, no match), TFEX_REPO, OUT (default .devserve/pageshot), BROWSER_WT (the page's build: default tfex-8p, which has
#      the virtual gamepads), DRIVER_WT (the test driver: default tfex-0281, whose shots leave out the page's buttons).
# The server must let localhost pages connect (our fork's ALLOWED_ORIGINS commit, never part of a PR).
set -uo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)
name=$1 ref=$2 server=$3
TFEX_REPO=${TFEX_REPO:-$HOME/Code/Towerfall Switch Mods/tfex-eight-players}
BROWSER_WT=${BROWSER_WT:-$HOME/Code/Towerfall Browser/.claude/worktrees/tfex-8p}
DRIVER_WT=${DRIVER_WT:-$HOME/Code/Towerfall Browser/.claude/worktrees/tfex-0281}
OUT=${OUT:-$REPO/.devserve/pageshot}
SP=13481 PP=18481 DH=19781 DJ=19782
WT="$OUT/tfex-$name" PAGE="$OUT/page-$name"
mkdir -p "$OUT"
fail() { for x in host join; do log $x > "$OUT/$name-$x.log" 2> /dev/null; done; echo "PAGESHOT-LOBBY $name FAIL: $1 | shots $OUT/$name-*.png"; exit 1; }
for p in $SP $PP $DH $DJ; do lsof -ti tcp:$p -sTCP:LISTEN > /dev/null && { echo "PAGESHOT-LOBBY $name FAIL: port $p in use"; exit 1; }; done
[ -x "$server" ] || { echo "PAGESHOT-LOBBY $name FAIL: no server at $server"; exit 1; }

git -C "$TFEX_REPO" worktree remove --force "$WT" 2> /dev/null; rm -rf "$WT"
git -C "$TFEX_REPO" worktree add -q --detach "$WT" "$ref" || { echo "PAGESHOT-LOBBY $name FAIL: no TF.EX commit $ref"; exit 1; }
for p in ${PAGESHOT_PATCHES:-}; do git -C "$WT" apply "$HERE/patches/$p.patch" || { echo "PAGESHOT-LOBBY $name FAIL: patch $p doesn't apply on $ref"; exit 1; }; done
TFEX_SRC="$WT" BROWSER_WT="$BROWSER_WT" "$REPO/tools/modmodes/build-page.sh" "$PAGE" > /dev/null || fail "the page's build (logs $PAGE)"

cleanup() {
  for d in $DH $DJ; do curl -s -m 5 "localhost:$d/quit" > /dev/null; done; sleep 2
  for p in $SP $PP; do lsof -ti tcp:$p -sTCP:LISTEN 2> /dev/null | xargs kill 2> /dev/null; done
  rm -rf ~/.towerfall-browser/netplay-PH-profile ~/.towerfall-browser/netplay-PJ-profile
  git -C "$TFEX_REPO" worktree remove --force "$WT" 2> /dev/null
}
trap cleanup EXIT
rm -rf ~/.towerfall-browser/netplay-PH-profile ~/.towerfall-browser/netplay-PJ-profile
(PORT=$SP METRICS_PORT=$((SP + 11000)) RELAY_PORT=$((SP + 12000)) exec "$server" > "$OUT/$name-server.log" 2>&1 &)
(cd "$BROWSER_WT" && TOWERFALL_WWW="$PAGE/www" exec python3 -u tools/serve.py $PP > "$OUT/$name-serve.log" 2>&1 &)
sleep 2
URL="http://localhost:$PP/?mute&nointro&autoplay&mods=TF.EX,Teuria.WiderSet&fortrise&tfexserver=ws://127.0.0.1:$SP"
drv() { (cd "$DRIVER_WT" && exec node tools/netplay-driver.mjs --url "$URL" --players $1 --port $2 --no-cache > "$OUT/$name-driver-$1.log" 2>&1 &); }
port() { [ "$1" = host ] && echo "$DH PH" || echo "$DJ PJ"; }
log() { set -- $(port $1); curl -s -m 5 "localhost:$1/$2/console?from=0"; }
k() { local who=$1; shift; set -- $(port $who) "$@"; local d=$1 p=$2; shift 2; for x in "$@"; do curl -s "localhost:$d/$p/key?k=$x" > /dev/null; sleep 1; done; }
cmd() { set -- $(port $1) "$2"; curl -s "localhost:$1/$2/eval" --data "towerfallCommand('$3')" > /dev/null; }
shot() { set -- $(port $1) "$2"; curl -s "localhost:$1/$2/shot" -o "$OUT/$name-$3.png"; }
loaded() { for _ in $(seq 1 180); do log $1 2> /dev/null | grep -q "SOUND LOADING" && return 0; sleep 1; done; return 1; }
# the online menu: skip TF.EX's first-run tutorial (a fresh profile), then the main menu: NETPLAY, STANDARD or WIDER (WiderSet)
online() { k $1 x; sleep 3; k $1 ArrowRight c; sleep 2; [ "${LOBBY_WIDE:-0}" = 1 ] && k $1 ArrowRight; k $1 c; sleep 3; shot $1 $1-online; }
guests() { local n=$1 i; shift; for i in $(seq 0 $((n - 1))); do cmd host "pad $i $*"; sleep 2; done; }

drv PH $DH
loaded host || fail "the host page didn't load"
sleep 8; online host
G=${HOST_GUESTS:-0}
if [ "$G" -gt 0 ]; then cmd host "pad attach $G"; sleep 2; guests $G start; sleep 2; fi
shot host panel
k host ArrowRight ArrowRight c; sleep 3; k host Enter; sleep 3                        # LOBBIES, the lobby builder
k host ArrowDown ArrowDown $(printf 'ArrowRight %.0s' $(seq 1 ${LOBBY_SIZE_KEYS:-8})); sleep 1   # LOBBY SIZE (up to its largest by default)
shot host builder
[ "${NO_JOIN:-0}" = 1 ] && { for x in host; do log $x > "$OUT/$name-$x.log"; done; echo "PAGESHOT-LOBBY $name OK: panel and builder | shots $OUT/$name-*.png"; exit 0; }
k host Enter; sleep 5                                                                   # create the lobby
drv PJ $DJ
loaded join || fail "the second page didn't load"
sleep 8; online join
k join ArrowRight ArrowRight c; sleep 5; k join c                                      # LOBBIES, join the first lobby
sleep 2; shot host host-joined-2s; sleep 2; shot host host-joined-4s; sleep 4          # the host's lobby feed (6 s) on the join
k join c; k host c; [ "$G" -gt 0 ] && guests $G a; sleep 4                              # everyone ready
shot host host-lobby; shot join join-lobby
k host c                                                                                # the host starts
for _ in $(seq 1 240); do log host | grep -q "Event: Synchronized" && break; sleep 1; done
log host | grep -q "Event: Synchronized" || fail "the match didn't synchronize (no GGRS Synchronized event)"
for _ in $(seq 1 120); do log host | awk '/Event: Synchronized/ {f=1} f && /\[perf\] [5-9][0-9]\.[0-9] frames/ {ok=1} END {exit !ok}' && break; sleep 1; done
sleep 3; shot host match
for x in host join; do log $x > "$OUT/$name-$x.log"; done
grep -qiE "desync" "$OUT/$name-host.log" "$OUT/$name-join.log" && fail "a desync line"
grep -qiE "unhandled|exception" "$OUT/$name-host.log" "$OUT/$name-join.log" && fail "an exception in a page"
echo "PAGESHOT-LOBBY $name OK: the match synchronized (GGRS), no desync or exception on either page | shots $OUT/$name-*.png"
