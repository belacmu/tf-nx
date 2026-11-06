#!/bin/bash
# build_folder.sh <out dir>: Move or Die's mod folder (sd:/xplay/mods/MoveOrDie/): its release's files (ModFile: meta.json, Content/) from
# the vendored source (vendor/moveordie), the version the module translates. The Switch ignores no DLL here: none is shipped.
set -eu
out=${1:?usage: build_folder.sh <out dir>}
here=$(cd "$(dirname "$0")" && pwd)
src="$here/../../vendor/moveordie/ModFile"
[ -f "$src/meta.json" ] || { echo "moveordie folder: no $src (git submodule update --init vendor/moveordie)" >&2; exit 1; }
cmp -s "$src/meta.json" "$here/meta.json" || { echo "moveordie folder: vendor/moveordie's meta.json differs from mods/MoveOrDie/meta.json" >&2; exit 1; }
rm -rf "$out"; mkdir -p "$out"
cp -R "$src/." "$out/"
echo "moveordie folder: $out: Move or Die $(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["version"])' "$src/meta.json")"
