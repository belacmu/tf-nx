#!/bin/bash
# lobby-keys.sh A|B [key ...]: presses the keys (0.5 s apart) on page A or B of lobby-start.sh, then saves a screenshot to $OUT/<A|B>.png
OUT=${OUT:-$PWD/lobby-out}
P=$1; shift
[ "$P" = A ] && U=localhost:19400/LA || U=localhost:19500/LB
for k in "$@"; do curl -s "$U/key?k=$k" >/dev/null; sleep 0.5; done
sleep 1; curl -s "$U/shot" -o "$OUT/$P.png"
