#!/bin/bash
# publish-hw.sh: the console runs master. Builds master in its own worktree (../switch-online-hw next to the main checkout, detached at
# master, never edited by hand) and publishes that build with the main checkout's tools/devserve-publish.sh (.devserve/channel/, what
# tools/devserver.py serves): the console picks it up at its next boot. Never publish another checkout's build to the console.
set -eu
here=$(cd "$(dirname "$0")/.." && pwd)
main=$(git -C "$here" worktree list --porcelain | awk '/^worktree /{sub(/^worktree /,""); print; exit}')
hw="$(dirname "$main")/switch-online-hw"
[ -d "$hw" ] || git -C "$main" worktree add --detach "$hw" master
if ! git -C "$hw" diff --quiet || ! git -C "$hw" diff --cached --quiet; then echo "publish-hw: $hw has local changes; not touching it" >&2; exit 1; fi
git -C "$hw" checkout -q --detach master
# the vendored mod sources the mod folders are built from (mods/*/build_folder.sh), at master's commits: a mod new on master has no
# checkout here yet (2026-11-05: MoveOrDie's folder failed and nothing was published). Their URLs are local paths: allow file clones.
subs=$(grep -ho 'vendor/[A-Za-z0-9_.-]*' "$hw"/mods/*/build_folder.sh 2>/dev/null | sort -u)
[ -z "$subs" ] || git -C "$hw" -c protocol.file.allow=always submodule update --init --quiet -- $subs
# the build stamp (__DATE__/__TIME__ in main.cpp) recompiles only with its file: touch them so every publish has its own
touch "$hw/hw-probe/exlaunch/source/program/main.cpp"
"$hw/tools/build-probe.sh"
# keep this build's ELF by its GNU build id (= the crash reports' "Module Id"), so every console crash symbolizes with its own build
bid=$(${DEVKITPRO:-/opt/devkitpro}/devkitA64/bin/aarch64-none-elf-readelf -n "$hw/hw-probe/exlaunch/tfx-hw-exlaunch.elf" | awk '/Build ID/{print $3}')
mkdir -p "$main/.devserve/elf" && cp "$hw/hw-probe/exlaunch/tfx-hw-exlaunch.elf" "$main/.devserve/elf/$bid-$(git -C "$hw" log -1 --format=%h).elf"
echo "publish-hw: ELF kept as .devserve/elf/$bid-*.elf (match a crash report's Module Id, lowercase, to it)"
"$main/tools/devserve-publish.sh" "$hw" "master $(git -C "$hw" log -1 --format='%h %s' | cut -c1-70)"
