#!/bin/bash
# smoke.sh <scenario>: one short Ryujinx-vs-browser run of a core multiplayer flow, ending in ONE line:
#   SMOKE <scenario> PASS|FAIL: <reason> | shot <png> | logs <dir>
# Scenarios (the Switch's flow is driven by hw.txt lobbyui; the page by tools/lobby-auto.sh's keys; GGRS's own checksum on both sides):
#   lobby      the Switch hosts through its menus, the page joins, one match to vsstop 2400
#   join       the page hosts, the Switch joins from its lobby browser, one match to vsstop 2400
#   private    the Switch creates a private lobby (PRIVATE -> CREATE), the page joins with its code, one match to vsstop 2400
#   privatejoin the page creates a private lobby, the Switch joins with its code (PRIVATE -> JOIN, the code read from sd:/xplay/joincode.txt
#              instead of the keyboard), one match to vsstop 2400
#   quickplay  both search QUICK PLAY, the server matches them, one match to vsstop 2400
#   rematch    an Instant lobby match (goal 1, ~2300 ticks: a match must end) and both vote REMATCH!: until match 2 synchronizes
#   archer     an Instant lobby match and both vote ARCHER SELECT: until 30 frames back on the roll call (TF.EX's ReturnToMenu(Rollcall))
#   team       the Switch hosts Team Deathmatch on BLUE (lobbymode 5, lobbyteam 0, lobbymax 2), the page joins on RED; the shot on the roll call
#              once the page is RED; one match to vsstop 2400
#   disconnect a lobby match the page leaves 8 s after it synchronized (navigates away): TF.EX leaves the lobby and the level runs on to vsstop 9600
#   leftquit   as disconnect, then the Switch QUITs from the level (Start, Down, Confirm) 2 s after every remote is gone: back on the lobby screen
#   report     as disconnect, with the server's match reports on (LVB_REPORTS): until TF.EX's MatchReport sent the Switch's (zipped log, accepted)
#   spectate   a second page spectates the Switch's lobby match (SMOKE_SPECTATE=early: joined before it, late: during it), to vsstop 9600
#   pause      a lobby match: the Switch opens TF.EX's pause overlay at frame 300 and RESUMEs (rollbacks go across it), to vsstop 2400
#   quit       an Instant lobby match, then the Switch QUITs from the end-of-match menu: until it is back on the NETPLAY menu
#   aiseat     as lobby, LoaderAI's AI (type SMOKE_AITYPE, default 1: NOVA) plays the Switch's seat instead of the test script (hw.txt
#              aiseat, TEST ONLY), to vsstop 9600: a round must end by a kill (the second round's level) with the state compare clean
#   three      the Switch hosts a 3-player lobby (lobbymax 3), two pages join, one 3-player match to vsstop 2400
#   localtwo   several local players (TF.EX v0.28.0): controller 2 presses START on the netplay menu (P2), the Switch hosts a 3-player lobby
#              (lobbylocal2) with both, one page joins: one 3-player match (2 on the Switch) to vsstop 2400 (the server seats a device's
#              guests from local_players: the browser repo's tfex-server and TF.EX's own, LVB_SERVER=real)
# lobby .. spectate (above, not the scripted-win / pause / quit / leave ones): LoaderAI's AI plays the Switch's seat (SMOKE_AI=0: the script).
# PASS needs: no guest exception; the scenario's log lines (in order of the flow); GGRS's checksum (TF.EX's fletcher16 of TF.State's bytes) reported no desync on any page;
# for the one-match scenarios also the state compare: TF.State's saved bytes the same on both sides on every dumped frame after 0 (every
# match runs at TF.EX's 240 Hz; vsstop counts ticks, 240 a second). A screenshot of the Ryujinx window is taken at the scenario's moment (the window is visible: run-probe.sh).
# Refuses to start when three Ryujinx instances already run, or the 1-minute load average is above 20.
# Extra hw.txt lines after the scenario are added (same key: replaced), e.g. tools/smoke.sh lobby "genpatch PlayerPatch".
# SMOKE_HOOKS="<Type.Method> ...": the hooks a change adds; the line names those the run never reached (see hooks() below).
# XPLAY_RJSLOT works as for every harness script (tools/rj-slot.sh). SMOKE_SHOTAT=<regex>: take the screenshot at that log line instead.
# A desync FAIL names its first differing frame and field (both sides dump TF.State's state every SMOKE_STATEDUMP frames, default 60;
# tools/tfex/statediff.py compares them; its report is statediff-<page>.txt in the logs dir).
set -u
here=$(cd "$(dirname "$0")/.." && pwd); cd "$here"
sc=${1:-}; shift || true
out=${XPLAY_OUT:-/tmp/xplay-runs-$(printf '%s' "$here" | shasum | cut -c1-8)${XPLAY_RJSLOT:+-$XPLAY_RJSLOT}}; mkdir -p "$out"
source tools/rj-slot.sh
fail() { echo "SMOKE ${sc:-?} FAIL: $1"; exit 1; }

