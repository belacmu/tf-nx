#!/bin/bash
# build_folder.sh <out dir>: Teams+'s mod folder (sd:/xplay/mods/TeamsPlus/, mods/TeamsPlus/mod.toml): its GitHub release's TeamsPlus.zip
# as it ships (the Switch ignores the DLL: its logic is translated into the module), the release of the version in
# mods/TeamsPlus/meta.json (tools/mods/update.sh sets it), downloaded once into .cache/mods/. XPLAY_TEAMPLUS=<a zip>: that one instead
# (its version must match too).
set -eu
out=${1:?usage: build_folder.sh <out dir>}
here=$(cd "$(dirname "$0")" && pwd)
read -r want repo < <(python3 -c 'import json,sys; m=json.load(open(sys.argv[1])); print(m["version"], m["update"]["github"]["repository"])' "$here/meta.json")
zip=${XPLAY_TEAMPLUS:-}
if [ -z "$zip" ]; then
  cache="$here/../../.cache/mods"; zip="$cache/TeamsPlus-$want.zip"
  if [ ! -f "$zip" ]; then
    mkdir -p "$cache"
    gh release download "v$want" -R "$repo" -p TeamsPlus.zip -O "$zip.part" --clobber >/dev/null 2>&1 && mv "$zip.part" "$zip" ||
      { rm -f "$zip.part"; echo "teamplus folder: can't download $repo release v$want's TeamsPlus.zip (gh)" >&2; exit 1; }
  fi
fi
have=$(unzip -p "$zip" meta.json | python3 -c 'import json,sys; print(json.load(sys.stdin)["version"])')
[ "$want" = "$have" ] || { echo "teamplus folder: $zip is Teams+ $have, the module carries $want" >&2; exit 1; }
rm -rf "$out"; mkdir -p "$out"
unzip -q "$zip" -d "$out"
echo "teamplus folder: $out: Teams+ $have (from $zip)"
