#!/bin/bash
# build_folder.sh <out dir>: WiderSet's mod folder (sd:/xplay/mods/WiderSet/, the catalog mod Teuria.WiderSet: mods/Teuria.WiderSet/mod.toml):
# the player's own WiderSet 0.9.0 download as FortRise reads it (meta.json, Content/: WideLevels, the 8-player images, its BG data).
# GameBanana doesn't allow redistribution: from XPLAY_WIDERSET=<the unzipped mod's folder>, else .cache/widerset-0.9.0 (git-ignored) of
# this checkout or the main one. Its wide levels must be the ones in hw-probe/exlaunch/source/program/widelevels.hpp (TF.EX's wide set, in
# FortRise's order: a different list desyncs wide lobbies), so a different download stops here.
set -eu
out=${1:?usage: build_folder.sh <out dir>}
repo=$(cd "$(dirname "$0")/../.." && pwd)
ws=${XPLAY_WIDERSET:-}
if [ -z "$ws" ]; then   # this checkout's .cache, else the main checkout's (worktrees share it)
  main=$(cd "$repo" && cd "$(git rev-parse --git-common-dir)/.." 2>/dev/null && pwd || echo "$repo")
  for c in "$repo/.cache/widerset-0.9.0" "$main/.cache/widerset-0.9.0"; do [ -d "$c/Content/WideLevels/Versus" ] && { ws=$c; break; }; done
fi
[ -n "$ws" ] && [ -f "$ws/meta.json" ] && [ -d "$ws/Content/WideLevels/Versus" ] ||
  { echo "widerset folder: no WiderSet 0.9.0 download (XPLAY_WIDERSET=<its unzipped folder>)" >&2; exit 1; }
wl="$ws/Content/WideLevels/Versus"
table="$repo/hw-probe/exlaunch/source/program/widelevels.hpp"
want=$(grep -E '^    \{ "' "$table" | sed -E 's/^    \{ "([^"]*)", \{ (.*) \} \},$/\1|\2/; s/"//g; s/, /,/g')
have=$(cd "$wl" && for t in */; do t=${t%/}; printf '%s|%s\n' "$t" "$(ls "$t" | grep '\.oel$' | LC_ALL=C sort | sed 's/\.oel$//' | paste -sd, -)"; done)
[ "$want" = "$have" ] || { echo "widerset folder: the wide levels at $wl differ from widelevels.hpp:" >&2; diff <(echo "$want") <(echo "$have") >&2; exit 1; }
rm -rf "$out"; mkdir -p "$out"
COPYFILE_DISABLE=1 rsync -a --exclude '._*' --exclude '*.pdb' "$ws/meta.json" "$ws/Content" "$out/"
echo "widerset folder: $out: $(grep -c '' <<<"$want") towers, $(find "$out/Content/WideLevels/Versus" -name '*.oel' | wc -l | tr -d ' ') wide levels (from $ws)"