# at most two Ryujinx at once (the coordinator, 2026-10-17: three plus builds kept the Mac at load 33-39 and every run crawled), and not
# on an overloaded Mac: the run waits for both (SMOKE_WAIT s, default 1200), then gives up. SMOKE_MAXLOAD: another load limit (0: none)
waited=0
while :; do
  n=$(pgrep -f '/Contents/MacOS/Ryujinx' | wc -l | tr -d ' ')
  load=$(sysctl -n vm.loadavg | awk '{print int($2)}')
  [ "$n" -lt 2 ] && { [ "${SMOKE_MAXLOAD:-20}" = 0 ] || [ "$load" -le "${SMOKE_MAXLOAD:-20}" ]; } && break
  [ $waited -ge ${SMOKE_WAIT:-1200} ] && fail "waited ${waited} s: $n Ryujinx instances running (at most 2), 1-minute load average $load (at most ${SMOKE_MAXLOAD:-20})"
  [ $waited = 0 ] && echo "smoke: waiting ($n Ryujinx running, load $load)" >&2
  sleep 15; waited=$((waited + 15))
done

base=("rngcapture 1" "lobbyui 1" "lobbyscript 1" "lobbypred 8" "notif 1")
# TF.State's saved state, dumped on both sides every SMOKE_STATEDUMP frames (default 60; 0: off): a desync then names the fields that
# differ (tools/tfex/statediff.py on the two logs, below) instead of needing a second run with the dumps on
dump=${SMOKE_STATEDUMP:-60}
[ "$dump" != 0 ] && base+=("statedump $dump")
envs=(); stallok=; until=; compare=1; shotdelay=2; pageexpect=()   # pageexpect: lines the LAST page's console must have   # the screenshot: shotdelay s after the shotat line
# the scenarios that need a match to end: the test script's Switch seat scores on the levels the seed gives with no variants on; TF.EX's
# tournament rules (NO AUTOBALANCE, SYMMETRICAL TREASURE: on entering NETPLAY, as on the PC since 2026-10-28) change the treasure and the
# script never scores (rematch 211811 stalled). Off in the builder, as a player can switch them.
scriptwins="lobbyvariants -NO AUTOBALANCE|-SYMMETRICAL TREASURE"
case "$sc" in
  lobby)     hw=("vsstop 2400"); expect=('\[tfex\] Send .*StartLobbyChoice' '\[np\] lobby match #1 synchronized'); shotat='\[np\] lobby match #1 synchronized' ;;
  join)      hw=("lobbyjoin 1" "vsstop 2400"); envs+=(LVB_PAGEHOST=1); expect=('\[ui\] SPECTATE refused' '\[tfex\] Lobby joined!' '\[np\] lobby match #1 synchronized'); shotat='\[np\] lobby match #1 synchronized' ;;
  private)   hw=("lobbyprivate 1" "vsstop 2400"); envs+=(LVB_JOINCODE=1); expect=('server message CreateLobbyResponse' '\[np\] lobby match #1 synchronized'); shotat='\[np\] lobby match #1 synchronized' ;;
  privatejoin) hw=("lobbyprivate 2" "joincode file" "vsstop 2400"); envs+=(LVB_PAGEPRIVATE=1); expect=('\[screens\] join code from the keyboard|JoinPrivate' '\[np\] lobby match #1 synchronized'); shotat='\[np\] lobby match #1 synchronized' ;;
  quickplay) hw=("lobbyquick 1" "vsstop 2400"); envs+=(LVB_PAGEQUICK=1); expect=('server message QuickPlayMatchFound' '\[np\] lobby match #1 synchronized'); shotat='\[np\] lobby match #1 synchronized' ;;
  rematch)   hw=("lobbylength 0" "lobbyvote 1" "$scriptwins"); envs+=(LVB_PAGEVOTE=1 "LVB_FAILON=lobby match #2 over at frame 0"); compare=; until='\[np\] lobby match #2 synchronized'
             expect=('match results finished' 'test: voting REMATCH!' '\[np\] lobby match #2 synchronized'); shotat=$until ;;
  archer)    hw=("lobbylength 0" "lobbyvote 2" "$scriptwins"); envs+=(LVB_PAGEVOTE=2); compare=; until='wide trace: state 5 frame 30 .*CurrentMatchSettings = Versus'   # 30 frames back on the roll call (its layout in the trace)
             expect=('match results finished' 'test: voting ARCHER SELECT' "$until"); shotat=$until ;;
  team)      hw=("lobbymode 5" "lobbyteam 0" "lobbymax 2" "vsstop 2400"); envs+=(LVB_TEAMKEY=ArrowDown); expect=('\[lobby\] seat 1: team RED' '\[tfex\] Send .*StartLobbyChoice' '\[np\] lobby match #1 synchronized'); shotat='\[lobby\] seat 1: team RED'; shotdelay=0 ;;   # (TF.EX's generated roll call joins the page's seat: no hand "seat N: ... joined" line)
  disconnect) hw=("vsstop 9600"); envs+=(LVB_PAGELEAVE=8); compare=; stallok=1   # TF.EX's GetEventAndUpdate: every remote gone -> LeaveLobby, the
             # level runs on locally until the harness stops it (no guest exception after the teardown, the 2026-10-17 crash; vsstop counts
             # only lobby-match ticks, so the harness's "stalled" stop is this scenario's end)
             expect=('\[np\] lobby match #1 synchronized' 'Disconnected from .* the level runs on without GGRS'); shotat='the level runs on without GGRS' ;;
  leftquit)  hw=("vsstop 9600" "lobbypause -1"); envs+=(LVB_PAGELEAVE=8); compare=; stallok=1; until='back to the lobby screen'   # the page leaves
             # mid-match, the level runs on (as disconnect), then the Switch QUITs as a player does (Start, Down, Confirm): TF.EX's way out
             # (console 2026-10-28: after the browser quit, the Switch couldn't leave the level)
             expect=('Disconnected from .* the level runs on without GGRS' '\[pause\] test: TF.EX.s IsDisconnected for 2 s' 'Level was left for the menu' 'back to the lobby screen'); shotat='\[pause\] test: after Start' ;;
  report)    hw=("vsstop 9600"); envs+=(LVB_PAGELEAVE=8 LVB_REPORTS=1); compare=; until='\[tfex\] Sent disconnect report'   # TF.EX's MatchReport: the
             # server turns reports on; the page leaves mid-match: TF.EX exports (no replay) as the lobby empties, an unfinished match -> the
             # Switch zips its log and POSTs it, the server accepts it (lvb-server.log "match report accepted", the zip kept in the out dir)
             expect=('MatchReport.Begin: the lobby has reports on' 'zip: report built: 1 entry' \
                     'HttpClient.PostAsync 127.0.0.1:[0-9]+/report\?group=.*&kind=disconnect .*status 200' 'Sent disconnect report \([0-9]+ KB\), server answered 200')
             shotat=$until ;;
  spectate)  hw=("vsstop 9600"); envs+=(LVB_PAGES=2 LVB_SPECTATE=${SMOKE_SPECTATE:-early})   # a second page spectates the Switch's lobby match
             # (TF.EX's SPECTATE, before the match: the session's spectators; SMOKE_SPECTATE=late: once it started, SpectatorJoined -> AddLateSpectator)
             expect=('\[np\] lobby match #1 synchronized' 'netplay_init config: .*"Spectators":\["[0-9a-f-]+"\]|Deferred late spectator .* added|Late spectator .* added to the session')
             pageexpect=('this page spectating' 'Synchronized with' 'first round spawned'); shotat='\[np\] lobby match #1 synchronized' ;;
  pause)     hw=("lobbypause 300 1" "vsstop 2400"); expect=('\[np\] lobby match #1 synchronized' 'pause\] test: overlay open at frame' 'after RESUME the overlay is closed'); shotat='pause\] test: overlay open at frame' ;;
  series)    hw=("lobbylength 0" "lobbyseries 3"); envs+=(LVB_PAGESERIES=1); compare=; until='\[lobby\] series: .*finished 1'   # a best-of-3: game 1,
             expect=('match results finished' '\[menu\] state 61' '\[np\] lobby match #2 synchronized' "$until"); shotat=$until ;;   # TF.EX's series screen (61), game 2 (3), the end
  options)   hw=("optionsdrive 1" "optionstext1 OPTNAME" "optionstext2 @SERVER@" "vsstop 1200"); envs+=(LVB_SETTINGS_SERVER=ws://127.0.0.1:1)   # TF.EX's netplay
             expect=('OPTIONS: TF.EX.s netplay settings added' '\[tfex\] TF.EX.s settings stored \(name OPTNAME, server ws://127.0.0.1:[0-9]+,' '\[tfex\] Send .*StartLobbyChoice')   # settings on the OPTIONS
             shotat='TF.EX.s settings stored \(name OPTNAME' ;;   # screen: the name and the server typed there (the run starts on a wrong server: the lobby works only with the typed one)
  quit)      hw=("lobbylength 0" "lobbyvote 3" "$scriptwins"); compare=; until='back to the lobby screen \(MainMenu 62\)'   # ([menu] state 62 also comes on the way in)
             expect=('match results finished' 'test: QUIT' 'back to the lobby screen \(MainMenu 62\)'); shotat=$until ;;
  localtwo)  hw=("lobbymax 3" "lobbylocal2 1" "vsstop 2400"); expect=('\[ui\] controller 2: START on the netplay menu' '\[ui\] controller 2: joined as P2' '\[ui\] controller 2: this device has a second seat' '\[tfex\] Send .*StartLobbyChoice' '\[np\] lobby match #1 synchronized'); shotat='\[np\] lobby match #1 synchronized' ;;
  backout)   hw=("lobbyback 1"); compare=; until='\[ui\] back on the main menu: done'; expect=('\[ui\] press B \(leave the lobby\)' '\[ui\] back on the main menu: done'); shotat=$until ;;   # the host backs out of a lobby the page's player is in (2026-10-25 console crash)
  aiseat)    hw=("aiseat ${SMOKE_AITYPE:-1}" "vsstop 9600"); expect=('\[aiseat\] hw.txt aiseat' '\[np\] lobby match #1 synchronized' '\[aiseat\] seat [0-9]: the AI plays' '\[pc\] round level #2'); shotat='\[pc\] round level #2' ;;   # (the miasma would take ~11000 ticks: a round over by 9600 is a kill)
  three)     hw=("lobbymax 3" "vsstop 2400"); envs+=(LVB_PAGES=2); expect=('\[tfex\] Send .*StartLobbyChoice' '\[np\] lobby match #1 synchronized'); shotat='\[np\] lobby match #1 synchronized' ;;
  *) echo "usage: tools/smoke.sh lobby|join|private|privatejoin|quickplay|rematch|archer|team|disconnect|leftquit|report|spectate|pause|quit|series|options|three|localtwo [extra hw.txt lines]" >&2; exit 2 ;;
