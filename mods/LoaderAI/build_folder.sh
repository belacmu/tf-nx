#!/bin/bash
# build_folder.sh <out dir>: LoaderAI's mod folder (sd:/xplay/mods/LoaderAI/): its release's files (ModFile: meta.json) from the vendored source
# (vendor/loader-ai), the version the module translates. No DLL is shipped: the module carries its translated code.
set -eu
out=${1:?usage: build_folder.sh <out dir>}
here=$(cd "$(dirname "$0")" && pwd)
src="$here/../../vendor/loader-ai/ModFile"
[ -f "$src/meta.json" ] || { echo "loaderai folder: no $src (git submodule update --init vendor/loader-ai)" >&2; exit 1; }
cmp -s "$src/meta.json" "$here/meta.json" || { echo "loaderai folder: vendor/loader-ai's meta.json differs from mods/LoaderAI/meta.json" >&2; exit 1; }
rm -rf "$out"; mkdir -p "$out"
cp -R "$src/." "$out/"
echo "loaderai folder: $out: LoaderAI $(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["version"])' "$src/meta.json")"
