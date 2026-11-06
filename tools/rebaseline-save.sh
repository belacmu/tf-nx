#!/bin/bash
# rebaseline-save.sh: make the *current* Ryujinx TowerFall save the reference run-probe.sh compares against
# (the user OK'd test runs changing the emulator save, 2026-10-14). The previous backup is kept as .save-backup-original
# (only the first time), both are git-ignored. Refuses while Ryujinx runs.
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
source "$root/tools/rj-slot.sh"; rj=$RJ; save="$rj/bis/user/save"; bk=$RJ_BACKUP   # XPLAY_RJSLOT: that slot's save and backup
if [ -n "${XPLAY_LOCK_HELD:-}" ]; then for _ in $(seq 1 60); do pgrep -x "$RJ_NAME" >/dev/null || break; sleep 1; done; fi   # the previous run exiting
pgrep -x "$RJ_NAME" >/dev/null && { echo "$RJ_NAME is running" >&2; exit 1; }
[ -d "$bk-original" ] || [ ! -d "$bk" ] || mv "$bk" "$bk-original"   # a fresh checkout or worktree has no backup yet
rm -rf "$bk"; mkdir -p "$bk/save"
cp -R "$save/0000000000000001" "$bk/save/"
(cd "$save" && find ./0000000000000001 -type f -exec shasum {} + | sort -k2) > "$bk/manifest.txt"
echo "baseline updated: $(wc -l < "$bk/manifest.txt") files"
