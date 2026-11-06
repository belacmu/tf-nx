#!/bin/bash
# page-edit.sh [hw.txt lines...]: the page hosts a lobby, the Switch joins and readies (hw.txt lobbyhost 0, lobbyui 1, lobbyjoin 1), then the
# page EDITS its lobby (START on its unjoined seat, PE_KEYS in the LobbyBuilder, START = update): the server un-readies everyone, so the
# Switch's own portrait must leave (TF.EX's UnreadyOwnRollcall) and lobbyui readies again; then the page readies and starts the match.
# Same harness as tools/lobby-auto.sh (tools/lobby-vs-browser.sh MANUAL). Output: <out>/page-edit.txt.
here=$(cd "$(dirname "$0")/../../../.." && pwd); cd "$here"
out=${XPLAY_OUT:-/tmp/xplay-runs-$(printf '%s' "$here" | shasum | cut -c1-8)${XPLAY_RJSLOT:+-$XPLAY_RJSLOT}}; mkdir -p "$out"
log="$out/page-edit.txt"; source tools/rj-slot.sh; source tools/harness-slot.sh
[ -n "${XPLAY_RJSLOT:-}" ] && [ -z "${XPLAY_SLOT:-}" ] && XS=$(( XS + 1 + $(printf '%s' "$XPLAY_RJSLOT" | cksum | cut -d' ' -f1) % 9 ))
DRV=${DRV:-$((19400 + XS))}; P=${LVB_PLAYER:-LA$XS}
[ $# -gt 0 ] || set -- "rngcapture 1" "lobbyhost 0" "lobbyui 1" "lobbyjoin 1" "lobbyscript 1" "npdelay 2" "lobbypred 8" "notif 1" "npdigest 1" "vsstop 600"
MANUAL=1 LVB_SYNCMAX=${LVB_SYNCMAX:-150} tools/lobby-vs-browser.sh "$@" > "$log" 2>&1 & pid=$!
until grep -q 'MANUAL:' "$log" 2>/dev/null || ! kill -0 $pid 2>/dev/null; do sleep 3; done
k() { for x in "$@"; do curl -s "localhost:$DRV/$P/key?k=$x" >/dev/null; sleep 0.8; done; }
if kill -0 $pid 2>/dev/null; then
  k c; sleep 1.5; k ArrowRight c; sleep 3; k Enter; sleep 3; k Enter   # LOBBIES, create (TF.EX's defaults)
  for _ in $(seq 1 90); do grep -q 'joined .* as player' "$out/lvb-server.log" 2>/dev/null && break; sleep 2; done
  sleep 15                                                           # the Switch joins and readies
  k Enter; sleep 3; k ${PE_KEYS:-ArrowRight}; sleep 1; k Enter; sleep 3   # edit: START, the game mode, START (update)
  echo "page-edit: the page edited its lobby" >> "$log"
  sleep 15; k c; sleep 4; k c                                        # ready, start once the Switch is ready again
fi
wait $pid
cat "$log" | tail -20
