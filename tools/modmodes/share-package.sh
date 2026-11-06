#!/usr/bin/env bash
# The private package for TF.EX's author (docs/PLAN-MOD-MODES-ONLINE-2026-10-31.md, handover step 4): the real mods we made playable
# online (Move or Die, Respawn, Soccer: ebe1.kenobi's, not ours to publish), built and as patches against their authors' branch, our
# test mod, and TF.EX's four mods built from the PR branch, with a README (tools/modmodes/share-README.md).
#   tools/modmodes/share-package.sh   -> .devserve/share/TF.EX-mod-modes-for-review.zip
# Env: TFEX_SRC (the TF.EX checkout on the PR branch, default ~/Code/TF.EX-mod-modes), as realmod.sh and build-page.sh.
set -uo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)
MODS="$HOME/Code/Towerfall Switch Mods"
TFEX_SRC=${TFEX_SRC:-$HOME/Code/TF.EX-mod-modes}
OUT=$REPO/.devserve/share
rm -rf "$OUT" && mkdir -p "$OUT/pkg/Mods" "$OUT/pkg/patches" "$OUT/pkg/source"

# TF.EX's four mods and the test mod, built from the PR branch (without Unmanaged/: the installed TF.EX keeps its native ggrs library)
TFEX_SRC="$TFEX_SRC" "$HERE/build-page.sh" "$OUT/build" > /dev/null || { echo "share-package: TF.EX's build failed (logs $OUT/build)"; exit 1; }
cp -R "$OUT/build/stage/tfex/." "$OUT/pkg/Mods/"
cp -R "$OUT/build/stage/mod/NetplayModeTest" "$OUT/pkg/Mods/"
(cd "$HERE" && git archive --format=tar HEAD NetplayModeTest | tar -x -C "$OUT/pkg/source")

# the real mods: built and staged by realmod.sh, and their change as one diff against their authors' branch
for m in moveordie:tf-mod-fortrise-variant-moveordie respawn:tf-mod-fortrise-gamemode-respawn soccer:tf-mod-fortrise-gamemode-soccer; do
  mod=${m%%:*} dir="$MODS/${m#*:}"
  line=$(BUILD_ONLY=1 OUT="$OUT/build-$mod" "$HERE/realmod.sh" "$mod" | tail -1)
  case "$line" in *" BUILT: "*) cp -R "${line#* BUILT: }" "$OUT/pkg/Mods/" ;; *) echo "share-package: $mod: $line"; exit 1 ;; esac
  git -C "$dir" diff "$(git -C "$dir" merge-base origin/HEAD HEAD)" HEAD > "$OUT/pkg/patches/$mod.patch"
done

tfex=$(git -C "$TFEX_SRC" rev-parse --short HEAD) branch=$(git -C "$TFEX_SRC" branch --show-current)
sed -e "s/@TFEX@/$tfex/g" -e "s/@BRANCH@/$branch/g" "$HERE/share-README.md" > "$OUT/pkg/README.md"
rm -f "$OUT/TF.EX-mod-modes-for-review.zip"
(cd "$OUT/pkg" && zip -q -r -X "$OUT/TF.EX-mod-modes-for-review.zip" .)
echo "share-package: $OUT/TF.EX-mod-modes-for-review.zip ($(du -h "$OUT/TF.EX-mod-modes-for-review.zip" | cut -f1); TF.EX $branch $tfex)"
