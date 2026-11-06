#!/usr/bin/env bash
# Builds libggrs_switch.a (the GGRS session + the test-8 spike, one staticlib so std is linked once) for the Switch and copies
# it to the probe's exlaunch checkout as rustlib/libggrs_spike.a (the name config.mk already links; it replaces the spike-only
# build). It also carries TF.EX's own ggrs_ffi (../ggrs-ffi-switch, docs/ws-ggrs-ffi.md; its netplay_* C API).
# Paths to the browser repo are relative (../../../Towerfall Browser): in a worktree that is switch-online/.claude/Towerfall Browser,
# a symlink to ~/Code/Towerfall Browser (made once by hand, 2026-10-18). Current nightly (tfx-rtc's dependencies need it; the target JSON is regenerated from its
# aarch64-unknown-linux-musl spec: no TLS, panic=abort, cortex-a57, PIC; built with rustc 1.101.0-nightly a30aa9064 2026-10-08). 
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
browser=${TOWERFALL_BROWSER:-$HOME/Code/Towerfall Browser}
dest=${HW_EXLAUNCH:-$(cd "$(dirname "$0")/.." && pwd)/hw-probe/exlaunch}/rustlib
source "$browser/tools/rustenv.sh"
cd "$here"
RUSTFLAGS="-C panic=abort" cargo +nightly build --release --lib \
  --target ./aarch64-unknown-linux-musl-notls.json -Zbuild-std=std,panic_abort -Zjson-target-spec
mkdir -p "$dest"
cp target/aarch64-unknown-linux-musl-notls/release/libggrs_switch.a "$dest/libggrs_spike.a"
echo "copied to $dest/libggrs_spike.a"
