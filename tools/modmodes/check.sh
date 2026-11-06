#!/usr/bin/env bash
# TF.EX's check for a lobby's mod game mode (docs/PLAN-MOD-MODES-ONLINE-2026-10-31.md step 2, as its missing-variants check): page A
# (the test mod 0.1.0) hosts a lobby in the test mod's mode; page B tries to join it from the lobby list, twice:
#   1. without the mod          -> refused: "NEEDS NETPLAYMODETEST 0.1.0", B never joins
#   2. with the mod at 0.2.0    -> refused the same way (a mode's mod at the host's version only)
# (B with the same version joins and plays: two-pages.sh.) Logs only, one line: MODCHECK PASS|FAIL: <reason> | logs <dir>
#   tools/modmodes/check.sh
# Env: as two-pages.sh (TFEX_SRC, MM_SERVER, BROWSER_WT, OUT default .devserve/modcheck).
set -uo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)
MM_SERVER=${MM_SERVER:-$HOME/Code/Towerfall Switch Mods/tf-ex-mm-mod-modes/target/debug/tf-ex-matchmaking-server}
BROWSER_WT=${BROWSER_WT:-$HOME/Code/Towerfall Browser/.claude/worktrees/tfex-0281}
OUT=${OUT:-$REPO/.devserve/modcheck}
SERVER_PORT=13472 PAGE_A=18472 PAGE_B=18473 DRV_A=19772 DRV_B=19773
[ -x "$MM_SERVER" ] || { echo "MODCHECK FAIL: no server at $MM_SERVER"; exit 1; }
rm -rf "$OUT" && mkdir -p "$OUT"
"$HERE/build-page.sh" "$OUT/a" || { echo "MODCHECK FAIL: page A's build | logs $OUT"; exit 1; }
MOD_VERSION=0.2.0 "$HERE/build-page.sh" "$OUT/b" || { echo "MODCHECK FAIL: page B's build | logs $OUT"; exit 1; }

quit_b() { curl -s "localhost:$DRV_B/quit" > /dev/null; sleep 2; rm -rf ~/.towerfall-browser/netplay-MB-profile; }
cleanup() {
  curl -s "localhost:$DRV_A/quit" > /dev/null; quit_b
  for p in $SERVER_PORT $PAGE_A $PAGE_B; do lsof -ti tcp:$p -sTCP:LISTEN 2>/dev/null | xargs kill 2>/dev/null; done
  rm -rf ~/.towerfall-browser/netplay-MA-profile
}
trap cleanup EXIT
(PORT=$SERVER_PORT METRICS_PORT=$((SERVER_PORT + 11000)) RELAY_PORT=$((SERVER_PORT + 12000)) "$MM_SERVER" 2>&1 \
  | perl -pe 'BEGIN { $| = 1 } s/\e\[[0-9;]*m//g' > "$OUT/server.log" &)
(cd "$BROWSER_WT" && TOWERFALL_WWW="$OUT/a/www" python3 -u tools/serve.py $PAGE_A > "$OUT/serve-a.log" 2>&1 &)
(cd "$BROWSER_WT" && TOWERFALL_WWW="$OUT/b/www" python3 -u tools/serve.py $PAGE_B > "$OUT/serve-b.log" 2>&1 &)
sleep 2
url() { echo "http://localhost:$1/?mute&nointro&autoplay&mods=$2&fortrise&tfexserver=ws://127.0.0.1:$SERVER_PORT&mode=online"; }
driver() { (cd "$BROWSER_WT" && node tools/netplay-driver.mjs --url "$(url "$1" "$2")" --players "$3" --port "$4" --no-cache > "$OUT/driver-$3.log" 2>&1 &); }

log() { curl -s "localhost:$1/$2/log?from=0"; }
k() { curl -s "localhost:$1/$2/key?k=$3" > /dev/null; sleep "${4:-0.6}"; }
wait_log() { # <driver port> <player> <pattern> <seconds>
  for _ in $(seq 1 "$4"); do log "$1" "$2" | grep -q "$3" && return 0; sleep 1; done
  return 1
}
fail() { log $DRV_A MA > "$OUT/MA.log"; log $DRV_B MB > "$OUT/MB.log" 2>/dev/null; echo "MODCHECK FAIL: $1 | logs $OUT"; exit 1; }
to_online() { # <driver port> <player>: past TF.EX's first-run tutorial onto its online menu
  wait_log "$1" "$2" "\[ModTest\] ready\|Installed mod TF.EX\|has been loaded" 180 || return 1
  sleep 10; k "$1" "$2" x
  wait_log "$1" "$2" "entering netplay" 40 || return 1
  sleep 3   # (the menu's items settle before keys)
}
# B looks at the lobby list and tries the first lobby: refused, never joined
try_join() { # <case>
  k $DRV_B MB ArrowRight; k $DRV_B MB ArrowRight; k $DRV_B MB c 4; k $DRV_B MB c 4
  log $DRV_B MB > "$OUT/MB-$1.log"
  grep -q "Can't join lobby because of its mod game mode" "$OUT/MB-$1.log" || fail "$1: B's lobby list didn't refuse the lobby for its mode"
  grep -q "Can't join lobby because of custom mod: NEEDS NETPLAYMODETEST 0.1.0" "$OUT/MB-$1.log" || fail "$1: B's join wasn't refused with NEEDS NETPLAYMODETEST 0.1.0"
  grep -q "joined .* as player\|Joined lobby" "$OUT/server.log" && fail "$1: B joined the lobby anyway"
  return 0
}

driver $PAGE_A "TF.EX,NetplayModeTest" MA $DRV_A
to_online $DRV_A MA || fail "page A didn't reach the online menu"
k $DRV_A MA ArrowRight; k $DRV_A MA ArrowRight; k $DRV_A MA c 3; k $DRV_A MA Enter 3    # LOBBIES, create
k $DRV_A MA ArrowRight; k $DRV_A MA ArrowRight; k $DRV_A MA ArrowRight                 # the first mod mode
k $DRV_A MA ArrowDown; k $DRV_A MA ArrowDown; k $DRV_A MA ArrowLeft; k $DRV_A MA Enter 4  # lobby size 2, create
grep -q "NetplayModeTest/ModTest" "$OUT/server.log" || sleep 3

driver $PAGE_A "TF.EX" MB $DRV_B
to_online $DRV_B MB || fail "page B (no mod) didn't reach the online menu"
try_join nomod
quit_b

driver $PAGE_B "TF.EX,NetplayModeTest" MB $DRV_B
to_online $DRV_B MB || fail "page B (mod 0.2.0) didn't reach the online menu"
log $DRV_B MB | grep -q "NetplayModeTest 0.2.0\|\[ModTest\] ready" || fail "page B didn't load the mod at 0.2.0"
try_join version
log $DRV_A MA > "$OUT/MA.log"
echo "MODCHECK PASS: without the mod and with 0.2.0, B is refused (NEEDS NETPLAYMODETEST 0.1.0) and never joins | logs $OUT"
