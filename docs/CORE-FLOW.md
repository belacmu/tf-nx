# The core flow: what "done" means for priority 1 (2026-10-17)

## THE PLAN (the user, 2026-10-17 evening): this section governs; the rows below are the flow's state

**The goal:** TF.EX runs on the Switch as TF.EX's own code, with only the adjustments the Switch really needs (each a marked
TFEX-DIFF), so a new TF.EX version is cheap to take. Matches worked this morning; the trouble since comes from a halfway state
between TF.EX's code and custom code. So: first make ALL of TF.EX translate, then move each custom piece over to TF.EX's own.

**Phase 1: 100% of TF.EX translates.**
1. Scope (the translator): all of TF.EX (TF.EX.Patchs, TF.EX.Domain incl. NetplayManager / MatchmakingService / models / services,
   CustomComponent), minus only what the Switch deliberately replaces (TF.State's state serialization and the GGRS FFI: the Switch's
   rollback engine; Steam and other desktop-only code), each exclusion listed with its reason. One coverage report over that whole
   scope, by methods AND lines. Today's 90% (396/438 methods, 73% of lines) covers Patchs + CustomComponent only.
2. Close the gaps the report lists: translator constructs and compile errors, missing game members/types (RE + bindings): the
   translator; TF.EX calls that need a Switch rule ([service] rules): A (menus, lobby, matchmaking, roll call) and B (match and
   after), in their areas. Done = every in-scope method compiles as generated code (behind its unit, not necessarily on).

**Phase 2: custom -> TF.EX native**, in flow order (menus, matchmaking, roll call, match start, the match, end of match,
disconnect): each row's hand piece is retired once TF.EX's generated code passes that row's smoke scenarios and a console check.
Measure: hand-written TF.EX look-alikes in tfex/ (~6,000 lines on 2026-10-17) go to zero, except the TFEX-DIFF list.

**Update (the user, 2026-10-17 19:4x): Phase 2 starts now; translation is demand-driven.** The coverage number isn't chased: the
remaining untranslated TF.EX code is mostly code the Switch replaces. Translation continues only where a switch-over needs it. Three
tracks, each in its own files: **menus** (rows 1-5, the translator: TF.EX's generated screens by default, the hand screens retired);
**lobby** (rows 6-8, A: the roll call through TF.EX's reconcile, the settings store, the hand lobby code retired); **match** (rows 9-16,
B with the translator: NetplayManager (a)(b)(c)). Measure: hand-written TF.EX look-alike lines going down. One hardware session per
finished track. The kill desync: parked until NetplayManager (a). **No emulator tests for now** (the user): work is built and
committed with its new behaviour behind its flag (default off) until tests are allowed; then each track is tested, its flag goes on,
and its hand code is removed. Started first: the translator (menus), then A/B within the weekly budget.

**Wide mode (the user, 2026-10-18, corrected):** WiderSet is NOT translated. The Switch has wide support and the wide levels
natively; on the Switch, TF.EX's wide lobbies use them (a marked TFEX-DIFF: the Switch's own wide mode stands in for WiderSet).
The job is only to fill the gaps so it behaves as TF.EX + WiderSet expect: both wide hardware matches on 2026-10-18 desynced (narrow
ones stayed in sync), and the side art (sidebars) still shows on wide. Owners: B (match side, the desync), A (the wide screen layout /
sidebars, menus side), as gap fixes in their rows; no WiderSet port.

**Evaluation follow-up (docs/EVALUATION-2026-10-18.md; the user approved 2026-10-18 12:3x; build only, no testing):**
- Wave 1 (parallel): translator #3 (gen/ churn: runtime compiled in place, stable or per-unit aggregates, check-gen.sh; lands
  FIRST, alone, then everyone merges master), then #2a/2b (rule keys checked against each tag, scripts fail on missing inputs
  or a stale vendor/tfex). B2 #1 (measurements, log line length), #5 (SD flushing off the transport thread), its #4 rows (finished
  genstart/genend/sessionpatch paths, dead branches, research toggles, the variants hand copy, the hw.txt 1 KB cut). A its #4 rows
  (genmm/genmenus, dead MatchmakingService members and Gen()-null fallbacks, glue test hooks). Coordinator #9 (untrack
  tfx-rtc/target-soft, no history rewrite) and #12 (docs consistency, the inventory as a script).
