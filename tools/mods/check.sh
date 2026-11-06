#!/bin/bash
# check.sh <name> [with|without]: a catalog mod's standard checks (mods/README.md "Checks"), one line each, then one summary line:
#   with      each [check] `with` command of mods/<name>/mod.toml (a localrun / smoke run printing "logs <dir>"), its log searched for every
#             `expect` line: PASS when the run passed and some run showed each line
#   without   a boot without the built mod folders (XPLAY_MODFOLDERS=0): PASS when the mod's "booted" line is absent (dormant)
# Both by default. Runs hidden (XPLAY_VISIBLE=0); waits for the load as AGENTS.md asks. A mod whose folder our tools don't build
# (mods/<name>/mod.toml [folder] build = "") needs its folder on the emulator's SD card for `with`.
set -u
here=$(cd "$(dirname "$0")/../.." && pwd); cd "$here"
name=${1:?usage: tools/mods/check.sh <mod name> [with|without]}
what=${2:-both}
out="test-results/mods/$name"; mkdir -p "$out"
wait_load() { while :; do set -- $(sysctl -n vm.loadavg | tr -d '{}'); awk -v a="$1" -v b="$2" 'BEGIN{exit !(a<12 && b<15)}' && break; sleep 20; done; }
spec=$(python3 tools/mods/catalog.py --checks "$name") || exit 1
fail=0; seen=""
if [ "$what" != without ]; then
  i=0
  while IFS= read -r cmd; do
    i=$((i + 1)); wait_load
    (export XPLAY_VISIBLE=0; eval "$cmd") > "$out/with-$i.out" 2>&1
    res=$(grep -E '^(LOCALRUN|SMOKE) ' "$out/with-$i.out" | tail -1)
    logs=$(grep -oE 'logs [^ ]+' "$out/with-$i.out" | tail -1 | cut -d' ' -f2)
    echo "CHECK $name with: $cmd -> ${res:-no result line ($out/with-$i.out)}"
    case "$res" in *PASS*) ;; *) fail=1 ;; esac
    [ -n "$logs" ] && seen="$seen $logs"
  done < <(sed -n 's/^with //p' <<<"$spec")
  while IFS= read -r e; do
    [ -z "$e" ] && continue
    if [ -n "$seen" ] && grep -rqaF -- "$e" $seen 2>/dev/null; then echo "CHECK $name expect: '$e' seen"
    else echo "CHECK $name expect: '$e' NOT seen"; fail=1; fi
  done < <(sed -n 's/^expect //p' <<<"$spec")
fi
if [ "$what" != with ]; then
  wait_load
  XPLAY_MODFOLDERS=0 XPLAY_VISIBLE=0 XPLAY_EXLAUNCH="$here/hw-probe/exlaunch" XPLAY_MOD=hw-probe XPLAY_TAG=without \
    XPLAY_UNTIL="MODS button added" tools/run-probe.sh 90 > "$out/without.out" 2>&1
  log=$(grep -oE '/Users/[^ ]*Ryujinx[^ ]*\.log' "$out/without.out" | tail -1)
  if [ -z "$log" ]; then echo "CHECK $name without: no run ($out/without.out)"; fail=1
  elif grep -qaF "[mods] $name booted" "$log"; then echo "CHECK $name without: FAIL: booted without its folder"; fail=1
  elif ! grep -qa "code mods booted" "$log"; then echo "CHECK $name without: FAIL: the boot didn't reach the mods ($log)"; fail=1
  else echo "CHECK $name without: dormant ($(grep -a 'code mods booted' "$log" | tail -1 | sed 's/.*booted/booted/'))"; fi
fi
echo "CHECK $name $([ $fail = 0 ] && echo PASS || echo FAIL) | $out"
exit $fail
