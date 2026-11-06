#!/bin/bash
# TF.EX's InputCodecTests on the generated InputCodec (tfexgen_Auto_InputCodec.*), compiled for the host: tools/tfex/translator/test/codec-test.sh
set -e
here="$(cd "$(dirname "$0")" && pwd)"; repo="$(cd "$here/../../../.." && pwd)"
prog="$repo/hw-probe/exlaunch/source/program/tfex"; out="$here/out/codec"; mkdir -p "$out"
clang++ -std=c++20 -O1 -DTFEXGEN_TEST=1 -Wall -Wno-unused-function -I"$prog/gen" -I"$prog/runtime" -I"$prog" \
  "$here/codec_test.cpp" "$prog/gen/tfexgen_Auto_InputCodec.cpp" -o "$out/codec_test"
"$out/codec_test"