- Wave 2 (after Wave 1 is merged): B2 #7's input-delay rule, #6 desync safety, the "every remote gone" TFEX-DIFF replaced by a crash
  fix + TF.EX's behaviour, then #10 split xplay.cpp. Later, after a test day: #7's rollback-cost work, #8 TF.State into the translator.

**Meanwhile:** fix only bugs that block this work. Parked until Phase 2 reaches them: the kill desync (12:55 and 17:44 hardware
matches: the Switch kills the browser's archer, the next check differs), the roll-call archer display, replays (off by decision),
settings polish, 3-4 players, wide/private on the console. After both phases: confirm every row on the console, then priority 2
(the update routine proven on the next TF.EX release, Switch and browser), then variants, then 240 Hz.

Priority 1 is done when every row below is **TF.EX's code** (generated, on by default), **has a smoke scenario that passes on master**,
and **was seen working on the console**. This table is the plan: the owners keep their rows current in each batch, and the coordinator
runs the full smoke suite on master after merges. Code column: `gen` = TF.EX's generated code, on by default; `gen-partial` = generated
functions called from hand hooks; `hand` = a hand port; `none` = missing.
Rows 1-5 moved from A to tfex-translator on 2026-10-17 15:40 (A was the bottleneck); B owns tools/smoke.sh and writes every new scenario.

Reassessed 2026-10-17 18:00 (all sessions stopped; master a8b5c6c = everything merged; the console runs d67a0cf).

| # | Step | Owner | TF.EX code (default) | Emulator | Console | Next |
|---|---|---|---|---|---|---|
| 1 | NETPLAY button, globe icon | translator | gen + TF.EX's icon | shot shows the globe | not checked | see it |
| 2 | NetplaySelect | translator | hand (gen behind genmenus 1) | all | seen (hand) | genmenus on by default once lobby/join/private pass |
| 3 | Lobby browser | translator | hand (gen behind genmenus 1) | join | seen (hand) | as 2 |
| 4 | Lobby builder | translator | hand (gen behind genmenus 1: create verified) | lobby | seen (hand) | as 2 |
| 5 | Private lobby, join by code | translator | hand | private, privatejoin pass | not tried | console: both ways, the keyboard |
| 6 | Quick play | A | hand screens + gen matchmaking | quickplay pass | not tried today | console: narrow and wide vs the live site |
| 7 | Matchmaking protocol | A | **gen, on** (genmm 0 = hand) | lobby/join/quickplay pass | ran in today's matches | console: the "PLAYER" name fix (master) |
| 8 | Roll call | A | gen (RollcallReconcile, RollCallElementPatch) | lobby, join, team pass | **bug: the browser's archer pick isn't drawn** | fix via TF.EX's reconcile; settings store |
| 9 | Match start, tower, seed | B | gen-partial (MapScenePatch); the start gen behind hw.txt genstart 1 (FFA: TF.EX EstablishSessionIfSynchronized / OnSessionEstablished / SpawnFirstRound), team: hand | lobby, join, privatejoin (genstart 1 pass) | RANDOM worked (towers 7, 12 wide) | genstart default after the user sees it; team mode (translator: type-test rule, docs/ws-match.md) |
| 10 | The match: rollback, digest | B | hand engine + gen ping line | all | **in sync on screen**; checksums disagree vs the live site (open) | settle on the fixed local page; NetplayManager (a) |
| 11 | Mid-match pause / QUIT | B | gen | pause, quit pass | QUIT worked (the user's +) | RESUME on the console |
| 12 | Disconnect | B | hand (default); gen behind hw.txt genend 1 (TF.EX EndMatch / EndMatchOnNetplayError / IsUnfinishedTeamMatch / Reset) | disconnect pass (both) | seen before (hand) | genend on by default after the user sees it; then retire the hand path |
| 13 | End of match, vote panel | B | gen | rematch, quit pass | not checked (quit early) | see the panel |
| 14 | REMATCH! | B | gen-partial | rematch pass | seen before | |
| 15 | ARCHER SELECT | B | gen | archer (to the roll call) | not tried | match 2 needs the page's vote= |
| 16 | QUIT to the menus | B | gen | quit pass | seen before | |
| 17 | 3-4 players | A + B | | three written, blocked by the harness page | | after the page is fixed |
| 18 | Browser side | browser | **live**: PR #71 (TF.EX's room, digest with a Switch player, WiderSet default) | 5 smoke pass on its build | lobby played, checksum alarms | the checksum question |

Infrastructure: the console log fix (1 MB head + rolling tail), the 240 Hz timing split and the per-rollback cost lines are on master,
not yet on the console. The emulator harness page is broken since ~16:45 (the coordinator's rebuild of switch-rtc); the browser
session is making one dedicated harness page. Replays: off by decision; settings: a saved store by decision (below).

Not in priority 1 (after it): variants beyond the safety gate, series, skins, spectators.

## 240 Hz on the Switch: not feasible as is, but the goal (2026-10-17, from the console's logs)
**The user's preference:** find optimizations so the Switch becomes a first-rate TF.EX citizen running TF.EX's own 240 Hz, so the
browser needs no Switch mode and desktop TF.EX can play Switch lobbies. 60 Hz is today's fallback, not the end state. The findings:
TF.EX runs its session at 240 Hz; the Switch runs 60 Hz, which is why the browser has a Switch mode and desktop TF.EX can't join a
Switch lobby. The console's own numbers (the 2026-10-17 match-hw-logs, `[hw] timing` and `[spike]` lines; Game.Update only, Draw not
measured): a 60 Hz frame's update is p50 ~3.1 ms (the game's tick ~1.2 ms, measured 2026-10-15 without rollback; rollback's end-of-tick
bookkeeping ~1.3 ms over 3500-4300 hot pages; GGRS and the rest), p99 ~10 ms on frames with a rollback. At 240 Hz a frame holds 4 ticks,
each with the same work (TimeMult 0.25 shortens movement, not the entity updates): ~12 ms before Draw in a 16.7 ms frame, and the same
network delay means 4x as many ticks to resimulate, so a rollback frame would take ~30-40 ms. Estimate, not a run: Draw isn't measured,
and the per-tick costs at 240 Hz are assumed equal to 60 Hz ones. So the Switch stays at 60 Hz and the browser's Switch mode stays.
A way for desktop TF.EX to play the Switch would be upstream: TF.EX choosing a 60 Hz session when a lobby has a Switch player (a PR like
the local-multiplayer one): the user's call, after priority 1.

**The path to 240 Hz (the user: lower priority than working variants, so after them; theories until measured):**
1. Measure first, on the console: Draw's time per frame, and a test option that runs the match at 240 Hz (4 ticks a frame) with the
   per-tick costs logged separately (game tick, rollback bookkeeping, GGRS), plus the cost of a resimulated tick.
2. Cut rollback's per-tick bookkeeping (~1.3 ms today, x4 at 240 Hz): e.g. less work per hot page, or saving less per tick.
3. Cut the game tick itself (~1.2 ms): profile the hottest update paths; work off the game thread where it stays deterministic
   (compares already use 2 worker cores).
4. Cheaper resimulation, so a rollback of 4x the ticks fits: resimulate without the parts that don't affect state (sound, rumble,
   graphics records are already handled specially).
Target: a 240 Hz frame (4 ticks + Draw) well under 16.7 ms, and rollback frames that don't stutter at normal pings.

## Decisions (the user, 2026-10-17 evening)
- **Settings:** TF.EX has user settings (its NetplayPreferences / options: name, server, input delay, instant replays, ...). The
  Switch has no settings screen yet, but it keeps the same values in a saved store shaped like TF.EX's, read by TF.EX's generated
  code through the services, so a settings screen can be built on it later without changing anything else.
- **Replays: not supported yet, look into it later.** Until then the Switch's replays setting is Off, so TF.EX turns instant replay
  off for the lobby (TF.EX's own rule: replays need every player's option on) and nobody waits. Later: the real replay (Integration B's
  analysis: the vanilla ReplayRecorder in lobby matches, played with GGRS paused, as TF.EX's spectator path; or TF.EX's
  InstantReplayFootage).