esac

# the Switch's seat played by LoaderAI's AI (hw.txt aiseat, TEST ONLY: real play in place of the test script) in the scenarios whose
# match needs no scripted moves; the others (a scripted win, pause, quit, a page leaving) keep the test script. SMOKE_AI=0: the script
# everywhere; SMOKE_AITYPE: the AI type (1 NOVA, 2 JIMMY)
case "$sc" in
  lobby|join|private|privatejoin|quickplay|team|three|localtwo|spectate)
    [ "${SMOKE_AI:-1}" != 0 ] && { hw+=("aiseat ${SMOKE_AITYPE:-1}"); expect+=('\[aiseat\] seat [0-9]: the AI plays'); } ;;
esac

shotat=${SMOKE_SHOTAT:-$shotat}   # SMOKE_SHOTAT=<regex>: the screenshot at another log line (e.g. 'NETPLAY at' for the main menu)
shotdelay=${SMOKE_SHOTDELAY:-$shotdelay}   # SMOKE_SHOTDELAY=<s>: that many seconds after it
# GGRS compares TF.EX's own checksum on both sides (ggrs_ffi's fletcher16 of TF.State's saved bytes, byte-identical to the PC's): the
# page's Switch-lobby digest off (?switchdigest=0, for page builds from before the browser dropped it)
urlextra="switchdigest=0${SMOKE_URLEXTRA:+&$SMOKE_URLEXTRA}"   # SMOKE_URLEXTRA=<a=b&c=d>: more page URL parameters
[ "$dump" != 0 ] && urlextra+="&statedump=$dump"
csum="GGRS checksum"
[ -n "$urlextra" ] && envs+=("LVB_URLEXTRA=$urlextra")
# hw.txt: base, then the scenario's, then the caller's (a later line with the same key replaces an earlier one)
lines=()
for l in "${base[@]}" "${hw[@]}" "$@"; do
  k=${l%% *}; keep=()
  for x in ${lines[@]+"${lines[@]}"}; do [ "${x%% *}" = "$k" ] || keep+=("$x"); done
  lines=(${keep[@]+"${keep[@]}"} "$l")
