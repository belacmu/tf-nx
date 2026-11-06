#!/usr/bin/env bash
# The browser page for mod-mode tests: a copy of the browser build's wwwroot whose hosted mods are TF.EX built from a local source folder
# and the test mod (tools/modmodes/NetplayModeTest). Used by two-pages.sh and check.sh.
#   tools/modmodes/build-page.sh <out dir>   (the page: <out dir>/www)
# Env: TFEX_SRC (default ~/Code/TF.EX-mod-modes), BROWSER_WT (the browser worktree, default tfex-0281), MOD_VERSION (the test mod's
#      meta.json version in this page, default its own: a page with another version of the mod), MOD_DEFINES (the test mod's
#      compile constants, e.g. NO_STATE: without its rollback state).
set -uo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=$(mkdir -p "$1" && cd "$1" && pwd)   # (absolute: the builds below run in other folders)
TFEX_SRC=$(cd "${TFEX_SRC:-$HOME/Code/TF.EX-mod-modes}" && pwd -P)
BROWSER_WT=${BROWSER_WT:-$HOME/Code/Towerfall Browser/.claude/worktrees/tfex-0281}
rm -rf "$OUT" && mkdir -p "$OUT/stage"
source "$BROWSER_WT/env.sh"

# TF.EX's four mods and the test mod, zipped as the page's hosted mods, in a copy of the browser build's wwwroot
for p in "src/state/TF.State.Core/TF.State.Core.csproj:DShad.TF.State" "src/replay/TF.Replay.Core/TF.Replay.Core.csproj:DShad.TF.Replay" \
         "src/utilities/TF.InputDisplayer/TF.InputDisplayer.Core/TF.InputDisplayer.Core.csproj:DShad.TF.InputDisplayer" \
         "src/network/TF.EX.Core/TF.EX.Core.csproj:DShad.TF.EX"; do
  proj=${p%%:*} name=${p#*:}
  dotnet build "$TFEX_SRC/$proj" -c Release -nologo -v q -clp:ErrorsOnly -p:ModLoaderPath="$TFEX_SRC/libs" -p:ModEnablePublish=false \
    -p:ModEnableZip=false > "$OUT/build.log" 2>&1 || { echo "build-page: TF.EX build failed ($name) | logs $OUT"; exit 1; }
  mkdir -p "$OUT/stage/tfex/$name" && cp -r "$TFEX_SRC/$(dirname "$proj")/bin/Release/net10.0/." "$OUT/stage/tfex/$name/"
  rm -f "$OUT/stage/tfex/$name"/*.deps.json; rm -rf "$OUT/stage/tfex/$name/Unmanaged"
done
(cd "$HERE/NetplayModeTest" && dotnet build -c Release -nologo -v q -clp:ErrorsOnly -p:ModLoaderPath="$TFEX_SRC/libs" ${MOD_DEFINES:+-p:DefineConstants=$MOD_DEFINES} > "$OUT/build.log" 2>&1) \
  || { echo "build-page: test mod build failed | logs $OUT"; exit 1; }
mkdir -p "$OUT/stage/mod/NetplayModeTest" && cp -r "$HERE/NetplayModeTest/bin/Release/net10.0/." "$OUT/stage/mod/NetplayModeTest/"
rm -f "$OUT/stage/mod/NetplayModeTest"/*.deps.json "$OUT/stage/mod/NetplayModeTest"/*.pdb
[ -n "${MOD_VERSION:-}" ] && python3 - "$OUT/stage/mod/NetplayModeTest/meta.json" "$MOD_VERSION" <<'PY'
import json, sys
m = json.load(open(sys.argv[1])); m["version"] = sys.argv[2]   # the mod's own version only (not its dependencies')
json.dump(m, open(sys.argv[1], "w"), indent="\t")
PY
cp -R "$BROWSER_WT/web/bin/Release/net10.0/publish/wwwroot" "$OUT/www"
rm -f "$OUT/www/hosted-mods/"*.zip
(cd "$OUT/stage/tfex" && zip -q -r -X "$OUT/www/hosted-mods/DShad.TF.EX-local.zip" .)
(cd "$OUT/stage/mod" && zip -q -r -X "$OUT/www/hosted-mods/NetplayModeTest.zip" .)
python3 - "$OUT/www/hosted-mods" "$OUT/stage/mod/NetplayModeTest/meta.json" <<'PY'
import hashlib, json, os, sys
d, meta = sys.argv[1], json.load(open(sys.argv[2]))
p = os.path.join(d, "hosted.json"); h = json.load(open(p))
def f(name):
    b = open(os.path.join(d, name), "rb").read()
    return {"name": name, "size": len(b), "sha256": hashlib.sha256(b).hexdigest(), "mirror": "hosted-mods/" + name}
h["mods"] = [h["mods"][0]]
h["mods"][0]["file"] = f("DShad.TF.EX-local.zip")
h["mods"].append({"hosted": True, "title": meta["displayName"], "author": meta["author"], "category": "Test", "page": "",
    "file": f("NetplayModeTest.zip"), "mods": [{"name": meta["name"], "version": meta["version"], "displayName": meta["displayName"],
    "dll": meta["dll"], "dependencies": meta["dependencies"], "optionalDependencies": meta.get("optionalDependencies", []),
    "status": "experimental"}]})
json.dump(h, open(p, "w"), indent=1)
PY

