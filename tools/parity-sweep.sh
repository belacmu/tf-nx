#!/bin/bash
# parity-sweep.sh [scenario file] [names...]: state parity between the Switch and TF.EX (the browser page), one match per scenario.
#
# Each scenario is one smoke.sh lobby match (the Switch hosts, the page joins) with TF.State's saved states dumped on both sides every
# 120 frames, then compared: tools/tfex/statediff.py names the fields that differ, --bytes compares the saved bytes GGRS checksums.
# Scenarios mix as much as possible per run (teams, several Switch players, wide, variants); one that fails is split up to dig in.
#
# The scenario file (default tools/parity/scenarios.txt): one per line, fields separated by " | ":
#   name | seed | hw.txt lines separated by ";" | environment (K=V ...)
# The tower, mode and variants come from hw.txt (lobbymap, lobbymode / lobbyteam, lobbyvariants TITLE|TITLE, lobbylocal2, lobbywide ...);
# a wide match also needs LVB_MODS=TF.EX,Teuria.WiderSet LVB_XLOBBY_EXTRA=wide=1 LVB_PREKEYS=c in the environment.
#
# The page: BROWSER_LOBBY_WT (default the browser worktree harness-dump: browser main + the page's ?statedump, the local server's
# TFEX_FIXED_SEED and xlobby wide=1, local harness commits that are never pushed). Each run's verdict is one line in <out>/summary.txt:
#   <name>: PASS|FAIL <smoke's verdict> | states: <first frames that differ> | bytes: <identical n of m>
# PASS needs no reader-check failure (statemeasure: each save read back must write the same bytes), smoke's PASS (in sync, GGRS checksum clean) and identical saved bytes on every dumped frame after 0 (frame 0 differs by
# Engine.TimeMult: not checksummed). Runs hidden on the private Ryujinx slot XPLAY_RJSLOT (default "update").
# Env: PARITY_OUT (default <scratch>/parity-<date>), PARITY_STOP (vsstop, default 1800).
set -u
here=$(cd "$(dirname "$0")/.." && pwd); cd "$here"
file=${1:-tools/parity/scenarios.txt}; shift || true
want=" $* "
out=${PARITY_OUT:-${TMPDIR:-/tmp}/parity-$(date +%Y%m%d-%H%M%S)}; mkdir -p "$out"
export BROWSER_LOBBY_WT=${BROWSER_LOBBY_WT:-$HOME/Code/Towerfall Browser/.claude/worktrees/harness-dump}
export XPLAY_RJSLOT=${XPLAY_RJSLOT:-update} XPLAY_VISIBLE=0 SMOKE_URLEXTRA="statedump=120${SMOKE_URLEXTRA:+&$SMOKE_URLEXTRA}"
stop=${PARITY_STOP:-1800}
echo "parity-sweep: $file -> $out (page $BROWSER_LOBBY_WT)"
pass=0; fail=0
while IFS= read -r line; do
  case "$line" in ''|\#*) continue ;; esac
  # fields split at " | " only (a variant list's own "|" has no spaces)
  rest="$line "; name=${rest%% | *}; rest=${rest#* | }; seed=${rest%% | *}; rest=${rest#* | }; hwl=${rest%% | *}; envl=${rest#* | }
  [ "$envl" = "$rest" ] && envl=""
  name=$(echo $name); seed=$(echo $seed)
  [ "$want" = "  " ] || [[ "$want" == *" $name "* ]] || continue
  hw=("statedump 120" "vsstop $stop" "statemeasure 1 0")   # statemeasure: every save read back and written again (the reader check)
  IFS=';' read -r -a extra <<< "$hwl"
  for x in "${extra[@]}"; do x=$(echo "$x" | sed 's/^ *//; s/ *$//'); [ -n "$x" ] && hw+=("$x"); done
  read -r -a envs <<< "$envl"
  r=$(env LVB_SEED="$seed" ${envs[@]+"${envs[@]}"} tools/smoke.sh lobby "${hw[@]}" 2>&1 | tail -1)
  d=${r##*logs }; mkdir -p "$out/$name"
  cp "$d"/ryujinx.log "$d"/lvb-browser.txt "$out/$name/" 2>/dev/null
  sd=$(python3 tools/tfex/statediff.py --bytes "$out/$name/ryujinx.log" "$out/$name/lvb-browser.txt" 2>&1); echo "$sd" > "$out/$name/statediff.txt"
  states=$(echo "$sd" | grep -E "^  frame [0-9]+: [0-9]+ fields" | grep -v "^  frame 0:" | head -2 | sed 's/^ *//' | tr '\n' ';')
  bytes=$(echo "$sd" | sed -n 's/^statediff: saved bytes identical on //p')
  rc=$(grep -ac "reader check at frame" "$out/$name/ryujinx.log" 2>/dev/null)
  verdict=FAIL
  case "$r" in *"SMOKE lobby PASS"*) [ -z "$states" ] && [ "${rc:-0}" = 0 ] && { [[ "$bytes" != *differ* ]] || [[ "$bytes" == *"differ at [0]" ]]; } && verdict=PASS ;; esac
  [ "$verdict" = PASS ] && pass=$((pass + 1)) || fail=$((fail + 1))
  v=${r#SMOKE lobby }; v=${v%% | shot*}
  echo "$name: $verdict $v | reader: ${rc:-0} bad | states: ${states:-same after frame 0} | bytes: ${bytes:-no dumps}" | tee -a "$out/summary.txt"
done < "$file"
echo "parity-sweep: $pass passed, $fail failed ($out/summary.txt)"
