#!/bin/bash
# lobby-vs-browser.sh [-s secs] [hw.txt lines...]: a LOBBY match, all local. The Switch build (Ryujinx, hidden) hosts a TF.EX lobby on a
# local tfex-server (cloudflare/tfex-server/local.mjs of the browser worktree; TF.EX's signaling room /room/<RoomId>);
# one headless Chrome page (switch-lobby build, `xlobby` logging + the reference scripts) joins it through TF.EX's own menus, readies;
# the Switch starts (StartLobby) and both play the match over WebRTC (tfx-rtc on the Switch) at 60 Hz. Then compares the two logs.
# Default hw.txt lines: rngcapture 1 / lobbyui 1 / lobbyscript 1 / lobbypred 8 / vsstop 4380; passed lines replace the default
# with the same key and add the rest (LVB_NODEFAULTS=1: only the passed lines).
# Ryujinx settings and window: XPLAY_RJCONFIG, XPLAY_VISIBLE (run-probe.sh).
# LVB_SEED=<n>: the local server gives every lobby that seed (needs the harness worktree's matchmaker.js TFEX_FIXED_SEED hook).
# LVB_REPORTS=1: the local server turns TF.EX's match reports on in its lobbies (IsReportEnabled) and accepts them (POST /report?kind=:
# logged in lvb-server.log, the zip kept as $out/report-<group>-<name>-<kind>.zip; the harness worktree's TFEX_REPORTS hook).
# LVB_UNTIL=<regex>: end the run on this guest-log pattern instead of "vsstop: parking" (e.g. a menu test).
# LVB_FAILON=<regex>: stop the run at once on this guest-log pattern (a scenario's known failure: no waiting out the stall timeout).
# LVB_URLEXTRA=<params>: added to the page URL, e.g. netdelay=40&netjitter=20 (tfnet.js: received packets held that long: an internet
# connection's latency, so the match rolls back).
# LVB_MODS=<a,b>: the page's mods (default TF.EX). A wide match (docs/ws-wide.md): LVB_PAGEWIDE=1 LVB_MODS=TF.EX,Teuria.WiderSet
# LVB_XLOBBY_EXTRA=wide=1 LVB_PREKEYS=c with hw.txt "lobbywide 1" (the page downloads WiderSet from GameBanana's mirror; LVB_PREKEYS:
# keys sent before the usual ones: c crosses WiderSet's STANDARD / WIDE picker on STANDARD). LVB_PAGEWIDE=1 (lobby-auto.sh) puts the
# page in WIDE, hosting or joining: TF.EX refuses a wide lobby from STANDARD ("WIDERSET MOD ON WIDE REQUIRED").
# LVB_SPECTATE=early|late (with LVB_PAGES=2): the last page spectates the Switch's lobby instead of joining (lobby-auto.sh): early = before
# the match starts (the session's spectators), late = once it started (SpectatorJoined -> the host's AddLateSpectator).
# LVB_PAGES=<1..8>: that many browser pages (players LA<slot>, LB<slot>, LC<slot>, ...) join the Switch's lobby: a 2-9 player match (pass
# hw.txt "lobbymax <pages + 1>"); every page's console is $out/lvb-browser-<player>.txt, lvb-browser.txt the first one's.
# Logs: $out/lvb-browser.txt (page console), $out/lvb-sd.txt (Switch), $out/lvb-server.log, $out/lvb-A.png (last page screenshot).
set -u
here=$(cd "$(dirname "$0")/.." && pwd); cd "$here"
out=${XPLAY_OUT:-/tmp/xplay-runs-$(printf '%s' "$here" | shasum | cut -c1-8)${XPLAY_RJSLOT:+-$XPLAY_RJSLOT}}; mkdir -p "$out"   # this checkout's run outputs (space-free; other agents run the same scripts)
WT=${BROWSER_LOBBY_WT:-$HOME/Code/Towerfall Browser/.claude/worktrees/tfex-0281}   # the browser on TF.EX v0.28.1 (branch claude/tfex-0.28.1 from main, 2026-10-31: the release pinned; v0.28.0 was tfex-028), every lobby at TF.EX's 240 Hz, built with the browser repo's own netplay build (vendor/netplay: ggrs-ffi with several local handles); gamefiles/ links the Steam TowerFall as tfex-028's
secs=
while getopts "s:" o; do case $o in s) secs=$OPTARG;; esac; done; shift $((OPTIND-1))
# hw.txt: the defaults, each replaced by a passed line with the same key (its first word), plus every passed line. (Before, any passed
# line dropped all defaults: "vsstop 600" alone meant no lobbyhost, and the Switch never hosted.) LVB_NODEFAULTS=1: only the passed lines.
if [ -z "${LVB_NODEFAULTS:-}" ]; then
  merged=()
  for d in "rngcapture 1" "lobbyui 1" "lobbyscript 1" "lobbypred 8" "vsstop 4380"; do
    found=; for a in "$@"; do [ "${a%% *}" = "${d%% *}" ] && found=1; done
    [ -n "$found" ] || merged+=("$d")
  done
  set -- ${merged[@]+"${merged[@]}"} "$@"
