#!/bin/bash
# test-levelgen.sh [seeds per level, default 20]: the C++ GenerateData + AddItems ports (levelgeom_core.hpp, levelitems_core.hpp)
# against tools/re/pcgen.py over every Cataclysm level: the PC's standard set (the browser's game files) and WiderSet's wide set
# (pcgen --wide: WiderSet's IL constant remaps; docs/ws-wide.md). Draw counts, entities and the final grid must be the same.
set -u
here=$(cd "$(dirname "$0")/.." && pwd); cd "$here"
n=${1:-20}
std="$HOME/Code/Towerfall Browser/gamefiles/TowerFall/DarkWorldContent/Levels/Versus/15 - Cataclysm"
main=$(cd "$(git rev-parse --git-common-dir)/.." && pwd)
wide=${XPLAY_WIDERSET:-$main/.cache/widerset-0.9.0}/Content/WideLevels/Versus/"15 - Cataclysm"
bin=${TMPDIR:-/tmp}/test-levelgen
clang++ -std=c++20 -O2 -Wall -o "$bin" tools/test-levelgen.cpp || exit 1
total=0; bad=0
for set in std wide; do
  if [ $set = std ]; then dir=$std; flag=; else dir=$wide; flag=--wide; fi
  ok=0; sbad=0
  for f in "$dir"/*.oel; do
    for k in $(seq 1 $n); do
      seed=$(( (k * 2654435761 + 109144726) % 2147483647 ))
      out=$(python3 tools/re/pcgen.py "$f" $seed --input --items $flag 2>&1 >/dev/null)
      want=$(printf '%s\n' "$out" | grep -v '^seed\|^wide\|^rect\|^base')
      got=$(printf '%s\n' "$out" | grep '^seed\|^wide\|^rect\|^base' | "$bin")
      if [ "$want" = "$got" ]; then ok=$((ok + 1)); else
        sbad=$((sbad + 1)); [ $sbad -le 3 ] && { echo "MISMATCH ($set) $(basename "$f") seed $seed"; diff <(echo "$want") <(echo "$got") | head -8; }
      fi
    done
  done
  echo "levelgen $set: $ok match, $sbad differ ($(ls "$dir"/*.oel | wc -l | tr -d ' ') levels x $n seeds)"
  bad=$((bad + sbad))
done
[ $bad = 0 ]
