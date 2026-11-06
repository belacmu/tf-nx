#!/bin/bash
# statedb.sh: rule 3 of docs/tfstate-rollback-measurement.md. Rebuilds the TF.State layout database and its outputs from the kept inputs
# (slotmaps.tgz: the runs' slot maps; rawdump.tgz: step B's raw dump and its run's slot maps) and prints every check:
#   accessors.json, layoutdb.json, layoutpredict.json, stateconflicts (sources, overlaps, leave-one-out), rawverify, stateconfirm
#   (placed vs confirmed), statescore,
#   tools/tfex/state-pins.toml, tfexgen_state_fieldcheck.inc
# then whether the committed files came out the same (a difference is a regression, or a change to commit with its reason).
# Exit 1 on a conflict or a difference. A translate (tools/tfex/translate.sh all v0.21.1) is still needed after a pin change.
set -e
cd "$(dirname "$0")"
work=$(mktemp -d "${TMPDIR:-/tmp}/statedb.XXXXXX")
trap '[ -n "$work" ] && [ -d "$work" ] && rm -r "$work"' EXIT
mkdir "$work/smaps" "$work/raw"
tar xzf slotmaps.tgz -C "$work/smaps"
tar xzf rawdump.tgz -C "$work/raw"
python3 accessors.py
python3 layoutdb.py "$work"/smaps/*.txt "$work/raw/slots.txt"
python3 layoutpredict.py
rc=0
python3 stateconflicts.py || rc=1
python3 rawverify.py "$work/raw/raw" | head -2
python3 stateconfirm.py "$work/raw/raw" --json "$work/confirm.json" || rc=1
python3 staterisk.py "$work/confirm.json" | head -12 || true   # (rule 4: informational here; a run checks it)
python3 statescore.py --all | grep -v "^layoutdb vs earlier leads"
python3 statepins.py "$work/raw/raw" | cut -c1-200
python3 ../tfex/state-fieldcheck.py
out="accessors.json layoutdb.json layoutpredict.json ../tfex/state-pins.toml ../../hw-probe/exlaunch/source/program/tfex/runtime/tfexgen_state_fieldcheck.inc"
if git diff --quiet -- $out; then echo "statedb: the committed files reproduced exactly"
else echo "statedb: DIFFERS from the committed files:"; git diff --stat -- $out; rc=1; fi
[ $rc = 0 ] && echo "statedb: OK" || echo "statedb: FAILED"
exit $rc