fi
# The run lasts only as long as it can prove something: a time limit from the match length (vsstop ticks at 60 Hz, x2, plus boot and
# join), and a watchdog (below) that ends it as soon as the page fails to join, the match fails to synchronize, the driver dies or the
# match stalls. LVB_MENUMAX (s from the start until every page connected to the lobby server, its online menu: default 300, +30 a page), LVB_MENUSETTLE (s for the menu to build after that before keys: 8), LVB_JOINMAX (s from the online menu until the page has joined, default 150), LVB_SYNCMAX (s from joining to synchronized, 60),
# LVB_MATCHMAX (s from synchronized to vsstop, default vsstop/30 + 30: a 4-player rollback match on a busy Mac runs ~7 ticks/s; give it -s too).
vst=$(printf '%s\n' "$@" | awk '$1 == "vsstop" {print $2}' | tail -1); vst=${vst:-4380}
menumax=${LVB_MENUMAX:-$(( 300 + 30 * (${LVB_PAGES:-1} - 1) ))}   # (+30 s a page after the first: they boot side by side; 300: a fresh profile re-patches the game, and on a busy Mac the page armed ~200 s in, after which the tutorial box still needs its nudges: wide8end 140106-144143)
[ -n "$secs" ] || secs=$(( 180 + vst / 30 + 40 * (${LVB_PAGES:-1} - 1) ))   # (+40 s for each page after the first: they join one at a time)
# Ports, page player and its Chrome profile per checkout (tools/harness-slot.sh): two sessions' harnesses never share a server, page or driver.
source tools/harness-slot.sh
# a private Ryujinx (XPLAY_RJSLOT, tools/rj-slot.sh) runs beside this checkout's shared-Ryujinx runs: its own ports (+1..9, never another checkout's)
[ -n "${XPLAY_RJSLOT:-}" ] && [ -z "${XPLAY_SLOT:-}" ] && XS=$(( XS + 1 + $(printf '%s' "$XPLAY_RJSLOT" | cksum | cut -d' ' -f1) % 9 ))
SERVER_PORT=${SERVER_PORT:-$((13300 + XS))}; PAGE_PORT=${PAGE_PORT:-$((18180 + XS))}; DRV=${DRV:-$((19400 + XS))}; P=${LVB_PLAYER:-LA$XS}
NP=${LVB_PAGES:-1}; PL=$P; for l in B C D E F G H; do [ $(( $(printf '%d' "'$l") - 65 )) -lt $NP ] && PL="$PL,L$l$XS"; done   # the pages' players
source tools/rj-slot.sh; sd="$RJ/sdcard/xplay"
source tools/ryujinx-lock.sh

