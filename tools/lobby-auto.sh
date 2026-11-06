#!/bin/bash
# lobby-auto.sh [hw.txt lines...]: tools/lobby-vs-browser.sh (MANUAL) plus the page's keys (What's new, LOBBIES, join the first lobby,
# ready), sent only once this run's own harness holds the Ryujinx lock and printed MANUAL. Output: <out>/lobby-auto.txt (also printed).
here=$(cd "$(dirname "$0")/.." && pwd); cd "$here"
out=${XPLAY_OUT:-/tmp/xplay-runs-$(printf '%s' "$here" | shasum | cut -c1-8)${XPLAY_RJSLOT:+-$XPLAY_RJSLOT}}; mkdir -p "$out"
log="$out/lobby-auto.txt"; source tools/rj-slot.sh; source tools/harness-slot.sh
[ -n "${XPLAY_RJSLOT:-}" ] && [ -z "${XPLAY_SLOT:-}" ] && XS=$(( XS + 1 + $(printf '%s' "$XPLAY_RJSLOT" | cksum | cut -d' ' -f1) % 9 ))   # as lobby-vs-browser.sh
DRV=${DRV:-$((19400 + XS))}; P=${LVB_PLAYER:-LA$XS}
MANUAL=1 tools/lobby-vs-browser.sh "$@" > "$log" 2>&1 & pid=$!
until grep -q 'MANUAL:' "$log" 2>/dev/null || ! kill -0 $pid 2>/dev/null; do sleep 3; done
k() { for x in "$@"; do curl -s "localhost:$DRV/$P/key?k=$x" >/dev/null; sleep 0.8; done; }
# past TF.EX's box in front of the main menu (v0.27's tutorial, or "What's new"; Back = SKIP / CONTINUE, both marked seen): only while
# the page hasn't entered netplay (lobby-vs-browser nudges it too)
# LVB_PAGEWIDE=1: the page plays wide (hosting or joining: TF.EX refuses a wide lobby from STANDARD and the other way round). It comes
# online on WiderSet's last pick: back to the main menu, Right + confirm = NETPLAY -> WiderSet's STANDARD / WIDER picker (STANDARD
# selected), Right + confirm = WIDER -> NetplaySelect
pagewide() { [ -n "${LVB_PAGEWIDE:-}" ] || return 0; k x; sleep 2; k ArrowRight; sleep 1; k c; sleep 3; k ArrowRight; sleep 1; k c; sleep 3; curl -s "localhost:$DRV/$P/shot" -o "$out/pagewide-$P.png"; }
pastnew() { grep -q '\[mode\] online: entering netplay' "$out/lvb-browser-$P.txt" 2>/dev/null || { k ${LVB_MENUKEY:-x}; sleep 1.5; }; }
# TF.EX's NetplaySelect (v0.27): QUICK PLAY (selected), RANKED, LOBBIES, PRIVATE
LOBBIES="ArrowRight ArrowRight"; PRIVATE="ArrowRight ArrowRight ArrowRight"
# LVB_PAGES=<n>: the other pages (LB<slot>, LC<slot>, ... one letter each) join the Switch's lobby the same way, all at once
NP=${LVB_PAGES:-1}; PAGES=$P; for l in B C D E F G H; do [ $(( $(printf '%d' "'$l") - 65 )) -lt $NP ] && PAGES="$PAGES L$l$XS"; done
# LVB_SPECTATE=early|late: the last page spectates instead of joining (TF.EX: LOBBIES, the first lobby selected, SPECTATE = MenuAlt2 =
# Tab on the keyboard); early: before the other pages join (the match starts once they are ready), late: once it started (the server's
# lobby_match_start)
SPECP=; if [ -n "${LVB_SPECTATE:-}" ] && [ $NP -ge 2 ]; then SPECP=${PAGES##* }; PAGES=${PAGES% *}; fi
spectate() {
  local P=$SPECP
  [ "$LVB_SPECTATE" = late ] && for _ in $(seq 1 300); do grep -q '"event":"lobby_match_start"' "$out/lvb-server.log" 2>/dev/null && break; kill -0 $pid 2>/dev/null || return; sleep 1; done
  [ "$LVB_SPECTATE" = late ] && sleep 4
  for try in 1 2 3; do
    pastnew; k $LOBBIES c; sleep 3; k Tab
    for _ in $(seq 1 15); do grep -q 'joined .* as spectator' "$out/lvb-server.log" 2>/dev/null && break; sleep 1; done
    grep -q 'joined .* as spectator' "$out/lvb-server.log" 2>/dev/null && { echo "lobby-auto: page $P spectates ($LVB_SPECTATE)" >&2; break; }
    curl -s "localhost:$DRV/$P/shot" -o "$out/spectate-$P-try$try.png"; echo "lobby-auto: page $P isn't spectating yet (try $try): backing out" >&2; k x x x; sleep 2
  done
}
if kill -0 $pid 2>/dev/null && [ -n "${LVB_JOINCODE:-}" ]; then
  # LVB_JOINCODE=1: the Switch hosts a private lobby (hw.txt lobbyui 1, lobbyprivate 1); the page joins it with the code (TF.EX: PRIVATE
  # -> JOIN -> JoinCodeEntry typed on the keyboard, Confirm) read from the local server's log, then readies
  code=; for _ in $(seq 1 90); do code=$(grep -o 'Private lobby [^ ]* (code [A-Z0-9]*)' "$out/lvb-server.log" 2>/dev/null | grep -o 'code [A-Z0-9]*' | cut -c6-); [ -n "$code" ] && break; sleep 2; done
  echo "lobby-auto: private lobby code ${code:-none}" >&2
  pastnew; k $PRIVATE c; sleep 3; k ArrowRight c; sleep 3
  for ch in $(echo "$code" | fold -w1); do k "$ch"; done; sleep 1; k c; sleep 5; k c
elif kill -0 $pid 2>/dev/null && [ -n "${LVB_PAGEQUICK:-}" ]; then
  # LVB_PAGEQUICK=1: both search QUICK PLAY (hw.txt lobbyui 1, lobbyquick 1); the server matches them and starts once both are ready
  pastnew; k c
  for _ in $(seq 1 90); do grep -q '\[quickplay\] matched' "$out/lvb-server.log" 2>/dev/null && break; sleep 2; done
  sleep 6; k c
elif kill -0 $pid 2>/dev/null && [ -n "${LVB_PAGEPRIVATE:-}" ]; then
  # LVB_PAGEPRIVATE=1: the page hosts a private lobby (TF.EX: PRIVATE -> CREATE, Down+Right = Sacred Ground, Enter); its code goes to
  # sd:/xplay/joincode.txt for the Switch (hw.txt lobbyui 1, lobbyprivate 2, joincode file); the page readies once the Switch joined
  jc="$RJ/sdcard/xplay/joincode.txt"; rm -f "$jc"
  pastnew; k $PRIVATE c; sleep 3; k c; sleep 3; k ArrowDown ArrowRight; sleep 1; k Enter
  code=; for _ in $(seq 1 60); do code=$(grep -o 'Private lobby [^ ]* (code [A-Z0-9]*)' "$out/lvb-server.log" 2>/dev/null | grep -o 'code [A-Z0-9]*' | cut -c6-); [ -n "$code" ] && break; sleep 2; done
  echo "lobby-auto: the page's private lobby code ${code:-none}" >&2; printf '%s' "$code" > "$jc"
  for _ in $(seq 1 90); do grep -q 'joined .* as player' "$out/lvb-server.log" 2>/dev/null && break; sleep 2; done
  sleep 12; k c; sleep 4; k c
elif kill -0 $pid 2>/dev/null && [ -n "${LVB_PAGEHOST:-}" ]; then
  # LVB_PAGEHOST=1: the page hosts (TF.EX: LOBBIES, Enter = create, Down+Right = Sacred Ground, Enter = create the lobby), the Switch joins
  # from its LobbyBrowser (hw.txt lobbyhost 0, lobbyui 1, lobbyjoin 1); the page readies once the Switch's seat is ready, then starts.
  # LVB_PAGEBUILD=<keys>: the LobbyBuilder keys instead of Down+Right ("-" = none: TF.EX's defaults, RANDOM map, STANDARD length = goal 5)
  bk=${LVB_PAGEBUILD:-ArrowDown ArrowRight}; [ "$bk" = - ] && bk=
  shot() { curl -s "localhost:$DRV/$P/shot" -o "$out/pagehost-$1.png"; }   # each step's screen (the page's menus, for when a step goes nowhere)
  shot 0-start; pastnew; shot 1-online
  pagewide
  k $LOBBIES c; sleep 3; shot 2-lobbies; k Enter; sleep 3; shot 3-builder; [ -n "$bk" ] && k $bk; sleep 1; k Enter; sleep 2; shot 4-created
  for _ in $(seq 1 90); do grep -q 'joined .* as player' "$out/lvb-server.log" 2>/dev/null && break; sleep 2; done
  sleep 12; [ -n "${LVB_PAGEPICK:-}" ] && { k $LVB_PAGEPICK; sleep 1; }   # LVB_PAGEPICK=<keys>: the page's archer (e.g. ArrowRight) before ready
  k c; sleep 4; k c   # ready, then confirm on the start banner
elif kill -0 $pid 2>/dev/null; then
  P0=$P; n=0
  [ -n "$SPECP" ] && [ "$LVB_SPECTATE" = early ] && spectate   # (before the players ready: the match starts once they all are)
  # every page at once (2 s apart): LOBBIES, join the first lobby, (team,) ready; then the ones without their "[xlobby] ... in a lobby"
  # line (the browser harness's, XplayLobby.cs) back out and retry, the others are left alone (sequential joins took ~40 s a page)
  # LVB_PAGEGUESTS=<n>: n more local players on each page (TF.EX: up to 2 guests a device), on the page's virtual gamepads (browser
  # web/Netplay/TestGamepads.cs), attached once the page is on the netplay menu (attached on the main menu, the game's input
  # reassignment could keep the page's start mode from entering netplay: wide8end 140106-143252); START there before joining (the
  # server seats a device's guests as it joins), A on the roll call (ready)
  pad() { curl -s "localhost:$DRV/$P/eval" --data "towerfallCommand('pad $*')" >/dev/null; }
  guests() { for g in $(seq 0 $(( ${LVB_PAGEGUESTS:-0} - 1 ))); do pad $g "$@"; sleep 1; done; }
  join1() { pastnew; pagewide; [ "${LVB_PAGEGUESTS:-0}" -gt 0 ] && { pad attach $LVB_PAGEGUESTS; sleep 1; }; guests start; sleep 1; k $LOBBIES c; sleep 3; k c; sleep 4; [ -n "${LVB_TEAMKEY:-}" ] && { k "$LVB_TEAMKEY"; sleep 1; }
    [ -n "${LVB_PAGEPICK:-}" ] && { k $LVB_PAGEPICK; sleep 1; }; k c; sleep 2
    case "${LVB_TEAMKEY:-}" in ArrowUp) guests up;; ArrowDown) guests down;; esac; guests a; }   # (the guests: the same team)   # LVB_TEAMKEY: the team (ArrowUp blue, ArrowDown red), LVB_PAGEPICK: the archer, before ready
  inlobby() { grep -q '\[xlobby\] .*in a lobby' "$out/lvb-browser-$P.txt" 2>/dev/null; }
  jobs_=; for P in $PAGES; do join1 & jobs_="$jobs_ $!"; sleep 2; done; wait $jobs_   # (only these: a bare wait also waits for the run itself)
  for try in 1 2 3; do
    for _ in $(seq 1 20); do miss=; for P in $PAGES; do inlobby || miss="$miss $P"; done; [ -z "$miss" ] && break; sleep 1; done
    [ -z "$miss" ] && break
    jobs_=; for P in $miss; do
      curl -s "localhost:$DRV/$P/shot" -o "$out/page-$P-try$try.png"
      echo "lobby-auto: page $P didn't join yet (try $try; $out/page-$P-try$try.png); backing out and retrying" >&2
      ( k x x x; sleep 2; join1 ) & jobs_="$jobs_ $!"; sleep 2
    done; wait $jobs_
  done
  # a ready can land before a page's roll call is up (several pages; one page whose roll call builds slower, e.g. with a mod archer
  # to resolve: 2026-10-30): once all are in, each readies again (Confirm on a joined seat does nothing in TF.EX's roll call); a
  # screenshot of each page after it
  sleep 8; for P in $PAGES; do k c; [ "${LVB_PAGEGUESTS:-0}" -gt 0 ] && guests a; sleep 1; curl -s "localhost:$DRV/$P/shot" -o "$out/page-$P-ready.png"; echo "lobby-auto: page $P readied again once in the lobby ($out/page-$P-ready.png)" >&2; done
  P=$P0
  [ -n "$SPECP" ] && [ "$LVB_SPECTATE" = late ] && spectate
  # the page sometimes isn't on the online menu yet (no "What's new" box, or still building): retry from wherever it is
  for try in 1 2 3; do
    [ -n "${LVB_PAGEHOST:-}${LVB_JOINCODE:-}${LVB_PAGEPRIVATE:-}${LVB_PAGEQUICK:-}" ] && break
    sleep 8; grep -q 'joined .* as player' "$out/lvb-server.log" 2>/dev/null && break
    echo "lobby-auto: the page didn't join yet (try $try); backing out and retrying" >&2
    k x x x; sleep 2; k $LOBBIES c; sleep 3; k c; sleep 4; [ -n "${LVB_TEAMKEY:-}" ] && { k "$LVB_TEAMKEY"; sleep 1; }; k c
  done
