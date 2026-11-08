#!/bin/bash
# update-test.sh: TF.NX's updater end to end in Ryujinx (hidden with XPLAY_VISIBLE=0, as every harness script), against a test server that
# answers as GitHub's releases do (tools/update-test/server.py): a release one build newer than this checkout's, made from its own build
# (hw-probe/exlaunch/deploy: the module, one TF.EX file, xplay/tfnx/meta.json, a README at the root that must be skipped). hw.txt
# "updatetest" walks the title to the main menu, the update dialog starts as AUTO UPDATE starts it, then OPTIONS shows the TF.NX entry.
# Prints one line:  UPDATETEST PASS|FAIL: <reason> | shot <png> | logs <dir>
# UPDATETEST_SLOW=<s>: the zip over about s seconds; a freeze of the game meanwhile (the hang watchdog's line) is a FAIL, progress.png the
# dialog mid-download.
# UPDATETEST_FAIL=504: the download fails as GitHub's did minutes after a release was made (504): PASS is the failure logged, the
# dialog gone, the game running on to OPTIONS (2026-11-08: the console crashed removing the dialog), nothing installed.
# PASS: no guest exception; the check, the download, the install, the restart step and OPTIONS logged; every file in place on the
# emulator's SD card with the zip's bytes. Afterwards every file the install wrote (and its .old) is removed from the emulator's SD card
# (Ryujinx runs the module from its own mods folder; the SD copies would only linger).
here=$(cd "$(dirname "$0")/.." && pwd); cd "$here"
out=${XPLAY_OUT:-/tmp/xplay-runs-$(printf '%s' "$here" | shasum | cut -c1-8)${XPLAY_RJSLOT:+-$XPLAY_RJSLOT}}
logs="$out/runs/update-$(date +%H%M%S)"; mkdir -p "$logs"
source tools/ryujinx-lock.sh
source tools/rj-slot.sh; sdroot="$RJ/sdcard"; sd="$sdroot/xplay"
d=hw-probe/exlaunch/deploy
[ -f "$d/subsdk9" ] || { echo "UPDATETEST FAIL: no build in $d | logs $logs"; exit 0; }
tfex=$(sed -n 's/.*TFEX_VERSION "\(.*\)".*/\1/p' hw-probe/exlaunch/source/program/tfex/gen/tfexgen_version.hpp)
build=$(sed -n 's/^TFNX_BUILD := //p' hw-probe/exlaunch/config.mk)
version="$tfex.$((build + 1))"
stage="$logs/release"; mkdir -p "$stage/atmosphere/contents/0100A1C00359C000/exefs" "$stage/xplay/tfex" "$stage/xplay/tfnx"
cp "$d/subsdk9" "$d/main.npdm" "$stage/atmosphere/contents/0100A1C00359C000/exefs/"
cp vendor/tfex/Changelog/CHANGELOG.md "$stage/xplay/tfex/CHANGELOG.md" 2>/dev/null || echo "changelog" > "$stage/xplay/tfex/CHANGELOG.md"
echo "the zip's root: never written by the updater" > "$stage/README.txt"
printf '{"name": "TF.NX", "version": "%s", "tfex": "%s", "game": "1.4.1.6", "notes": "UPDATE TEST: A ONE-LINE NOTE"}\n' "$version" "$tfex" > "$stage/xplay/tfnx/meta.json"
(cd "$stage" && COPYFILE_DISABLE=1 zip -qr -X "../TF.NX-v$version.zip" .)
files=$(cd "$stage" && find atmosphere xplay -type f)
port=${UPDATETEST_PORT:-18471}
python3 tools/update-test/server.py "$port" "$version" "$logs/TF.NX-v$version.zip" > "$logs/server.log" 2>&1 &
srv=$!
trap 'kill $srv 2>/dev/null' EXIT
sleep 1
printf 'updatetest\nupdatesource http://127.0.0.1:%s/releases\n' "$port" > "$sd/hw.txt"
rm -f "$sd/script.txt"
tools/rebaseline-save.sh >/dev/null || exit 1
cfg=$(python3 -c "
import json,sys; c=json.load(open(sys.argv[1]))
print(json.dumps({'vsync_mode': 2, 'enable_custom_vsync_interval': True, 'custom_vsync_interval': 60, 'audio_backend': 'Dummy',
                  'update_checker_type': 'Off', 'input_config': c['input_config']}))" "$RJ/Config.json") || exit 1
until='\[update\] TEST: done|\[update\] ERROR'; [ -n "${UPDATETEST_FAIL:-}" ] && until='\[update\] TEST: done'
XPLAY_RJCONFIG="$cfg" XPLAY_UNTIL="$until" XPLAY_LOGKEEP="$logs/ryujinx.log" XPLAY_EXLAUNCH="$here/hw-probe/exlaunch" XPLAY_MOD=hw-probe \
  XPLAY_TAG=hw tools/run-probe.sh "${UPDATETEST_SECS:-180}" > "$logs/run-probe.out" 2>&1 &
rp=$!
# a screenshot of the dialog (its first phase) and of OPTIONS (3 s after its line)
wb=${TMPDIR:-/tmp}/xplay-winid; [ "$wb" -nt "$here/tools/winid.swift" ] || swiftc -O -o "$wb" "$here/tools/winid.swift" 2>/dev/null
t0=$(date +%s); live=; shot1=; shot2=; shot0=
while kill -0 $rp 2>/dev/null; do
  pid=$(pgrep -n -x "$RJ_NAME")
  [ -z "$live" ] && [ -n "$pid" ] && live=$(lsof -p "$pid" -Fn 2>/dev/null | sed -n 's/^n\(.*\/Ryujinx_[^/]*\.log\)$/\1/p' | head -1)
  if [ -n "$live" ] && [ "$(stat -f %m "$live")" -ge "$t0" ]; then
    if [ -z "$shot0" ] && [ -n "${UPDATETEST_SLOW:-}" ] && /usr/bin/grep -aq '\[update\] the download starts' "$live"; then   # mid-download: the dialog's progress
      sleep 10; w=$("$wb" "$pid" 2>/dev/null); [ -n "$w" ] && screencapture -x -o -l "$w" "$logs/progress.png" 2>/dev/null; shot0=1
    fi
    if [ -z "$shot1" ] && /usr/bin/grep -aq '\[update\] RESTART\|\[update\] installed' "$live"; then
      w=$("$wb" "$pid" 2>/dev/null); [ -n "$w" ] && screencapture -x -o -l "$w" "$logs/dialog.png" 2>/dev/null; shot1=1
    fi
    if [ -z "$shot2" ] && /usr/bin/grep -aq '\[update\] TEST: the TF.NX entry selected' "$live"; then
      sleep 1; w=$("$wb" "$pid" 2>/dev/null); [ -n "$w" ] && screencapture -x -o -l "$w" "$logs/shot.png" 2>/dev/null; shot2=1
    fi
  fi
  sleep 0.5
done
wait $rp
log="$logs/ryujinx.log"
# the files in place, then gone again (the emulator's SD card as it was)
bad=""
for f in $files; do cmp -s "$stage/$f" "$sdroot/$f" || bad="$bad $f"; done
[ -f "$sdroot/README.txt" ] && cmp -s "$stage/README.txt" "$sdroot/README.txt" && bad="$bad README.txt(written)"
for f in $files; do rm -f "$sdroot/$f" "$sdroot/$f.old" "$sdroot/$f.dl"; done
rm -rf "$sdroot/atmosphere/contents/0100A1C00359C000" "$sd/tfnx" "$sd/update"; rmdir "$sdroot/atmosphere/contents" "$sdroot/atmosphere" "$sd/tfex" 2>/dev/null
res() { echo "UPDATETEST $1$([ -f "$logs/shot.png" ] && echo " | shot $logs/shot.png") | logs $logs"; exit 0; }
[ -f "$log" ] || res "FAIL: no Ryujinx log ($(grep -a 'run ended' "$logs/run-probe.out" | tail -1))"
grep -aq '\[hang\] the game froze' "$log" && res "FAIL: the game froze ($(grep -a '\[hang\] the game froze' "$log" | head -1 | sed 's/.*\[hang\] //' | cut -c1-120))"
grep -aq "Unhandled exception" "$log" && res "FAIL: the guest crashed ($(grep -a 'Unhandled exception' "$log" | head -1 | cut -c1-160))"
if [ -n "${UPDATETEST_FAIL:-}" ]; then
  grep -aq "\[update\] ERROR: the download failed (status $UPDATETEST_FAIL" "$log" || res "FAIL: no '[update] ERROR: the download failed (status $UPDATETEST_FAIL' line"
  grep -aq '\[update\] TEST: the TF.NX entry selected' "$log" || res "FAIL: the game didn't go on to OPTIONS after the failure"
  grep -aq '\[update\] installed' "$log" && res "FAIL: something was installed"
  res "PASS: the download failed ($UPDATETEST_FAIL), logged, the game went on to OPTIONS"
fi
grep -aq '\[update\] ERROR' "$log" && res "FAIL: $(grep -a '\[update\] ERROR' "$log" | head -1 | sed 's/.*\[update\] //' | cut -c1-200)"
for want in "check: TF.NX .* the latest release is $version" "DOWNLOADING V$version" "downloaded [0-9]* bytes" "installed TF.NX $version" ".*the game must be restarted" "the dialog dismissed" "TEST: OPTIONS" "TEST: the TF.NX entry selected"; do
  grep -aq "\[update\] $want" "$log" || res "FAIL: no '[update] $want' line"
done
[ -z "$bad" ] || res "FAIL: on the SD card not as in the zip:$bad"
res "PASS: TF.NX $version checked, downloaded, installed ($(echo $files | wc -w | tr -d ' ') files), restart step, OPTIONS"
