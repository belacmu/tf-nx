#!/bin/bash
# pc-levels.sh <romfs root> [<PC Content dir> [<PC DarkWorldContent dir>]]: puts the PC game's versus levels under <romfs root> at
# Content/Levels/PCVersus/<tower>/ (not over the Switch's Content/Levels/Versus: offline modes keep the Switch's own levels), and the
# mod loads those in online mode only (pcgameplay.cpp), so the Switch plays exactly the levels TF.EX (the browser) plays. 40 of the 141 versus levels differ between the Switch's 1.4.1.6 data and the PC's
# (chests, spawners, orbs, jump pads, chain lengths...), which desyncs any round on one of them. Copied from the user's own PC files at
# install time (never redistributed); default source = the browser's game files (what its TF.EX loads), else the Steam install.
# A marker file .xplay-pc-levels marks a romfs made by this script (hw-probe/make-sd.sh restore removes such a romfs).
set -eu
dst=${1:?romfs root}
src=${2:-}
for c in "$src" "$HOME/Code/Towerfall Browser/gamefiles/TowerFall/TowerFall.app/Contents/Resources/Content" \
         "$HOME/Library/Application Support/Steam/steamapps/common/TowerFall/TowerFall.app/Contents/Resources/Content"; do
  [ -n "$c" ] && [ -d "$c/Levels/Versus" ] && { src=$c; break; }
done
[ -d "$src/Levels/Versus" ] || { echo "pc-levels: no PC Content/Levels/Versus found (pass the PC Content dir)" >&2; exit 1; }
mkdir -p "$dst/Content/Levels"
# older installs put the PC levels over the Switch's: remove them (only from a romfs this script made, see the marker below)
[ -f "$dst/.xplay-pc-levels" ] && rm -rf "$dst/Content/Levels/Versus"
# no macOS metadata on the card: on FAT (the SD card) copies get "._<name>" AppleDouble files, which the game takes for towers and
# levels and crashes reading (GameData.Load, hardware 2026-10-16); COPYFILE_DISABLE stops most, the find removes the rest
COPYFILE_DISABLE=1 rsync -a --delete --exclude '._*' "$src/Levels/Versus/" "$dst/Content/Levels/PCVersus/"
# The PC keeps the Dark World towers' versus levels (12 The Amaranth .. 15 Cataclysm: Content/Levels/Versus has only their tower.xml)
# in DarkWorldContent/Levels/Versus (GameData.GetDarkWorldDirectory), next to TowerFall.app; the Switch has its own (1.4.1.6) ones in
# Content/Levels/Versus; the PC's go to PCVersus with the others, loaded in online mode (they differ in 51 of 51: ChalicePads, chests,
# spawners, crumble blocks; the ChalicePads change Cataclysm's generated items, LevelRandomItems.AddItems).
dw=${3:-$src/../../../../DarkWorldContent}
[ -d "$dw/Levels/Versus" ] || { echo "pc-levels: no PC DarkWorldContent/Levels/Versus at $dw (pass it as the 3rd argument)" >&2; exit 1; }
for t in "$dw/Levels/Versus"/*/; do
  n=$(basename "$t"); [ -d "$dst/Content/Levels/PCVersus/$n" ] || { echo "pc-levels: no tower $n in the PC versus levels" >&2; exit 1; }
  COPYFILE_DISABLE=1 rsync -a --exclude '._*' --exclude 'tower.xml' "$t" "$dst/Content/Levels/PCVersus/$n/"
done
# TF.EX's wide set (WiderSet 0.9.0's levels) is no longer copied here: it is read from WiderSet's own folder, sd:/xplay/mods/WiderSet
# (the catalog mod Teuria.WiderSet, tools/mods/widerset_folder.sh; docs/PLAN-TFNX-2026-11-04.md). An older romfs's copy goes.
rm -rf "$dst/Content/Levels/PCWide"
find "$dst" -name '._*' -delete 2>/dev/null || true
date '+%F %T' > "$dst/.xplay-pc-levels"
echo "pc-levels: $(find "$dst/Content/Levels/PCVersus" -name '*.oel' | wc -l | tr -d ' ') PC versus levels -> $dst/Content/Levels/PCVersus"