done
# hw.txt lobbyarcher N (the Switch steps to archer N, e.g. a mod archer): the page picks the next archer (TF.EX gives a mod archer that
# isn't shared yet a vanilla stand-in, the first one no lobby record claims: the page's own default, which the game then won't let
# the page ready on), and the sync deadline allows for the Switch's stepping (it counts from the page's join: 2026-10-30, 66 s)
for l in "${lines[@]}"; do
  [ "${l%% *}" = lobbyarcher ] && envs+=("LVB_PAGEPICK=${LVB_PAGEPICK:-ArrowRight}" "LVB_SYNCMAX=${LVB_SYNCMAX:-150}")
done

rm -f "$out"/smoke-*.png "$out/lvb-phases.txt" "$out/smoke-run.txt"   # (smoke-run.txt: a run that never starts must not be judged by the last one's)
wb=${TMPDIR:-/tmp}/xplay-winid
[ "$wb" -nt "$here/tools/winid.swift" ] || swiftc -O -o "$wb" "$here/tools/winid.swift" 2>/dev/null
# the run (lobby-auto: lobby-vs-browser MANUAL + the page's keys); LVB_NODEFAULTS: the lines above are the whole hw.txt
pre=()   # a whole Instant match (~3000 ticks at ~30/s on a busy Mac) plus the next step: a longer run limit and match time
# the scripted inputs end an Instant match only on some levels: a seed where seat 0 wins round 1 (frame 3157; LVB_SEED overrides)
[ -n "$until" ] && { envs+=("LVB_UNTIL=$until" LVB_MATCHMAX=${untilmatch:-400} "LVB_SEED=${LVB_SEED:-1374205500}"); pre=(-s ${untilrun:-600}); }   # (a scenario may set untilmatch / untilrun: s from sync, s in all)
env ${envs[@]+"${envs[@]}"} LVB_NODEFAULTS=1 tools/lobby-auto.sh ${pre[@]+"${pre[@]}"} "${lines[@]}" > "$out/smoke-run.txt" 2>&1 &
la=$!
# the screenshot: this run's own Ryujinx log (the instance started after this script: an early line such as the main menu's "NETPLAY at"
# comes long before the harness writes its phases, which is what the shooter used to wait for), then the moment's line + 2 s
t_start=$(date +%s)
etime_s() { ps -o etime= -p "$1" 2>/dev/null | awk '{n=split($1,a,/[-:]/); s=0; m=1; for(i=n;i>=1;i--){s+=a[i]*m; m=(i==n||i==n-1)?m*60:(i==n-2?m*24:m)} print s}'; }
(
  log=; for _ in $(seq 1 1200); do
    kill -0 $la 2>/dev/null || break
    pid=$(pgrep -n -x "$RJ_NAME")
    if [ -n "$pid" ] && [ "$(etime_s "$pid")" -le $(( $(date +%s) - t_start + 2 )) ]; then
      log=$(lsof -p "$pid" -Fn 2>/dev/null | sed -n 's/^n\(.*\/Ryujinx_[^/]*\.log\)$/\1/p' | head -1)
      [ -n "$log" ] && break
    fi
    sleep 0.5
  done
  while kill -0 $la 2>/dev/null; do
    if [ -n "$log" ] && /usr/bin/grep -aqE "$shotat" "$log"; then
      sleep $shotdelay; w=$("$wb" "$(pgrep -n -x "$RJ_NAME")" 2>/dev/null)
      [ -n "$w" ] && screencapture -x -o -l "$w" "$out/smoke-$sc.png" 2>/dev/null
      # the page at the same moment (its driver, from lobby-auto's MANUAL line): smoke-<scenario>-page.png
      pg=$(sed -n 's/.*curl \(localhost:[0-9]*\/[A-Za-z0-9]*\)\/key.*/\1/p' "$out/lobby-auto.txt" 2>/dev/null | tail -1)
      [ -n "$pg" ] && curl -s -m 10 "$pg/shot" -o "$out/smoke-$sc-page.png"
      break
    fi
    sleep 0.5
  done
) &
shooter=$!
wait $la
kill $shooter 2>/dev/null; wait $shooter 2>/dev/null

