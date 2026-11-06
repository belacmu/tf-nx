#!/bin/bash
# menu-walk.sh [hw.txt lines...]: a menu-only Ryujinx run (no browser page): the given hw.txt lines (default: the TF.EX screens walk,
# "lobbyui 3"), ended when the guest log shows "screens walk: done" ($MW_UNTIL), a crash, or the time limit
# ($MW_SECS, 180). Holds the Ryujinx lock; the SD's hw.txt is put back afterwards. Prints the [screens]/[ui]/[pc]/[menu] lines.
# MW_SERVER=<ws(s) url> | official: TF.EX's server setting for this run (its NetplaySettings.Server in sd:/xplay/TF.EX.settings.json, put
# back afterwards); "official" is TF.EX's own NetplayPreferences.OfficialServer, read from vendor/tfex (the real TF.EX server: its
# leaderboard and profiles). Unset: whatever the setting holds (the harness's local server).
set -u
here=$(cd "$(dirname "$0")/.." && pwd); cd "$here"
out=${XPLAY_OUT:-/tmp/xplay-runs-$(printf '%s' "$here" | shasum | cut -c1-8)${XPLAY_RJSLOT:+-$XPLAY_RJSLOT}}; mkdir -p "$out"
[ $# -gt 0 ] || set -- "lobbyui 3"
source tools/ryujinx-lock.sh
source tools/rj-slot.sh; sd="$RJ/sdcard/xplay"
# the Switch's settings and scripts for this run (put back at the end): hw.txt = the lines given; no menu script, no net.txt stages
# (a leftover script.txt drives the menu itself)
for f in hw.txt script.txt net.txt TF.EX.settings.json; do cp "$sd/$f" "$out/mw-$f.bak" 2>/dev/null || : > "$out/mw-$f.bak"; done
trap 'for f in hw.txt script.txt net.txt TF.EX.settings.json; do cp "$out/mw-$f.bak" "$sd/$f"; done; ryujinx_unlock' EXIT
if [ -n "${MW_SERVER:-}" ]; then
  srv=$MW_SERVER
  [ "$srv" = official ] && srv=$(sed -n 's/.*OfficialServer = "\([^"]*\)".*/\1/p' vendor/tfex/src/network/TF.EX.Domain/Models/NetplayPreferences.cs)
  [ -n "$srv" ] || { echo "menu-walk: no server for MW_SERVER=$MW_SERVER" >&2; exit 1; }
  # the file keeps its fixed size (the JSON, then spaces: the Switch made it at 1024 bytes)
  python3 -c 'import json,sys; p,s=sys.argv[1],sys.argv[2]; t=open(p).read(); d=json.loads(t); d["Server"]=s; j=json.dumps(d,separators=(",",":")); open(p,"w").write(j+" "*max(0,len(t)-len(j)))' "$sd/TF.EX.settings.json" "$srv" || exit 1
  echo "menu-walk: TF.EX's server for this run: $srv" >&2
fi
printf '%s\n' "$@" > "$sd/hw.txt"; : > "$sd/script.txt"; : > "$sd/net.txt"
tools/rebaseline-save.sh >/dev/null || exit 1
XPLAY_UNTIL="${MW_UNTIL:-screens walk: done}" XPLAY_EXLAUNCH="$here/hw-probe/exlaunch" XPLAY_MOD=hw-probe XPLAY_TAG=mw tools/run-probe.sh "${MW_SECS:-180}" > "$out/mw-run.out" 2>&1
grep -o 'run ended: .*' "$out/mw-run.out"
L=$(grep -o 'log: .*' "$out/mw-run.out" | cut -c6-)
[ -f "$L" ] && /usr/bin/grep -a -E '\[(screens|ui|pc|menu)\]|Unhandled exception' "$L" | sed 's/.*\(\[[a-z]*\]\)/\1/' | grep -v '\[menu\] vt \|\[menu\] Image:' | head -40
