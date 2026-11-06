#!/usr/bin/env bash
# Puts the hardware probe on the SD card, for one console visit (see README.md).
#   hw-probe/make-sd.sh install <SD mount> <A|B> [<Mac LAN IP>]   probe + that run's files
#   hw-probe/make-sd.sh install <SD mount> <N|W|P> <server host> <room>   netplay against a browser `xnet` page through the server's relay
#       (N: scripted seat 0, rollback, comparable with the browser's log; W: N plus write tracking (hw.txt rbwrite 1, its self-test
#       first); P: seat 0 is the real controller). The room must start with "xnet-". The Switch is peer SWITCH_PEER (default
#       33333333-3333-4333-8333-333333333333, what tools/hw-peer.sh expects).
#   hw-probe/make-sd.sh install <SD mount> L <server host>   a LOBBY the Switch hosts (NETPLAY on the main menu) with the real
#       controller, rollback on; the browser joins it (tools/hw-peer.sh lobby, or the live site). See ../HARDWARE-SESSION.md.
#   hw-probe/make-sd.sh restore <SD mount>                       the original exefs back
#   DEVSERVER="<Mac IP> <port>" with install: later builds and test files come over Wi-Fi (tools/devserver.py; HARDWARE-SESSION.md)
# The probe is a native mod and takes the title's one exefs slot, so install backs up what is there
# (default: combined-mod's exefs) to <SD>/xplay-backup/exefs and restore puts it back. Nothing else on
# the card is touched; sd:/xplay/ holds the probe's files and logs.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
build=${HW_EXLAUNCH:-$here/exlaunch}/deploy   # tools/build-probe.sh builds it
cmd=${1:?usage: make-sd.sh install <SD mount> <A|B> [<Mac LAN IP>] | restore <SD mount>}
sd=${2:?SD mount point}
title="$sd/atmosphere/contents/0100A1C00359C000"
exe="$title/exefs"
backup="$sd/xplay-backup/exefs"
romBackup="$sd/xplay-backup/romfs-off"   # netplay runs: the installed mod's romfs moved aside (same card: a rename), so the game data is vanilla like the browser's
case "$cmd" in
install)
  run=${3:?run A or B}; ip=${4:-}
  [ -f "$build/subsdk9" ] && [ -f "$build/main.npdm" ] || { echo "no build in $build (run exlaunch.sh build there)" >&2; exit 1; }
  tpl=$run; [ "$run" = W ] && tpl=N   # W = N's files + write tracking
  [ -d "$here/sd-template/run$tpl" ] || { echo "run must be A, B, N, W, P or L" >&2; exit 1; }
  case "$run" in   # check the arguments before anything on the card changes
    N|W|P) [ -n "${4:-}" ] || { echo "run $run needs the server host" >&2; exit 1; }
           case "${5:-}" in xnet-*) ;; *) echo "run $run needs a room name starting with xnet-" >&2; exit 1;; esac ;;
    L) [ -n "${4:-}" ] || { echo "run L needs the server host" >&2; exit 1; } ;;
  esac
  if [ ! -d "$backup" ] && [ -d "$exe" ]; then mkdir -p "$backup" && cp -R "$exe/." "$backup/" && echo "backed up the exefs to $backup"; fi
  mkdir -p "$exe" "$sd/xplay"
  # the previous run's logs are kept, not deleted: moved to sd:/xplay/runs/<time>-run<letter>/ (run.txt says which run made them)
  logs=$(cd "$sd/xplay" && ls hw-log.txt trial-log.txt vs-log-3m.txt vs-log.txt rb-study.txt 2>/dev/null | tr '\n' ' ' || true)
  if [ -n "$logs" ]; then
    prev=$(cat "$sd/xplay/run.txt" 2>/dev/null || echo unknown)
    keep="$sd/xplay/runs/$(date +%m%d-%H%M%S)-run$prev"; mkdir -p "$keep"
    (cd "$sd/xplay" && mv $logs "$keep/" && cp hw.txt "$keep/" 2>/dev/null || true)
    echo "kept the previous run's logs ($logs) in ${keep#$sd/}"
  fi
  cp "$build/subsdk9" "$build/main.npdm" "$exe/"
  # a clean slate for the run: this run's files in, the other run's and old logs out
  rm -f "$sd/xplay/"{net.txt,netplay.txt,script.txt,hw.txt,vs0.txt,vs1.txt}
  cp -R "$here/sd-template/run$tpl/xplay/." "$sd/xplay/"
  [ "$run" = W ] && echo "rbwrite 1" >> "$sd/xplay/hw.txt"
  echo "$run" > "$sd/xplay/run.txt"
  find "$exe" "$sd/xplay" -name '._*' -delete 2>/dev/null || true   # macOS AppleDouble files on the FAT card (see tools/pc-levels.sh)
  # DEVSERVER="<Mac IP> <port>": the probe keeps itself and its test files in sync with tools/devserver.py over Wi-Fi (devupdate.cpp)
  if [ -n "${DEVSERVER:-}" ]; then echo "$DEVSERVER" > "$sd/xplay/devserver.txt"; echo "dev updates from $DEVSERVER (run tools/devserver.py on the Mac)"; fi
  if [ "$run" = N ] || [ "$run" = W ] || [ "$run" = P ] || [ "$run" = L ]; then
    if [ -d "$title/romfs" ] && [ ! -d "$romBackup/romfs" ]; then
      mkdir -p "$romBackup" && mv "$title/romfs" "$romBackup/romfs"
      [ -f "$title/romfs_metadata.bin" ] && mv "$title/romfs_metadata.bin" "$romBackup/"
      echo "moved the mod's romfs aside to $romBackup (vanilla game data for netplay)"
    fi
    # the PC's versus levels (what TF.EX plays: the Switch's own differ in 40 of 141 levels); marked, so restore takes it out again
    if [ ! -d "$title/romfs" ] || [ -f "$title/romfs/.xplay-pc-levels" ]; then
      "$here/../tools/pc-levels.sh" "$title/romfs" && rm -f "$title/romfs_metadata.bin"
    else echo "WARNING: $title/romfs is not ours and was not moved aside: the Switch plays its own levels (desyncs)" >&2; fi
    host=${4:?run $run needs the server host, e.g. the host of TFNX_SERVER in ~/.config/tfnx/local.mk}
    if [ "$run" = L ]; then room=lobby; else room=$5; fi
    peer=${SWITCH_PEER:-33333333-3333-4333-8333-333333333333}
    ipaddr=$(dig +short "$host" | grep -E '^[0-9.]+$' | head -1)   # the console's name lookup can fail: this is the fallback
    [ -n "$ipaddr" ] || { echo "couldn't resolve $host" >&2; exit 1; }
    for f in net.txt netplay.txt; do [ -f "$sd/xplay/$f" ] || continue; sed -i.bak -e "s/SERVER_HOST/$host/g" -e "s/SERVER_IP/$ipaddr/g" -e "s/ROOM/$room/g" -e "s/SWITCH_PEER/$peer/g" "$sd/xplay/$f" && rm "$sd/xplay/$f.bak"; done
    if [ "$run" = L ]; then
      echo "the Switch hosts a lobby on wss://$host (NETPLAY on the main menu); browser: tools/hw-peer.sh lobby"
    else
      echo "the Switch joins wss://$host/relay/$room as $peer (seat 0); browser (seat 1): tools/hw-peer.sh xnet $room"
    fi
  elif [ -f "$sd/xplay/net.txt" ]; then
    [ -n "$ip" ] || { echo "run A needs the Mac's LAN IP as the 4th argument" >&2; exit 1; }
    sed -i.bak "s/MAC_LAN_IP/$ip/" "$sd/xplay/net.txt" && rm "$sd/xplay/net.txt.bak"
  fi
  find "$sd/atmosphere/contents/0100A1C00359C000" "$sd/xplay" "$sd/xplay-backup" -maxdepth 2 -name "._*" -delete 2>/dev/null || true   # macOS metadata files on the FAT card
  echo "installed the probe for run $run; sd:/xplay now holds: $(ls "$sd/xplay" | tr '\n' ' ')"
  ;;
restore)
  [ -d "$backup" ] || { echo "no backup at $backup" >&2; exit 1; }
  rm -rf "$exe" && mkdir -p "$exe" && cp -R "$backup/." "$exe/"
  if [ -f "$title/romfs/.xplay-pc-levels" ]; then   # the PC levels install put there (only a romfs with our marker is removed)
    rm -rf "$title/romfs"; rm -f "$title/romfs_metadata.bin"; echo "removed the netplay PC levels romfs"
  fi
  if [ -d "$romBackup/romfs" ] && [ ! -d "$title/romfs" ]; then
    mv "$romBackup/romfs" "$title/romfs"; [ -f "$romBackup/romfs_metadata.bin" ] && mv "$romBackup/romfs_metadata.bin" "$title/"
    rmdir "$romBackup" 2>/dev/null; echo "moved the mod's romfs back"
  fi
  echo "restored the exefs from $backup (logs are still in sd:/xplay/)"
  ;;
*) echo "unknown command $cmd" >&2; exit 1 ;;
esac
