#!/usr/bin/env bash
# Builds the GGRS loopback spike (VIABILITY-TESTS test 8) for the Switch: libggrs_spike.a, copied
# into the net probe's exlaunch checkout (default ~/Code/tfx-net-exlaunch/rustlib).
#
# - GGRS comes from the browser repo's pinned, patched checkout (the fork TF.EX uses), so run
#   ../../Towerfall Browser/tools/build-netplay.sh once first if vendor/netplay-src is missing.
# - The target is aarch64-unknown-linux-musl-notls.json: the stock aarch64 musl target with
#   has-thread-local=false (std keeps thread-locals behind pthread keys, which shim.c provides),
#   PIC, abort on panic. `std` is rebuilt from source (-Zbuild-std).
# - The Rust toolchain is the browser repo's (tools/rustenv.sh, nightly-2025-02-01).
# - The result links with -nodefaultlibs (see config.mk), so every libc call binds to the game's own
#   libc at load time; shim.c supplies what the game doesn't export.
# Also: `cargo run --release --bin loop` runs the same test natively on the Mac.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
browser=${TOWERFALL_BROWSER:-$HOME/Code/Towerfall Browser}
dest=${NET_EXLAUNCH:-$HOME/Code/tfx-net-exlaunch}/rustlib
source "$browser/tools/rustenv.sh"
cd "$here"
RUSTFLAGS="-C panic=abort" cargo +nightly-2025-02-01 build --release --lib \
  --target ./aarch64-unknown-linux-musl-notls.json -Zbuild-std=std,panic_abort --ignore-rust-version
mkdir -p "$dest"
cp target/aarch64-unknown-linux-musl-notls/release/libggrs_spike.a "$dest/"
echo "copied to $dest"
