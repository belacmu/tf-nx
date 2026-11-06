#!/bin/bash
set -e

# Verify the path is set.
if [ -z "${RYU_PATH}" ]; then
    echo "RYU_PATH appears to not be set! Check your exlaunch.sh?"
fi

# Setup the path to the game's mods folder. This mod deploys to its OWN named
# folder (never the shared "mods/") so it can't clobber towerball-mod's native
# build; tools/ryujinx-activate.sh flips which exefs folder is live.
export MODS_PATH="${RYU_PATH}/mods/contents/${PROGRAM_ID}/three-team-mod"

# Ensure directory exists. (Quoted: RYU_PATH may contain spaces, e.g. macOS
# "Application Support".)
mkdir -p "${MODS_PATH}/exefs";

# Copy over files.
cp "${OUT}"/* "${MODS_PATH}/exefs"

# Also ship this mod's romfs layer (menuAtlas.xml etc.) — the folder can get
# parked/recreated by ryujinx-activate.sh flips from the parallel towerball
# session, and an exefs-only recreation reintroduces the atlas boot crash.
ROMFS_SRC="${ROMFS_SRC:-$(cd "$(dirname "$0")/../../../.." && pwd)/../three-team-mod/romfs}"
if [ -d "${ROMFS_SRC}" ]; then
    mkdir -p "${MODS_PATH}/romfs"
    cp -R "${ROMFS_SRC}/." "${MODS_PATH}/romfs/"
fi
