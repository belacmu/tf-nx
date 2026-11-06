#!/usr/bin/env bash
# A real mod's game mode online (docs/PLAN-MOD-MODES-ONLINE-2026-10-31.md), built from its source with our patch (each mod's branch
# `online`): two browser pages play it in a TF.EX lobby with a network delay so the match rolls back, or (SYNCTEST=1) one page runs
# TF.EX's sync test on it (GGRS rolls back CHECK frames every frame and compares the checksums). Logs only, one line:
#   <MOD> PASS|FAIL: <reason> | logs <dir>
#   PASS (two pages): synchronized, no GGRS desync, no exception or main-loop error, TF.State's saved states identical on every
#   compared frame after 0 (as tools/smoke.sh asks). PASS (sync test): no "Sync test desync detected", no exception or error.
#   tools/modmodes/realmod.sh moveordie|respawn|soccer|towerball
#   BUILD_ONLY=1 tools/modmodes/realmod.sh <mod>   (only the mod built and staged, ready to install: $OUT/stage/<mod>/<name>)
# Env: MOD_SRC (the mod's project folder, default below), FORTRISE (a full FortRise to build the mod against: TF.EX's libs/ lack what
#      mods use; default the Mac Steam install's), PLAY (seconds of play, default 60), NETDELAY (ms, default 40), SYNCTEST=1, CHECK
#      (sync test check distance, default 2), MAP, STATEDUMP (every Nth GGRS save dumped, default 60); MM_SERVER, BROWSER_WT as
#      two-pages.sh.
set -uo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)
MODS="$HOME/Code/Towerfall Switch Mods"
mod=${1:-}
# per mod: source project, the folder its meta.json and content are copied from, the mod's name when meta.json has none, the mode's
# id for the test command (_ for a space), the place in the lobby's GAME MODE list (rights from LMS), a team mode (1: each page picks
# a team), how the drivers play, the ports' base and the pages' letter
case "$mod" in
  moveordie) src="$MODS/tf-mod-fortrise-variant-moveordie/VSCode" files=../ModFile name=MoveOrDie id=MoveOrDie/Move_or_Die
             rights=3 team=0 play=keys base=79 L=M ;;
  respawn)   src="$MODS/tf-mod-fortrise-gamemode-respawn/VSCode" files=../ModFile name=Ebe1.Respawn id=Ebe1.Respawn/Ebe1.Respawn
             rights=3 team=0 play=upshot base=80 L=R ;;
  soccer)    src="$MODS/tf-mod-fortrise-gamemode-soccer/VSCode" files=../ModFile name=Soccer id=Soccer/SOCCER
             rights=3 team=1 play=soccer base=90 L=S ;;
  towerball) src="$MODS/ExampleFortRiseMod/TowerBall" files=. name=Suyooo.TowerBall id=Suyooo.TowerBall/TowerBall
             rights=3 team=1 play=keys base=74 L=T ;;
  *) echo "usage: tools/modmodes/realmod.sh moveordie|respawn|soccer|towerball"; exit 2 ;;
esac
TAG=$(echo "$mod" | tr a-z A-Z)
MOD_SRC=${MOD_SRC:-$src}
MM_SERVER=${MM_SERVER:-$MODS/tf-ex-mm-mod-modes/target/debug/tf-ex-matchmaking-server}
BROWSER_WT=${BROWSER_WT:-$HOME/Code/Towerfall Browser/.claude/worktrees/tfex-0281}
FORTRISE=${FORTRISE:-$HOME/Library/Application Support/Steam/steamapps/common/TowerFall/TowerFall.app/Contents/FortRise.app/Contents/MacOS}
OUT=${OUT:-$REPO/.devserve/$mod}
PLAY=${PLAY:-60} NETDELAY=${NETDELAY:-40}
SERVER_PORT=134$base PAGE_PORT=184$base DRV=197$base
A=${L}A B=${L}B S=${L}S
if [ -z "${BUILD_ONLY:-}" ]; then
  [ -x "$MM_SERVER" ] || { echo "$TAG FAIL: no server at $MM_SERVER"; exit 1; }
  "$HERE/build-page.sh" "$OUT" || { echo "$TAG FAIL: the page's build | logs $OUT"; exit 1; }
fi
mkdir -p "$OUT"

# the mod, built and added to the page's hosted mods (meta.json: FortRise reads the name from the build, the page from meta.json)
source "$BROWSER_WT/env.sh"
(cd "$MOD_SRC" && dotnet build -c Release -nologo -v q -clp:ErrorsOnly -p:ModLoaderPath="$FORTRISE" -p:ModEnablePublish=false \
  -p:ModEnableZip=false > "$OUT/mod-build.log" 2>&1) || { echo "$TAG FAIL: the mod's build | logs $OUT"; exit 1; }
