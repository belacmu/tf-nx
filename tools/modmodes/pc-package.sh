#!/usr/bin/env bash
# A package for testing the TF.EX fork's mod modes on a Windows PC with FortRise and TF.EX v0.28.1 installed (the user's): TF.EX's four
# mods built from the fork (without Unmanaged/: the installed TF.EX keeps its Windows ggrs_ffi) and the test mod, with the steps.
#   tools/modmodes/pc-package.sh   -> .devserve/pc/TF.EX-mod-modes-pc.zip
# Env: as build-page.sh.
set -uo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)
OUT=$REPO/.devserve/pc
"$HERE/build-page.sh" "$OUT/build" > /dev/null || { echo "pc-package: the build failed (logs $OUT/build)"; exit 1; }
rm -rf "$OUT/pkg" && mkdir -p "$OUT/pkg/Mods"
cp -R "$OUT/build/stage/tfex/." "$OUT/pkg/Mods/"
cp -R "$OUT/build/stage/mod/NetplayModeTest" "$OUT/pkg/Mods/"
ip=$(ipconfig getifaddr en0 2>/dev/null || echo "<this Mac's IP>")
cat > "$OUT/pkg/README.txt" <<EOF
TF.EX fork (belacmu/TF.EX, mod-modes-online at $(git -C "${TFEX_SRC:-$HOME/Code/TF.EX-mod-modes}" rev-parse --short HEAD)): mod versus modes online, to test on a PC.

1. Back up your TowerFall Mods folder (FortRise's: next to the game, Mods\\).
2. Copy the folders in Mods\\ here over your Mods\\: DShad.TF.EX, DShad.TF.State, DShad.TF.Replay, DShad.TF.InputDisplayer
   (replace the files; keep DShad.TF.EX\\Unmanaged\\ as it is: the Windows netplay library), and add NetplayModeTest.
3. Start TowerFall. OPTIONS > TF.EX > SERVER: ws://$ip:13471 (the Mac's test server; the Mac starts it).
4. NETPLAY > LOBBIES. Either create a lobby (GAME MODE: right past TEAM DEATHMATCH to MOD TEST) or join the Mac's.
What to look at: the lobby offers MOD TEST; the match plays; small markers appear every 90 frames (invisible: the log says
"[ModTest] round 0: ...", "marker added"); no "desync" in TF.EX's log.
To undo: put the backup back.
EOF
rm -f "$OUT/TF.EX-mod-modes-pc.zip"
(cd "$OUT/pkg" && zip -q -r -X "$OUT/TF.EX-mod-modes-pc.zip" .)
echo "pc-package: $OUT/TF.EX-mod-modes-pc.zip"
