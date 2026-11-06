#!/bin/bash
# rj-slot-setup.sh <name>: makes a private Ryujinx for XPLAY_RJSLOT=<name> (tools/rj-slot.sh): an APFS clone of /Applications/Ryujinx.app
# renamed to Ryujinx<name> (executable, bundle id; re-signed ad hoc with its entitlements), and a data folder cloned from the shared one (keys, firmware, the
# TowerFall save, profiles, shader cache, sdcard, combined-mod and hw-probe, the user's Config.json). App Nap off for the copy.
# Run it again to refresh from the shared data (refuses while either Ryujinx runs: the shared data must be at rest). ~350 MB (clones).
set -eu
n=${1:?usage: rj-slot-setup.sh <name>}
[[ $n =~ ^[A-Za-z0-9]+$ ]] || { echo "slot names are letters and digits" >&2; exit 1; }
S="$HOME/Library/Application Support/Ryujinx"; B="$HOME/.ryujinx-slots"; A="$B/Ryujinx$n.app"; D="$B/$n"; T=0100a1c00359c000
pgrep -x Ryujinx >/dev/null && { echo "the shared Ryujinx is running; try again when it isn't" >&2; exit 1; }
pgrep -x "Ryujinx$n" >/dev/null && { echo "Ryujinx$n is running" >&2; exit 1; }
mkdir -p "$B"
if [ ! -d "$A" ] || [ /Applications/Ryujinx.app/Contents/MacOS/Ryujinx -nt "$A/Contents/MacOS/Ryujinx$n" ]; then
  rm -rf "$A"; cp -cR /Applications/Ryujinx.app "$A"
  mv "$A/Contents/MacOS/Ryujinx" "$A/Contents/MacOS/Ryujinx$n"
  /usr/libexec/PlistBuddy -c "Set CFBundleIdentifier org.ryujinx.Ryujinx$n" -c "Set CFBundleExecutable Ryujinx$n" -c "Set CFBundleName Ryujinx$n" "$A/Contents/Info.plist"
  # keep the original's entitlements (com.apple.security.hypervisor: without it the guest dies at boot, "Unexpected result Denied")
  e=$(mktemp); codesign -d --entitlements :- /Applications/Ryujinx.app > "$e" 2>/dev/null
  codesign --force --deep -s - --entitlements "$e" "$A" 2>/dev/null; rm -f "$e"
fi
defaults write "org.ryujinx.Ryujinx$n" NSAppSleepDisabled -bool YES
rm -rf "$D"; mkdir -p "$D/games" "$D/mods/contents/$T" "$D/mods/_disabled/$T"
cp -cR "$S/system" "$S/prod.keys" "$S/profiles" "$S/bis" "$S/sdcard" "$D/"
[ -d "$S/games/$T" ] && cp -cR "$S/games/$T" "$D/games/"
cp "$S/Config.json" "$D/Config.json"
for m in combined-mod hw-probe; do for a in contents _disabled; do if [ -d "$S/mods/$a/$T/$m" ]; then cp -cR "$S/mods/$a/$T/$m" "$D/mods/_disabled/$T/"; fi; done; done
mv "$D/mods/_disabled/$T/combined-mod" "$D/mods/contents/$T/"
rm -rf "$B/$n.save-backup"
echo "slot $n: $A, data $D ($(du -sh "$D" | cut -f1))"
