#!/usr/bin/env bash
# A screenshot of TF.EX in a browser page, for a PR's before/after or a doc (docs/SCREENSHOTS.md: the rules). Builds the page from a
# TF.EX commit in a throwaway worktree (test patches applied there, never in the commit), opens it headless, picks STANDARD or WIDER
# in VERSUS, runs a TF.EX console command, waits until the game draws again after the command's log line, and saves the shot.
#   tools/pageshot/shot.sh <name> <TF.EX commit> "<command>" [until regex]
#   e.g. PAGESHOT_WIDE=1 PAGESHOT_PATCHES=team-spawn-log tools/pageshot/shot.sh before upstream/main "test TDM 0 2" '\[pageshot\] tower'
# Prints one line: PAGESHOT <name> OK|FAIL: <reason> | shot <png> | log <file>
# Env: PAGESHOT_WIDE=1 (WIDER in VERSUS, and patches/wide-test-tower: TF.EX's test command on WiderSet's tower of that name),
#      PAGESHOT_PATCHES (more patches/<name>.patch, space-separated: log lines to prove what the shot shows),
#      TFEX_REPO (the TF.EX clone, default ~/Code/Towerfall Switch Mods/tfex-eight-players), OUT (default .devserve/pageshot),
#      BROWSER_WT (default tfex-0281), PAGESHOT_PORT (page; the driver +1300, default 18481),
#      PAGESHOT_WAIT (seconds for the log line after the command, default 180: a long command such as the scenario catalog needs more).
set -uo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)
name=$1 ref=$2 cmd=$3 until=${4:-}
TFEX_REPO=${TFEX_REPO:-$HOME/Code/Towerfall Switch Mods/tfex-eight-players}
BROWSER_WT=${BROWSER_WT:-$HOME/Code/Towerfall Browser/.claude/worktrees/tfex-0281}
OUT=${OUT:-$REPO/.devserve/pageshot}
PP=${PAGESHOT_PORT:-18481}; DRV=$((PP + 1300)); P=PS$PP
WT="$OUT/tfex-$name" LOG="$OUT/$name.log" SHOT="$OUT/$name.png"
mkdir -p "$OUT"
fail() { echo "PAGESHOT $name FAIL: $1 | shot $SHOT | log $LOG"; exit 1; }
# one browser page of our own at a time (AGENTS.md): the port pair must be free
for p in $PP $DRV; do lsof -ti tcp:$p -sTCP:LISTEN > /dev/null && fail "port $p in use (another pageshot run?)"; done

# the TF.EX source: a detached worktree of <ref>, with the test patches
git -C "$TFEX_REPO" worktree remove --force "$WT" 2> /dev/null; rm -rf "$WT"
git -C "$TFEX_REPO" worktree add -q --detach "$WT" "$ref" || fail "no TF.EX commit $ref in $TFEX_REPO"
patches=${PAGESHOT_PATCHES:-}; [ "${PAGESHOT_WIDE:-0}" = 1 ] && patches="wide-test-tower $patches"
for p in $patches; do git -C "$WT" apply "$HERE/patches/$p.patch" || fail "patch $p doesn't apply on $ref"; done
TFEX_SRC="$WT" BROWSER_WT="$BROWSER_WT" "$REPO/tools/modmodes/build-page.sh" "$OUT/page-$name" > /dev/null || fail "the page's build (logs $OUT/page-$name)"

cleanup() {
  curl -s "localhost:$DRV/quit" > /dev/null; sleep 2
  lsof -ti tcp:$PP -sTCP:LISTEN 2> /dev/null | xargs kill 2> /dev/null
  rm -rf ~/.towerfall-browser/netplay-$P-profile
  git -C "$TFEX_REPO" worktree remove --force "$WT" 2> /dev/null
}
trap cleanup EXIT
rm -rf ~/.towerfall-browser/netplay-$P-profile   # a fresh profile: TF.EX's first-run tutorial, then the main menu
(cd "$BROWSER_WT" && TOWERFALL_WWW="$OUT/page-$name/www" exec python3 -u tools/serve.py $PP > "$OUT/$name-serve.log" 2>&1 &)
sleep 2
(cd "$BROWSER_WT" && node tools/netplay-driver.mjs --url "http://localhost:$PP/?mute&nointro&autoplay&mods=TF.EX,Teuria.WiderSet&fortrise" \
  --players $P --port $DRV --no-cache > "$OUT/$name-driver.log" 2>&1 &)
console() { curl -s "localhost:$DRV/$P/console?from=0"; }
k() { curl -s "localhost:$DRV/$P/key?k=$1" > /dev/null; sleep "${2:-1.5}"; }
for _ in $(seq 1 180); do console 2> /dev/null | grep -q "SOUND LOADING" && break; sleep 1; done
console | grep -q "SOUND LOADING" || fail "the page didn't load in 180 s"
sleep 8
k x 3                                                       # skip TF.EX's first-run tutorial
k c 2                                                       # VERSUS: the STANDARD / WIDER picker (WiderSet)
[ "${PAGESHOT_WIDE:-0}" = 1 ] && k ArrowRight
k c 3                                                       # the roll call: WiderSet's IsWide is set
curl -s "localhost:$DRV/$P/eval" --data "towerfallCommand(\"$cmd\")" > /dev/null
mark=${until:-\[command\] }
for _ in $(seq 1 "${PAGESHOT_WAIT:-180}"); do console | grep -q "$mark" && break; sleep 1; done
console | grep -q "$mark" || { console > "$LOG"; fail "never logged '$mark' within ${PAGESHOT_WAIT:-180} s of the command"; }
# a command that loads a level holds the page in one long frame (35 s to minutes): shoot once it draws at 50+ frames/s again after
# the line, or fail (a shot taken before shows the screen before the command)
drawn() { console | M="$mark" awk '$0 ~ ENVIRON["M"] {f=1} f && /\[perf\] [5-9][0-9]\.[0-9] frames/ {ok=1} END {exit !ok}'; }
for _ in $(seq 1 300); do drawn && break; sleep 1; done
drawn || { console > "$LOG"; fail "the game didn't draw at 50+ frames/s within 300 s of '$mark'"; }
sleep 2
curl -s "localhost:$DRV/$P/shot" -o "$SHOT" || fail "no screenshot"
console > "$LOG"
grep -qiE "unhandled|exception" "$LOG" && fail "an exception in the page"
echo "PAGESHOT $name OK: $(grep -m1 -E "$mark" "$LOG" | cut -c1-200) | shot $SHOT | log $LOG"
