#!/bin/bash
# TF.EX's Harmony patch targets vs the Switch build: which (type, method) TF.EX patches exist on 1.4.1.6 by name.
# Usage: tools/tfex/patch-targets.sh [tag]   (default: the pinned submodule commit). Output: stdout.
set -e
here="$(cd "$(dirname "$0")" && pwd)"; repo="$here/../.."
out="$(mktemp -d)"; trap 'rm -rf "$out"' EXIT
src="$repo/vendor/tfex/src"
if [ -n "$1" ]; then git -C "$repo/vendor/tfex" worktree add -q --detach "$out/wt" "$1"; src="$out/wt/src"; fi
python3 "$here/targets.py" "$src" "$out/targets.json"
# main.flat is untracked: it lives in the main checkout's tools/re ($XPLAY_RE_DIR to override).
re="${XPLAY_RE_DIR:-$HOME/Code/Towerfall Switch Mods/switch-online/tools/re}"
(cd "$re" && python3 "$here/check.py" "$out")
[ -n "$1" ] && git -C "$repo/vendor/tfex" worktree remove --force "$out/wt"
