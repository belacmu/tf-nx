#!/bin/bash
# compile-gen.sh <tag>: every unit the coverage run translates, compiled (syntax and types) with the Switch toolchain and the module's
# flags. "Translatable" in the coverage report means no blocker; this checks it also compiles. Prints the failing files and errors.
set -u
here="$(cd "$(dirname "$0")" && pwd)"; repo="$(cd "$here/../../../.." && pwd)"
tag="${1:-v0.21.0}"
out="$here/out/compile"; rm -rf "$out"; mkdir -p "$out/program/tfex/gen"
prog="$repo/hw-probe/exlaunch/source/program"
# the module's tree around tfex/gen (headers the generated code includes)
rsync -a --exclude gen "$prog/" "$out/program/" >/dev/null
TFEX_COVERAGE_GEN="$out/program/tfex/gen" TFEX_COVERAGE_OUT=/dev/null "$repo/tools/tfex/translate.sh" coverage-raw "$tag" >/dev/null 2>&1
# the headers of units outside coverage's scope (TF.State.Core's State_Api, the FortRise units Fr_* / GlueFr*), from the module's gen/:
# without them 38 files failed on a missing header and every method in them counted as not compiling (fbeec67, 2026-10-28)
for h in "$prog"/tfex/gen/tfexgen_*.hpp; do [ -e "$out/program/tfex/gen/$(basename "$h")" ] || cp "$h" "$out/program/tfex/gen/"; done
CXX=/opt/devkitpro/devkitA64/bin/aarch64-none-elf-g++
src="$repo/hw-probe/exlaunch/source"
flags=(-std=gnu++23 -fsyntax-only -fno-rtti -fno-exceptions -D__SWITCH__ -D__RTLD_6XX__ -DEXL_LOAD_KIND=Module -DEXL_LOAD_KIND_ENUM=2 -DEXL_PROGRAM_ID=0x0100a1c00359c000 -DTFEXGEN_COVERAGE -I"$out/program" -I"$out/program/tfex" -I"$out/program/tfex/gen" -I"$out/program/tfex/runtime" -I"$src" -I"$src/program" -I"$src/lib" -I"$src/nn" -I/opt/devkitpro/libnx/include -w -fmax-errors=40)
fail=0; n=0
for f in "$out"/program/tfex/gen/tfexgen_*.cpp; do
  n=$((n+1))
  if ! $CXX "${flags[@]}" -c "$f" -o /dev/null 2> "$f.err"; then
    fail=$((fail+1)); echo "== $(basename "$f"): $(grep -c 'error' "$f.err") errors"; grep -m3 'error' "$f.err" | sed 's/^.*gen\///' | cut -c1-200
  else rm -f "$f.err"; fi
done
echo "compile-gen: $((n-fail)) of $n generated files compile"
python3 "$here/attribute-errors.py" "$out/program/tfex/gen" > "$out/compile-errors.txt"
echo "compile-gen: $(wc -l < "$out/compile-errors.txt" | tr -d ' ') methods with compile errors -> $out/compile-errors.txt"
cat "$out"/program/tfex/gen/*.err 2>/dev/null | grep -o 'error: .*' | sed 's/[0-9]\+//g' | sort | uniq -c | sort -rn | head -40
