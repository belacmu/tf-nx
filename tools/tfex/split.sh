#!/bin/bash
# The splitter's member list for TF.EX at <tag> (tools/tfex/splitter): prints the path of the JSON-lines file.
# Usage: tools/tfex/split.sh <tag|commit>. Cached by commit in .cache/tfex/: split-<sha>.jsonl and the tree src-<sha>/ (the
# report reads member source from it). Builds the splitter on first use (dotnet; NuGet restore of Microsoft.CodeAnalysis.CSharp).
set -e
here="$(cd "$(dirname "$0")" && pwd)"; repo="$(cd "$here/../.." && pwd)"
dotnet="${DOTNET:-$(command -v dotnet || echo /usr/local/share/dotnet/dotnet)}"
"$here/ensure-vendor.sh" "$1" || exit 1   # an uninitialized vendor/tfex (a fresh worktree) is set up first
sha="$(git -C "$repo/vendor/tfex" rev-parse --verify -q "$1^{commit}")" || { echo "split.sh: unknown TF.EX tag/commit '$1' (git -C vendor/tfex fetch --tags?)" >&2; exit 1; }
cache="$repo/.cache/tfex"; mkdir -p "$cache"
out="$cache/split-$sha.jsonl"; tree="$cache/src-$sha"
dll="$here/splitter/bin/out/splitter.dll"
if [ ! -f "$dll" ] || [ -n "$(find "$here/splitter" -maxdepth 1 -newer "$dll" -name '*.cs*')" ]; then
  "$dotnet" build -c Release -o "$here/splitter/bin/out" "$here/splitter" >/dev/null || { echo "split.sh: splitter build failed" >&2; exit 1; }
  rm -f "$cache"/split-*.jsonl
fi
if [ ! -d "$tree" ]; then mkdir -p "$tree.tmp" && git -C "$repo/vendor/tfex" archive "$sha" src | tar -x -C "$tree.tmp" && mv "$tree.tmp" "$tree"; fi
if [ ! -f "$out" ]; then "$dotnet" "$dll" "$tree/src" "$out.tmp" 2>/dev/null; mv -f "$out.tmp" "$out"; fi
echo "$out"