fi
# LVB_PAGEVOTE=1|2: once the page's match results finished (its own MatchEnded), it votes in TF.EX's end-of-match menu:
# Confirm on the results (opens the menu), then REMATCH! (1: Confirm on the first item) or ARCHER SELECT (2: Down, Confirm)
if [ -n "${LVB_PAGEVOTE:-}" ]; then
  for _ in $(seq 1 300); do grep -q 'sending MatchEnded' "$out/lvb-browser.txt" 2>/dev/null && break; kill -0 $pid 2>/dev/null || break; sleep 1; done   # the page's own results finished
  if [ "$LVB_PAGEVOTE" = 2 ]; then
    for i in 1 2; do sleep 2; curl -s "localhost:$DRV/$P/shot" -o "$out/vote-$i-before.png"; k c; done   # (popups, the menu)
    sleep 2; k ArrowDown; sleep 1; k c; what="ARCHER SELECT"
  else
    for i in 1 2 3; do sleep 2; curl -s "localhost:$DRV/$P/shot" -o "$out/vote-$i-before.png"; k c; done   # (popups, menu, REMATCH!; a vote is locked after the first)
    what="REMATCH!"
  fi
  sleep 2; curl -s "localhost:$DRV/$P/shot" -o "$out/vote-after.png"; echo "lobby-auto: the page voted ($what)" >&2
