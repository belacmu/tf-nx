#!/bin/bash
# method-errors.sh Type.Method...: the generated code of those methods and their compile errors (after compile-gen.sh)
d="$(cd "$(dirname "$0")" && pwd)/out/compile/program/tfex/gen"
for m in "$@"; do
  f=$(grep -l "/\*@M $m\*/" "$d"/*.cpp | head -1); [ -z "$f" ] && { echo "$m: not found"; continue; }
  echo "=== $m ($(basename "$f"))"
  awk -v m="/*@M $m*/" 'index($0,"/*@M ")==1{on=($0==m)} on{print NR": "$0}' "$f" | head -${LINES_MAX:-25}
  [ -f "$f.err" ] && grep -E "^.*$(basename "$f"):[0-9]+:[0-9]+: error" "$f.err" | while IFS=: read -r _ ln col rest; do
    awk -v m="/*@M $m*/" -v L="$ln" 'index($0,"/*@M ")==1{on=($0==m)} NR==L&&on{print "  ERR " L ":" }' "$f"
  done | sort -u | head -5
  [ -f "$f.err" ] && awk -v m="/*@M $m*/" 'index($0,"/*@M ")==1{on=($0==m)} on{print NR}' "$f" > /tmp/.lines_$$ && grep -E "$(basename "$f"):($(paste -sd'|' /tmp/.lines_$$)):" "$f.err" | sed 's/.*gen\///' | cut -c1-220 | head -6; rm -f /tmp/.lines_$$
done
