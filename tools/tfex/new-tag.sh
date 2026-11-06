#!/bin/bash
# "A new TF.EX tag came out": the whole routine in one command (README.md "New TF.EX tag"). Commits nothing.
# Usage: tools/tfex/new-tag.sh <new> [<old>]
#   <new>  a tag (v0.22.0), a commit, or a ref of another remote (fork/couch-local): anything git -C vendor/tfex rev-parse knows
#   <old>  what the port is at now; default: vendor/tfex's checked-out commit (its tag when it has one)
#   TFEX_REMOTE=<name>  the remote to fetch first (default origin; a fork: git -C vendor/tfex remote add <name> <url>, once)
#   TFEX_NO_BUILD=1     skip the module build (tools/build-probe.sh)
#   TFEX_FROM=<step>    resume at that step (3, 4, 4b, 5, 5b, 6, 7) after fixing what stopped a run: vendor/tfex already at <new>, the
#                       earlier steps' output kept; give <old> explicitly (the log is appended to)
# Steps: fetch; update report (update.sh); move vendor/tfex to <new>; coverage at <new> (also runs bind.py's repairs of
# binding.json); regenerate tfex/gen; rule keys (fails on a key that resolved at <old> and no longer does: the keys unresolved at
# <old> already, older debt, are listed in the report but don't stop the update); manifest check; the backlog (new TF.EX
# code in no step is listed); module build; the
# one-page summary (new-tag.py). Fails first when vendor/tfex isn't at the commit HEAD records, or the FortRise DLL is missing (inputs.py).
# Writes tools/tfex/reports/<old>-<new>.md, <old>-<new>-summary.md, translator-coverage-<new>.md (a "/" in a ref becomes "_").
set -e
here="$(cd "$(dirname "$0")" && pwd)"; repo="$(cd "$here/../.." && pwd)"
[ $# -ge 1 ] || { sed -n 2,10p "$0" >&2; exit 2; }
sub="$repo/vendor/tfex"; rep="$here/reports"; cache="$repo/.cache/tfex"; mkdir -p "$cache"
new="$1"
"$here/ensure-vendor.sh" || exit 1
# vendor/tfex must be where HEAD records it (evaluation rec. 2b): else <old> is guessed from a stray checkout and a later
# `git commit -a` moves the pointer back
order="1 2 3 4 4b 5 5b 6 7"; from="${TFEX_FROM:-1}"
case " $order " in *" $from "*) ;; *) echo "new-tag.sh: TFEX_FROM=$from is no step ($order)" >&2; exit 2 ;; esac
runs() { [ "$(echo "$order" | tr ' ' '\n' | grep -nx "$1" | cut -d: -f1)" -ge "$(echo "$order" | tr ' ' '\n' | grep -nx "$from" | cut -d: -f1)" ]; }
ptr="$(git -C "$repo" ls-tree HEAD vendor/tfex | awk '{print $3}')"
if [ "$from" = 1 ] || [ "$from" = 2 ]; then
  if [ -n "$ptr" ] && [ "$(git -C "$sub" rev-parse HEAD)" != "$ptr" ]; then
    echo "new-tag.sh: vendor/tfex is at $(git -C "$sub" rev-parse --short HEAD), HEAD records ${ptr:0:7}: git submodule update vendor/tfex first" >&2; exit 1
  fi