fi
# LVB_PAGESERIES=1: a best-of series: after each of the page's games (its MatchEnded) and TF.EX's series screen, Confirm a few times
# (the tower when it is the page's pick, then READY), for up to 3 games
if [ -n "${LVB_PAGESERIES:-}" ]; then
  for g in 1 2 3; do
    for _ in $(seq 1 400); do [ "$(grep -c 'sending MatchEnded' "$out/lvb-browser.txt" 2>/dev/null)" -ge "$g" ] && break; kill -0 $pid 2>/dev/null || break; sleep 1; done
    kill -0 $pid 2>/dev/null || break
    sleep 6; for i in 1 2 3 4; do k c; sleep 2; done
    curl -s "localhost:$DRV/$P/shot" -o "$out/series-$g.png"; echo "lobby-auto: the page readied on the series screen (after game $g)" >&2
  done
fi
# LVB_PAGELEAVE=<s>: that many seconds after the page's lobby match synchronized, the page leaves (navigates away: its room and server
# connections close, as when a player closes the tab mid-match)
if [ -n "${LVB_PAGELEAVE:-}" ]; then
  for _ in $(seq 1 300); do grep -q 'session established' "$out/lvb-browser.txt" 2>/dev/null && break; kill -0 $pid 2>/dev/null || break; sleep 1; done
  sleep "$LVB_PAGELEAVE"; curl -s "localhost:$DRV/$P/shot" -o "$out/leave-before.png"
  curl -s "localhost:$DRV/$P/eval" --data 'location.href = "about:blank"' >/dev/null; echo "lobby-auto: the page left mid-match" >&2
fi
wait $pid
rm -f "$RJ/sdcard/xplay/joincode.txt"
keep="$out/runs/$(date +%H%M%S)"; mkdir -p "$keep"   # this run's logs (the next run overwrites lvb-*)
cp "$log" "$out"/lvb-browser.txt "$out"/lvb-sd.txt "$out"/lvb-run.out "$out"/lvb-server.log "$keep/" 2>/dev/null
L=$(grep -o 'log: .*' "$out/lvb-run.out" | cut -c6-); if [ -f "$out/lvb-ryujinx.log" ]; then cp "$out/lvb-ryujinx.log" "$keep/ryujinx.log"; elif [ -f "$L" ]; then cp "$L" "$keep/ryujinx.log"; fi
echo "lobby-auto: logs kept in $keep" >> "$log"
cat "$log"