# the Switch's files: lobby settings, scripts, no menu script, no net.txt stages; TF.EX.settings.json -> the local server, set aside
# and put back at the end (2026-10-18: a settings file naming the real server sent every harness lobby there)
# TF.EX's own settings for the run (its settings store, sd:/xplay/TF.EX.settings.json: name, the local server, input delay), padded to
# the store's fixed 1024 bytes; netplay.txt keeps only the Switch's connection keys
tfex_settings() { python3 -c 'import json,sys; d={"InputDelayMs":33,"AcceptedRollbackFrames":2,"Name":"RYUJINX","Server":sys.argv[1],"AutoAdjustInputDelay":"PROPOSE","CustomSkins":"FULL","AutoUpdate":False,"PlayerId":"","LastVersionSeen":"0.21.1"}; s=json.dumps(d,separators=(",",":")); sys.stdout.write(s+" "*(1024-len(s)))' "$1"; }
cp "$sd/netplay.txt" $out/lvb-netplay.txt.bak; cp "$sd/net.txt" $out/lvb-net.txt.bak; cp "$sd/script.txt" $out/lvb-script.txt.bak
rm -f $out/lvb-settings.json.bak; [ -f "$sd/TF.EX.settings.json" ] && mv "$sd/TF.EX.settings.json" $out/lvb-settings.json.bak
trap 'cp $out/lvb-netplay.txt.bak "$sd/netplay.txt"; cp $out/lvb-net.txt.bak "$sd/net.txt"; cp $out/lvb-script.txt.bak "$sd/script.txt"; rm -f "$sd/TF.EX.settings.json"; [ -f $out/lvb-settings.json.bak ] && mv $out/lvb-settings.json.bak "$sd/TF.EX.settings.json"; ryujinx_unlock' EXIT
# rtcbind 127.0.0.1: WebRTC over loopback (the Mac's firewall can drop UDP on the LAN address); no STUN (unreachable from there)
printf 'rtcbind 127.0.0.1\nstun off\n' > "$sd/netplay.txt"
tfex_settings "${LVB_SETTINGS_SERVER:-ws://127.0.0.1:$SERVER_PORT}" > "$sd/TF.EX.settings.json"   # (LVB_SETTINGS_SERVER: tests start elsewhere)
: > "$sd/net.txt"; : > "$sd/script.txt"
printf '%s\n' "$@" | sed "s|@SERVER@|ws://127.0.0.1:$SERVER_PORT|g" > "$sd/hw.txt"   # (@SERVER@: the local server, for tests that type it)
rm -f "$sd/vs-log-3m.txt"; touch $out/lvb-start.stamp   # (a run that ends before the Switch writes its tick log compared a stale one)
for i in 0 1 2 3; do cp ${LVB_SCRIPTS:-test-scripts/versus}/script$i.txt "$sd/vs$i.txt"; done   # LVB_SCRIPTS=<dir>: other seat scripts (script0..3.txt)
tools/rebaseline-save.sh >/dev/null || exit 1

started=
# always this run's own server (a leftover one would have another run's TFEX_FIXED_SEED); the port is this checkout's
pkill -f "local.mjs --port $SERVER_PORT( |$)" 2>/dev/null; pkill -f "driver-quiet.mjs.*--port $DRV( |$)" 2>/dev/null
lsof -ti tcp:$SERVER_PORT -sTCP:LISTEN 2>/dev/null | xargs kill 2>/dev/null; sleep 1   # (a real server left on this port)
if [ "${LVB_SERVER:-}" = real ]; then
  # LVB_SERVER=real: TF.EX's own matchmaking server (Fcornaire/tf-ex-matchmaking-server, our fork: browser pages allowed, the harness's log
  # lines; docs/SERVER-DIFF-2026-10-26.md) instead of local.mjs. LVB_SEED and LVB_REPORTS (zips in $out/reports) work as with local.mjs.
  RS=${TFEX_REAL_SERVER:-$HOME/Code/Towerfall Switch Mods/tf-ex-matchmaking-server/target/release/tf-ex-matchmaking-server}
  [ -x "$RS" ] || { echo "LVB_SERVER=real: no server binary at $RS (cargo build --release in the fork)" >&2; exit 1; }
  (TFEX_FIXED_SEED=${LVB_SEED:-} PORT=$SERVER_PORT METRICS_PORT=$((SERVER_PORT + 11000)) RELAY_PORT=$((SERVER_PORT + 12000)) ${LVB_REPORTS:+REPORTS_DIR=$out/reports} \
     "$RS" 2>&1 | perl -pe 'BEGIN { $| = 1 } s/\e\[[0-9;]*m//g' > $out/lvb-server.log &)
else
  # LVB_SERVER_WT=<browser worktree>: local.mjs from there (a server change), the page still from $WT
  (cd "${LVB_SERVER_WT:-$WT}" && TFEX_FIXED_SEED=${LVB_SEED:-} TFEX_REPORTS=${LVB_REPORTS:-} TFEX_REPORTS_DIR=$out node cloudflare/tfex-server/local.mjs --port $SERVER_PORT --verbose > $out/lvb-server.log 2>&1 &)
fi
started="$started server"
if ! curl -s -m 2 -o /dev/null "localhost:$PAGE_PORT/"; then (cd "$WT" && python3 tools/serve.py $PAGE_PORT > $out/lvb-serve.log 2>&1 &); started="$started page"; fi
sleep 2

