# source tools/harness-slot.sh (after `here` is set): XS = this checkout's port slot, 10..80 in steps of 10 (from a hash of its path;
# XPLAY_SLOT overrides). Slot 0 (the old fixed ports 13300/18180/19400) is left to checkouts without this file.
XS=${XPLAY_SLOT:-$(( (0x$(printf '%s' "$here" | shasum | cut -c1-4) % 8 + 1) * 10 ))}