else
  [ $# -ge 2 ] || { echo "new-tag.sh: TFEX_FROM=$from needs <old> given" >&2; exit 2; }
  [ "$(git -C "$sub" rev-parse HEAD)" = "$(git -C "$sub" rev-parse "$new^{commit}")" ] || { echo "new-tag.sh: TFEX_FROM=$from: vendor/tfex isn't at $new" >&2; exit 1; }
fi
git -C "$sub" fetch -q --tags "${TFEX_REMOTE:-origin}" || echo "new-tag.sh: fetch from ${TFEX_REMOTE:-origin} failed (offline?); using what vendor/tfex has" >&2
old="${2:-$(git -C "$sub" describe --tags --exact-match HEAD 2>/dev/null || git -C "$sub" rev-parse --short HEAD)}"
for r in "$old" "$new"; do
  git -C "$sub" rev-parse --verify -q "$r^{commit}" >/dev/null || { echo "new-tag.sh: '$r' is not a tag, commit or ref in vendor/tfex" >&2; exit 1; }
done
fo="${old//\//_}"; fn="${new//\//_}"
log="$cache/new-tag-$fo-$fn.log"; [ "$from" = 1 ] && : > "$log"
step() { echo "== $*" | tee -a "$log" >&2; }
# the rule keys already unresolved at <old> (older debt): the gate at 4b stops only on keys that resolved there. Taken once per <old>
# commit, before vendor/tfex moves (translate.sh rules runs at the commit HEAD records)
oldc="$(git -C "$sub" rev-parse --short "$old^{commit}")"; base="$cache/rule-keys-unresolved-$oldc.txt"
unresolved() { awk '/^## Unresolved/{p=1;next} /^## /{p=0} p && /^- /' "$1" | sed 's/: .*//' | sort -u; }
baseline() {   # (vendor/tfex at <old> for it, then back where it was: a resumed run has it at <new>)
  [ -s "$base" ] && return 0
  local at tracked rc=0; at="$(git -C "$sub" rev-parse HEAD)"; tracked="$(git -C "$repo" ls-files -- "$rep/rule-keys-$fo.md")"
  git -C "$sub" checkout -q --detach "$old"
  "$here/translate.sh" rules "$old" >>"$log" 2>&1 || rc=$?
  if [ "$rc" = 0 ] || [ "$rc" = 3 ]; then { echo "# rule keys unresolved at $old ($oldc)"; unresolved "$rep/rule-keys-$fo.md"; } > "$base"
  else echo "new-tag.sh: the rule keys at $old couldn't be listed (see $log): 4b compares against nothing" >&2; fi
  if [ -n "$tracked" ]; then git -C "$repo" checkout -q -- "$rep/rule-keys-$fo.md"; else rm -f "$rep/rule-keys-$fo.md"; fi
  git -C "$sub" checkout -q --detach "$at"
}

if runs 1; then
step "1/7 update report $old -> $new"
TFEX_JSON="$cache/update-$fo-$fn.json" "$here/update.sh" "$old" "$new" "$rep/$fo-$fn.md" 2>>"$log"
baseline
fi
if runs 2; then
step "2/7 vendor/tfex -> $new"
git -C "$sub" checkout -q --detach "$new"
fi
if runs 3; then
step "3/7 coverage at $new (about a minute; repairs binding.json)"
"$here/translate.sh" coverage "$new" >>"$log" 2>&1 || echo "new-tag.sh: coverage failed (see $log)" >&2
fi
# TF.State's state, before gen/: its MessagePack formatters (a new [Key] in GameState is in the checksummed bytes: v0.27.0's MatchStats
# desynced every frame with v0.22.2's glue) and the private fields its units read (tools/re/statepins.py: the layout database's pins,
# else a state gap; v0.27.0's TriggerArrow.primed / normalSprite). statepins reads the binding of the last translate.sh all: it runs
# once more after it when it added pins.
if runs 4; then
step "4/7 TF.State's MessagePack glue and state pins at $new, then regenerate tfex/gen"
"$here/translate.sh" state-msgpack "$new" >>"$log" 2>&1 || echo "new-tag.sh: translate.sh state-msgpack failed (see $log)" >&2
"$here/translate.sh" all "$new" >>"$log" 2>&1 || echo "new-tag.sh: translate.sh all failed (see $log)" >&2
raw=$(mktemp -d); tar xzf "$repo/tools/re/rawdump.tgz" -C "$raw"
pins0=$(shasum "$here/state-pins.toml")
(cd "$repo/tools/re" && python3 statepins.py "$raw/raw") >>"$log" 2>&1 || echo "new-tag.sh: statepins.py failed (needs .cache/towerfall-pc.il; see $log)" >&2
rm -rf "$raw"
if [ "$(shasum "$here/state-pins.toml")" != "$pins0" ]; then
  "$here/translate.sh" all "$new" >>"$log" 2>&1 || echo "new-tag.sh: translate.sh all failed (see $log)" >&2
fi
git -C "$repo" diff --stat -- hw-probe/exlaunch/source/program/tfex/gen tools/tfex/binding.json > "$cache/gen-diff-$fo-$fn.txt"
git -C "$repo" status --short -- hw-probe/exlaunch/source/program/tfex/gen >> "$cache/gen-diff-$fo-$fn.txt"
fi
if runs 4b; then
step "4b/7 rule keys at $new (every [service] / [fortrise] / [invalid] key and [[unit]] member resolves; unused ones listed)"
baseline
rc=0; "$here/translate.sh" rules "$new" >>"$log" 2>&1 || rc=$?
if [ "$rc" = 3 ]; then
  if [ -s "$base" ]; then fresh="$(comm -13 <(grep -v '^#' "$base") <(unresolved "$rep/rule-keys-$fn.md"))"; else fresh="$(unresolved "$rep/rule-keys-$fn.md")"; fi
  n_old=$(grep -vc '^#' "$base" 2>/dev/null || echo 0)
  if [ -n "$fresh" ]; then
    echo "$fresh" >&2
    echo "new-tag.sh: rule keys that resolved at $old and no longer do at $new (above; tools/tfex/reports/rule-keys-$fn.md): fix or delete them," >&2
    echo "  then resume: TFEX_FROM=4b tools/tfex/new-tag.sh $new $old" >&2; exit 1
  fi
  echo "new-tag.sh: no new unresolved rule keys ($n_old unresolved at $old already: older debt, tools/tfex/reports/rule-keys-$fn.md)" >&2
elif [ "$rc" != 0 ]; then echo "new-tag.sh: the rule-key check failed (see $log)" >&2; exit 1; fi
fi
if runs 5; then
step "5/7 manifest prune and check at $new (entries whose members are all translated now go: tfexgen_members.txt)"
python3 "$here/manifest.py" prune "$new" >>"$log" 2>&1 || echo "new-tag.sh: manifest prune failed (see $log)" >&2
python3 "$here/manifest.py" check "$new" > "$cache/manifest-check-$fo-$fn.txt" 2>&1 || true
fi
if runs 5b; then
step "5b/7 backlog at $new (docs/TFEX-BACKLOG.md: every member generated, replaced, hand, skipped or in a step of tools/tfex/backlog.toml)"
if ! python3 "$here/backlog.py" --tag "$new" --check >>"$log" 2>&1; then
  python3 "$here/backlog.py" --tag "$new" --list unassigned >&2
  python3 "$here/backlog.py" --tag "$new" --list patch >&2
  echo "new-tag.sh: TF.EX code at $new in no step, or Harmony patches with no unit (above): a step in tools/tfex/backlog.toml, a" >&2
  echo "  [[unit]] hooks = true in translate.toml for a patch, or a [skip] in overrides.toml" >&2
fi
fi
build="skipped (TFEX_FROM)"
if runs 6; then
step "6/7 module build"
if [ -n "$TFEX_NO_BUILD" ]; then build="skipped"
elif "$repo/tools/build-probe.sh" >>"$log" 2>&1; then build="OK"; else build="FAILED (see ${log#"$repo"/})"; fi   # (the log relative to the repo: no home path in the committed report)
[ "${build#FAILED}" = "$build" ] || grep -h 'error:' "$HOME"/.cache/tfx-build/"$(basename "$repo")"-*/build.log 2>/dev/null | sed 's|.*/source/program/||' | sort -u | head -20 >&2
fi
step "7/7 summary"
python3 "$here/new-tag.py" --json "$cache/update-$fo-$fn.json" --old "$old" --new "$new" \
  --old-sha "$(git -C "$sub" rev-parse --short "$old^{commit}")" --new-sha "$(git -C "$sub" rev-parse --short "$new^{commit}")" \
  --commits "$(git -C "$sub" log --oneline "$old..$new" | wc -l | tr -d ' ')" \
  --cov-old "$rep/translator-coverage-$fo.md" --cov-new "$rep/translator-coverage-$fn.md" \
  --gen-diff "$cache/gen-diff-$fo-$fn.txt" --manifest-check "$cache/manifest-check-$fo-$fn.txt" --build "$build" \
  > "$rep/$fo-$fn-summary.md"
echo "summary: tools/tfex/reports/$fo-$fn-summary.md (full report: $fo-$fn.md; log: $log)" >&2
echo "commit when it builds: git add vendor/tfex tools/tfex/reports (rule-keys-, inputs-) tools/tfex/binding.json hw-probe/exlaunch/source/program/tfex/gen docs/TFEX-BACKLOG.md" >&2