# The pages' Chrome profiles start as clones of a warm template, not empty. An empty profile made every run copy and verify all the game
# files into the page's storage (OPFS) and patch TowerFall.exe again with MonoMod in wasm: 31-117 s to the online menu on 2026-11-02, and
# past LVB_MENUMAX (180 s) with three harnesses running. The template is a profile closed once the page has patched TowerFall.exe and
# installed the mods (FortRise's second "up to date" line), before the game starts (no save, no settings: a run still starts from the
# game's defaults); it is made once per page build (the _framework
# listing) and cloned per run (cp -c: APFS clones, nothing copied). One from an older build is still correct (the page checks its files'
# version and the patch's stamp itself), only slower. LVB_WARM=0: empty profiles, as before.
warm_profiles() {
  local tpl="$HOME/.towerfall-browser/netplay-tpl$XS-profile" www="${TOWERFALL_WWW:-$WT/web/bin/Release/net10.0/publish/wwwroot}" key wp=$((DRV + 5)) t=0
  key=$(ls -l "$www/_framework" 2>/dev/null | shasum | cut -c1-16)
  if [ "$(cat "$tpl.key" 2>/dev/null)" != "$key" ]; then
    rm -rf "$tpl" "$tpl.key"; pkill -f "driver-quiet.mjs.*--port $wp( |$)" 2>/dev/null
    (cd "$WT" && CONSOLE_TEE="$out/lvb-warm.txt" node "$here/browser-60hz/xnet-tools/driver-quiet.mjs" --url "http://localhost:$PAGE_PORT/?mute&nointro&autoplay&mods=${LVB_MODS:-TF.EX}&fortrise" \
       --players tpl$XS --port $wp > $out/lvb-warm-driver.log 2>&1 &)
    until grep -q 'TowerFall.Patch.dll is up to date' $out/lvb-warm.txt 2>/dev/null || [ $t -ge 240 ]; do sleep 1; t=$((t + 1)); done
    sleep 2
    curl -s "localhost:$wp/quit" >/dev/null; sleep 1; pkill -f "driver-quiet.mjs.*--port $wp( |$)" 2>/dev/null
    for i in $(seq 30); do pgrep -f "user-data-dir=$tpl" >/dev/null || break; sleep 1; done
    if grep -q 'TowerFall.Patch.dll is up to date' $out/lvb-warm.txt 2>/dev/null && ! pgrep -f "user-data-dir=$tpl" >/dev/null; then
      rm -f "$tpl"/Singleton*; echo "$key" > "$tpl.key"; echo "page profile: a warm template made in ${t} s (lvb-warm.txt)"
    else rm -rf "$tpl"; echo "page profile: no warm template (no FortRise \"up to date\" line in ${t} s, lvb-warm.txt): empty profiles"; fi
  fi
  for p in ${PL//,/ }; do
    rm -rf "$HOME/.towerfall-browser/netplay-$p-profile"
    [ -f "$tpl.key" ] && cp -c -R "$tpl" "$HOME/.towerfall-browser/netplay-$p-profile"
  done
}
[ "${LVB_WARM:-1}" = 0 ] || warm_profiles

# the page first (it loads for a while: .NET, the FortRise patch, mods), the Switch right after (its lobby is up ~5 s after boot);
# the page waits on the online menu until the keys below
t0=$SECONDS
S0=$(tr -d '\n' < ${LVB_SCRIPTS:-test-scripts/versus}/script0.txt); S1=$(tr -d '\n' < ${LVB_SCRIPTS:-test-scripts/versus}/script1.txt)
S2=$(tr -d '\n' < ${LVB_SCRIPTS:-test-scripts/versus}/script2.txt); S3=$(tr -d '\n' < ${LVB_SCRIPTS:-test-scripts/versus}/script3.txt)
CMD=$(python3 -c "import urllib.parse,sys; print(urllib.parse.quote(sys.argv[1], safe=''))" "xlobby script0=$S0 script1=$S1 script2=$S2 script3=$S3 maxticks=4400 origin=266${LVB_XLOBBY_EXTRA:+ $LVB_XLOBBY_EXTRA}")
URL="http://localhost:$PAGE_PORT/?mute&nointro&autoplay&mods=${LVB_MODS:-TF.EX}&fortrise&tfexserver=ws://127.0.0.1:$SERVER_PORT&mode=online${LVB_URLEXTRA:+&$LVB_URLEXTRA}&command=$CMD"
rm -f $out/lvb-browser*.txt $out/lvb-stop $out/lvb-phases.txt
ln -s lvb-browser-$P.txt $out/lvb-browser.txt
# The pages' Chrome profiles start from a template (an APFS clone, per browser worktree, mod set and TF.EX zip): FortRise's patched game and TF.EX's
# first-run tutorial already done, so a page boots in about a minute instead of 2-4 (a fresh profile re-patches the game every run).
# The template is the first page's profile after a run whose pages all reached the online menu; LVB_FRESHPROFILE=1: fresh profiles
# (and no template made).
tpl="$HOME/.towerfall-browser/netplay-template-$(printf '%s' "$WT ${LVB_MODS:-TF.EX} $(ls "$WT"/vendor/tfex/*.zip 2>/dev/null | xargs -n1 basename 2>/dev/null)" | shasum | cut -c1-10)-profile"   # (with the browser's TF.EX zip: a new TF.EX, a new template)
for p in ${PL//,/ }; do
  rm -rf "$HOME/.towerfall-browser/netplay-$p-profile"
  [ -z "${LVB_FRESHPROFILE:-}" ] && [ -d "$tpl" ] && cp -Rc "$tpl" "$HOME/.towerfall-browser/netplay-$p-profile"
done
[ -d "$tpl" ] && [ -z "${LVB_FRESHPROFILE:-}" ] && echo "lobby-vs-browser: the pages start from the template profile $tpl"
(cd "$WT" && CONSOLE_TEE="$out/lvb-browser-{p}.txt" node "$here/browser-60hz/xnet-tools/driver-quiet.mjs" --url "$URL" --players $PL --port $DRV > $out/lvb-driver.log 2>&1 &)
rm -f $out/lvb-ryujinx.log
XPLAY_LOGKEEP=$out/lvb-ryujinx.log XPLAY_STOPFILE=$out/lvb-stop XPLAY_UNTIL="${LVB_UNTIL:-vsstop: parking}" XPLAY_EXLAUNCH="$here/hw-probe/exlaunch" XPLAY_MOD=hw-probe XPLAY_TAG=vs tools/run-probe.sh "$secs" > $out/lvb-run.out 2>&1 &
rp=$!
# watchdog: phase times into lvb-phases.txt; lvb-stop (ends the run) once the run can't prove anything any more
(
  # bash 3.2 (macOS): no associative arrays; the phase times are a_<phase> variables
  ph() { eval "[ -n \"\${a_$1:-}\" ]" && return; eval "a_$1=$(( SECONDS - t0 ))"; echo "t+$(( SECONDS - t0 ))s $1" >> $out/lvb-phases.txt; }
  stop() { [ -f $out/lvb-stop ] || { echo "$1" > $out/lvb-stop; echo "t+$(( SECONDS - t0 ))s STOP: $1" >> $out/lvb-phases.txt; }; }
  a_page_connected=; a_page_joined=; a_synchronized=
  while kill -0 $rp 2>/dev/null; do
    sleep 1; t=$(( SECONDS - t0 ))
    grep -q 'xlobby.*cfg\|\[command\] xlobby' $out/lvb-browser.txt 2>/dev/null && ph page_online_menu
    grep -q 'CreateLobby\|lobby created\|"event":"lobby_created"' $out/lvb-server.log 2>/dev/null && ph switch_lobby
    [ "$(grep -l '\[mode\] online: entering netplay' $out/lvb-browser-*.txt 2>/dev/null | wc -l)" -ge $NP ] && ph page_connected   # every page's online menu
    [ "$(grep -c 'joined .* as player\|\[quickplay\] matched' $out/lvb-server.log 2>/dev/null)" -ge $(( NP - ${LVB_SPECTATE:+1}0 / 10 )) ] && ph page_joined   # every page (a spectating one: LVB_SPECTATE)
    # (the Switch's telemetry events lobby_match_start / netplay_synchronized / vsstop are gone since 2e1d293: the server's own line
    # and the pages' GGRS event instead; vsstop ends the run through run-probe's XPLAY_UNTIL)
    grep -q ' started (' $out/lvb-server.log 2>/dev/null && ph match_start
    [ "$(grep -l 'Event: Synchronized ' $out/lvb-browser-*.txt 2>/dev/null | wc -l)" -ge $NP ] && ph synchronized   # every page
    [ -n "${LVB_FAILON:-}" ] && l=$(/usr/bin/grep -aE -m1 "$LVB_FAILON" $out/lvb-ryujinx.log 2>/dev/null) && { stop "failed: ${l##*OutputDebugString: }"; break; }
    pgrep -f "driver-quiet.mjs.*--port $DRV( |$)" >/dev/null || { stop "the page's driver exited (lvb-driver.log)"; break; }
    if [ -z "$a_page_connected" ] && [ -z "$a_page_joined" ]; then [ $t -gt $menumax ] && { stop "the page never reached the online menu in $menumax s (no '[mode] online: entering netplay')"; break; }
    elif [ -z "$a_page_joined" ]; then [ $(( t - ${a_page_connected:-0} )) -gt ${LVB_JOINMAX:-150} ] && { stop "the page didn't join the lobby in ${LVB_JOINMAX:-150} s after its online menu"; break; }
    elif [ -z "$a_synchronized" ]; then [ $(( t - a_page_joined )) -gt ${LVB_SYNCMAX:-60} ] && { stop "no netplay sync ${LVB_SYNCMAX:-60} s after joining"; break; }
    elif [ $(( t - a_synchronized )) -gt ${LVB_MATCHMAX:-$(( vst / 30 + 30 ))} ]; then stop "the match stalled (no vsstop ${LVB_MATCHMAX:-$(( vst / 30 + 30 ))} s after sync)"; break; fi
  done
) &
wd=$!

key() { for k in "$@"; do curl -s "localhost:$DRV/$P/key?k=$k" >/dev/null; sleep 0.6; done; curl -s "localhost:$DRV/$P/shot" -o $out/lvb-A.png; }
# wait for every page's online menu, then the Switch's lobby. A page's online menu is up once it connected to the lobby server
# ("[mode] online: entering netplay"; the page connects to the lobby server only when LOBBIES / QUICK PLAY opens, so "connected to
# ws" comes too late to wait for); then LVB_MENUSETTLE s (default 8) for the menu to build. A fixed 20 s after xlobby armed sent the keys
# too early on a loaded Mac (they landed on QUICK PLAY). No menu within LVB_MENUMAX s (default 180): the run stops, saying so.
menu=
# The page enters netplay by itself (?mode=online) once its main menu sits on Main; a box in front of it (TF.EX v0.27's tutorial on a
# fresh profile, "What's new" after a version change) holds it there until a key (Back: SKIP / CONTINUE, both marked seen): LVB_MENUNUDGE s (default 25) after xlobby armed without "[mode] online", the page gets
# ${LVB_MENUKEY:-x} (a screenshot first: lvb-menu-N.png), up to 3 times, 20 s apart. (By then a plain main menu has entered netplay.)
armed=; nudged=0
while :; do   # (the watchdog stops the run after LVB_MENUMAX s without it)
  [ "$(grep -l '\[mode\] online: entering netplay' $out/lvb-browser-*.txt 2>/dev/null | wc -l)" -ge $NP ] && { menu=1; break; }
  [ -f $out/lvb-stop ] && break; kill -0 $rp 2>/dev/null || break; sleep 1
  [ -z "$armed" ] && grep -q 'xlobby.*cfg\|\[command\] xlobby' $out/lvb-browser.txt 2>/dev/null && armed=$SECONDS
  if [ -n "$armed" ] && [ $nudged -lt 3 ] && [ $(( SECONDS - armed )) -ge $(( ${LVB_MENUNUDGE:-25} + 20 * nudged )) ]; then
    nudged=$((nudged + 1))
    for pp in ${PL//,/ }; do   # every page still off its online menu (LVB_PAGES: each has its own box)
      grep -q '\[mode\] online' $out/lvb-browser-$pp.txt 2>/dev/null && continue
      curl -s "localhost:$DRV/$pp/shot" -o $out/lvb-menu-$pp-$nudged.png
      echo "lobby-vs-browser: page $pp hasn't entered netplay $(( SECONDS - armed )) s after xlobby armed: pressing ${LVB_MENUKEY:-x} (try $nudged; $out/lvb-menu-$pp-$nudged.png)" >&2
      curl -s "localhost:$DRV/$pp/key?k=${LVB_MENUKEY:-x}" >/dev/null
    done
  fi
done
for _ in $(seq 1 60); do grep -q 'created [A-Za-z]* lobby\|"event":"lobby_created"' $out/lvb-server.log 2>/dev/null && break; [ -f $out/lvb-stop ] && break; sleep 1; done
[ -n "$menu" ] && sleep ${LVB_MENUSETTLE:-8}
if [ -f $out/lvb-stop ]; then
  echo "page keys not sent: $(cat $out/lvb-stop)"
elif [ -z "${MANUAL:-}" ]; then
  [ -n "${LVB_PREKEYS:-}" ] && { key $LVB_PREKEYS; sleep 2; }   # e.g. c: a "What's new" box (shown when the page's mod set changes)
  key ArrowRight ArrowRight c    # LOBBIES (v0.27's NetplaySelect: QUICK PLAY, RANKED, LOBBIES, PRIVATE)
  sleep 3; key c                 # join the first lobby (the Switch's)
  sleep 4; key c                 # pick the archer: ready
  echo "page keys sent; waiting for the match"
else
  key; echo "MANUAL: drive the page with curl localhost:$DRV/$P/key?k=<key>, screenshot localhost:$DRV/$P/shot"
fi
wait $rp
kill $wd 2>/dev/null
[ -f $out/lvb-stop ] || sleep 6   # the page logs its last ticks
curl -s "localhost:$DRV/quit" >/dev/null; sleep 2; pkill -f "driver-quiet.mjs.*--port $DRV( |$)"
case "$started" in *server*) pkill -f "local.mjs --port $SERVER_PORT( |$)"; lsof -ti tcp:$SERVER_PORT -sTCP:LISTEN 2>/dev/null | xargs kill 2>/dev/null;; esac
case "$started" in *page*) pkill -f "serve.py $PAGE_PORT";; esac
[ -n "$menu" ] && [ -z "${LVB_FRESHPROFILE:-}" ] && [ ! -d "$tpl" ] && cp -Rc "$HOME/.towerfall-browser/netplay-$P-profile" "$tpl" 2>/dev/null && echo "lobby-vs-browser: template profile made: $tpl"
for p in ${PL//,/ }; do rm -rf "$HOME/.towerfall-browser/netplay-$p-profile"; done
# the Switch's tick log only if this run wrote it, for the page's match (the first lobby match's seed on both sides)
nolog=; swseed=$(grep -a -o 'lobby match: seed [0-9]*' $out/lvb-ryujinx.log 2>/dev/null | head -1 | grep -o '[0-9]*$'); pgseed=$(grep -a -o 'RandomLevelSeed=[0-9]*' $out/lvb-browser.txt 2>/dev/null | head -1 | cut -d= -f2)
if [ ! -s "$sd/vs-log-3m.txt" ]; then nolog="the Switch wrote no vs-log-3m.txt"
elif [ ! "$sd/vs-log-3m.txt" -nt $out/lvb-start.stamp ]; then nolog="its vs-log-3m.txt is older than this run"
elif [ -n "$swseed" ] && [ -n "$pgseed" ] && [ "$swseed" != "$pgseed" ]; then nolog="the seeds differ (Switch $swseed, page $pgseed)"; fi
if [ -n "$nolog" ]; then : > $out/lvb-sd.txt; else tr -d '\000' < "$sd/vs-log-3m.txt" | sed 's/ *$//' | grep '^\[vs\]\|^\[cr\]' > $out/lvb-sd.txt; fi
grep -a '\[tfex\]\|lobby' $out/lvb-run.out | head -20 | cut -c1-200
echo "browser: $(grep -c '\[xplay2\] T ' $out/lvb-browser.txt) ticks; switch: $(grep -c '^\[vs\] [0-9-]' $out/lvb-sd.txt) lines"
echo "phases ($secs s limit):"; sed 's/^/  /' $out/lvb-phases.txt 2>/dev/null
if [ -n "$nolog" ]; then echo "no Switch tick log from this run ($nolog): not compared"; else
python3 tools/compare-vs.py $out/lvb-browser.txt $out/lvb-sd.txt | head -9 | cut -c1-230
for p in ${PL//,/ }; do [ $p = $P ] || { echo "page $p:"; python3 tools/compare-vs.py $out/lvb-browser-$p.txt $out/lvb-sd.txt | grep "differing ticks\|first divergence\|^switch origin" | cut -c1-230; }; done
fi
