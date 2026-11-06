# source tools/ryujinx-lock.sh (from a test script, after `here` is set): only one Ryujinx test at a time, across checkouts and agents.
# Waits for /tmp/xplay-ryujinx.lock (a stale lock of a dead process is taken over); released when the sourcing script exits.
# XPLAY_RJSLOT=<name> (tools/rj-slot.sh): that private Ryujinx's own lock /tmp/xplay-ryujinx-<name>.lock instead (runs beside the shared one).
# Nested scripts (lobby-auto.sh -> lobby-vs-browser.sh) see XPLAY_LOCK_HELD and don't lock again. Scripts with their own EXIT trap
# must call ryujinx_unlock in it. run-probe.sh still refuses to start while any Ryujinx runs (the user may be playing).
source "$(dirname "${BASH_SOURCE[0]}")/rj-slot.sh"
if [ -z "${XPLAY_LOCK_HELD:-}" ]; then
  _lock=$RJ_LOCK; _waited=0
  until mkdir "$_lock" 2>/dev/null; do
    _owner=$(cat "$_lock/owner" 2>/dev/null || true); _pid=${_owner%% *}
    if [ -n "$_pid" ] && ! kill -0 "$_pid" 2>/dev/null; then echo "ryujinx-lock: stale lock ($_owner), taking it" >&2; rm -rf "$_lock"; continue; fi
    [ $_waited = 0 ] && echo "ryujinx-lock: waiting for: $_owner" >&2
    _waited=1; sleep 5
  done
  echo "$$ ${here:-?} $(date +%T)" > "$_lock/owner"
  export XPLAY_LOCK_HELD=1
  ryujinx_unlock() { rm -rf "$RJ_LOCK"; }
  trap ryujinx_unlock EXIT
else
  ryujinx_unlock() { :; }
fi
