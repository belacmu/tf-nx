#!/bin/bash
# TF.EX -> Switch C++ (tools/tfex/translator, README.md "Translator").
# Usage: tools/tfex/translate.sh models|code|all <tag> [out dir]   |   rules <tag> (rule keys: reports/rule-keys-<tag>.md, exit 3 on unresolved)
#          (<tag>: a tag, commit or ref in vendor/tfex)   (default out: hw-probe/exlaunch/source/program/tfex/gen)
#        tools/tfex/translate.sh coverage <tag>      (compiles every generated unit too: about 3 minutes)
#        tools/tfex/translate.sh fields-glue <tag> (game structs' fields for TF.EX's reflection -> translator/glue/GameFields.cs)
#        tools/tfex/translate.sh state-msgpack <tag> (TF.State's GameState MessagePack formatters -> translator/glue/StateMsgPack.cs)
# The tree comes from split.sh's cache (git archive of vendor/tfex at <tag>); config: tools/tfex/translate.toml.
set -e
here="$(cd "$(dirname "$0")" && pwd)"; repo="$(cd "$here/../.." && pwd)"
dotnet="${DOTNET:-$(command -v dotnet || echo /usr/local/share/dotnet/dotnet)}"
[ $# -ge 2 ] || { sed -n 2,5p "$0" >&2; exit 2; }
what="$1"; tag="$2"; out="${3:-$repo/hw-probe/exlaunch/source/program/tfex/gen}"
jl="$("$here/split.sh" "$tag")"; src="${jl%.jsonl}"; src="${src/split-/src-}/src"
dll="$here/translator/bin/out/translator.dll"
if [ ! -f "$dll" ] || [ -n "$(find "$here/translator" -maxdepth 2 \( -name '*.cs' -o -name '*.csproj' \) -newer "$dll" -not -path '*/test/*' -not -path '*/bin/*' -not -path '*/obj/*')$(find "$repo/hw-probe/exlaunch/source/program/tfex/runtime" -type f -newer "$dll")" ]; then
  "$dotnet" build -c Release -o "$here/translator/bin/out" "$here/translator" > /dev/null || { echo "translate.sh: translator build failed" >&2; exit 1; }
fi
case "$what" in models|code|all|rules|coverage|coverage-raw) python3 "$here/inputs.py" check "$tag" || exit 1 ;; esac   # FortRise DLL, vendor/tfex pointer, sha256 record
export TFEX_LABEL="$tag ($(git -C "$repo/vendor/tfex" rev-parse --short "$tag^{commit}"))"
run() { "$dotnet" "$dll" "$@"; }
case "$what" in
  models) mkdir -p "$out"; run models "$src" "$out" "$here/translate.toml" ;;
  code) mkdir -p "$out"; run code "$src" "$out" "$here/translate.toml" ;;
  all) mkdir -p "$out"; run models "$src" "$out" "$here/translate.toml"; run code "$src" "$out" "$here/translate.toml" ;;
  state-msgpack)   # TF.State's GameState formatters (MessagePack-CSharp's bytes) as glue: translator/glue/StateMsgPack.cs, then translate as usual
    run msgpack-glue "$src" "$here/translator/glue/StateMsgPack.cs" "$here/translate.toml" TF.State.Domain.Models.GameState TF.State.Domain.Models.MatchStats ;;   # + MatchStats[] plain (VersusAwards seed)
  fields-glue)   # TF.EX's reflection over game structs' fields (MatchStatsReport.BuildRawStats) as glue: translator/glue/GameFields.cs
    run fields-glue "$src" "$here/translator/glue/GameFields.cs" "$here/translate.toml" TowerFall.MatchStats
    # FortRise's patch_MatchVariants.StoreVanillaVariantsAsCustom (the FortRise glue's workspace); NoAutobalance: the PC's, not the Switch's
    run fields-glue "$src" "$here/translator/glue/fortrise/GameFieldsFr.cs" "$here/translate.toml" 'TowerFall.MatchVariants:TowerFall.Variant!NoAutobalance' ;;
  rules)   # rule keys (evaluation rec. 2a): every [service] / [fortrise] / [invalid] key and [[unit]] member resolves at <tag> and is
           # used by some translation (else parked = "..."); exit 3 on an unresolved key.
    # "used": looked up while translating all of TF.EX in scope (the coverage run, without its compile step; gen/ untouched)
    t="$(mktemp "${TMPDIR:-/tmp}/rules.XXXXXX")"; mkdir -p "$here/reports"
    TFEX_COVERAGE_OUT=/dev/null TFEX_RULES_OUT="$here/reports/rule-keys-${tag//\//_}.md" run coverage "$src" "$here/translate.toml" 2> "$t" || rc=$?
    grep '^rules:' "$t" >&2 || tail -5 "$t" >&2; rm -f "$t"; exit "${rc:-0}" ;;
  coverage-raw) run coverage "$src" "$here/translate.toml" ;;
  coverage)   # translatable = no blocker AND the generated C++ compiles (translator/test/compile-gen.sh: the Switch toolchain)
    "$here/translator/test/compile-gen.sh" "$tag" | grep "compile-gen:" >&2
    TFEX_COMPILE_ERRORS="$here/translator/test/out/compile/compile-errors.txt" \
    TFEX_COVERAGE_OUT="${TFEX_COVERAGE_OUT:-$here/reports/translator-coverage-${tag//\//_}.md}" run coverage "$src" "$here/translate.toml" ;;
  *) echo "translate.sh: unknown '$what'" >&2; exit 2 ;;
esac
