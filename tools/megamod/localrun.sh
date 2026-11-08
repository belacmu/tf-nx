#!/bin/bash
# localrun.sh <players> <mode> [extra hw.txt lines...]: one local-play run of the megamod in Ryujinx (hidden with XPLAY_VISIBLE=0, as
# every harness script): hw.txt "localvs <players> <mode>" (megamod/localtest.cpp walks controllers 1..<players> to a local match on the
# 2-8 roll call; mode 3 LAST MAN STANDING, 5 TEAM DEATHMATCH, 6 TOWERBALL), until 30 s into the match. Prints one line:
#   LOCALRUN <players> <mode> PASS|FAIL: <reason> | logs <dir>
# PASS: no guest exception, the level reached with <players> players in <mode>, 30 s of it. XPLAY_RJSLOT works as for the other scripts.
set -u
here=$(cd "$(dirname "$0")/../.." && pwd); cd "$here"
players=${1:?usage: localrun.sh <players> <mode> [hw.txt lines]}; mode=${2:?mode}; shift 2
out=${XPLAY_OUT:-/tmp/xplay-runs-$(printf '%s' "$here" | shasum | cut -c1-8)${XPLAY_RJSLOT:+-$XPLAY_RJSLOT}}
logs="$out/runs/local-$(date +%H%M%S)"; mkdir -p "$logs"
source tools/ryujinx-lock.sh
source tools/rj-slot.sh; sd="$RJ/sdcard/xplay"
# <mode> a name (TEAMS+): a custom mode, GAME MODE scrolled to it (hw.txt localmode); the level must be in the mode it showed
num=$mode; case "$mode" in *[!0-9]*) num=0;; esac
{ echo "localvs $players $num"; [ "$num" = 0 ] && echo "localmode $mode"; printf '%s\n' "$@"; } > "$sd/hw.txt"
rm -f "$sd/script.txt"
tools/rebaseline-save.sh >/dev/null || exit 1
# LOCALRUN_SHOTAT=<regex>: a screenshot at that log line (below). Ryujinx's controllers for the run: the slot's (6 at most) plus copies of its last one up to <players> (the game makes a pad only for a
# controller the system has; run-probe restores Config.json after the run). LOCALRUN_EXACTPADS=1: exactly <players> controllers (the
# other npad slots empty, as a console with that many: LoaderAI's AI slots, tfex/AiSlotButton.cpp)
cfg=$(python3 -c "
import json,sys; c=json.load(open(sys.argv[1])); ic=c['input_config']; n=int(sys.argv[2])
while len(ic) < n: e=dict(ic[-1]); e['player_index']='Player%d' % (len(ic)+1); ic.append(e)
if sys.argv[3] == '1': del ic[n:]
print(json.dumps({'vsync_mode': 2, 'enable_custom_vsync_interval': True, 'custom_vsync_interval': 60, 'audio_backend': 'Dummy',
                  'update_checker_type': 'Off', 'input_config': ic}))" "$RJ/Config.json" "$players" "${LOCALRUN_EXACTPADS:-0}") || exit 1
XPLAY_RJCONFIG="$cfg" XPLAY_UNTIL='\[mmtest\] level ran 30 s' XPLAY_LOGKEEP="$logs/ryujinx.log" XPLAY_EXLAUNCH="$here/hw-probe/exlaunch" XPLAY_MOD=hw-probe \
  XPLAY_TAG=hw tools/run-probe.sh "${LOCALRUN_SECS:-240}" > "$logs/run-probe.out" 2>&1 &
rp=$!
# LOCALRUN_SHOTAT=<regex> (as smoke.sh's SMOKE_SHOTAT): a screenshot of the window LOCALRUN_SHOTDELAY s (2) after that line of this run's log -> $logs/shot.png
# (LOCALRUN_SHOTN=<n>: after its n-th match); LOCALRUN_SHOTAT2=<regex>: a second one after that, LOCALRUN_SHOTDELAY2 s (2) after its line -> $logs/shot2.png
# (this run's Ryujinx: the newest instance started after this point; its log found through the files it has open)
if [ -n "${LOCALRUN_SHOTAT:-}" ]; then
  wb=${TMPDIR:-/tmp}/xplay-winid; [ "$wb" -nt "$here/tools/winid.swift" ] || swiftc -O -o "$wb" "$here/tools/winid.swift" 2>/dev/null
  t0=$(date +%s); live=; k=1
  while kill -0 $rp 2>/dev/null; do
    pid=$(pgrep -n -x "$RJ_NAME")
    [ -z "$live" ] && [ -n "$pid" ] && live=$(lsof -p "$pid" -Fn 2>/dev/null | sed -n 's/^n\(.*\/Ryujinx_[^/]*\.log\)$/\1/p' | head -1)
    if [ "$k" = 1 ]; then re=$LOCALRUN_SHOTAT; n=${LOCALRUN_SHOTN:-1}; d=${LOCALRUN_SHOTDELAY:-2}; f=shot.png
    else re=$LOCALRUN_SHOTAT2; n=1; d=${LOCALRUN_SHOTDELAY2:-2}; f=shot2.png; fi
    if [ -n "$live" ] && [ "$(stat -f %m "$live")" -ge "$t0" ] && [ "$(/usr/bin/grep -acE "$re" "$live")" -ge "$n" ]; then
      sleep "$d"; w=$("$wb" "$pid" 2>/dev/null); [ -n "$w" ] && screencapture -x -o -l "$w" "$logs/$f" 2>/dev/null
      [ "$k" = 1 ] && [ -n "${LOCALRUN_SHOTAT2:-}" ] && { k=2; continue; }
      break
    fi
    sleep 0.5
  done
fi
wait $rp
log="$logs/ryujinx.log"
res() { echo "LOCALRUN $players $mode $1$([ -f "$logs/shot.png" ] && echo " | shot $logs/shot.png")$([ -f "$logs/shot2.png" ] && echo " | shot2 $logs/shot2.png") | logs $logs"; exit 0; }
[ -f "$log" ] || res "FAIL: no Ryujinx log ($(grep -a 'run ended' "$logs/run-probe.out" | tail -1))"
grep -aq "Unhandled exception" "$log" && res "FAIL: the guest crashed ($(grep -a 'Unhandled exception' "$log" | head -1 | cut -c1-160))"
lv=$(grep -a '\[mmtest\] level:' "$log" | head -1 | sed 's/.*\[mmtest\] //')
[ -n "$lv" ] || res "FAIL: no level reached (last screen: $(grep -a '\[mmtest\] screen' "$log" | tail -1 | sed 's/.*\[mmtest\] //'))"
# (TowerBall plays mode 6 masked as 5: its session line instead)
want=$mode; if [ "$num" = 0 ]; then want=$(grep -ao "\[mmtest\] GAME MODE: $mode (mode [0-9]*)" "$log" | grep -o 'mode [0-9]*)' | tr -dc 0-9); [ -n "$want" ] || res "FAIL: GAME MODE never showed $mode"; fi
[ "$mode" = 6 ] && { want=5; grep -aq '\[towerball\] mode-6 session live' "$log" || res "FAIL: no TowerBall session ($lv)"; }
ai=$(printf '%s\n' "$@" | sed -n 's/^localai \([0-9]*\).*/\1/p' | tail -1)   # (hw.txt localai <n>: LoaderAI's AI players join too)
ail=$(printf '%s\n' "$@" | sed -n 's/^localaisteps \(.*\)/\1/p' | tail -1 | tr -cd L | wc -c | tr -d ' ')   # (localaisteps' L presses: an AI each, filling)
ai=$(( ${ai:-0} + ${ail:-0} > 8 - players ? 8 - players : ${ai:-0} + ${ail:-0} ))
case "$lv" in "level: $((players + ${ai:-0})) players"*", mode $want,"*) ;; *) res "FAIL: $lv";; esac
grep -aq '\[mmtest\] level ran 30 s' "$log" || res "FAIL: the level didn't run 30 s ($lv)"
res "PASS: $lv"
