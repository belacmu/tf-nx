#!/bin/bash
# toggle-test.sh: FortRise's TOGGLE MODS on the Switch, two starts in Ryujinx (hidden, as every harness script). Each start walks hw.txt
# "lobbyui 6" (tfex/MainMenuPatch.cpp AutoToggle: MODS -> TOGGLE MODS -> the first mod folder ON <-> OFF -> back). The first start turns
# that folder off (sd:/xplay/mods/blacklist.txt written, the restart notice); the second starts with it off (the registry lists it, not
# loaded) and turns it back on, so the emulator's card ends as it began. Prints one line:
#   TOGGLE PASS|FAIL: <reason> | shot <png> | logs <dir>
# (shot: the TOGGLE MODS screen after the first start's toggle). XPLAY_RJSLOT works as for the other scripts.
set -u
here=$(cd "$(dirname "$0")/../.." && pwd); cd "$here"
out=${XPLAY_OUT:-/tmp/xplay-runs-$(printf '%s' "$here" | shasum | cut -c1-8)${XPLAY_RJSLOT:+-$XPLAY_RJSLOT}}
logs="$out/runs/toggle-$(date +%H%M%S)"; mkdir -p "$logs"
source tools/ryujinx-lock.sh
source tools/rj-slot.sh; sd="$RJ/sdcard/xplay"
res() { echo "TOGGLE $1$([ -f "$logs/shot.png" ] && echo " | shot $logs/shot.png") | logs $logs"; exit 0; }
[ -s "$sd/mods/blacklist.txt" ] && grep -q '"' "$sd/mods/blacklist.txt" && res "FAIL: $sd/mods/blacklist.txt already turns a mod off (start from none)"
wb=${TMPDIR:-/tmp}/xplay-winid; [ "$wb" -nt "$here/tools/winid.swift" ] || swiftc -O -o "$wb" "$here/tools/winid.swift" 2>/dev/null
for run in 1 2; do
  echo "lobbyui 6" > "$sd/hw.txt"; rm -f "$sd/script.txt"
  tools/rebaseline-save.sh >/dev/null || res "FAIL: the save couldn't be rebaselined"
  XPLAY_UNTIL='\[ui\] toggle walk: done' XPLAY_LOGKEEP="$logs/ryujinx-$run.log" XPLAY_EXLAUNCH="$here/hw-probe/exlaunch" XPLAY_MOD=hw-probe \
    XPLAY_TAG=hw tools/run-probe.sh "${TOGGLE_SECS:-180}" > "$logs/run-probe-$run.out" 2>&1 &
  rp=$!
  if [ $run = 1 ]; then   # the screenshot: TOGGLE MODS with the folder just switched (as localrun.sh's)
    t0=$(date +%s); live=
    while kill -0 $rp 2>/dev/null; do
      pid=$(pgrep -n -x "$RJ_NAME")
      [ -z "$live" ] && [ -n "$pid" ] && live=$(lsof -p "$pid" -Fn 2>/dev/null | sed -n 's/^n\(.*\/Ryujinx_[^/]*\.log\)$/\1/p' | head -1)
      if [ -n "$live" ] && [ "$(stat -f %m "$live")" -ge "$t0" ] && /usr/bin/grep -aq 'press A (that folder' "$live"; then sleep 1;
        w=$("$wb" "$pid" 2>/dev/null); [ -n "$w" ] && screencapture -x -o -l "$w" "$logs/shot.png" 2>/dev/null; break
      fi
      sleep 0.5
    done
  fi
  wait $rp
  log="$logs/ryujinx-$run.log"
  [ -f "$log" ] || res "FAIL: start $run: no Ryujinx log"
  grep -aq "Unhandled exception" "$log" && res "FAIL: start $run: the guest crashed ($(grep -a 'Unhandled exception' "$log" | head -1 | cut -c1-160))"
  grep -aq '\[ui\] toggle walk: on TOGGLE MODS' "$log" || res "FAIL: start $run: TOGGLE MODS didn't open ($(grep -a '\[ui\] toggle walk' "$log" | tail -1 | sed 's/.*\[ui\] //'))"
  grep -aq '\[ui\] toggle walk: done$' "$log" || res "FAIL: start $run: the walk didn't finish"
done
l1="$logs/ryujinx-1.log"; l2="$logs/ryujinx-2.log"
grep -aq 'blacklist.txt written: 1 folder(s) off' "$l1" || res "FAIL: start 1 didn't write one folder off ($(grep -a 'blacklist.txt' "$l1" | tail -1 | sed 's/.*\[mods\] //'))"
grep -aq 'TOGGLE MODS changed which mods load' "$l1" || res "FAIL: start 1: no restart notice"
off=$(grep -a 'NOT loaded): turned off' "$l2" | head -1 | sed 's/.*\[mods\]   //')
[ -n "$off" ] || res "FAIL: start 2 didn't start with a folder off"
grep -aq 'blacklist.txt written: 0 folder(s) off' "$l2" || res "FAIL: start 2 didn't turn it back on"
res "PASS: start 1 turned a folder off; start 2 began with $off, turned it on again"
