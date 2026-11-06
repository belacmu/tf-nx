#!/bin/bash
# build_folder.sh <out dir>: Teams+'s mod folder (sd:/xplay/mods/TeamsPlus/, mods/TeamsPlus/mod.toml): its PC release zip as it ships
# (the Switch ignores the DLL: its logic is translated into the module). From XPLAY_TEAMPLUS=<the zip>, else the PC repository's
# release/TeamsPlus.zip next to this checkout (../three-team-mod/pc-fortrise: ./build.sh Release). Its version must be the one in
# mods/TeamsPlus/meta.json (the logic the module carries), so a stale zip stops here.
set -eu
out=${1:?usage: build_folder.sh <out dir>}
here=$(cd "$(dirname "$0")" && pwd)
zip=${XPLAY_TEAMPLUS:-}
if [ -z "$zip" ]; then
  d=$here; while [ "$d" != / ] && [ ! -d "$d/three-team-mod/pc-fortrise" ]; do d=$(dirname "$d"); done
  zip="$d/three-team-mod/pc-fortrise/release/TeamsPlus.zip"
fi
[ -f "$zip" ] || { echo "teamplus folder: no release zip at $zip (XPLAY_TEAMPLUS=<TeamsPlus.zip>)" >&2; exit 1; }
want=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["version"])' "$here/meta.json")
have=$(unzip -p "$zip" meta.json | python3 -c 'import json,sys; print(json.load(sys.stdin)["version"])')
[ "$want" = "$have" ] || { echo "teamplus folder: $zip is Teams+ $have, the module carries $want (rebuild the release)" >&2; exit 1; }
rm -rf "$out"; mkdir -p "$out"
unzip -q "$zip" -d "$out"
echo "teamplus folder: $out: Teams+ $have (from $zip)"
