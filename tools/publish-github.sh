#!/bin/bash
# The public copy of TF.NX (GitHub belacmu/tf-nx): master's tree as one snapshot commit on top of the GitHub history. This repo's own
# history stays here (it has private details in old commits); GitHub only ever gets current trees.
#   tools/publish-github.sh [ref]   makes the snapshot of <ref> (default master) in ../switch-online-public, build-checked, and commits
#                                   it to the local branch `public` (nothing leaves the machine)
#   tools/publish-github.sh push    pushes `public` to GitHub's main (the user's OK first)
# The public tree differs from <ref> in:
#   - gen/: translated with TFEX_PUBLIC=1, without the code mods marked `public = false` in translate.toml [code_mods] (no licence that
#     lets us distribute their translation), and their catalog folders (mods/<name>/) left out;
#   - keep_local: files that stay here.
# Refuses a tree with something that must stay private (the patterns below). Then tools/release.sh in ../switch-online-public makes the
# release zip from that very commit.
set -euo pipefail
repo=$(cd "$(dirname "$0")/.." && pwd); cd "$repo"
url=https://github.com/belacmu/tf-nx.git
git remote get-url github > /dev/null 2>&1 || git remote add github "$url"
if [ "${1:-}" = push ]; then
  git rev-parse -q --verify refs/heads/public > /dev/null || { echo "publish-github: no local branch public (run without push first)" >&2; exit 1; }
  git push github public:main; echo "publish-github: pushed $(git rev-parse --short public) -> $url"; exit 0
fi
ref=${1:-master}
main="$(git worktree list --porcelain | awk '/^worktree /{sub(/^worktree /,""); print; exit}')"   # (the main checkout, from any worktree)
pub="$(dirname "$main")/switch-online-public"
gen=hw-probe/exlaunch/source/program/tfex/gen

# the public worktree at <ref> (build outputs, ignored by git, are kept: incremental builds)
if [ -d "$pub" ]; then git -C "$pub" checkout -q --detach --force "$ref" && git -C "$pub" clean -qfd
else git worktree add -q --detach "$pub" "$ref"; fi
# its submodules at <ref>'s commits (a stale vendored mod failed the catalog check: vendor/teamplus at 0.1.2 under master's 0.1.3)
git -C "$pub" submodule update -q

# paths that stay in this repo only: dumps of the running game's memory (tools/re: the TF.EX update's state pins read them locally)
keep_local=(tools/re/rawdump.tgz tools/re/slotmaps.tgz)
git -C "$pub" rm -q --ignore-unmatch "${keep_local[@]}"

# the code mods without a licence: their catalog folders out, gen/ translated without them
private_mods=$(python3 - "$pub/tools/tfex/translate.toml" <<'PY'
import sys, tomllib
for k, v in tomllib.load(open(sys.argv[1], "rb")).get("code_mods", {}).items():
    if v.get("public", True) is False: print(v["mod"])
PY
)
for m in $private_mods; do [ -d "$pub/mods/$m" ] && git -C "$pub" rm -r -q "mods/$m"; done
tfex=$(git rev-parse "$ref:vendor/tfex")
tag=$(git -C vendor/tfex describe --tags --exact-match "$tfex" 2>/dev/null || echo "$tfex")
rm -rf "${pub:?}/$gen"
TFEX_PUBLIC=1 tools/tfex/translate.sh all "$tag" "$pub/$gen" > /dev/null
(cd "$pub" && python3 tools/mods/catalog.py > /dev/null)
echo "publish-github: left out of the public tree: $(echo $private_mods)"

# it must build (the module; the Rust library is the main checkout's, tools/build-probe.sh)
(cd "$pub" && tools/build-probe.sh > /dev/null) || { echo "publish-github: the public tree doesn't build (cd $pub && tools/build-probe.sh)" >&2; exit 1; }

git -C "$pub" add -A
tree=$(git -C "$pub" write-tree)
# what must never be published (AGENTS.md: no home paths, LAN addresses or server hosts; no file names of downloaded game copies)
deny='ziperto|nsw2u|NSwTcH|/Users/[a-z]|olavthon|thon\.com|[a-z0-9-]+\.[a-z0-9-]+\.workers\.dev'
if hits=$(git grep -I -l -i -E "$deny" "$tree" -- . ':!tools/publish-github.sh' 2>/dev/null); then
  echo "publish-github: not published, private details in:" >&2; echo "$hits" | sed 's/^[0-9a-f]*://' >&2; exit 1
fi

parent=$(git rev-parse -q --verify refs/heads/public || true)
if [ -n "$parent" ] && [ "$(git rev-parse "$parent^{tree}")" = "$tree" ]; then echo "publish-github: public already has this tree"; exit 0; fi
msg="TF.NX $(date +%Y-%m-%d): $ref at $(git rev-parse --short "$ref")"
commit=$(GIT_AUTHOR_NAME=belacmu GIT_AUTHOR_EMAIL=belacmu@gmail.com GIT_COMMITTER_NAME=belacmu GIT_COMMITTER_EMAIL=belacmu@gmail.com \
  git commit-tree "$tree" ${parent:+-p "$parent"} -m "$msg")
git update-ref refs/heads/public "$commit" ${parent:+"$parent"}
git -C "$pub" checkout -q --detach "$commit"   # (its tree: the worktree is clean, tools/release.sh names this commit)
echo "publish-github: $msg -> branch public ($(git rev-parse --short "$commit")); built in $pub. Push: tools/publish-github.sh push"
