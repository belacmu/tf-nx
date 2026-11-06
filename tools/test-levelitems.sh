#!/bin/bash
# test-levelitems.sh [seeds per level, default 40]: the C++ AddItems port (levelitems_core.hpp) against tools/re/pcgen.py (the PC's
# GenerateData + AddItems, which reproduces the browser's grid) over every PC Cataclysm level: draws, entities, final grid.
set -u
here=$(cd "$(dirname "$0")/.." && pwd); cd "$here"
n=${1:-40}
dw="$HOME/Code/Towerfall Browser/gamefiles/TowerFall/DarkWorldContent/Levels/Versus/15 - Cataclysm"
bin=${TMPDIR:-/tmp}/test-levelitems
clang++ -std=c++20 -O2 -Wall -o "$bin" tools/test-levelitems.cpp || exit 1
ok=0; bad=0
for f in "$dw"/*.oel; do
  for k in $(seq 1 $n); do
    seed=$(( (k * 2654435761 + 109144726) % 2147483647 ))
    ref=$(python3 tools/re/pcgen.py "$f" $seed --state --items 2>&1 >/dev/null)
    want=$(printf '%s\n' "$ref" | grep -v '^state\|^grid' | sed 's/^generate draws [0-9]* //')
    got=$(printf '%s\n' "$ref" | grep '^state\|^grid' | "$bin")
    if [ "$want" = "$got" ]; then ok=$((ok + 1)); else
      bad=$((bad + 1)); [ $bad -le 3 ] && { echo "MISMATCH $(basename "$f") seed $seed"; diff <(echo "$want") <(echo "$got") | head -8; }
    fi
  done
done
echo "levelitems: $ok match, $bad differ ($(ls "$dw"/*.oel | wc -l | tr -d ' ') levels x $n seeds)"
[ $bad = 0 ]
