#!/bin/bash
# ensure-vendor.sh [ref]: vendor/tfex and vendor/fortrise usable in this checkout (the TF.EX tools call it first). A fresh checkout or
# worktree has the submodules uninitialized (`git submodule status`: -<sha>), and then git commands in vendor/tfex run on the outer repo. So: init it
# (clones the pinned commit; network), and fetch TF.EX's tags when <ref> is unknown or there are none (describe / v0.21.1 by name).
here="$(cd "$(dirname "$0")" && pwd)"; repo="$(cd "$here/../.." && pwd)"; sub="$repo/vendor/tfex"
if [ ! -e "$sub/.git" ]; then
  echo "ensure-vendor: vendor/tfex isn't initialized here: git submodule update --init vendor/tfex" >&2
  git -C "$repo" submodule update --init -q vendor/tfex || { echo "ensure-vendor: the submodule init failed (offline?)" >&2; exit 1; }
fi
# vendor/fortrise (the FortRise units' source, at its pinned commit): without it translate.sh dropped those units and check-gen
# reported ~41 files differing (2026-10-21)
if [ ! -e "$repo/vendor/fortrise/.git" ]; then
  echo "ensure-vendor: vendor/fortrise isn't initialized here: git submodule update --init vendor/fortrise" >&2
  git -C "$repo" submodule update --init -q vendor/fortrise || { echo "ensure-vendor: the submodule init failed (offline?)" >&2; exit 1; }
fi
# vendor/teamplus (Teams+, the FortRise mod translated onto the Switch: ../three-team-mod/pc-fortrise, a local repository, hence file://)
if [ ! -e "$repo/vendor/teamplus/.git" ]; then
  echo "ensure-vendor: vendor/teamplus isn't initialized here: git submodule update --init vendor/teamplus" >&2
  git -C "$repo" -c protocol.file.allow=always submodule update --init -q vendor/teamplus || { echo "ensure-vendor: the vendor/teamplus init failed" >&2; exit 1; }
fi
# a worktree's .cache: the main checkout's shared inputs (towerfall-pc.il for tools/re's statepins / layoutdb, the FortRise-patched game,
# pc-src ...) linked in when missing; .cache/tfex stays per checkout (its logs and reports). Without them new-tag.sh's state pins failed
# in a worktree (2026-11-02, the v0.29.0 update)
main="$(git -C "$repo" worktree list --porcelain | sed -n 's/^worktree //p' | head -1)"
if [ -n "$main" ] && [ "$main" != "$repo" ] && [ -d "$main/.cache" ]; then
  mkdir -p "$repo/.cache"
  for e in "$main/.cache"/*; do
    n="$(basename "$e")"
    [ "$n" = tfex ] || [ -e "$repo/.cache/$n" ] || ln -s "$e" "$repo/.cache/$n"
  done
fi
if { [ -n "$1" ] && ! git -C "$sub" rev-parse --verify -q "$1^{commit}" >/dev/null; } || [ -z "$(git -C "$sub" tag -l 'v*' | head -1)" ]; then
  git -C "$sub" fetch -q --tags origin || echo "ensure-vendor: fetching TF.EX's tags failed (offline?)" >&2
fi
exit 0
