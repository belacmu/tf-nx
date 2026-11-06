#!/bin/bash
# build_folder.sh <out dir>: Soccer's mod folder (sd:/xplay/mods/Soccer/): its release's files (ModFile: meta.json, Content/) from
# the vendored source (vendor/soccer), the version the module translates. The Switch ignores no DLL here: none is shipped.
set -eu
out=${1:?usage: build_folder.sh <out dir>}
here=$(cd "$(dirname "$0")" && pwd)
src="$here/../../vendor/soccer/ModFile"
[ -f "$src/meta.json" ] || { echo "soccer folder: no $src (git submodule update --init vendor/soccer)" >&2; exit 1; }
cmp -s "$src/meta.json" "$here/meta.json" || { echo "soccer folder: vendor/soccer's meta.json differs from mods/Soccer/meta.json" >&2; exit 1; }
rm -rf "$out"; mkdir -p "$out"
cp -R "$src/." "$out/"
echo "soccer folder: $out: Soccer $(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["version"])' "$src/meta.json")"
