#!/bin/bash
# build-folders.sh <mods dir>: every mod folder our tools build (mods/<name>/mod.toml [folder] build: third-party files, never in git) into
# <mods dir>/<name> (an SD card's sd:/xplay/mods, or the console channel's mods/). XPLAY_EXTRAS=0 (the "TF.EX Switch" build): the
# extras-only folders are removed instead; XPLAY_MODFOLDERS=0 (tests: a boot without them): every one is removed. One line per folder.
set -eu
here=$(cd "$(dirname "$0")/../.." && pwd)
out=${1:?usage: build-folders.sh <mods dir>}
mkdir -p "$out"
python3 "$here/tools/mods/catalog.py" --folders | while IFS=$'\t' read -r folder extras dir cmd; do
  if [ "${XPLAY_MODFOLDERS:-1}" = 0 ] || { [ "$extras" = 1 ] && [ "${XPLAY_EXTRAS:-1}" = 0 ]; }; then
    rm -rf "${out:?}/$folder"; echo "mod folders: $folder removed (XPLAY_MODFOLDERS=0, or not in this build)"; continue
  fi
  (cd "$dir" && $cmd "$out/$folder") || { echo "mod folders: $folder FAILED" >&2; exit 1; }
done
