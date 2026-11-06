#!/bin/bash
# TF.EX update report (TFEX-BASE.md section 7): what changed between two TF.EX tags, where it lands in our port, class A-E.
# Usage: tools/tfex/update.sh <old> <new> [out.md]   (stdout when no out file; <old>, <new>: a tag, commit or ref in vendor/tfex)
#   TFEX_NO_TARGETS=1  skip patch-targets.sh (needs tools/re/main.flat in the main checkout, ~10 s per tag)
#   TFEX_NO_TRANSLATE=1 skip the translator run (tools/tfex/translate.sh at both tags, diffed)
#   TFEX_JSON=<file>   also write the findings as JSON (tools/tfex/new-tag.sh's one-page summary)
#   tools/tfex/expect/<old>-<new>.toml, when present, adds a check against a hand reading.
set -e
here="$(cd "$(dirname "$0")" && pwd)"; repo="$(cd "$here/../.." && pwd)"
[ $# -ge 2 ] || { sed -n 2,5p "$0" >&2; exit 2; }
old="$1"; new="$2"; out="${3:-/dev/stdout}"
jo="$("$here/split.sh" "$old")"; jn="$("$here/split.sh" "$new")"
so="${jo%.jsonl}"; so="${so/split-/src-}/src"; sn="${jn%.jsonl}"; sn="${sn/split-/src-}/src"
cache="$repo/.cache/tfex"
args=()
if [ -z "$TFEX_NO_TARGETS" ]; then
  for t in "$old" "$new"; do
    sha="$(git -C "$repo/vendor/tfex" rev-parse "$t^{commit}")"; f="$cache/targets-$sha.txt"
    if [ ! -s "$f" ]; then
      if TFEX_ALL=1 "$here/patch-targets.sh" "$t" > "$f.tmp" 2>&1; then mv -f "$f.tmp" "$f"; else echo "update.sh: patch-targets.sh $t failed (see $f.tmp)" >&2; fi
    fi
  done
  args+=(--targets-old "$cache/targets-$(git -C "$repo/vendor/tfex" rev-parse "$old^{commit}").txt"
         --targets-new "$cache/targets-$(git -C "$repo/vendor/tfex" rev-parse "$new^{commit}").txt")
fi
exp="$here/expect/${old//\//_}-${new//\//_}.toml"; [ -f "$exp" ] && args+=(--expect "$exp")
[ -n "$TFEX_JSON" ] && args+=(--json "$TFEX_JSON")
# the translator's output at both tags (TFEX_NO_TRANSLATE=1 skips it): what a re-run changes in tfex/gen/
if [ -z "$TFEX_NO_TRANSLATE" ]; then
  for t in "$old" "$new"; do
    g="$cache/gen-$(git -C "$repo/vendor/tfex" rev-parse "$t^{commit}")"
    if [ ! -d "$g" ] || [ "$here/translator/bin/out/translator.dll" -nt "$g" ] || [ "$here/translate.toml" -nt "$g" ]; then
      rm -rf "$g"; "$here/translate.sh" all "$t" "$g" > "$g.log" 2>&1 || echo "update.sh: translate.sh $t failed (see $g.log)" >&2
    fi
  done
  args+=(--gen-old "$cache/gen-$(git -C "$repo/vendor/tfex" rev-parse "$old^{commit}")" --gen-new "$cache/gen-$(git -C "$repo/vendor/tfex" rev-parse "$new^{commit}")")
fi
python3 "$here/update.py" --old-tag "$old" --new-tag "$new" --old "$jo" --new "$jn" --old-src "$so" --new-src "$sn" "${args[@]}" > "$out"
