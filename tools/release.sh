#!/bin/bash
# release.sh: TF.NX's release zip, dist/TF.NX-<version>-<commit>.zip, from THIS checkout's build (hw-probe/exlaunch/deploy,
# the extras build; tools/build-probe.sh first). Extracted to an SD card's root it is the whole install (docs/PLAN-TFNX-2026-11-04.md):
#   atmosphere/contents/0100A1C00359C000/exefs/   the module (subsdk9, main.npdm)
#   xplay/tfex/                                   TF.EX's own files (tools/devserve.list's sd:/xplay/tfex/ lines)
#   xplay/mods/<name>/                            the catalog mods we may ship (mods/<name>/mod.toml [release] ship = true)
#   README.txt, NOTICE.txt, MODS.txt              for players (tools/release/), the catalog (docs/MODS-CATALOG.md)
# Never: game files (the romfs layer: the PC levels, atlases made from the game's), third-party mod files, test files (hw.txt...).
# Online play's PC levels are made on the console from the Switch's own and a delta compiled into the module (pclevels.cpp).
set -eu
root=$(cd "$(dirname "$0")/.." && pwd); cd "$root"
tfex=$(sed -n 's/.*TFEX_VERSION "\(.*\)".*/\1/p' hw-probe/exlaunch/source/program/tfex/gen/tfexgen_version.hpp)
version="$tfex.$(sed -n 's/^TFNX_BUILD := //p' hw-probe/exlaunch/config.mk)"   # TF.EX's version plus TF.NX's build number
commit=$(git rev-parse --short HEAD)
[ -z "$(git status --porcelain -- hw-probe mods tools/release)" ] || { echo "release: the checkout has uncommitted changes (commit first: the release names its commit)" >&2; exit 1; }
# upstream versions (tools/updates/check.py): a warning, not a stop (a release between TF.EX versions is still a release)
python3 tools/updates/check.py > /dev/null 2>&1; uc=$?
[ $uc = 2 ] && echo "release: WARNING: something TF.NX carries has a newer upstream version (tools/updates/check.py)" >&2
[ $uc = 1 ] && echo "release: WARNING: the update check couldn't reach a source (tools/updates/check.py)" >&2
d=hw-probe/exlaunch/deploy
[ -f "$d/subsdk9" ] && [ -f "$d/main.npdm" ] || { echo "release: no build in $d (tools/build-probe.sh)" >&2; exit 1; }
[ "$d/subsdk9" -nt hw-probe/exlaunch/config.mk ] || { echo "release: the build is older than config.mk (rebuild)" >&2; exit 1; }
strings hw-probe/exlaunch/tfx-hw-exlaunch.elf | grep -q "TF.NX $version, TF.EX $tfex" ||   # (subsdk9 is compressed: its ELF)
  { echo "release: the build isn't TF.NX $version with TF.EX $tfex (rebuild)" >&2; exit 1; }
name="TF.NX-$version-$commit"
stage="dist/$name"; rm -rf "$stage"; mkdir -p "$stage"
ex="$stage/atmosphere/contents/0100A1C00359C000/exefs"; mkdir -p "$ex"
cp "$d/subsdk9" "$d/main.npdm" "$ex/"
grep -E '^sd:/xplay/tfex/' tools/devserve.list | while read -r dst src; do
  mkdir -p "$stage/$(dirname "${dst#sd:/}")"; cp "$src" "$stage/${dst#sd:/}"
done
included=""
for m in mods/*/mod.toml; do
  n=$(basename "$(dirname "$m")")
  ship=$(python3 -c 'import sys,tomllib; print(tomllib.load(open(sys.argv[1],"rb")).get("release",{}).get("ship",False))' "$m")
  [ "$ship" = True ] || continue
  build=$(python3 -c 'import sys,tomllib; print(tomllib.load(open(sys.argv[1],"rb")).get("folder",{}).get("build",""))' "$m")
  [ -n "$build" ] || { echo "release: $n ships but has no [folder] build" >&2; exit 1; }
  (cd "$(dirname "$m")" && $build "$root/$stage/xplay/mods/$n" >/dev/null)
  included="$included    - $n $(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["version"])' "mods/$n/meta.json")\n"
done
limits="  - None known. Report problems with the log: sd:/xplay/hw-log.txt."
INCLUDED="$(printf "$included")" LIMITS="$(printf "$limits")" VERSION=$version TFEX=$tfex COMMIT=$commit python3 - "$stage" <<'PY'
import os, sys
for f in ("README.txt", "NOTICE.txt"):
    t = open(os.path.join("tools/release", f)).read()
    for k in ("VERSION", "TFEX", "COMMIT", "INCLUDED", "LIMITS"): t = t.replace("@" + k + "@", os.environ[k])
    open(os.path.join(sys.argv[1], f), "w").write(t)
PY
cp docs/MODS-CATALOG.md "$stage/MODS.txt"
find "$stage" -name '._*' -delete
rm -f "dist/$name.zip"; (cd "$stage" && COPYFILE_DISABLE=1 zip -qr -X "../$name.zip" .)   # (its contents at the root: extracted onto the SD card's)
echo "release: dist/$name.zip ($(du -h "dist/$name.zip" | cut -f1); $(find "$stage" -type f | wc -l | tr -d ' ') files; TF.NX $version, TF.EX $tfex, commit $commit)"
