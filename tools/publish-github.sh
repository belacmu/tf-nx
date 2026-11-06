#!/bin/bash
# Publish master to the GitHub copy (belacmu/tf-nx) as one snapshot commit: its tree as it is now, on top of the GitHub history.
# This repo's own history stays here (it has private details in old commits); GitHub only ever gets current trees.
#   tools/publish-github.sh [ref]   (default master)
# Refuses when the tree has something that must stay private (the patterns below). Needs the user's OK to run (it pushes).
# The GitHub history lives in the local branch `public` (one commit per snapshot, author belacmu); the remote is `github`.
set -euo pipefail
repo=$(cd "$(dirname "$0")/.." && pwd); cd "$repo"
ref=${1:-master}
url=https://github.com/belacmu/tf-nx.git
git remote get-url github > /dev/null 2>&1 || git remote add github "$url"

tree=$(git rev-parse "$ref^{tree}")
# what must never be published (AGENTS.md: no home paths, LAN addresses or server hosts; no file names of downloaded game copies)
deny='ziperto|nsw2u|NSwTcH|/Users/[a-z]|olavthon|thon\.com|[a-z0-9-]+\.[a-z0-9-]+\.workers\.dev'
if hits=$(git grep -I -l -i -E "$deny" "$tree" -- . ':!tools/publish-github.sh' 2>/dev/null); then
  echo "publish-github: not published, private details in:" >&2; echo "$hits" | sed 's/^[0-9a-f]*://' >&2; exit 1
fi

parent=$(git rev-parse -q --verify refs/heads/public || true)
if [ -n "$parent" ] && [ "$(git rev-parse "$parent^{tree}")" = "$tree" ]; then echo "publish-github: GitHub already has this tree"; exit 0; fi
msg="TF.NX $(date +%Y-%m-%d): $ref at $(git rev-parse --short "$ref")"
commit=$(GIT_AUTHOR_NAME=belacmu GIT_AUTHOR_EMAIL=belacmu@gmail.com GIT_COMMITTER_NAME=belacmu GIT_COMMITTER_EMAIL=belacmu@gmail.com \
  git commit-tree "$tree" ${parent:+-p "$parent"} -m "$msg")
git update-ref refs/heads/public "$commit" ${parent:+"$parent"}
git push github public:main
echo "publish-github: $msg -> $url ($(git rev-parse --short "$commit"))"
