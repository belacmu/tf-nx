#!/bin/bash
# page-variant.sh [hw.txt lines...]: the page hosts a lobby with one variant turned on from its VARIANTS page (PV_KEYS: the cursor moves
# from the first variant; default ArrowDown ArrowRight ArrowRight = ARCHERS #10 RETURN AS GHOSTS), the Switch browses and tries to join
# (hw.txt lobbyhost 0, lobbyui 1, lobbyjoin 1). For variants the Switch refuses: the run ends on the harness's join timeout. Output:
# <out>/page-variant.txt, the page's builder screenshot <out>/page-variant.png.
here=$(cd "$(dirname "$0")/../../../.." && pwd); cd "$here"
out=${XPLAY_OUT:-/tmp/xplay-runs-$(printf '%s' "$here" | shasum | cut -c1-8)${XPLAY_RJSLOT:+-$XPLAY_RJSLOT}}; mkdir -p "$out"
log="$out/page-variant.txt"; source tools/rj-slot.sh; source tools/harness-slot.sh
[ -n "${XPLAY_RJSLOT:-}" ] && [ -z "${XPLAY_SLOT:-}" ] && XS=$(( XS + 1 + $(printf '%s' "$XPLAY_RJSLOT" | cksum | cut -d' ' -f1) % 9 ))
DRV=${DRV:-$((19400 + XS))}; P=${LVB_PLAYER:-LA$XS}
[ $# -gt 0 ] || set -- "rngcapture 1" "lobbyhost 0" "lobbyui 1" "lobbyjoin 1" "lobbyscript 1" "npdelay 2" "lobbypred 8" "notif 1" "vsstop 600"
MANUAL=1 LVB_JOINMAX=${LVB_JOINMAX:-200} tools/lobby-vs-browser.sh "$@" > "$log" 2>&1 & pid=$!
until grep -q 'MANUAL:' "$log" 2>/dev/null || ! kill -0 $pid 2>/dev/null; do sleep 3; done
k() { for x in "$@"; do curl -s "localhost:$DRV/$P/key?k=$x" >/dev/null; sleep 0.8; done; }
# the online menu (its server connection); only after 60 s a "c" for a "What's new" box (a c on the online menu would pick QUICK PLAY)
for i in $(seq 1 45); do grep -q '\[mode\] online: entering netplay' "$out/lvb-browser.txt" 2>/dev/null && break; [ $i -eq 30 ] && k c; sleep 2; done
sleep 6
if kill -0 $pid 2>/dev/null; then
  k ArrowRight c; sleep 3; k Enter; sleep 3                          # LOBBIES, the LobbyBuilder
  k ArrowDown ArrowDown ArrowDown ArrowDown ArrowDown c; sleep 2       # VARIANTS
  k ${PV_KEYS:-ArrowDown ArrowRight ArrowRight} c; sleep 1
  curl -s "localhost:$DRV/$P/shot" -o "$out/page-variant.png"
  k Enter                                                            # create
  echo "page-variant: lobby created with a variant" >> "$log"
fi
wait $pid
tail -6 "$log"
grep -a -o '"Variants":\[[^]]*\]' "$out/lvb-server.log" | sort -u | head -3
