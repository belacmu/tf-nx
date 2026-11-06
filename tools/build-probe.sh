#!/bin/bash
# build-probe.sh [clean]: builds the hardware probe (hw-probe/exlaunch, the Switch module) of THIS checkout. make can't handle the spaces
# in the repo's path, so the tree is mirrored (rsync, timestamps kept: incremental) into a space-free folder per checkout,
# ~/.cache/tfx-build/<checkout id>, built there with devkitPro, and deploy/ (subsdk9, main.npdm) copied back to hw-probe/exlaunch/deploy.
# The Rust library (rustlib/libggrs_spike.a, not in git): this checkout's own build (ggrs-switch/build.sh) if it is newer than the main
# checkout's, else the main checkout's, copied whenever it differs (a stale copy linked old code: "[rtld] Unresolved symbol: tfx_..." and
# a jump to null the first time the function ran). A changed library relinks (make doesn't see it: the mirror's ELF is removed), and the
# build fails if the module has unresolved tfx_* symbols.
# XPLAY_EXTRAS=0 builds "TF.EX Switch" (no megamod; hw-probe/exlaunch/config.mk) in its own mirror folder into hw-probe/exlaunch/deploy-core
# (its ELF tfx-hw-exlaunch-core.elf); the default (1) is "TF.EX Switch + extras" into deploy/, what the console runs.
set -eu
here=$(cd "$(dirname "$0")/.." && pwd)
export XPLAY_EXTRAS=${XPLAY_EXTRAS:-1}
case "$XPLAY_EXTRAS" in 1) variant=""; ;; 0) variant="-core" ;; *) echo "build-probe: XPLAY_EXTRAS is 1 or 0" >&2; exit 1 ;; esac
src="$here/hw-probe/exlaunch"
id=$(printf '%s' "$here" | shasum | cut -c1-10)
dst="$HOME/.cache/tfx-build/$(basename "$here" | tr -c 'A-Za-z0-9_-\n' '_')-$id$variant"
mkdir -p "$dst"
lib="$src/rustlib/libggrs_spike.a"
main="$(git -C "$here" worktree list --porcelain | awk '/^worktree /{sub(/^worktree /,""); print; exit}')"
mainlib="$main/hw-probe/exlaunch/rustlib/libggrs_spike.a"
if [ "$mainlib" != "$lib" ] && [ -f "$mainlib" ]; then
  if [ ! -f "$lib" ]; then mkdir -p "$src/rustlib"; cp -p "$mainlib" "$lib"; echo "build-probe: rustlib copied from $main"
  elif ! cmp -s "$mainlib" "$lib"; then
    if [ "$lib" -nt "$mainlib" ]; then echo "build-probe: rustlib: keeping this checkout's own build (newer than $main's)"
    else cp -p "$mainlib" "$lib"; echo "build-probe: rustlib refreshed from $main (it differed)"; fi
  fi
fi
[ -f "$lib" ] || { echo "build-probe: no rustlib/libggrs_spike.a: run ggrs-switch/build.sh" >&2; exit 1; }
# the catalog mods (mods/<name>/, mods/README.md): the generated table current, each mod's native code (src/) into the mirror's
# source/program/mods/<name>/ (make compiles every folder under source/program)
python3 "$here/tools/mods/catalog.py" --check || { echo "build-probe: FAILED: the mod catalog is out of date (tools/mods/catalog.py)" >&2; exit 1; }
rsync -a --delete --exclude build/ --exclude deploy/ --exclude '*.elf' --exclude .rustlib.sum "$src/" "$dst/"
mkdir -p "$dst/source/program/mods"
for m in "$here"/mods/*/src; do [ -d "$m" ] && rsync -a --delete "$m/" "$dst/source/program/mods/$(basename "$(dirname "$m")")/"; done
# an object whose source moved or went (2026-10-18: tfex/runtime compiled in place, no longer copied into tfex/gen/): its .d names a
# file that no longer exists, and make stops on it ("No rule to make target"); drop that .d and its .o so it rebuilds
for d in "$dst"/build/*.d; do
  [ -f "$d" ] || continue
  s=$(tr -d '\\\n' < "$d" | awk -F': *' '{ split($2, a, " "); print a[1]; exit }')
  if [ -n "$s" ] && [ ! -f "$s" ]; then rm -f "$d" "${d%.d}.o"; echo "build-probe: dropped stale $(basename "${d%.d}") (its source $s is gone)"; fi
done
cd "$dst"
if [ "${1:-}" = clean ]; then rm -rf build deploy; fi
libsum=$(shasum "$dst/rustlib/libggrs_spike.a" | cut -c1-40)
if [ "$(cat .rustlib.sum 2>/dev/null)" != "$libsum" ]; then rm -f ./*.elf ./*.nso; echo "$libsum" > .rustlib.sum; echo "build-probe: rustlib changed: relinking"; fi
DEVKITPRO=${DEVKITPRO:-/opt/devkitpro} ./exlaunch.sh build > build.log 2>&1 || { grep -E 'error' build.log | head -20 >&2; echo "build-probe: FAILED (log: $dst/build.log)" >&2; exit 1; }
grep -E ' warning:' build.log | head -5 || true
out="$src/deploy$variant"
mkdir -p "$out"
cp deploy/subsdk9 deploy/main.npdm "$out/"
cp "$(basename "$dst").elf" "$src/tfx-hw-exlaunch$variant.elf" 2>/dev/null || true   # the build names the ELF after its folder
unres=$(${DEVKITPRO:-/opt/devkitpro}/devkitA64/bin/aarch64-none-elf-nm -u "$(basename "$dst").elf" | awk '{print $2}' | grep -E '^(tfx_|netplay_|ggrs_ffi_)' || true)
if [ -n "$unres" ]; then echo "build-probe: FAILED: the module has unresolved Rust symbols (rustlib/libggrs_spike.a too old? run ggrs-switch/build.sh):" >&2; echo "$unres" | head -20 >&2; rm -f "$out/subsdk9"; exit 1; fi
# Any other unresolved symbol of ours is a call to nothing at run time (8a5ffb5: rnd::tfex::gen::GenStateOn crashed every death):
# the module may only import the SDK (nn::), libc and the C++ runtime.
unres=$(${DEVKITPRO:-/opt/devkitpro}/devkitA64/bin/aarch64-none-elf-nm -u -C "$(basename "$dst").elf" | sed 's/^ *U //' | grep '::' | grep -vE '^(nn|std|__cxxabiv1|__gnu_cxx)::' || true)
if [ -n "$unres" ]; then echo "build-probe: FAILED: the module calls functions nothing defines (a declaration in the wrong namespace?):" >&2; echo "$unres" | head -20 >&2; rm -f "$out/subsdk9"; exit 1; fi
echo "build-probe: OK -> $out ($(date +%H:%M:%S))"