keep=$(grep -o 'logs kept in .*' "$out/smoke-run.txt" 2>/dev/null | tail -1 | cut -c14-)
[ -n "$keep" ] || { echo "SMOKE $sc FAIL: the run kept no logs (it didn't run: $(tail -1 "$out/smoke-run.txt" 2>/dev/null | cut -c1-160)) | shot none | logs $out"; exit 1; }
rl=$out/lvb-ryujinx.log; [ -n "$keep" ] && [ -f "$keep/ryujinx.log" ] && rl=$keep/ryujinx.log
shot=$out/smoke-$sc.png; [ -s "$shot" ] || shot="none"
# SMOKE_HOOKS="Ice.Update TriggerArrow.PlayEyeAnim": the hooks a change adds; the line says which of them ran (the generated hooks'
# "[tfexgen] hook <Type.Method> call 1" line) and which the run never reached (a PASS that didn't exercise the change)
hooks() {
  local n ran="" miss="" h; n=$(/usr/bin/grep -aoE '\[tfexgen\] hook [A-Za-z0-9_.]* call 1[: ]' "$rl" 2>/dev/null | sort -u | wc -l | tr -d ' ')
  for h in ${SMOKE_HOOKS//,/ }; do
    if /usr/bin/grep -aqE "\[tfexgen\] hook ${h//./\\.} call 1[: ]" "$rl" 2>/dev/null; then ran="$ran $h"; else miss="$miss $h"; fi
  done
  echo " | $n TF.EX hooks ran${SMOKE_HOOKS:+${ran:+; ran:$ran}${miss:+; NOT REACHED:$miss}}"
}
res() { echo "SMOKE $sc $1: $2$(hooks) | shot $shot | logs ${keep:-$out}"; [ "$1" = PASS ]; exit $?; }
# what differs, from the dumped states (statediff.py: the Switch's log against a page's console, field by field with TF.State's names;
# --bytes: the saved bytes GGRS checksums): the first differing frame and field for the FAIL line, the whole report next to the logs
statediff() {
  local page=$1 rep=${keep:-$out}/statediff-$(basename "$1" .txt).txt
  [ "$dump" = 0 ] && { echo "no state dumps (SMOKE_STATEDUMP=0)"; return; }
  # not the match's last second: the Switch parks at vsstop and stops sending inputs, so the page's last frames may be predictions GGRS
  # never confirms (wide7 120856 under load: frame 2340 saved while the Switch was interrupted, then the Switch was gone)
  local last; last=$(grep -ao '\[state\] dump frame [0-9]*' "$rl" | awk '{print $4}' | sort -n | tail -1)
  python3 tools/tfex/statediff.py "$rl" "$page" --bytes --before $(( ${last:-0} > 240 ? ${last:-0} - 240 : 0 )) > "$rep" 2>&1
  # the first differing frame after 0 (frame 0 differs in passing matches too: the state saved before the first advance, e.g.
  # OrbLogic's EngineTimeMult), else frame 0
  local first fr; first=$(grep -E '^  frame [0-9]+: ([0-9]+ fields differ|missing)' "$rep" | grep -v -m1 '^  frame 0:')
  [ -n "$first" ] || first=$(grep -m1 -E '^  frame [0-9]+: ([0-9]+ fields differ|missing)' "$rep")
  if [ -n "$first" ]; then
    fr=$(echo "$first" | sed -E 's/^  frame ([0-9]+):.*/\1/')
    echo "statediff: ${first#  }; first: $(grep -A1 -m1 -E "^  frame $fr: [0-9]+ fields differ" "$rep" | tail -1 | sed 's/^ *//' | cut -c1-140) ($(basename "$rep"))"
  elif grep -q 'compared 0' "$rep"; then echo "statediff: no common dumped frames ($(basename "$rep"))"
  else echo "statediff: the dumped states are identical; $(grep -m1 'saved bytes identical' "$rep" | sed 's/^statediff: //') ($(basename "$rep"))"; fi
}

[ -s "$rl" ] || res FAIL "no Ryujinx log (the run never started: $(tail -1 "$out/smoke-run.txt"))"
/usr/bin/grep -aq 'Unhandled exception' "$rl" && res FAIL "the guest crashed ($(/usr/bin/grep -a -m1 'Unhandled exception' "$rl" | cut -c1-120))"
stop=$(grep -a 'STOP:' "$out/lvb-phases.txt" 2>/dev/null | head -1 | sed 's/.*STOP: //')
case "$stop" in failed:*) res FAIL "the harness stopped the run on a known failure: ${stop#failed: }" ;; esac
[ -z "$stallok" ] && case "$stop" in *stalled*) res FAIL "the harness stopped a stalled match: $stop" ;; esac   # (the ticks before it can be in sync: not a PASS)
for e in "${expect[@]}"; do
  /usr/bin/grep -aqE "$e" "$rl" || res FAIL "never logged '$e'${stop:+ (harness: $stop)}"
