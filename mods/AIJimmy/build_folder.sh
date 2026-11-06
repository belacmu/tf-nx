#!/bin/bash
# build_folder.sh <out dir>: AIJimmy's mod folder (sd:/xplay/mods/AIJimmy/): its release's files (ModFile: meta.json) from the vendored source
# (vendor/ai-jimmy), the version the module translates. No DLL is shipped: the module carries its translated code.
set -eu
out=${1:?usage: build_folder.sh <out dir>}
here=$(cd "$(dirname "$0")" && pwd)
src="$here/../../vendor/ai-jimmy/ModFile"
[ -f "$src/meta.json" ] || { echo "aijimmy folder: no $src (git submodule update --init vendor/ai-jimmy)" >&2; exit 1; }
cmp -s "$src/meta.json" "$here/meta.json" || { echo "aijimmy folder: vendor/ai-jimmy's meta.json differs from mods/AIJimmy/meta.json" >&2; exit 1; }
rm -rf "$out"; mkdir -p "$out"
cp -R "$src/." "$out/"
echo "aijimmy folder: $out: AIJimmy $(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["version"])' "$src/meta.json")"
