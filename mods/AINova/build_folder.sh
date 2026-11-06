#!/bin/bash
# build_folder.sh <out dir>: AINova's mod folder (sd:/xplay/mods/AINova/): its release's files (ModFile: meta.json) from the vendored source
# (vendor/ai-nova), the version the module translates. No DLL is shipped: the module carries its translated code.
set -eu
out=${1:?usage: build_folder.sh <out dir>}
here=$(cd "$(dirname "$0")" && pwd)
src="$here/../../vendor/ai-nova/ModFile"
[ -f "$src/meta.json" ] || { echo "ainova folder: no $src (git submodule update --init vendor/ai-nova)" >&2; exit 1; }
cmp -s "$src/meta.json" "$here/meta.json" || { echo "ainova folder: vendor/ai-nova's meta.json differs from mods/AINova/meta.json" >&2; exit 1; }
rm -rf "$out"; mkdir -p "$out"
cp -R "$src/." "$out/"
echo "ainova folder: $out: AINova $(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["version"])' "$src/meta.json")"