done
lastpage=$(ls "$out"/lvb-browser-*.txt 2>/dev/null | sort | tail -1)
for e in ${pageexpect[@]+"${pageexpect[@]}"}; do
  grep -aqE "$e" "$lastpage" 2>/dev/null || res FAIL "the page $(basename "$lastpage" .txt) never logged '$e'${stop:+ (harness: $stop)}"
done
[ ${#pageexpect[@]} -gt 0 ] && grep -aq '^page error' "$lastpage" 2>/dev/null && res FAIL "the page $(basename "$lastpage" .txt) had an error: $(grep -a -m2 '^page error' "$lastpage" | tail -1 | cut -c1-120)"
for b in "$out"/lvb-browser-*.txt; do
  d=$(grep -ac 'DesyncDetected' "$b" 2>/dev/null); [ "${d:-0}" -gt 0 ] && res FAIL "the $csum reported $d desyncs on $(basename "$b" .txt); $(statediff "$b")"
done
if [ -n "$compare" ]; then   # the saved bytes of every dumped frame after 0 the same on both sides (the page writes no tick log at 240 Hz)
  [ "$dump" = 0 ] && res FAIL "the state compare needs the state dumps (SMOKE_STATEDUMP=0)"
  rep=${keep:-$out}/statediff-match.txt
  python3 tools/tfex/statediff.py "$rl" "$(ls "$out"/lvb-browser-*.txt | sort | head -1)" --bytes > "$rep" 2>&1
  line=$(grep 'saved bytes identical' "$rep" | sed 's/^statediff: //')
  [ -z "$line" ] && res FAIL "no state compare ($(basename "$rep"))${stop:+ (harness: $stop)}"
  case "$line" in *"differ at [0]"|*frames) ;; *) res FAIL "out of sync: $line; $(statediff "$(ls "$out"/lvb-browser-*.txt | sort | head -1)")" ;; esac   # (frame 0 differs in every match)
  res PASS "in sync ($line), $csum clean, flow lines seen"
fi
res PASS "flow lines seen, $csum clean (no state compare: two matches or a menu ending)"
