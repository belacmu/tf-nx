#!/bin/bash
# check-gen.sh [tag]: does hw-probe/exlaunch/source/program/tfex/gen/ match what the translator makes from this tree's rules
# (translate.toml, overrides.toml, the translator) and the TF.EX tag? Regenerates into a temp folder and diffs; exit 0 = in step,
# 1 = gen/ differs (run `tools/tfex/translate.sh all <tag>` and commit gen/), 2 = usage / translator failure.
# The coordinator runs it after every merge (gen/ merges with merge=ours, .gitattributes: a merge keeps one side's gen/, and this
# says whether that is the merged rules' output). <tag>: default the vendor/tfex commit HEAD records, by its tag.
set -e
here="$(cd "$(dirname "$0")" && pwd)"; repo="$(cd "$here/../.." && pwd)"
gen="$repo/hw-probe/exlaunch/source/program/tfex/gen"
"$here/ensure-vendor.sh" || exit 2
ptr="$(git -C "$repo" ls-tree HEAD vendor/tfex | awk '{print $3}')"
# the name gen/ records ("from TF.EX (vendor/tfex) <ref> (<sha>)") when it is still that commit: the label is in every file, so a
# branch name (a fork's, untagged) regenerated as its bare hash would differ everywhere; else the commit's tag, else the hash
recorded="$(grep -rhom1 'from TF.EX (vendor/tfex) [^ ]* (' "$gen" 2>/dev/null | head -1 | awk '{print $4}')"
[ -n "$recorded" ] && [ "$(git -C "$repo/vendor/tfex" rev-parse -q --verify "$recorded^{commit}" 2>/dev/null)" = "$ptr" ] || recorded=""
tag="${1:-${recorded:-$(git -C "$repo/vendor/tfex" describe --tags --exact-match "$ptr" 2>/dev/null || echo "$ptr")}}"
tmp="$(mktemp -d "${TMPDIR:-/tmp}/check-gen.XXXXXX")"; trap 'rm -rf "$tmp"' EXIT
"$here/translate.sh" all "$tag" "$tmp" > "$tmp.log" 2>&1 || { tail -5 "$tmp.log" >&2; echo "check-gen: the translator failed (log above)" >&2; rm -f "$tmp.log"; exit 2; }
rm -f "$tmp.log"
if diff -rq "$gen" "$tmp" > "$tmp.diff" 2>&1; then rm -f "$tmp.diff"; echo "check-gen: gen/ matches the rules at $tag"; exit 0; fi
sed "s|$tmp|<regenerated>|; s|$repo/||" "$tmp.diff" | head -40 >&2; n=$(wc -l < "$tmp.diff" | tr -d " "); rm -f "$tmp.diff"
echo "check-gen: gen/ differs from the rules' output at $tag ($n files): run tools/tfex/translate.sh all $tag and commit gen/" >&2
exit 1
