#!/bin/bash
# run-probe.sh <seconds>: one safe run of the probe in Ryujinx (hidden by default, the user 2026-10-22: tests run in the background; XPLAY_VISIBLE=1 shows the window).
#   1. Checks Ryujinx isn't running, and that the TowerFall save data matches the backup (making the
#      backup on first use, in ../.save-backup/, which git ignores).
#   2. Copies the probe's build (deploy/ of its exlaunch checkout) into the crossplay-probe mod and
#      makes it the active native mod.
#   3. Runs TowerFall in a visible window (XPLAY_VISIBLE=0: hidden via keephidden.swift) at a timer-paced 60 fps (Custom vsync), no audio; a normal
#      background window (Ryujinx has no headless mode).
#   4. Makes combined-mod the active native mod again, checks the save data again, and prints the
#      probe's log lines (not the per-frame "T" lines) plus the log's path.
# Environment: XPLAY_EXLAUNCH (default ~/Code/tfx-crossplay-exlaunch), XPLAY_NSP (the base NSP),
#   XPLAY_MOD (the Ryujinx mod folder to deploy into, default crossplay-probe), XPLAY_TAG (the log
#   prefix to print, default xplay; the network probe uses XPLAY_MOD=net-probe XPLAY_TAG=net), XPLAY_STOPFILE (a file whose
#   appearance ends the run; its text = why), XPLAY_UNTIL (an
#   extended regex: end the run as soon as Ryujinx's log matches it or shows an unhandled exception), XPLAY_RJCONFIG (see below),
#   XPLAY_RJSLOT (a private Ryujinx next to the shared one, tools/rj-slot.sh), XPLAY_LOGKEEP (a file to copy the Ryujinx log to).
set -u
here=$(cd "$(dirname "$0")" && pwd)
root=$(dirname "$here")
main=$(dirname "$(git -C "$root" rev-parse --path-format=absolute --git-common-dir)")   # the main checkout (a worktree lives elsewhere)
mods="$(dirname "$main")"
total=${1:?usage: run-probe.sh <seconds>}
exl=${XPLAY_EXLAUNCH:-$HOME/Code/tfx-crossplay-exlaunch}
nsp=${XPLAY_NSP:-$(sed -n 's#^XPLAY_NSP *[:?]*= *##p' ~/.config/tfnx/local.mk 2>/dev/null)}   # your dump of the game: private (AGENTS.md)
[ -n "$nsp" ] || { echo "run-probe: no game file: set XPLAY_NSP, or XPLAY_NSP := <path to your .nsp> in ~/.config/tfnx/local.mk" >&2; exit 1; }
source "$here/rj-slot.sh"; rj=$RJ
save="$rj/bis/user/save"   # TowerFall's is ./0000000000000001
backup=$RJ_BACKUP
activate="$mods/tools/ryujinx-activate.sh"
if [ ! -x "$activate" ]; then   # a git worktree (.claude/worktrees/<name>): the mods' tools sit next to the main checkout
  main="$(git -C "$root" worktree list --porcelain | awk '/^worktree /{sub(/^worktree /,""); print; exit}')"
  activate="$(dirname "$main")/tools/ryujinx-activate.sh"
fi
[ -x "$activate" ] || { echo "no ryujinx-activate.sh (looked next to the checkout and the main checkout)" >&2; exit 1; }
mod=${XPLAY_MOD:-crossplay-probe}
tag=${XPLAY_TAG:-xplay}
probe="$rj/mods/_disabled/0100a1c00359c000/$mod/exefs"

# a caller holding the Ryujinx lock (tools/ryujinx-lock.sh) waits for the previous run's Ryujinx to finish exiting; otherwise a
# running Ryujinx may be the user's: don't start. By process name: `pgrep -f <path>` also matched shells whose command line had it.
# (a private slot's process is Ryujinx<slot>: the two never see each other)
if [ -n "${XPLAY_LOCK_HELD:-}" ]; then for _ in $(seq 1 60); do pgrep -x "$RJ_NAME" >/dev/null || break; sleep 1; done; fi
pgrep -x "$RJ_NAME" >/dev/null && { echo "$RJ_NAME is already running; not starting" >&2; exit 1; }
if [ ! -f "$backup/manifest.txt" ]; then
  mkdir -p "$backup/save" && cp -R "$save/0000000000000001" "$backup/save/" &&
    (cd "$save" && find ./0000000000000001 -type f -exec shasum {} + | sort -k2) > "$backup/manifest.txt"
  echo "backed up the save data to $backup"
