# Status

Kept short on purpose (docs/STRATEGY-2026-10-28.md section 4): one "now" paragraph, the open bugs, what waits on a console run and
what isn't pushed. Detail goes in commit messages and the doc a piece of work belongs in. The long status this replaced, with every
dated entry from 2026-10-15 to 2026-10-29, is `git show dfb1beb7:STATUS.md`.

## Now (2026-10-31)
**2026-10-31: TF.EX v0.28.1** (versions only in its C#; its ggrs-ffi v3.0.1's fix is the PC's own WebRTC): the Switch translated at
v0.28.1 and the browser pinned to it (browser branch claude/tfex-0.28.1, worktree tfex-0281, not pushed); smoke lobby in sync, both
connecting with ?version=0.28.1 (the official server only lets the latest version play).

**Mod game modes online (research, 2026-10-31):** docs/PLAN-MOD-MODES-ONLINE-2026-10-31.md. TF.EX already carries modded variants
online (TF.State's RegisterVariantStateEvents) and a lobby mod list, not mod game modes. The end goal is TowerBall online (FortRise's TowerBall mod);
steps MoveOrDie (one piece of state), Respawn (mid-round respawns), then TowerBall. Every player: the same mod at the same version. The 8-player UI (WiderSet's select / results screens): later, low.

The Switch runs TF.EX v0.28.0, the release itself (pin 5313052; our #283 fork retired: v0.28.0 has TF.EX's own local players) as
generated code: 71.5% of TF.EX's lines, 0 hand ports, replays (20%) left for last (docs/TFEX-BACKLOG.md, generated). Checked in Ryujinx
against the v0.28.0 page (browser worktree tfex-028) on TF.EX's own server (LVB_SERVER=real, our fork's branch switch-online-028):
lobby in sync, localtwo (two local players on the Switch, START on the netplay menu, + a page) in sync, also on our own server
(the browser repo's tfex-server now seats TF.EX's guests), rematch PASS (072659): the level after a match runs FNA's fixed time step
(xplay.cpp FixedStepUpdates: TF.EX's 240 Hz step ran at one update per present, so the awards outlasted the 30 s vote window). On master since: FortRise's MODS / OPTIONS screens, TF.EX's
module boot and tutorial, the roll-call UI in place, the Switch-only variants hidden online, SPECTATE refused, the wide layout back
after ARCHER SELECT. Next is the strategy's step list (docs/STRATEGY-2026-10-28.md section 2; leaving a match, the menu screens and the
TF.State remainder are done): TF.EX's tests as host checks; spectators and replays last (the user, 2026-10-29). Online matches now run in TF.EX's Netplay mode 11 as on the PC (FortRise's mode registry and its MonoMod patches
on GetRoundLogic / MatchSettings, generated; every Mode reader: docs/TEAM-MODES-RE.md section 7). Later: mods that add game modes over
TF.EX (needs FortRise-interface dispatch in the translator, the same mode numbers on every machine, our own mod loading).

**2026-10-30: the megamod is in** (docs/PLAN-MEGAMOD-2026-10-30.md, step 2; was ../combined-mod): TowerBall, 8 players and three
teams for local play, in `hw-probe/exlaunch/source/program/megamod/`, every hook calling through while TF.EX's online mode is on.
Emulator: 8-player LAST MAN STANDING, three-team TDM and 4v4 TowerBall on the 2-8 roll call, each 30 s (tools/megamod/localrun.sh 8
3|5|6); smoke lobby in sync with it in (141505). The SSB roster is FortRise archer mods (step 1). Next: hair and custom SFX for FortRise
archers on the Switch (step 3).

**2026-10-29: every match runs at TF.EX's 240 Hz** (branch claude/tfex-240-only here and in the browser repo): the browser's 60 Hz rule
for lobbies with a Switch or only browsers, the Switch's 60 HZ MODE and the hw.txt hz / page ?hz overrides are gone; smoke compares
TF.State's saved bytes (the page writes no tick log at 240). smoke lobby PASS 175202 against harness-240 (39 of 40 dumped frames
identical, frame 0 as always). Browser PR #92 (240 Hz) is on browser main; the console's v0.28.0 build needs PR #93's page
(:18300 serves it from worktree tfex-028; the live site once #93 is merged).

**TF.EX pin: upstream only (the user, 2026-11-06).** TF.NX's main pins an upstream TF.EX release, never our fork: since 2026-11-06
**v0.30.0** (3450744, 2026-11-14: #319 TF.State's HeavyPhysicsObject (ghost halos) and ice fixes; #320 a spectator can host a private
lobby (LB / left trigger on the lobby screen: the Switch's MenuAlt2); #321 revivers ticked in a fixed order (LevelReviverOrderPatch);
report: tools/tfex/reports/v0.29.1-v0.30.0-summary.md). Before: v0.29.1 (bd7480f, 2026-11-07). Mod game modes
stay local until TF.EX has them online (Fcornaire/TF.EX#314). Our fork's branches and their state: docs/TFEX-FORKS.md. Moving off the fork took out its users: TF.State's
RegisterStateEvents rules, PlayerCorpsePatch, our NetplayModeTest test mod and the lobby's custom-mode preset; vendor/moveordie is back on
ebe1kenobi's release (b497071). The GAME MODE scroll our fork fixed doesn't happen on the PC with released mods (TF.EX loads after them);
the Switch's boot order now follows FortRise's over every loaded mod (FrModsBoot: TF.EX waits for TF.State), so local VERSUS reaches
Soccer's mode again (check.sh Soccer PASS 172411; smoke lobby PASS 172313 in sync, the Switch's Netplay mode now 14 with three mod modes
loaded, the browser's 11, as on a modded PC). The browser runs the official release (v0.30.0 on its branch claude/tfex-0.30.0, the harness worktree tfex-0300; its cloudflare/tfex-server
doesn't do spectator-hosted lobbies yet, belacmu/towerfall-browser#102).

- **switch-online has no GitHub repository yet** (2026-11-02, the user: to fix soon): the checkout has no remote, so master is only on
  this Mac. Checked for publishing: tools/re/main.flat (the game's code) is git-ignored and was never committed; no file over 20 MB in
  the history. A push sends what master reaches: ~102 MB of file history, of it gen/ 15 MB (keep it: a checkout builds from gen/, the
  translator needs the PC game files that aren't in the repo) and tfx-rtc/target-soft 54 MB (Rust build output, untracked since
  7e01030b; only a history rewrite would drop it). The local .git is bigger (~635 MB pack) from unreachable objects, among them TF.EX's
  history fetched into the outer repo when a worktree's vendor/tfex was uninitialized (2026-11-02: a stray branch, deleted).
  tools/re/rawdump.tgz (memory dumps for statepins.py) to look at before going public.

## Fix queue (the user, 2026-11-07; the slowdown first)
- **Slowdown in 8-player local Teams+ matches with 7 AIs (critical).** The console's numbers (2026-11-10): the first 10 s of each
  round 43-53 fps, the game at 74-90% speed; the AIs' decisions 4-11 ms a frame (1 ms later in the round), the team outlines 2-3 ms;
  the most sampled stacks in AINova's WorldSnapshot.Capture (its Hazard). Every Nova AI read the same screen grid every frame: now
  computed once a frame and shared (glue NovaGrid, [[rewrite]]s in WorldSnapshot; emulator: 5 of 6 cells reused, the AIs play as
  before). Console proof: `[nova] the AIs' world grid shared: ...` and the [perf] window 1's AIs' ms. Still to see: the outlines' 2-3 ms
  and Nova's own planning (Rollout) if window 1 stays under 60 fps.
  Earlier notes: Local matches had no timing in the log (the
  timing split counts only while player 1 is alive). Since 2026-11-07 a local match logs `[perf] local match, 8 players, window N`
  every 600 drawn frames (fps, the game's speed against real time, ms by part: Level.Update, the AIs' decisions, team outlines, draw)
  and `[perf]   N% of M samples in updates:` (the main thread's most sampled stacks; symbolize with the build's ELF). Emulator, 1
  player + 7 AIs, Teams+: 60 fps, the game at 100% speed, no slowdown; the first 10 s of a round 15 ms a frame in Level.Update
  (the AIs 0.3 ms), most samples in LightingLayer.RenderLightCanvas (the game's own, as on the PC; a GPU wait there), 3.4 ms after.
  Waiting for the console's numbers. The Switch's team outlines (12 ms of a frame in the emulator, a GPU wait too) are off in Teams+.
- Teams+ colours: 7 AIs Solo, the player on yellow: every archer had a blue outline (Solo and yellow), and the player's mod archer
  (Kirby) was coloured yellow. The outlines (2026-11-07): the Switch's own styling, the user's: a player on a team has an outline of
  its team's colour, a lone wolf none. The game tagged every non-red player into the blue canvas; now blue and red are the game's,
  yellow is drawn into a third canvas the same way (tfex/TeamPlusOutlines.cpp, the old three-team mod's yellow ring), lone wolves have
  none, all once per drawn frame. Emulator: P2 blue outline, P4 yellow, P1 / P3 none. Console proof: `[teamplus] outline: the yellow
  team's outlines drawn`.
  Sprites (2026-11-07, the user's): Teams+'s gold (yellow) / white (wolves) recolours are off on the Switch (overrides.toml
  VisualHooks.Load; Recolor / VisualHooks not translated): every archer keeps its own colours, as in the Switch's team modes. That
  also ends Kirby going gold (its blue and red sheets differ over 94% of the body, so the gold mask was the whole archer). Console
  proof: `[mods] TeamsPlus constructed: 6 of 6 patch classes hooked` and no `recoloured` lines.
- The right stick changes an AI's Teams+ team: done 2026-11-07 (team screen: up / down picks an AI, left / right moves it a column; emulator: P2 to BLUE, P4 to YELLOW, the level's colours matched). Console proof: `[aislot] team screen: P1's right stick moves slot N's AI right`.
- The AI cursor's label ("R NEXT: JIMMY") under the portrait instead of above it: done 2026-11-07 (emulator screenshots, both roll calls).
- The between-rounds score board with AIs (the user, 2026-11-07): every AI name ("NOVA 2".."NOVA 8") was off the screen's left edge:
  the glue AiNames (Archer's) set each name's X to 20, its own layout, while the game right-aligns the labels by the coins. Fixed
  2026-11-07: each name keeps the game's place, a row's labels laid out again right to left 9 px apart, the crown moved past them
  (half size when a team row is too wide). Emulator: 8 players, all names by their coins. Console proof: `[aislot] round results: N AI
  name(s) of M labels, rows laid out in the game's place`.
- Changing one AI's archer changed other AIs' archers (4 players): the game moves a slot that isn't joined off an archer someone joins
  on (RollcallElement.EnforceCharacterLock), so those AIs had never joined. Fixed 2026-11-07 (an AI confirms again until joined);
  console proof: no `[aislot] ... not joined after its confirm` past a few, no ERROR.

## Open bugs
Switch:
- **Team-mode tick cost: fixed 2026-11-02 (095c02a8: the Switch game's own TeamOutlinesLayer drawn once per frame, not in every tick), waits for a console run (the check below).** Was: the road to EIGHT players online: the Switch's per-tick cost with 3 players doesn't fit 240 Hz (console
  2026-11-01; not worked on yet, deliberately). Two local players + a PC felt laggy. Logs (`[np] TF.EX's TFGame.Update over the last
  600 calls`): about 2.45 ms per tick with 2 players, 3.6-3.7 ms with 3 (12:53 match 2, build 1ab8027c; 13:59, build beaccedd: the
  same, so no regression). Four ticks per 60 Hz frame: ~10 ms with 2 fits the 16.7 ms frame, ~15 ms with 3 doesn't (12:53's timing
  summary: frame period p50 21 ms, p99 114 ms). TF.State's per-tick save is ~0.7 ms of it (`[rb] save cost`). Not the state dumps:
  14:44, build 05b140dd, no `statedump`, 3 players: 3.6-3.8 ms per tick, frame period p50 29.7 ms. The lag-free 3-player matches
  the user remembers (2026-10-25, build Oct 25 09:09) ran at 60 Hz: one tick per frame, 4.1 ms, 11.6 ms idle, frame p50 16.65 ms.
  Since matches run at TF.EX's 240 Hz (2026-10-28/29) a frame is four ticks and a tick is barely cheaper (3.7 vs 4.1 ms): ~4x the
  work per frame. Eight players at 240 Hz needs the per-tick cost (game update, TF.State save, TF.EX's patches) profiled and cut,
  by several times.
  By players (console, every 240 Hz match on record): 2 players 2.44-2.59 ms (3 matches), 3 players 3.60-3.70 ms (3 matches,
  each with a second Switch player): about +1.15 ms per player; untested beyond 3 or with a third remote player. The emulator can't
  measure it: per own tick (resimulations included) 4.3-4.4 ms for 2, 3 (remote or local) and 4 players alike (2026-11-01, slot B,
  vsstop 9600 each), its own overhead dominates. `[np] per tick over the last N own ticks` (every 2400, with the players count,
  e43a1ba4) is the line to read in a console match with 4+ players.
  18:29-18:33 (build 17:50, Switch seat 1 vs the localhost browser, wide towers, extra archers all local Switch players): per
  `TFGame.Update over the last 600 calls` (~0.96 ticks a call) 2 players 2.60 ms, 3 players 2.66 ms, 4 players 3.04 ms (3.15-3.23
  once the round got busy); level ticks per drawn frame p50 3.19 / 3.33 / 3.63-4.29 ms, idle p50 5.5 / 6.0 / 5.0-3.3 ms, frame period
  p50 17.4 / 18.5 / 19.0-19.7 ms. All three at 240 Hz (`the session's rate: 240 Hz`, TimeMult 0.25; match 3 ran ~5300 ticks in
  under 24 s of upload timestamps, so real time). So it grows with 4 but far less than the 14:44 3-player match (3.6-3.8 ms), which
  differed in: team mode (mode 5, FortRise TeamMode, NO FRIENDLY FIRE + TEAM REVIVE on) vs deathmatch (mode 3, 2 variants); the
  official server vs ours; Windows vs the browser; non-wide random tower vs wide; Switch at seat 0 (host) vs 1; 6.3 vs ~3 ticks per
  drawn frame (catching up vs waiting). Per-tick cost is the Switch's own simulation, so the server and the opponent's client should
  not change it; the mode and variants (team logic, TF.EX's TeamReviver patches) are the suspects.
  20:00 (build 18:47, 4 players: 3 Switch + the browser, our server, team mode 5 with TEAM REVIVE, wide tower 0; log
  20261101-200122-match): very laggy, and **not the revive fix**: the worst lag came before anyone died (frames of 200-590 ms, up
  to 87 ticks catching up per frame, lvTick 1100-3100; per call 4.0-4.8 ms, `per tick` 4.447 ms), the first PlayerCorpse /
  TeamReviver came after it, and from then on per call 3.3-3.8 ms with 20-48 ms spikes. So team mode costs ~+1.2 ms a tick over
  deathmatch at 4 players (4.1-4.4 vs 3.04) before any reviver exists. Also desynced from the first checksum (frame 480, every 480
  after, 10+), also before any death.
  20:05 (same session, team mode, TEAM REVIVE **off**, 4 players vs the browser; log 20261101-200638-live): **no desync**, so the
  20:00 desync comes with TEAM REVIVE on while the browser runs official v0.28.1 without #312 (a version mismatch, not team mode).
  Smooth (no frame over 25 ms from lvTick 1162 to 6506), then **one drawn frame of 23.7 s**: `[spike] lvTick 6506 ... frame period
  23691.0 ms, 77 ticks 23677.9 ms (rollback save 4981.0 ...)`. The screen froze while the game ran on (the user: controls fine).
  77 TFGame.Update calls of ~300 ms each, ~65 ms of TF.State save per call. (Read at first as "everything ~50x slower": wrong, see
  20:28.) Not a hang (hangwatch never fired), not the GC, no log upload then.
  **20:28 (build 63e233f6 with the [slow] sampler; team mode, TEAM REVIVE off, 4 players vs the browser; log
  20261101-202915-match): the freeze is 240 Hz's catch-up spiral, not slow ticks.** A tick costs what it always does (per tick
  4.76 / 4.26 ms; 600 calls at 6.12 own advances per call, 26.3 ms per call = 4.3 ms a tick; the main thread ran 85% of every slow
  update's wall time: busy, not waiting). But 4 players in team mode cost more than 240 Hz's 4.17 ms budget, so TF.EX's accumulator
  (TFGame.cs: `while (Accumulator > fpsDelta)`, no cap, the PC's too) owes more ticks each update, each update runs longer (102 ms
  growing to 782 ms, 30 slow updates), nothing is drawn (`[spike] frame period 11718.3 ms, 49 ticks`), until the per-tick cost
  drops (archers die) and it catches up. The "ticks" in a [spike] line are TFGame.Update calls, each running many ticks.
  Profile (97 samples in the slow updates, by our functions on the stack): Level.Update 53, TF.EX's TFGameUpdate_Patch 46,
  **Player.DoWrapRender 34 and LevelEntity.Render 33** (drawing code inside the update: Level.Update +0x3fc calls `[Level+0xc8]`'s
  method at +0xb68c40, whose virtual call reaches +0xaf07b0, which calls LevelEntity.Render -> Player.DoWrapRender ->
  GraphicsComponent.DrawOutline...), TF.State capture 20, Player.Update 10. Lead: what in a team match's update draws the players
  (team outlines?); ~35% of the update time is ~1.5 ms a tick, about the team-mode overhead over deathmatch (4.3-4.8 vs 3.04 ms).
  **Found (2026-11-02): the Switch game's own `TeamOutlinesLayer`, which the PC doesn't have** (no TeamOutlines / DrawTeamOutline in
  the PC's IL, vanilla or FortRise-patched). The Switch's Level ctor, +0xa7b0e8: `if (Session.MatchSettings.TeamMode)` (get_TeamMode
  +0xaba740) `{ Level+0xc8 = new TeamOutlinesLayer() (+0xb68a30); SetLayer(-1, it); }`; the PC's ctor has no layer -1. The Switch's
  Level.Update ends, after ReplayRecorder.RecordUpdate (Level+0x70, +0xb2e390, as on the PC), with an extra
  `if (Level+0xc8 != null) +0xb68c40(it)`: the layer draws every team's players into its two render targets (fields +0x70 and +0x78:
  the RedOutline / BlueOutline names in the name pool) through +0xb68c80 per target: render-target switch, then every entity's
  virtual render (-> LevelEntity.Render -> Player.DoWrapRender -> GraphicsComponent.DrawOutline). Drawing work, done in the update:
  at the Switch's own 60 Hz once a frame, at TF.EX's 240 Hz four times a frame plus once per resimulated tick. That is the
  team-mode overhead (~35% of the slow updates' samples, ~1.5 ms a tick).
  **Fixed (2026-11-02, xplay.cpp TeamOutlinesHook, TFEX-DIFF S-native):** in a session +0xb68c40 is skipped inside Level.Update and
  called once per drawn frame, just before Game.Draw. Emulator: smoke team PASS (in sync, saved bytes identical), `[np] the team
  outlines ... drawn once per drawn frame`; smoke team `vsstop 4800`: `team outlines: 3253 of Level.Update's calls skipped` = every
  update (2400 own + 853 resimulated), in sync on 77 frames; visible run (XPLAY_VISIBLE=1, behind other windows): the drawn frame
  shows the blue and red outlines round the two archers, drawn only by the new path. The emulator draws only ~2 frames of a 240 Hz
  match (it can't keep 240 Hz either: the same catch-up, 00,00 FPS), so a screenshot of live play isn't possible there. Waits for a
  console run: a 4-player team match, `[np] team outlines: N of Level.Update's
  calls skipped, M drawn before Draw` (every 2400 ticks), per tick near deathmatch's 3.04 ms (was 4.3-4.8), no `[slow]` lines, and the
  team outlines still showing round the archers.
  Checking a fix without the console: the emulator's times can't show it (its overhead hides per-player and per-mode costs, above), so
  count work instead of timing it: the same code runs on both, so a count per tick (e.g. LevelEntity.Render / Player.DoWrapRender
  calls inside TFGame.Update, entities updated) is the console's exactly. A 4-player team smoke run (the `team` scenario with
  `localtwo`'s local players, or `three` + a page) logging those counts shows the drawing gone; the console then confirms the time
  (per tick under 4.17 ms, no `[slow]` updates).
  Earlier lines to read (hangwatch.cpp, still on):
  (hangwatch.cpp): `[slow] TFGame.Update #N over 100 ms: W ms, the main thread ran C ms of it` (C close to W: the work itself is
  slow; C far below W: the thread waited or was preempted) and `[slow] sample N ...: pc, lr, frames: game+0x...` (the main thread's
  stack every 100 ms while an update runs long; symbolize with the build's ELF). The `[np]` per-call and per-tick lines no longer go
  negative across a match start, so the per-tick line prints in every match. The per-tick line printed only in match 1: the own-tick
  delta goes negative across a match start (`-8.85 own advances per call`) and its static total never gets back to 2400.
- **Player names behind the portraits on the match results screen** (console 2026-11-01): fixed on branch
  claude/hardware-test-version-crash-5d0b03, not yet seen on the console. The Switch's VersusPlayerMatchResults.Render draws its
  portrait again after the components, over TF.EX's name (an OutlineText component); EndOfMatch.cpp draws the names once more
  after it (S-hud).
- **TF.EX's test mode can't start a team match: fixed** (2026-11-02, branch claude/tdm-testmode-levels). tools/testmode.sh `TDM 0 1 42 2`
  aborted 15 s in: VersusLevelSystem.GetNextRoundLevel (+0xbae8f0, the frame after GenLevels) Calc.LoadXML threw (FileStream) on
  `Content/Levels/Versus/01 - Twilight Spire/tb.oel`: the megamod's TowerBall level is in every tower's VersusTowerData.Levels (the romfs
  lists it first), TF.EX's test mode puts Levels[startLevel] first (StartOnLevel), and the PC romfs has no tb.oel. Not team-specific: any
  start level that indexes the Switch's list is off by one. Fix (pcgameplay.cpp PcTowerLevels): while online the towers' level lists hold the
  PC's NN.oel only, as on the PC; restored offline. Proof: testmode-175647 PASS, 0 mismatches, rounds on 00.oel then 04.oel. Log lines:
  `[pc] tower level lists as the PC's: 16 non-PC entries ... taken out` and `[pc] round level #N: <path> -> <file>` (first 8, every 50th).
  The console's three menu crashes of 2026-11-01 are NOT this: they are CPU faults caught by net::crashlog::Handler ("User Break" through
  nn's UserExceptionHandler; every abort ends at nnSdk+0x37eba8). 18:26 and 19:57 (build b8f67fc7, ELF 5038710605e6...-39a4d5c0): in
  MainMenu.Update's Orig (MainMenuPatch.cpp:220) -> +0x93d400, a loop over a static list calling +0xa61a10 per item, in TF.EX's screens 65 /
  62, right after `[input] controllers changed ... PlayerInputs rebuilt` in the 19:57 log. 20:25 / 20:27 (build 532bba58, -216af912): in a
  match, under HUD.Update (tfexgen_hooks.cpp:3616) -> +0xa585e4 -> +0x92f464 -> +0x9039a0. Both open.
- **Desync from about frame 3600 on the console** (2026-10-24, at 60 Hz): not reproduced in the emulator at 240 Hz (smoke lobby
  `vsstop 14400`, 60 s: saved bytes identical on every dumped frame, run 185905, 2026-10-29). Waits for a long console match at 240 Hz.
- **Input delay can't be changed on a single Joy-Con** (TF.EX's MenuAlt2 + Left/Right gesture). By the game's code it should work
  as SL held + the stick: its npad reader maps a sideways Joy-Con's SL to LeftShoulder (+0x30e44 / +0x30fc0), which MenuAlt2 reads.
  Waits for a console try (checklist below).
- **A second pair of controllers takes a player seat** and the user's own controller stops working in a lobby (2026-10-18, before
  #283's local players, 2026-10-24): waits for a console check with two pairs (checklist).
- **GC pauses ~15-30 ms each** on the console (Boehm marks the whole live heap; no parallel marker in the game's build). 2026-10-29: 2/3 of
  what fed the collections at 240 Hz was GameState.MatchStats' value list, 128 inline 264-byte entries (33 KB) made twice a save; now 8
  (translate.toml [capacity]). smoke lobby: GC_malloc 60 -> 22.5 MB and 5 -> 2 collections per 600 saves (runs 230736, 231816).
  2026-11-02, what this Boehm build is (tools/re): stop-the-world is cooperative (+0x2861490: a flag, then spin / yield / sleep until every
  managed thread acknowledges); incremental code is compiled in but there are no dirty bits for it on Horizon (no mprotect, no write barrier
  in the BRUTE code), so no incremental or generational mode. Objects of pointer-free types and arrays are already GC_malloc_atomic (NewObject
  +0x610: type flag bit 3). The `[gc]` line now times each collection whole and by phase: in the emulator the mark is ~85-90%, stopping and
  starting the world ~0, finalize + reclaim ~10%, so the pause is the live heap's mark (console 10.3 MB with pointers, emulator 6 MB). **Rarer
  (ON, hw.txt `gcmin`, default 40 MB):** Boehm collected every ~21.5 MB allocated on the console (13 MB in the emulator; min_bytes_allocd from
  the live heap / gcdivisor 1), about every 1.7 s at 240 Hz; the heap is already ~60 MB with ~14 MB live, so a 40 MB floor on the threshold
  fits in it (a trampoline on GC_should_collect +0x2856fa0 raising its cached threshold +0x48e25a0). smoke lobby: 2 -> ~0.6 collections per
  600 saves, heap still 59.8 MB (run 172324). Console check: `[gc] a collection waits for at least 40 MB allocated`, then `[gc] over the last
  600 saves` with 0-1 collections, `a collection every 40.0 MB allocated`, the heap near 60 MB, the `[spike]` lines with rollback-save
  pauses about a third as often. **Shorter (open):** the census (`[gc] census`, on by default at a session's 1200th and 4800th save;
  `gccensus 0` off) lists the live objects by type. Emulator (run 171710): normal 5.1 MB + uncollectable 0.9 MB; ~2.1 MB without a class
  descriptor (runtime data: statics, tables, our uncollectable blocks), ~2.1 MB of XML DOM (XmlText 10667, XmlElement 6146, XmlAttribute
  7225, XmlAttributeCollection, ArrayList: the game's own SpriteData etc. keep the XmlElements), SpriteBatchItem 4608 (576 KB, FNA's
  batch). The console's census says what its extra 4 MB is; the next step is shrinking the biggest owner it names.
- Player counts hard-coded to 4 / 6 in several places (BACKLOG item 4): not a bug. TF.EX's lobby is 2-4 players, wide included
  (LobbyVersusPlayerCountButton.MAX_PLAYERS 4), and 6 is the game's own seat count; future work for a mod with more players.
- ~~The console's DNS lookups fail~~ fixed 2026-11-01 (ac0e00e2): nn::socket::AddrInfo has ai_addr before ai_canonname; the
  official server's name resolved on the console, no `ip` fallback needed.
- **Coloured Joy-Cons on the netplay menu** (the user, 2026-11-02): fixed on branch claude/hardware-test-version-crash-5d0b03, not
  yet seen on the console. Archer select already drew each player's Joy-Con in its real colours (JoyConInput.DrawIcon); TF.EX's
  LocalPlayersPanel drew only the plain picture. Glue Rewrites.DrawControllerIcon adds the two colour layers as DrawIcon does.
- **Browsers can't use TF.EX's official server**: its nginx answers 403 to any WebSocket with an Origin header (every browser);
  the server program itself has no check. Only its operator can allow our site's origin (our fork's switch-online-028 has an
  allowed-origins option that could go upstream). Until then console-vs-browser tests use our worker (SERVER: LOCAL).
Browser:
- **Crash after selecting SUPPORT** (v0.27's NETPLAY-menu button): `window is not defined` from dotnet.native.worker.mjs (SDL_OpenURL
  runs window.open on the game's worker). FIXED, merged 2026-11-02 (belacmu/towerfall-browser#96, d158f15; the user checked it
  on their iPhone): the page opens it (tfurl_open, as tfclip_write); when the browser refuses (no user gesture: a pad press, or
  the gesture lost on the way through the game's worker), TF.EX shows the address. Then the page also shows an "Open
  patreon.com/…" link a tap opens (belacmu/towerfall-browser#97, 3ac8a1d, merged 2026-11-02; the user checked it on their iPhone).
- Wide-game rematch crash: fixed in the TF.EX fork, not pushed (below).
Upstream TF.EX:
- **TEAM REVIVE: the fix works, Windows vs Switch (2026-11-01 14:4x, the user; console log 20261101-144437: a TDM 2 v 1 with
  TEAM REVIVE, two TeamReviver constructors, a revive finished (FinishReviving), no desync).** Earlier setup: vendor/tfex = fork `team-revive-modmodes`
  fb377d5 (mod-modes-online 8d3a18d + the fix); the PC package `.devserve/pc/TF.EX-team-revive-pc.zip` (the same commit, four mods).
  The match on 2026-11-01 12:5x (20261101-125350) had the fix on neither side (master's unit had no source file at the pin). Console
  log: `[tfexgen] hook PlayerCorpse.Added call N: PlayerCorpsePatch.PlayerCorpse_Added_Postfix=1 PlayerCorpsePatch...=1`, then
  `[tfexgen] hook TeamReviver...` lines when a reviver is made; no desync. Upstream (2026-11-01): issue Fcornaire/TF.EX#311, PR Fcornaire/TF.EX#312
  (branch team-revive-netplay d2a61bf on upstream main). When it lands, vendor/tfex goes back to mod-modes-online on top of it.
- **TEAM REVIVE does nothing online** (Mode 11 never makes a TeamReviver): docs/TFEX-UPSTREAM-CANDIDATES.md candidate 3. Not
  reported yet; the other two candidates there aren't either. Our fix: TF.EX branch `team-revive-netplay` (d39831d on upstream main;
  3d8c0aa = the same on fbeec67, branch team-revive-v0.27.0), in ~/Code/TF.EX-team-revive, not pushed. Checked 2026-10-29:
  smoke team with TEAM REVIVE vs the browser worktree harness-revive (built from 3d8c0aa), the Switch translated from 3d8c0aa
  (branch claude/team-revive-test): PASS 173658, a reviver on both sides in two rounds, 0 differing ticks. Not covered: an actual
  revive (1 vs 1 has no teammate to do it), so the fix's cut-short path hasn't run.
  Still so in TF.EX v0.28.0 (2026-10-30, run 081455: TEAM REVIVE on, a death, 0 TeamReviver constructors; 173658 with the fix: 8).
  Upstream's TEAM REVIVE work (#211, 60ac432, 2026-08-17: the reviver tracked by TF.State, 3-player TDM) didn't change where the
  game makes revivers: PlayerCorpse.Added's `Mode == TeamDeathmatch` (the PC's IL), and online the Mode is TF.EX's Netplay mode.

## Waiting for a console run
Each line is the log line that proves it (published builds only; boot the build in Ryujinx first, AGENTS.md):
- TF.EX v0.30.0 (2026-11-14, TF.NX 0.30.0.0). Boot line `TF.NX 0.30.0.0, TF.EX 0.30.0`; an online match on TF.EX's official server
  (only the latest version may play there). Three things the emulator run didn't cover: a team match with a revive (#321's order:
  `[tfexgen] hook Scene.Update call 1: LevelReviverOrderPatch.Scene_Update=1`, in sync through the revive); a ghost halo in a rollback
  match (TF.State's new HeavyPhysicsObject state reads `image` and `spin` at predicted offsets, unconfirmed: a desync or crash after a
  ghost's halo drops points at them, tools/re/statepins.py); a spectator hosting a private lobby with LB / ZL on the lobby screen
  (`[pause] ... Alt2 32` in menu-button lines; against TF.EX's official server, ours lacks it until deployed).
- Teams+ teams start together on wide levels (2026-11-10, the console: teams spread over the map; its log: Moonstone `7 of 13 levels
  (PlayerSpawns >= 8)`, `no left-side markers ... Blue falls back to the anchor pool` on 4 of 5 rounds). Our wide-local GetLevels
  answered before Teams+'s GetLevels prefix and kept only FFA levels (no TeamSpawnA/B), so the spawner never had team sides. Now a
  shell around the generated hook (pcgameplay.cpp GetLevelsShell): the tower's Levels swapped for WiderSet's level data, Teams+'s
  rule (`PlayerSpawns + 2 TeamSpawns >= players`: every level that seats everyone) and the game's run as on the PC. Teams+ f539a78
  (the user's: "use the defined spawn points per level, but reassign which team/player it belongs to") places every archer itself:
  a level's PlayerSpawn and team markers are one set of points, Blue fills from the leftmost, Red from the rightmost, Yellow takes the
  free group close together and away from them (the middle when there is one), lone wolves the points farthest from everyone; no
  random shuffle (online-ready). Only 4-a-side levels with 3/3/2 split Yellow (no other fit). Proof: `[pc] GetLevels (wide, local
  play): 09 - Moonstone, 13 of 13 levels, a mod's rule (Teams+)`, `[tfex] spawns: N point(s) for T team(s) and W lone wolf/wolves`
  (no "sharing a point" with 8 players), and teams together on screen.
- The update without the frozen main menu (2026-11-08, the console: the menu froze for the whole download, the dialog appeared at the end
  and vanished): the Switch build's managed Thread runs its work on the caller, so TF.EX's Task.Run ran the download on the game thread.
  The download now runs on the updater's own thread (tfnxupdate.cpp StartDownload), its phase and bytes shown in TF.EX's dialog (with
  its bar), TF.EX's work after it on the game thread (glue Updates.Run / Pump); a check that isn't fresh (NETPLAY after 5 minutes) the
  same way. The dialog stays after the install ("RESTART THE GAME TO FINISH", as TF.EX restarts the PC game there). Emulator:
  UPDATETEST_SLOW=30 tools/update-test.sh PASS (no hang watchdog line, progress.png with the bar), UPDATETEST_FAIL=504 PASS. Proof:
  `[update] the download starts on the updater's thread`, no `[hang] the game froze` during it.
- The update dialog's crash on a failed download (2026-11-08, the console: GitHub answered 504 minutes after 0.29.1.3 was released,
  the download failed in its first frame, TF.EX's HandleAutoUpdate removed a dialog not yet in the scene: a null Scene). The dialog is
  hidden and removed a frame later (glue Updates.RemoveDialog, [[rewrite]]). Emulator: UPDATETEST_FAIL=504 tools/update-test.sh PASS
  (the failure shown, RETRY, the game goes on). Proof: `[update] ERROR: the download failed ...` then
  `[update] the update failed before its dialog was on the screen` and no crash. Not on GitHub yet (the next release).
- Mod archers' heads, bows and corpses with a white box (2026-11-08, the user: Meta Knight's head a white square): the Switch's FNA PNG
  load cleared the colour of transparent pixels in the first quarter of an image only (+0x20a10: the loop's bound in pixels, its
  offset in bytes), so a mod atlas's lower rows drew their hidden colour. Fixed (xplay.cpp; GPU readback in the emulator: row 120's
  transparent pixels were 255,255,255,0). Proof: `[xplay] fixes: PNG load clears every transparent pixel`; Meta Knight / Malicious
  Metaguard / Crimson Co-Star with no box around the head.
- Score board names: full size, a row too wide wraps onto more lines and the rows below move down (no smaller names; the user).
  Emulator: the 5 v 3 board, red row on two lines.
- Turning mods on and off (2026-11-08): MODS > TOGGLE MODS is FortRise's UIModToggler, translated (ENABLE ALL, DISABLE ALL, an ON / OFF
  per folder in sd:/xplay/mods; a mod's dependents go off with it, FortRise's rule). Leaving it writes sd:/xplay/mods/blacklist.txt and,
  when something changed, shows "RESTART THE GAME TO APPLY THE MOD CHANGES" (FortRise restarts by itself on the PC). At start a folder
  in blacklist.txt is listed but not loaded. Emulator: tools/mods/toggle-test.sh PASS (off, restart, on). Proof on the console: `[mods]
  sd:/xplay/mods/blacklist.txt written: N folder(s) off`, then after a restart `[mods]   <mod> ... NOT loaded): turned off (TOGGLE MODS`
  and that mod's things gone (an archer, a mode); turning it on again brings them back after the next restart.
- P7 and P8 on one archer (2026-11-08, the user: every time after L's AI fill): TFGame.CharacterTaken is unrolled over seats 0-5 on the
  Switch (WiderSet widens the PC's loop to 8), so P7's / P8's archers counted as free; seats 6-7 added (eightplayer.hpp
  CharacterTakenHook). Emulator: localrun 2 3 "localaisteps LLLLLL": 8 players, archers 0-7 all different (the emulator's save has
  every archer unlocked, so the collision itself didn't show there). Proof on the console: after L fills the slots, P7 and P8 on
  different archers; `[aislot] slot N's AI: archer A is taken, the next free one first` for any AI that was moved.
- 8-player TEAM DEATHMATCH with AIs (2026-11-08): the game's team screen has P7/P8 on screen (rows 15 px apart, WiderSet's step),
  the right stick moves an AI between BLUE and RED there as on Teams+'s, a wide local TDM draws only levels with enough TeamSpawns
  (Sacred Ground 00 has none: SpawnPlayersTeams threw), and a round results row too wide for half size wraps onto two lines.
  Emulator: localrun 2 5 "localai 6" "localteamsteps rdrdrdrdldl" localroundend PASS (5 v 3). Proof: `[8player] team select: rows 15
  px apart, 2 cursor(s) for P7/P8 added`, `[aislot] team screen: P1's right stick moves slot N's AI right`, `[pc] GetLevels (wide,
  local play): ... (TeamSpawns >= N)`; the wrap itself (narrow screen, 6+ names in a row) is unchecked.
- TF.NX's updater from its GitHub releases (2026-11-07, docs/PLAN-UPDATER-2026-11-07.md): TF.EX's update flow translated (its
  NETPLAY check, StartUpdate, UpdaterDialog) over program/tfnxupdate.cpp (check, HTTPS download, zip unpack, journalled install).
  Emulator: tools/update-test.sh PASS (a test server answering as GitHub). On a console WITHOUT sd:/xplay/devserver.txt (with it the
  updater is off: `[update] dev server set ...: GitHub updates off`): `[update] TF.NX <v>, updates from https://github.com/belacmu/tf-nx/releases`
  and `[update] check: TF.NX <v> runs, the latest release is <v> (up to date)` (a real TLS request to github.com: the first one from the
  console). OPTIONS: `TF.NX <v>` under TF.EX's AUTO UPDATE, state UP TO DATE. A real update needs a release named TF.NX-v<version>.zip
  with xplay/tfnx/meta.json (tools/release.sh makes both).
- The archer-select crash's cause (2026-11-06, found in the emulator with the new throw log): switching a portrait to a mod archer
  with its own menu gem (@Gem..._MENU: CRIMSON CO-STAR after BLIND PRINCE on the console) threw KeyNotFoundException (FortRise's
  translated sprite registry wrote "System.String" as the texture). Fixed in the translator. Proof: an AI or a player scrolls through
  the SSB archers on archer select with no `[throw]` line; a round with one plays. WiderSet's 8-slot look (two rows of four, P1-P8,
  60 x 60 portraits, since 2026-11-10 60 x 90 (TFEX-DIFF S-ui, the user's ask: the rows' inner edges kept, grown toward the screen's
  edges; the join flash cut to the crop, the AI cursor in the gap between the rows); since 2026-11-07 no P labels, each input's icon and name drawn in front of its portrait: `[8player] roll call: input icons drawn in front of the portraits`) and the 8 labels: `[8player] label ... "SUPPORTS UP TO 8 ARCHERS"`, `[8player] roll call
  portraits: 60 x 90 crops (rows at y 60 / 180)`.
- Archer-select crash with the AI controls (console 2026-11-06 21:31, build 290915b9): a managed exception in the game's
  RollcallElement.EnterJoined (an AI slot confirming after a NOVA -> JIMMY swap on slot 6, WIDE, 29 archers), rethrown from its foreach
  cleanup, so the report loses the throw site; three emulator replays of the same steps (localaisteps) didn't crash. Every managed
  throw is now logged first-chance from boot: `[throw] #<n> from main+<offset>: <type>` names it (symbolize: tools/re callmap). Also
  fixed then: two AIs could confirm the same archer (`[aislot] slot N's AI: archer A is taken, the next free one first`), and L / ZL
  fills, then empties, then fills (`the AI leaves slot 8` ... `slot 2`, then `an AI joins slot 2`).
- AI controls on archer select with Joy-Cons (2026-11-06): dead on the console. The game polls a pair's npad id 0 as JoyDual and then
  as JoyLeft / JoyRight in the same frame (console logs 2026-11-05/06), and rightstick.cpp kept the last poll, a style with no right
  stick, so Full() was false. Now a single Joy-Con poll doesn't replace a right stick polled for that id in the last 100 ms. Proof:
  `[aislot] P1 joined: a right stick` on a pair / handheld, then `[aislot] an AI joins slot`; `[input] right stick: a pair (JoyDual)
  poll connected`. If L / ZL alone still adds no AI: the `[aislot]` lines from that session say why.
  Second cause (console log 2026-11-06 21:22: handheld, archer select entered 4 times, no `JoinedUpdate`, no `[aislot]` control line):
  the controls ran only for a player who had confirmed; now for every player there with a controller. Proof:
  `[aislot] P1 on archer select: a right stick`, the cursor's label over an empty slot, `[aislot] an AI joins slot` before confirming.
- Mod archers' sounds after the game's loading (2026-11-05): 974d98ec aborted on the console a moment after the title screens
  (`[frmods] ERROR: the game has 0 character sounds`, then a null read in ArcherLoader's AimCancel fallback: the menu runs while the
  game still loads). LoadSounds now waits for TFGame.Loaded (FortRise's MainMenuLoadWait): `[frmods] character sounds 16 -> N`, no ERROR.
- The channel's deletes said (2026-11-05): the console kept sd:/xplay/mods/ThreeTeamMode (loaded beside TeamsPlus) though the manifest
  deletes it, with no line. Now `[dev] delete sd:/xplay/mods/ThreeTeamMode: done`, or `can't tell what it is (result 0x...)`, or
  `[dev] ERROR: nn::fs::GetEntryType not found`; the next boot's `[mods]` list without ThreeTeamMode. The mod registry is 64 entries
  (was 32, 30 used): `(30 of 64 entries used)`, `mod registry 30/64` in the `[pools]` lines.
- Soccer on a level with cobwebs (2026-11-05): 07963ce1 aborted on the console after the level was picked (the game's Cobwebs.SceneBegin
  threw: Soccer clears the cobwebs' walls). vendor/soccer at our fork's switch-cobwebs (ca2ea4f) removes them with the pitch; localrun
  2 SOCCER on Sacred Ground PASS 224143 (crashed before). On the console: a Soccer match on Sacred Ground plays. Also the channel's
  deletes: all six `failed` on the console's 22:32 boot; now `failed (result 0x..., a folder)` names why.
- TF.NX's mod catalog check (2026-11-04, docs/PLAN-TFNX-2026-11-04.md step 1): with sd:/xplay/mods/TeamsPlus as the PC release,
  `[mods] Teams+ 0.1.1: TF.NX's logic, its folder has the 2 needed files` then `[mods] TeamsPlus constructed` (was `[teamplus] Teams+ constructed` before its generated boot). Emulator: smoke lobby
  PASS 182325 with the folder; a folder at 0.1.2 without Content/menu/teamC2x.png: `... has no Content/menu/teamC2x.png (TF.NX's 0.1.1
  logic needs it): not loaded`, Teams+ not booted. Not run: a full folder at another version (the notice line).
- TowerBall as a catalog mod (2026-11-04, docs/PLAN-TFNX-2026-11-04.md): the channel brings sd:/xplay/mods/Switch.TowerBall (tools/mods/
  folders.list); `[mods] TowerBall 1.0.0: TF.NX's logic, its folder has the 4 needed files`, `[mods] TowerBall: 2 of 2 game textures
  replaced by its own`, `[megamod] TowerBall started (its folder Switch.TowerBall)`; in a TowerBall match `[mods] TowerBall: Content/
  Levels/.../tb.oel from its folder`, the baskets, ball and TOWERBALL banner drawn (never seen in the emulator: hidden runs).
- WiderSet as a catalog mod (2026-11-04, docs/PLAN-TFNX-2026-11-04.md): the channel brings sd:/xplay/mods/WiderSet (3 MB); `[mods]   Teuria.
  WiderSet 0.9.0 (WiderSet, loaded)`, `[pc] WiderSet started (its folder)`, `[8player] the 2-8 lobby offered`; VERSUS shows 2-8 ARCHERS;
  an 8-player local match: `[pc] GetLevels (wide, local play)` and `Wider Set: Content/WideLevels/... from its folder`; a wide online
  lobby as before (its levels from the folder now, no romfs PCWide).
- Teams+ 0.1.2 through the channel (2026-11-04, Teams+ 6b45804: WiderSet no longer its optional dependency, so FortRise's order loads it
  before TF.EX and TEAMS+ is reachable on GAME MODE with WiderSet installed): the channel brings sd:/xplay/mods/TeamsPlus (its release
  zip, mods/TeamsPlus/build_folder.sh) and deletes the old layouts' leftovers (sd:/xplay/mods/WiderSet, the romfs's PCWide, 6P-Versus,
  Versus tb.oel and DarkWorldContent/Atlas): `[mods] Teams+ 0.1.2: TF.NX's logic`, `code mods booted in FortRise's order: TeamsPlus,
  TF.EX, Switch.TowerBall, Teuria.WiderSet`, TEAMS+ on GAME MODE after TOWERBALL; no "a second Teuria.WiderSet" line. Emulator: check.sh
  TeamsPlus PASS, smoke lobby PASS 211434 with Teams+ installed.
- PC levels from the delta (2026-11-05, pclevels.cpp): the channel deletes the romfs's PCVersus; online: `[pclevels] <tower>/<n>.oel: the
  PC level from the Switch's own and its delta`, never `[pclevels] ERROR` (a console whose level files differ from the extract's: CRC);
  a match in sync with a PC or browser player on every tower worth trying. Boot line: `TF.NX 0.29.1.0, TF.EX 0.29.1`.
- Code plugins (2026-11-04, docs/DISTRIBUTION-2026-11-04.md "Code plugins", parked): with no plugin.elf in any mod folder,
  `[plugins] 0 plugin(s) found in N mod folder(s)`; the proof plugin (plugins/HelloPlugin) only if asked.
- Rarer GC pauses (2026-11-02, hw.txt gcmin 40 by default): an online match at 240 Hz; `[gc] a collection waits for at least 40 MB
  allocated`, the `[gc] over the last 600 saves` lines with 0-1 collections (were 1-2) and the heap near 60 MB; the `[gc] census` lines
  (what the 10.3 MB live heap is) for the next step.
- Mod game modes (master beaccedd, 2026-11-01: TF.EX pinned to our fork belacmu/TF.EX 8d3a18d until the PR is merged, then the
  release tag; docs/PLAN-MOD-MODES-ONLINE-2026-10-31.md). Nothing changes for built-in matches: a lobby match on the official server
  to its end, in sync (no "DesyncDetected"). Mod modes themselves need a mod on the SD card (only on request).
- CONFIRMED on the console (the user, 2026-11-02). Two local players from one Joy-Con pair split in two (2026-11-01: right through the roll call, then both archers on one Joy-Con from
  archer select on). TF.EX v0.28.1's InputService.IsSameDevice compares `pad.XGamepad`; the translation tested a missing type, so no
  two objects were ever one device: the guest's Joy-Con became the local controller ("the local controller" flipping 0 <-> 1 in the
  log). Now AsPad / SamePad (tfexgen_services.cpp: one JoyCon = one device). Proof: "the local controller" stays on one index while
  the guest presses buttons, and each archer follows its own Joy-Con in the match.
- TF.EX's toasts in a level centred (2026-11-01: 50 px left): layer 4 draws in the 420-wide canvas in standard mode too (glue
  Ledger.NotificationCenterX, S-hud). Seen, not logged.
- Switch vs TF.EX on a PC, a match to its end (2026-11-01: desync at the PC's match-winning kill, frame 15823. The PC's replay
  (statediff-decoded) shows WasFinalKill / IsEnding / slow motion; the Switch's vs-log shows Level.CanEnd going 0 on that frame:
  the Switch-only ArcherShowdown (red/cyan archer unlock, no PC counterpart) started because the console's save has one locked, so
  its round never ended). tfex/SwitchUnlocks.cpp keeps ShouldStartRed/CyanArcherShowdown false in a TF.EX session, as TF.State's
  SkipInExSession does for the PC's unlock events. Proof: `[unlock] UnlockData.ShouldStart...ArcherShowdown: false in a TF.EX
  session` (when the save has one locked), and both sides on the results screen with no DesyncDetected.
- NETPLAY on TF.EX's official server (2026-11-01 crash-01793527851, build 39a4d5c0: "CHECKING VERSION" never ended, as TF.EX's
  HandleAutoUpdate is absent (S-update); DisableAllControllers had emptied TFGame.PlayerInputs, its 5 s lock ran out, and the game's
  controller refresh (MInput.UpdateJoysticks +0x93d570 -> JoyConInput.UpdateStyle +0xa61a40) read an empty slot). Now IAutoUpdater
  reports up to date (overrides.toml S-update: no updater on the console; the server's MIN_CLIENT_VERSION refuses an old one), and the
  refresh waits for EnableAllControllers, not the timeout (InputLock.cpp). Proof: no "menu input locked" line on pressing NETPLAY,
  the netplay menu opens; a later "input lock ran out (5 s) before EnableAllControllers" line with no crash after it.
- The netplay menu's local players (TF.EX v0.28.0 LocalPlayersPanel): "P1" and the controller's small icon at the left, the SWAP /
  JOIN hints at the right edge; no large controller outline over the left side (2026-10-31: the icon was the 198x128 controls/gamepad,
  and the panel sat on the 420 screen's layer instead of the UI layer). Seen, not logged.
- Teams+ (2026-10-31; sd:/xplay/mods/TeamsPlus = Teams+'s meta.json + Content, as the PC release): `[teamplus] Teams+ constructed
  (sd:/xplay/mods/TeamsPlus): its mode registered, 7 of 7 patch classes hooked`, `[teamplus] native patches (Teams+'s IL edits): 1 of 1
  words, 2 of 2 OnPlayerDeath call sites, SensorBlock's team lookup hooked`, `[towerball-mode] TOWERBALL inserted as FortRise's fourth
  built-in GAME MODE (6 modes)`. VERSUS: GAME MODE scrolls LMS, HH, TDM, TOWERBALL, TEAMS+ (its icon; `[frmodes] GAME MODE button draws
  custom mode 11 as TEAMS+`); TEAMS: four columns SOLO | BLUE | YELLOW | RED; a match: `[tfex] match start: N group(s) -> score slots ...`,
  `three-team match live`, yellow gold / wolves white (`recoloured 3/3`); a round's end: the results board one row per group. Not seen yet
  anywhere: a lone wolf triggering a sensor block (`[teamplus] SensorBlock's team colour ...`). Known gap: no TEAMS+ subtitle under the
  tower name on the round intro (FortRise's VersusStart IL patch; TEAM DEATHMATCH's is the game's own). Online: Netplay is mode 12 with
  Teams+ installed (smoke lobby in sync).
- Controllers changed in the online menu (2026-10-31): split the Joy-Cons into two single Joy-Cons (or attach / detach one) while on
  NETPLAY: `[input] controller refresh in TF.EX's screen 62`, then `[input] controllers changed in TF.EX's screen 62: PlayerInputs
  rebuilt ..., TF.EX's local player and guests rebound`; the panel's P1 icon becomes the single Joy-Con's and P2 JOIN shows the other.
  Before: the game refreshed controllers only on its own screens, so the online menu kept the old icons and missed the new Joy-Cons.
- Madeline online (GameBanana's Celeste mod and the SSB roster are on the card since 2026-10-30, by FTP): `[frmods] Celeste 1.2.0: its archers are the Switch's
  own archer 9`; one Madeline in the roll call; against a PC / page with the mod, the Switch's Madeline shows as Madeline there.
- MEXICAN PUPPET (MexicanPuppet in sd:/xplay/mods/, replacing BeardedPuppet): `[frmods] MexicanPuppet 1.0.0: local play's alt archer 6;
  online its own archer N`. Local: the Prancing Puppet alt is MEXICAN PUPPET, no extra puppet. Online: MEXICAN PUPPET is its own archer
  (`Skin bundle MexicanPuppet/MexicanPuppet sent`; the PC player sees the bearded portraits), the puppet's alt can't be picked.
- A mod archer naming the game's art (MexicanPuppet as an archer, any `<Aimer>aimers/...</Aimer>`): plays without the Player
  constructor crash of 2026-10-30 (the console's crash 01793380167: `new Image(ArcherData.Aimer)` on a null aimer).
- A mod archer's own sounds: `[frmods] character sounds 16 -> 18 (2 mod archers' own ...)` with the roster on the card; Rosalina
  (SSB.GalacticExile) sounds like herself (jump, shoot, death), the others as their SFXFallback archer.
- FortRise hair on a mod archer (an archer mod with `<Hair>` and a colour): `[frmods] hair: FortRise's drawing for a mod archer's hair`
  and the hair drawn in its colour; Madeline's hair (TENACIOUS TRAVELER) as before (red, the game's own). Its corpse keeps that hair:
  `[frmods] hair: FortRise's drawing for a mod archer's corpse`.
- A mod archer's own victory music (a `<VictoryMusic>` naming its Content/Music file): `[frmods] music: <mod>/<id> plays (a mod's
  track)` when it wins a match, heard, then `... ended, then TheArchives`. An OGG adds `[frmods] music: an OGG track decoded`.
- The megamod (local play): `[megamod] installed: TowerBall, 8 players (local play); ... words changed`; the channel brings its romfs (devserve-publish runs
  tools/megamod/romfs.py). VERSUS 2-8 with 7-8 controllers (`[8player] AssignInputs: created N JoyConInput(s) for slots 7/8`), TOWERBALL
  in the mode select (`[towerball-mode] renamed mode 6 -> TOWERBALL`) and a match (`[towerball] mode-6 session live`, `level paths ->
  tb.oel`), a plain two-team TDM (three teams: Teams+ above). Then NETPLAY: `[megamod] online (TF.EX's): 26 words back to the game's` and a lobby match as usual
  (`[pc] GetLevels: ... (the tower's non-PC files left out: the megamod's tb.oel)`); back to the main menu: `local play (the megamod's)`.
- TF.EX's module boot: `[tfex] module boot (TF.EX's TFEXModModule): icon registered, ...`, the four `TF.EX/imgs/tutorial/*.png ...
  loaded`, the tutorial after PRESS START (fresh settings: delete sd:/xplay/TF.EX.settings.json first).
- MODS menu: `[mods] main menu: FortRise's MODS button added`, `[mods] MODS screen: 6 mods listed`; look at the list and SEARCH.
- Roll-call UI: `[menu] roll call UI:` lines; INPUT DELAY advisor at the right, EDIT LOBBY whole, standard and wide.
- Switch-only variants: `[frmods] the Switch-only variants (4) hidden and off in netplay`.
- SPECTATE refused: L / ZL on a listed lobby gives the notification and `[ui] SPECTATE refused`, no crash.
- Wide ARCHER SELECT: `[lobby] back to the lobby screen (MainMenu 5): the wide layout`.
- Mode 11 online: `[vs] mode: MatchSettings.Mode 11, the lobby's N`, `[frmods] custom mode value 11 obtained`; one LMS, HH and TDM match
  each.
- Leaving after the browser quits mid-match (fixed 2026-10-28, smoke leftquit): the pause menu works and leads back to the menu.
- The 240 Hz abort, fixed (branch claude/240hz-batcher-abort, gfxmem.cpp): the game's NVN command buffer had a fixed 256 KiB a frame
  and no out-of-memory callback; now the driver gets extra chunks. Boot: `[gfx] command buffer memory callback set: ... (pool ok) ...
  (ok)`; in a 240 Hz match `[gfx] the frame's command memory ran out ...: chunk N of 16 added` and no abort; the `[pools]` line's
  `gfx command chunks` peak says how many a frame needed. Also clears `smoke team` (60 Hz, it ran out by ~100 bytes): PASS 172256.
- Pull a Joy-Con at the main menu with the mod running (a one-off Ryujinx crash on the detach path, 2026-10-17).
- Input delay on a single sideways Joy-Con: on the roll call, hold SL and push the stick left / right; `[menu] input-delay button: L (SL
  on a single Joy-Con) 1, ZL 0 -> MenuAlt2Check 1; with it Left 1 ...` and the delay changes. If MenuAlt2Check stays 0, the line says
  which part fails.
- DNS: fixed and seen on the console 2026-11-01 (the official server by name).
- A long match at 240 Hz (several minutes) against the browser: no desync (the old frame-3600 one).
- After a match at 240 Hz, the awards and the end-of-match menu at normal speed: `[np] a Level outside a session at TF.EX's step (41666
  ticks): N updates this frame (FNA's fixed time step)` (N about 4 on the console), then `[lobby] voted REMATCH!` / `ARCHER SELECT`.
- A freeze: `[hang] the game froze N s ago (no frame): the main thread` with its pc / lr / frames (hangwatch.cpp; boot: `[hang] watchdog
  started (main thread known, its handle found; suspend yes)`).
- GC at 240 Hz: `[gc] over the last 600 saves: 1-2 GC collections ...` (was 4-5); the max pause stays ~15 ms.
- Controllers: a second Joy-Con pair in a lobby: it joins as a local player only when it presses A; the first player's controller keeps
  working.

## Not pushed
- TF.EX v0.28.0 is live everywhere (2026-10-30): browser PR #93 merged and the site deployed, the tfex-server worker deployed (guests),
  master 9e526dd8 published to the console (boot-checked); :18300 serves worktree tfex-028. Our TF.EX PR #283 closed (superseded by #306).
- The matchmaking-server fork's branch `switch-online-028` (upstream main with guests + our three: allowed origins, harness log lines,
  TFEX_FIXED_SEED; our #283 seating dropped), local.
- The TF.EX fork branch `lmp-v0.27.0` (v0.27.0 + #283's two commits) IS pushed (belacmu/TF.EX, fbeec67; checked 2026-10-29,
  as is every local TF.EX branch): retired by v0.28.0.

## Where things are
AGENTS.md (rules, tools, layout), docs/STRATEGY-2026-10-28.md (the plan), docs/TFEX-BACKLOG.md (what of TF.EX runs, generated),
docs/BACKLOG-2026-10-25.md (the open items in detail), tools/tfex/README.md (updates and the translator).
