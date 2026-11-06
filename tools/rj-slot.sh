# source tools/rj-slot.sh: which Ryujinx a test script drives. Unset XPLAY_RJSLOT: the shared one (/Applications/Ryujinx.app, data in
# ~/Library/Application Support/Ryujinx, lock /tmp/xplay-ryujinx.lock). XPLAY_RJSLOT=<name> (made once by tools/rj-slot-setup.sh <name>):
# a private instance that runs next to the shared one: its own app copy ~/.ryujinx-slots/Ryujinx<name>.app whose process and bundle id
# are Ryujinx<name> (the shared scripts' `pgrep -x Ryujinx` and keephidden never see it), its own data folder ~/.ryujinx-slots/<name>
# (--root-data-dir: keys, firmware, save, mods, sdcard, config), save backup and lock /tmp/xplay-ryujinx-<name>.lock.
# Sets RJ (data folder), RJ_NAME (process name), RJ_APP, RJ_BUNDLE, RJ_LOCK, RJ_BACKUP (run-probe's save reference; needs `root`),
# RJ_ARGS (Ryujinx arguments before the game; use as ${RJ_ARGS[@]+"${RJ_ARGS[@]}"}: bash 3.2 calls an empty array unbound).
if [ -n "${XPLAY_RJSLOT:-}" ]; then
  RJ_NAME=Ryujinx$XPLAY_RJSLOT; RJ="$HOME/.ryujinx-slots/$XPLAY_RJSLOT"; RJ_APP="$HOME/.ryujinx-slots/$RJ_NAME.app"
  RJ_BUNDLE=org.ryujinx.$RJ_NAME; RJ_LOCK=/tmp/xplay-ryujinx-$XPLAY_RJSLOT.lock; RJ_BACKUP="$HOME/.ryujinx-slots/$XPLAY_RJSLOT.save-backup"
  RJ_ARGS=(--root-data-dir "$RJ")
  [ -x "$RJ_APP/Contents/MacOS/$RJ_NAME" ] && [ -d "$RJ/bis" ] || { echo "no Ryujinx slot $XPLAY_RJSLOT (tools/rj-slot-setup.sh $XPLAY_RJSLOT)" >&2; exit 1; }
else
  RJ_NAME=Ryujinx; RJ="$HOME/Library/Application Support/Ryujinx"; RJ_APP=/Applications/Ryujinx.app
  RJ_BUNDLE=org.ryujinx.Ryujinx; RJ_LOCK=/tmp/xplay-ryujinx.lock; RJ_BACKUP="${root:-.}/.save-backup"
  RJ_ARGS=()
fi