fi
check() { (cd "$save" && shasum -c "$backup/manifest.txt" 2>/dev/null) | grep -c ': OK$'; }
want=$(wc -l < "$backup/manifest.txt" | tr -d ' ')
[ "$(check)" = "$want" ] || { echo "save data differs from the backup; not running" >&2; exit 1; }

# XPLAY_EXTRAS=0: the "TF.EX Switch" build (tools/build-probe.sh's deploy-core/, no megamod and none of its game data)
deploy="$exl/deploy"; [ "${XPLAY_EXTRAS:-1}" = 0 ] && deploy="$exl/deploy-core"
mkdir -p "$probe" && cp "$deploy/subsdk9" "$deploy/main.npdm" "$probe/"
# netplay: the PC's versus levels (what TF.EX plays) come from the module's delta over the Switch's own (pclevels.cpp, 2026-11-05): no files;
# XPLAY_PCLEVELS=1 puts the PC's files in the romfs again (tools/pc-levels.sh: a comparison; the module prefers its delta)
if [ "$mod" = hw-probe ] && [ "${XPLAY_PCLEVELS:-0}" = 1 ]; then "$here/pc-levels.sh" "$(dirname "$probe")/romfs" || exit 1   # its count line goes into the run output
elif [ "$mod" = hw-probe ]; then rm -rf "$(dirname "$probe")/romfs/Content/Levels/Versus" "$(dirname "$probe")/romfs/Content/Levels/PCVersus" "$(dirname "$probe")/romfs/.xplay-pc-levels"; fi   # (none: the module's delta makes them, pclevels.cpp)
# the megamod's game data (TowerBall's levels and art, 8-player's levels: tools/megamod/romfs.py), after the line above (it may remove
# Content/Levels/Versus); XPLAY_MEGAMOD_ROMFS=0: none
if [ "$mod" = hw-probe ] && [ "${XPLAY_EXTRAS:-1}" = 0 ]; then python3 "$here/megamod/romfs.py" --clean "$(dirname "$probe")/romfs" || exit 1
elif [ "$mod" = hw-probe ] && [ "${XPLAY_MEGAMOD_ROMFS:-1}" = 1 ]; then python3 "$here/megamod/romfs.py" "$(dirname "$probe")/romfs" || exit 1; fi
RYUJINX_DIR="$rj" "$activate" "$mod" >/dev/null
# TF.EX's mod folder on the SD card, as the console gets it (tools/devserve.list's sd:/xplay/tfex/ lines: CHANGELOG.md, icon.png, the
# NETPLAY icon, the tutorial's slides): an emulator without them crashed at the NETPLAY button (2026-10-29, smoke team 171829)
if [ "$mod" = hw-probe ]; then
  grep -E '^sd:/xplay/tfex/' "$root/tools/devserve.list" | while read -r dst src; do
    [ -f "$root/$src" ] || continue
    out="$rj/sdcard/${dst#sd:/}"; mkdir -p "$(dirname "$out")"
    cmp -s "$root/$src" "$out" || cp "$root/$src" "$out"
  done
  # the mod folders our tools build (tools/mods/folders.list: TowerBall's), as the console's channel has them; XPLAY_EXTRAS=0 removes the extras
  "$root/tools/mods/build-folders.sh" "$rj/sdcard/xplay/mods" || exit 1
  # the console's other mod folders (2026-11-05: its SD has 19 SSB archer packs, Celeste, MexicanPuppet; the emulator had none, and the
  # first mod-archer crash showed only on the console): ../additional-characters/fortrise-mods beside the main checkout, then
  # tools/devserve.list's sd:/xplay/mods/ file lines over them and its `delete sd:/xplay/mods/...` lines, as the channel does.
  # XPLAY_CONSOLEMODS=0 leaves them out (and removes them).
  main=$(cd "$(git -C "$root" rev-parse --git-common-dir)/.." && pwd)
  cm="$main/../additional-characters/fortrise-mods"
  if [ -d "$cm" ]; then
    n=0
    for d in "$cm"/*/; do
      f=$(basename "$d")
      if [ "${XPLAY_CONSOLEMODS:-1}" = 0 ]; then rm -rf "${rj:?}/sdcard/xplay/mods/$f"; continue; fi
      rsync -a --delete "$d" "$rj/sdcard/xplay/mods/$f/"; n=$((n + 1))
    done
    if [ "${XPLAY_CONSOLEMODS:-1}" != 0 ]; then
      grep -E '^sd:/xplay/mods/' "$root/tools/devserve.list" | while read -r dst src; do
        case "$src" in /*) ;; *) src="$main/$src" ;; esac
        [ -f "$src" ] || continue
        out="$rj/sdcard/${dst#sd:/}"; mkdir -p "$(dirname "$out")"; cmp -s "$src" "$out" || cp "$src" "$out"
      done
      echo "console mod folders: $n from $cm"
    fi
  else echo "console mod folders: none ($cm not found)"; fi
  grep -E '^delete sd:/xplay/mods/' "$root/tools/devserve.list" | while read -r _ dst; do rm -rf "${rj:?}/sdcard/${dst#sd:/}"; done
fi

# XPLAY_RJCONFIG='{...}': Ryujinx Config.json values for this run only (restored on exit, however the run ends); XPLAY_RJCONFIG= for none.
# Default (2026-10-17, the user: no more hiding): a normal VISIBLE window, behind the front app (open -g), with Custom vsync at 60 (Ryujinx paces frames by a timer, so a hidden window still runs at
# 60 fps; Switch vsync stalls to ~16 fps hidden, Unbounded runs 4x faster and coincided with the MoltenVK/AGX crashes of 2026-10-15),
# no audio, no update prompt. XPLAY_VISIBLE=0: hidden instead (keephidden; a hidden window draws nothing, so screenshots come out black).
# A hidden app is also throttled by macOS App Nap (~10 fps); once per Mac: `defaults write org.ryujinx.Ryujinx NSAppSleepDisabled -bool YES`
# (undo: `defaults delete org.ryujinx.Ryujinx NSAppSleepDisabled`). Checked below.
XPLAY_RJCONFIG=${XPLAY_RJCONFIG-'{"vsync_mode": 2, "enable_custom_vsync_interval": true, "custom_vsync_interval": 60, "audio_backend": "Dummy", "update_checker_type": "Off"}'}
cfg="$rj/Config.json"
if [ -n "$XPLAY_RJCONFIG" ]; then
  # a backup left by a run that never got to restore it (crash, power-off) holds the user's real settings: put it back first
  [ -f "$cfg.xplay-backup" ] && { echo "run-probe: restoring the Ryujinx config left by an interrupted run"; mv -f "$cfg.xplay-backup" "$cfg"; }
  cp "$cfg" "$cfg.xplay-backup"
  trap 'mv -f "$cfg.xplay-backup" "$cfg"' EXIT
  python3 -c "import json,sys; p=sys.argv[1]; c=json.load(open(p)); c.update(json.loads(sys.argv[2])); json.dump(c, open(p,'w'), indent=2)" "$cfg" "$XPLAY_RJCONFIG" || exit 1
fi

[ "${XPLAY_VISIBLE:-0}" = 1 ] || [ "$(defaults read "$RJ_BUNDLE" NSAppSleepDisabled 2>/dev/null)" = 1 ] ||
  echo "run-probe: WARNING: App Nap is on for $RJ_NAME: a hidden run will crawl (~10 fps). defaults write $RJ_BUNDLE NSAppSleepDisabled -bool YES" >&2
bin=${TMPDIR:-/tmp}/xplay-keephidden2   # 2: takes the bundle id
[ "$bin" -nt "$here/keephidden.swift" ] || swiftc -O -o "$bin" "$here/keephidden.swift"
marker=$(mktemp); touch "$marker"
# A normal window, launched behind the front app (default); XPLAY_VISIBLE=0: hidden the whole time (keephidden; that plus Unbounded vsync
# coincided with the GPU crashes)
if [ "${XPLAY_VISIBLE:-0}" = 1 ]; then open -g -n -a "$RJ_APP" --args ${RJ_ARGS[@]+"${RJ_ARGS[@]}"} "$nsp"; else open -g -j -n -a "$RJ_APP" --args ${RJ_ARGS[@]+"${RJ_ARGS[@]}"} "$nsp"; fi
pid=
for _ in $(seq 1 200); do pid=$(pgrep -n -x "$RJ_NAME") && break; sleep 0.02; done
# hidden from the first moment: an early keephidden (it hides every Ryujinx) covers the pid settling below
[ "${XPLAY_VISIBLE:-0}" = 1 ] || { "$bin" $$ 15 "$RJ_BUNDLE" >/dev/null & }   # tied to this script (the first Ryujinx pid can be a short-lived stage)
# The first pid can be a short-lived launch stage (macOS showed "two equal instances" and the pid reused by another program seconds
# later): keephidden then watched the wrong process and the run ended (and its Ryujinx was killed) when that process went away.
# Take the Ryujinx that is still running a few seconds later.
for _ in $(seq 1 40); do
  sleep 0.25
  p2=$(pgrep -n -x "$RJ_NAME") || continue
  [ "$p2" != "$pid" ] && { echo "run-probe: Ryujinx pid $pid -> $p2"; pid=$p2; }
done
if [ "${XPLAY_VISIBLE:-0}" = 1 ]; then ( end=$((SECONDS + total)); while [ $SECONDS -lt $end ] && kill -0 "$pid" 2>/dev/null; do sleep 0.5; done ) & kh=$!
else "$bin" "$pid" "$total" "$RJ_BUNDLE" & kh=$!; fi
# this Ryujinx's own log (the one it holds open; another instance's run writes a log at the same time): by lsof, else the newest
rjlog() { lsof -p "$pid" -Fn 2>/dev/null | sed -n 's/^n\(.*\/Ryujinx_[^/]*\.log\)$/\1/p' | head -1; }
log=; for _ in $(seq 1 20); do log=$(rjlog); [ -n "$log" ] && break; sleep 0.5; done
# XPLAY_LOGKEEP=<file>: this run's Ryujinx log kept there, hard-linked at once (Ryujinx keeps only its 3 newest logs and deletes the rest
# when an instance starts: with other slots running, this run's log is gone within minutes; the link keeps the file and its growth)
[ -n "${XPLAY_LOGKEEP:-}" ] && [ -n "$log" ] && { rm -f "$XPLAY_LOGKEEP"; ln "$log" "$XPLAY_LOGKEEP" 2>/dev/null; }
why="time limit ($total s)"
if [ -n "${XPLAY_UNTIL:-}" ]; then   # stop early once the log shows this pattern (or a crash)
  while kill -0 "$kh" 2>/dev/null; do
    l=${log:-$(find "$HOME/Library/Logs/Ryujinx" -name '*.log' -newer "$marker" | head -1)}
    if [ -n "$l" ] && /usr/bin/grep -aq -E "$XPLAY_UNTIL|Unhandled exception" "$l"; then why="log matched: $(/usr/bin/grep -aoE "$XPLAY_UNTIL|Unhandled exception" "$l" | head -1)"; sleep 2; kill "$kh" 2>/dev/null; break; fi
    # XPLAY_STOPFILE: the calling harness ends the run early (its contents = why), e.g. once the run can no longer prove anything
    if [ -n "${XPLAY_STOPFILE:-}" ] && [ -f "$XPLAY_STOPFILE" ]; then why="stopped by the harness: $(cat "$XPLAY_STOPFILE")"; kill "$kh" 2>/dev/null; break; fi
    sleep 2
  done
fi
wait "$kh" 2>/dev/null; rm -f "$marker"
kill -0 "$pid" 2>/dev/null || why="Ryujinx (pid $pid) exited by itself after $SECONDS s of this script"
echo "run ended: $why"
kill "$pid" 2>/dev/null; sleep 3; kill -9 "$pid" 2>/dev/null
[ -n "${XPLAY_LOGKEEP:-}" ] && [ -n "$log" ] && [ ! -s "$XPLAY_LOGKEEP" ] && cp "$log" "$XPLAY_LOGKEEP" 2>/dev/null   # no link (another volume)

RYUJINX_DIR="$rj" "$activate" combined-mod >/dev/null
echo "active native mod: $(ls "$rj/mods/contents/0100a1c00359c000/")"
echo "save data: $(check) of $want files match the backup"
[ -n "$log" ] || log=$(ls -t "$HOME"/Library/Logs/Ryujinx/*.log | head -1)
[ -s "${XPLAY_LOGKEEP:-}" ] && log=$XPLAY_LOGKEEP   # the kept copy outlives Ryujinx's own name for it
/usr/bin/grep -a "\[$tag\]\|PrintGuestStackTrace" "$log" | sed 's/.*OutputDebugString: //' | /usr/bin/grep -v "^\[xplay\] T "
echo "per-frame lines: $(/usr/bin/grep -a -c '\[xplay\] T ' "$log"); log: $log"