stage="$OUT/stage/$mod/$name"
rm -rf "$OUT/stage/$mod" && mkdir -p "$stage"
cp -r "$MOD_SRC/bin/Release/net10.0/." "$stage/"
cp -R "$MOD_SRC/$files/meta.json" "$stage/"
[ -d "$MOD_SRC/$files/Content" ] && cp -R "$MOD_SRC/$files/Content" "$stage/"   # (the release zip's files)
rm -f "$stage"/*.deps.json "$stage"/*.pdb
python3 - "$stage/meta.json" "$name" <<'PY'
import json, sys
m = json.load(open(sys.argv[1])); m.setdefault("name", sys.argv[2])
json.dump(m, open(sys.argv[1], "w"), indent="\t")
PY
[ -n "${BUILD_ONLY:-}" ] && { echo "$TAG BUILT: $stage"; exit 0; }
(cd "$OUT/stage/$mod" && zip -q -r -X "$OUT/www/hosted-mods/$name.zip" .)
python3 - "$OUT/www/hosted-mods" "$stage/meta.json" "$name" <<'PY'
import hashlib, json, os, sys
d, meta, zipname = sys.argv[1], json.load(open(sys.argv[2])), sys.argv[3] + ".zip"
def deps(l): return [{"name": x.split(":")[0], "version": x.split(":")[1]} if isinstance(x, str) else x for x in l]
p = os.path.join(d, "hosted.json"); h = json.load(open(p))
b = open(os.path.join(d, zipname), "rb").read()
h["mods"].append({"hosted": True, "title": meta["name"], "author": meta.get("author", ""), "category": "Test", "page": "",
    "file": {"name": zipname, "size": len(b), "sha256": hashlib.sha256(b).hexdigest(), "mirror": "hosted-mods/" + zipname},
    "mods": [{"name": meta["name"], "version": meta["version"], "displayName": meta.get("displayName", meta["name"]), "dll": meta["dll"],
              "dependencies": deps(meta.get("dependencies", [])), "optionalDependencies": deps(meta.get("optionalDependencies", [])), "status": "experimental"}]})
json.dump(h, open(p, "w"), indent=1)
PY
[ $? = 0 ] || { echo "$TAG FAIL: the mod's hosted catalog entry | logs $OUT"; exit 1; }

cleanup() {
  curl -s "localhost:$DRV/quit" > /dev/null; sleep 2
  for p in $SERVER_PORT $PAGE_PORT; do lsof -ti tcp:$p -sTCP:LISTEN 2>/dev/null | xargs kill 2>/dev/null; done
  rm -rf ~/.towerfall-browser/netplay-$A-profile ~/.towerfall-browser/netplay-$B-profile ~/.towerfall-browser/netplay-$S-profile
}
trap cleanup EXIT
(PORT=$SERVER_PORT METRICS_PORT=$((SERVER_PORT + 11000)) RELAY_PORT=$((SERVER_PORT + 12000)) "$MM_SERVER" 2>&1 \
  | perl -pe 'BEGIN { $| = 1 } s/\e\[[0-9;]*m//g' > "$OUT/server.log" &)
(cd "$BROWSER_WT" && TOWERFALL_WWW="$OUT/www" python3 -u tools/serve.py $PAGE_PORT > "$OUT/serve.log" 2>&1 &)
sleep 2

log() { curl -s "localhost:$DRV/$1/log?from=0"; }
key() { curl -s "localhost:$DRV/$1/key?k=$2${3:+&hold=$3}" > /dev/null; }
k() { key "$1" "$2" "${3:-}"; sleep "${4:-0.6}"; }
ERRORS="An exception occurs\|Unhandled exception\|Error in MainLoop"
keys=(ArrowLeft ArrowRight c x ArrowUp ArrowDown z) vert=(ArrowUp ArrowDown c)
# one turn of play for page $1 (towards $2 for soccer: where the ball and the other team are)
turn() {
  key "$1" "${keys[$((RANDOM % ${#keys[@]}))]}" 200; sleep 0.2
  case "$play" in
    # an arrow shot straight up falls back on its shooter, still under it: deaths, so lives go down and players respawn
    upshot) (key "$1" ArrowUp 600 &); sleep 0.15; key "$1" x 120; sleep 1.5 ;;
    # run over the ball with up/down, jumps and charged kicks mixed in
    soccer) key "$1" "$2" 700; key "$1" "${vert[$((RANDOM % 3))]}" 250; key "$1" x $((RANDOM % 700 + 50)) ;;
  esac
}

if [ -n "${SYNCTEST:-}" ]; then
  cmd=$(python3 -c "import urllib.parse; print(urllib.parse.quote('test $id 0 ${MAP:-0} 42 ${CHECK:-2}'))")
  (cd "$BROWSER_WT" && node tools/netplay-driver.mjs --url "http://localhost:$PAGE_PORT/?mute&nointro&autoplay&mods=TF.EX,$name&fortrise&command=$cmd" \
    --players $S --port $DRV --no-cache > "$OUT/driver.log" 2>&1 &)
  for _ in $(seq 1 240); do log $S | grep -q "Starting test session" && break; sleep 1; done
  log $S | grep -q "Starting test session" || { log $S > "$OUT/$S.log"; echo "$TAG FAIL: the sync test didn't start | logs $OUT"; exit 1; }
  sleep 8
  end=$((SECONDS + PLAY))
  while [ $SECONDS -lt $end ]; do turn $S ArrowRight; [ "$play" = soccer ] && turn $S ArrowLeft; done
  log $S > "$OUT/$S.log"
  n=$(grep -c "Sync test desync detected" "$OUT/$S.log"); x=$(grep -c "$ERRORS" "$OUT/$S.log")
  [ "$n" -gt 0 ] && { echo "$TAG FAIL: sync test: $n desync(s), first: $(grep -m1 -A3 'Sync test desync detected' "$OUT/$S.log" | tr '\n' ' ' | cut -c1-300) | logs $OUT"; exit 1; }
  [ "$x" -gt 0 ] && { echo "$TAG FAIL: sync test: $x exception(s) or error(s), first: $(grep -m1 "$ERRORS" "$OUT/$S.log" | cut -c1-200) | logs $OUT"; exit 1; }
  echo "$TAG PASS: sync test (check distance ${CHECK:-2}), $PLAY s, no desync, no exception | logs $OUT"; exit 0
fi

(cd "$BROWSER_WT" && node tools/netplay-driver.mjs --url "http://localhost:$PAGE_PORT/?mute&nointro&autoplay&mods=TF.EX,$name&fortrise&tfexserver=ws://127.0.0.1:$SERVER_PORT&mode=online&netdelay=$NETDELAY&statedump=${STATEDUMP:-60}" \
  --players $A,$B --port $DRV --no-cache > "$OUT/driver.log" 2>&1 &)
wait_both() { for _ in $(seq 1 "$2"); do log $A | grep -q "$1" && log $B | grep -q "$1" && return 0; sleep 1; done; return 1; }
fail() { for P in $A $B; do log $P > "$OUT/$P.log"; curl -s "localhost:$DRV/$P/shot" -o "$OUT/$P.png"; done; echo "$TAG FAIL: $1 | logs $OUT"; exit 1; }

wait_both "$name" 180 || fail "the pages didn't load $name"
sleep 10
for P in $A $B; do k $P x; done
wait_both "entering netplay" 40 || fail "the pages didn't reach TF.EX's online menu"
sleep 3
k $A ArrowRight; k $A ArrowRight; k $A c "" 3; k $A Enter "" 3      # LOBBIES, create
for _ in $(seq 1 $rights); do k $A ArrowRight; done                   # GAME MODE: LMS -> ... -> the mod's mode
k $A ArrowDown; k $A ArrowDown; k $A ArrowLeft; k $A Enter "" 4       # lobby size 2, create
k $B ArrowRight; k $B ArrowRight; k $B c "" 4; k $B c "" 5             # LOBBIES, join the first lobby
[ "$team" = 1 ] && { k $A ArrowUp; k $B ArrowDown "" 2; }             # a team mode: A blue, B red (TF.EX's roll call: UP/DOWN)
k $A c; k $B c "" 3; k $A c                                            # ready, start
wait_both "Synchronized" 90 || fail "the session didn't synchronize"
end=$((SECONDS + PLAY))
while [ $SECONDS -lt $end ]; do turn $A ArrowRight & turn $B ArrowLeft; wait; done
for P in $A $B; do log $P > "$OUT/$P.log"; done
d=$(cat "$OUT/$A.log" "$OUT/$B.log" | grep -c "DesyncDetected")
x=$(cat "$OUT/$A.log" "$OUT/$B.log" | grep -c "$ERRORS")
[ "$d" -gt 0 ] && fail "$d GGRS desync report(s)"
[ "$x" -gt 0 ] && fail "$x exception(s) or error(s), first: $(cat "$OUT/$A.log" "$OUT/$B.log" | grep -m1 "$ERRORS" | cut -c1-200)"
python3 "$REPO/tools/tfex/statediff.py" "$OUT/$A.log" "$OUT/$B.log" --bytes > "$OUT/statediff.txt" 2>&1
first=$(grep -E '^  frame [0-9]+: ([0-9]+ fields differ|missing)' "$OUT/statediff.txt" | grep -v -m1 '^  frame 0:')
[ -n "$first" ] && fail "the saved states differ: ${first#  }; first: $(grep -A1 -m1 "^${first%%:*}:" "$OUT/statediff.txt" | tail -1 | sed 's/^ *//' | cut -c1-140) (statediff.txt)"
grep -q 'compared 0' "$OUT/statediff.txt" && fail "no common dumped frames (statediff.txt)"
python3 "$REPO/tools/tfex/statediff.py" --mods "$OUT/$A.log" > "$OUT/mods.txt" 2>&1
echo "$TAG PASS: $name online, $PLAY s of play with ${NETDELAY} ms delay, synchronized, no desync, the saved states identical ($(grep -o "compared [0-9]*" "$OUT/statediff.txt")), no exception; mods' state: $(head -1 "$OUT/mods.txt") | logs $OUT"
