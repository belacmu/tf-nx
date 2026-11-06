#!/bin/bash
# pc-il.sh [<FortRise version>]: the PC side of parity.py / round7.py: TowerFall 1.3.3.1 as TF.EX players run it, i.e. patched by
# FortRise (its MonoModRules change game code: Spikeball.Update's 1f / 180f is 0.005555555f, Arrow.MaxSeekAngle, Engine.Update's
# TimeMult...), not the vanilla exe. Patches .cache/towerfall-steam/TowerFall.exe with the browser harness's FortRisePatch (the page's
# own patch step) and writes its IL listing to .cache/towerfall-pc.il (the vanilla game's to .cache/towerfall-pc-vanilla.il).
# The browser worktree: $BROWSER_WT (default harness-tfex027, whose vendor/fortrise is the version the page runs).
set -e
here="$(cd "$(dirname "$0")" && pwd)"; repo="$(cd "$here/../.." && pwd)"
main="$(git -C "$repo" worktree list --porcelain | sed -n 's/^worktree //p' | head -1)"
bwt="${BROWSER_WT:-$HOME/Code/Towerfall Browser/.claude/worktrees/harness-tfex027}"
ver="${1:-$(cat "$bwt/vendor/fortrise/version.txt")}"
out="$main/.cache/fortrise-patched-$ver"
dotnet="${DOTNET:-$(command -v dotnet || echo /usr/local/share/dotnet/dotnet)}"
(cd "$bwt" && "$dotnet" run -c Release --project tools/FortRisePatch -- "$main/.cache/towerfall-steam/TowerFall.exe" "$out" | tail -1)
tmp="$(mktemp -d)"; ~/.dotnet/tools/ilspycmd -il "$out/TowerFall.Patch.dll" -o "$tmp" >/dev/null
mv "$tmp/TowerFall.Patch.il" "$main/.cache/towerfall-pc.il"
~/.dotnet/tools/ilspycmd -il "$main/.cache/towerfall-steam/TowerFall.exe" -o "$tmp" >/dev/null   # (the vanilla game: fortrise-diff.py)
mv "$tmp/TowerFall.il" "$main/.cache/towerfall-pc-vanilla.il"; rmdir "$tmp"
echo "pc-il: .cache/towerfall-pc.il = TowerFall 1.3.3.1 + FortRise $ver"
