#!/bin/bash
# Stage-1 test: the generated lobby models against TF.EX itself (the oracle: TF.EX's own model classes + MessagePack, as TF.EX
# reads and writes server JSON). (The hand-written tfex/Models.* it also compared against were retired 2026-10-18.)
# Usage: tools/tfex/translator/test/models-test.sh [tag] (default v0.21.0). Samples: test/samples/{real,synthetic}.
set -e
here="$(cd "$(dirname "$0")" && pwd)"; repo="$(cd "$here/../../../.." && pwd)"; tfex="$repo/tools/tfex"
tag="${1:-v0.21.0}"; dotnet="${DOTNET:-$(command -v dotnet || echo /usr/local/share/dotnet/dotnet)}"
jl="$("$tfex/split.sh" "$tag")"; src="${jl%.jsonl}"; src="${src/split-/src-}/src"
out="$here/out"; rm -rf "$out"; mkdir -p "$out/tfex/gen" "$out/bin"
prog="$repo/hw-probe/exlaunch/source/program/tfex"
cp "$prog"/Json.* "$out/tfex/"   # the module's layout: tfex/gen/ beside tfex/Json.hpp (the runtime includes ../Json.hpp)
"$dotnet" build -c Release -o "$here/oracle/bin/out" -p:TfexSrc="$src" "$here/oracle" > "$out/oracle-build.log" 2>&1 || { tail -20 "$out/oracle-build.log"; exit 1; }
"$tfex/translate.sh" models "$tag" "$out/tfex/gen" >/dev/null
clang++ -std=c++20 -O1 -DTFEXGEN_TEST=1 -Wall -Wno-unused-function -I"$out/tfex/gen" -I"$prog/runtime" -I"$out/tfex" "$here/models_test.cpp" "$out/tfex/gen/tfexgen_Models.cpp" "$out/tfex/Json.cpp" -o "$out/bin/models_test"
fail=0
for set in real synthetic; do
  mkdir -p "$out/$set/oracle" "$out/$set/gen" "$out/$set/hand"
  "$dotnet" "$here/oracle/bin/out/oracle.dll" "$here/samples/$set" "$out/$set/oracle" 2> "$out/$set/oracle.log" || { cat "$out/$set/oracle.log"; fail=1; }
  "$out/bin/models_test" "$here/samples/$set" "$out/$set/gen" "$out/$set/hand"
  python3 "$here/compare.py" "$out/$set" || fail=1
done
exit $fail
