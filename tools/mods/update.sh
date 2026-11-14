#!/bin/bash
# update.sh <mod> [<tag>]: a translated catalog mod (mods/<mod>/, logic = "translated") brought to one of its upstream releases in one
# step, nothing edited by hand: its GitHub release (default: the latest; mod.toml [update] github, else its meta.json's
# update.github.repository), the vendored submodule checked out at that tag (fetched from GitHub), mods/<mod>/meta.json the release's
# own, gen/ retranslated at the TF.EX version this build is, the catalog regenerated, the module built (tools/build-probe.sh).
# Its SD folder comes from the release too where the mod's build_folder does that (Teams+: the release's TeamsPlus.zip).
# Commits nothing: review `git status`, commit, then land and publish as usual. Prints one line at the end:
#   UPDATE <mod> <old> -> <new>: done | <reason>
set -euo pipefail
here=$(cd "$(dirname "$0")/../.." && pwd); cd "$here"
mod=${1:?usage: tools/mods/update.sh <mod> [<tag>]}
tag=${2:-}
fail() { echo "UPDATE $mod: $*" >&2; exit 1; }
[ -f "mods/$mod/mod.toml" ] || fail "no mods/$mod/mod.toml"

# the mod's vendored source and release repository (mod.toml; meta.json's own update field as FortRise has it)
read -r logic vendored repo old < <(python3 - "$mod" <<'EOF'
import json, sys, tomllib
mod = sys.argv[1]
t = tomllib.load(open(f"mods/{mod}/mod.toml", "rb"))
meta = json.load(open(f"mods/{mod}/{t.get('meta', 'meta.json')}"))
repo = t.get("update", {}).get("github") or meta.get("update", {}).get("github", {}).get("repository", "")
print(t.get("logic", "-"), t.get("vendored", "-"), repo or "-", meta.get("version", "-"))
EOF
)
[ "$logic" = translated ] || fail "logic is $logic: only a translated mod is updated from its vendored release"
[ "$vendored" != - ] || fail "mod.toml has no vendored source"
[ "$repo" != - ] || fail "no GitHub release source (mod.toml [update] github, or meta.json update.github.repository)"
vpath=$(cd "mods/$mod" && cd "$vendored" 2>/dev/null && pwd) || true
if [ -z "$vpath" ]; then   # the submodule not checked out in this checkout yet
  sub=$(python3 -c 'import os,sys; print(os.path.relpath(os.path.normpath(os.path.join("mods", sys.argv[1], sys.argv[2])), "."))' "$mod" "$vendored")
  git -c protocol.file.allow=always submodule update --init "${sub%%/VSCode}" >/dev/null || fail "can't check out $sub"
  vpath=$(cd "mods/$mod" && cd "$vendored" && pwd)
fi
vroot=$(git -C "$vpath" rev-parse --show-toplevel)

# the release: the latest unless a tag was given
[ -n "$tag" ] || tag=$(gh api "repos/$repo/releases/latest" --jq .tag_name) || fail "GitHub $repo: no latest release"
git -C "$vroot" fetch -q "https://github.com/$repo.git" "refs/tags/$tag:refs/tags/$tag" 2>/dev/null || fail "GitHub $repo has no tag $tag"
git -C "$vroot" checkout -q "$tag"
src=$vroot/meta.json; [ -f "$src" ] || src=$vroot/ModFile/meta.json
[ -f "$src" ] || fail "release $tag has no meta.json (root or ModFile/)"
cp "$src" "mods/$mod/meta.json"
new=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["version"])' "mods/$mod/meta.json")
echo "update.sh: $mod $old -> $new ($repo $tag; ${vroot#$here/} checked out at it)"

# gen/ at the TF.EX version this build is (the tag the generated headers name; made locally at their commit when this clone lacks it)
read -r tfex tfexc < <(grep -h -o -m1 'TF.EX (vendor/tfex) v[^ ]* ([0-9a-f]*)' hw-probe/exlaunch/source/program/tfex/gen/*.hpp | head -1 |
  sed 's/TF.EX (vendor\/tfex) \(v[^ ]*\) (\([0-9a-f]*\))/\1 \2/')
[ -n "${tfex:-}" ] || fail "can't read the TF.EX version from gen/"
git -C vendor/tfex rev-parse -q --verify "refs/tags/$tfex" >/dev/null || git -C vendor/tfex tag "$tfex" "$tfexc" || fail "vendor/tfex lacks $tfex ($tfexc)"
tools/tfex/translate.sh all "$tfex" > "${TMPDIR:-/tmp}/update-$mod-translate.log" 2>&1 || fail "translate failed (${TMPDIR:-/tmp}/update-$mod-translate.log)"
python3 tools/mods/catalog.py
tools/build-probe.sh > "${TMPDIR:-/tmp}/update-$mod-build.log" 2>&1 || fail "build failed (${TMPDIR:-/tmp}/update-$mod-build.log)"
echo "UPDATE $mod $old -> $new: done (gen/, mods/$mod/meta.json, the catalog, the submodule; built). Review git status and commit."
