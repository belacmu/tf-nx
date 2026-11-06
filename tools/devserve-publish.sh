#!/bin/bash
# devserve-publish.sh <checkout> [label]: what tools/devserver.py offers the console, from ONE checkout (a verified build), so the console
# never gets a build that wasn't tested: that checkout's mod (hw-probe/exlaunch/deploy), its PC-levels layout (its tools/pc-levels.sh),
# and its tools/devserve.list (paths rewritten into the channel), in .devserve/channel/. The server reads .devserve/channel/devserve.list.
set -eu
here=$(cd "$(dirname "$0")/.." && pwd)
src=$(cd "${1:?checkout}" && pwd); label=${2:-$(git -C "$src" log -1 --format='%h %s' | cut -c1-80)}
ch="$here/.devserve/channel"; tmp="$ch.new"
rm -rf "$tmp"; mkdir -p "$tmp"
cp "$src/hw-probe/exlaunch/deploy/subsdk9" "$src/hw-probe/exlaunch/deploy/main.npdm" "$tmp/"
# (no PC levels since 2026-11-05: the module makes them from the Switch's own and its delta, pclevels.cpp)
mkdir -p "$tmp/romfs"
# the megamod's game data (TowerBall's levels and art, 8-player's levels: tools/megamod/romfs.py; local play only)
[ -f "$src/tools/megamod/romfs.py" ] && python3 "$src/tools/megamod/romfs.py" "$tmp/romfs" >/dev/null
sed -e "s#hw-probe/exlaunch/deploy/#.devserve/channel/#; s#\.devserve/romfs/#.devserve/channel/romfs/#" "$src/tools/devserve.list" > "$tmp/devserve.list"
# the mod folders our tools build (mods/<name>/mod.toml [folder]), in mods/, one sd:/xplay/mods/<name>/ line each
if [ -x "$src/tools/mods/build-folders.sh" ]; then
  "$src/tools/mods/build-folders.sh" "$tmp/mods" >/dev/null
  for d in "$tmp/mods"/*/; do [ -d "$d" ] && echo "sd:/xplay/mods/$(basename "$d")/ .devserve/channel/mods/$(basename "$d")/" >> "$tmp/devserve.list"; done
fi
echo "$label" > "$tmp/VERSION"
rm -rf "$ch"; mv "$tmp" "$ch"
echo "devserve-publish: $(cat "$ch/VERSION") -> $ch ($(find "$ch/romfs" -type f | wc -l | tr -d ' ') romfs files)"
