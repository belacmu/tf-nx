# Changes worth taking upstream to TF.EX

Things found while porting / profiling on the Switch that TF.EX itself would benefit from (or should know about). Each entry: what, evidence,
the Switch-side workaround (a TFEX-DIFF), and the status. Add new ones here as they come up; keep the evidence reproducible.

## Candidates

### 1. A load during the round intro replays the whole intro so far (quadratic over the intro; 16x worse at 240 Hz)
- **Where:** `state/TF.State.TowerFallExtensions/Level.cs` `LoadState`, the `isIntroRunning` block: it recreates `VersusStart` and calls its
  `Update()` `HUD.VersusStart.CoroutineState` times, the number of intro updates so far.
- **Evidence (Ryujinx sync test at 240 Hz, hw.txt allocwatch, 2026-10-24):** managed allocations per load climb through the intro, 2,000 ->
  5,000 -> 8,000 (72 KB -> 155 KB -> 237 KB) over the first ~900 loads, then drop to ~600 (41 KB) once the round has started. The intro
  lasts ~4 s, so at 240 Hz it is ~960 updates: each rollback in that window replays up to ~960 coroutine updates, each through TF.State's
  Coroutine and HUD hooks. At 60 Hz the same intro is ~240 updates (the earlier "58 per load" average).
- **Why it matters:** cost per load proportional to the intro's age, during the first seconds of every round; the GC load it makes.
- **What the cost is:** on the console's by-type count (hw.txt allocwatch) ~90% of those allocations are TF.State's own HUD bookkeeping in
  `CoroutinePatch.Coroutine_Update`: each replayed step does `hudService.Get()` + `hudService.Update(hud)` (5 HUD model objects), although
  LoadState then sets the HUD state from the loaded state (`hudService.Update(gameState.Entities.Hud)`), overwriting every count the replay made.
- **Possible upstream changes:** (a) skip the counting in `Coroutine_Update` while `StateFlags.IsRestoring` (what the Switch does; same result);
  (b) longer term, save the intro's own state (coroutine position / tweens) instead of a counter to replay.
- **Switch workaround (TFEX-DIFF S-performance, 2026-10-24):** (a), as a [[replace]] of `CoroutinePatch.Coroutine_Update` by
  `TfexGlue.StateGlue.Coroutine_Update` (glue/State.cs: TF.State's body plus the `IsRestoring` check). Ryujinx 240 Hz sync test: allocations per
  intro load 2,000 / 5,000 / 8,000 -> ~500; intro loads 0.5-0.74 ms -> 0.16-0.27 ms; 0 mismatches; smoke lobby vs the browser 0/589.
  The replay of the game's `VersusStart.Update` itself remains (smaller).
- **Status:** found 2026-10-24; not reported upstream. **Switch workaround removed 2026-10-27** (TF.State's own Coroutine_Update runs, for
  fewer custom differences): Ryujinx 240 Hz sync test PASS, 0 mismatches, loads avg 0.26 -> 0.30 ms, TFGame.Update 3.00 -> 3.14 ms a call
  (testmode-240-135910 vs -170646); smoke rematch PASS (165854). The upstream change (a) would bring the saving back for everyone.

### 2. Drawing an arrow changes its saved rotation (a desync between any two players, rare)
- **What happens:** TowerFall's `Arrow.DoWrapRender` adds the wiggle angle to every image's `Rotation`, draws, then subtracts it.
  In floats `(x + n) - n` isn't always `x`, and TF.State saves the sprites' rotation (e.g. `BombArrow.NormalSprite` /
  `BuriedSprite`). How many times an arrow was drawn between two saves differs per machine (frame skips; resimulated frames
  aren't drawn), so the saved state, and GGRS's checksum, can differ by one ULP while the game plays the same.
- **Seen:** the Switch against the browser page, a team match with bomb arrows (docs/TEST-PLAN-PARITY.md, mix-team-two-wide, seed
  2001): BombArrow sprite rotation 0x40490fda vs 0x40490fdb. Any two TF.EX players are exposed the same way.
- **Fix idea:** keep the exact rotation across the draw (save before, restore after) instead of subtracting, e.g. a TF.EX patch on
  `Arrow.DoWrapRender`; or leave sprite rotation out of the checksummed state and derive it from Direction on load.

### 3. TEAM REVIVE does nothing online (found 2026-10-29; reported 2026-11-01: issue Fcornaire/TF.EX#311, fix PR #312)
- **What happens:** ApplyNetplayMode (TF.EX fbeec67 Extensions/VersusMode.cs:15-22, called by MainMenuPatch.CreateNetplaySelect
  :808 and CreateLobbyBrowser :1655) sets MatchSettings.Mode to TF.EX's Netplay mode (11); nothing sets the lobby's mode back (only
  NetplayRoundLogic / NetplayVersusMode read lobby.GameData.Mode). The game makes a TeamReviver only in PlayerCorpse.Added when
  `Mode == TeamDeathmatch && Variants.TeamRevive` (PC 1.3.3.3 PlayerCorpse.cs:247); neither FortRise's patch_PlayerCorpse nor
  TF.State's PlayerCorpse_Added_Postfix touches that. The variant isn't blocked (UnauthorizedVariant: TreasureDraft only), so it can
  be picked and is on in the match, but no reviver is made.
- **Seen:** the user on the console 2026-10-25 (docs/BACKLOG-2026-10-25.md item 6); the browser harness logs
  `Session.MatchSettings.Mode(int)=11` (web/Netplay/XplayVersus.cs). The Switch matches the PC (pcgameplay.cpp: no TeamReviver from
  corpses online).
- **Why TF.EX's tests miss it:** its team-reviver scenario (Scenarios/ScenarioCatalog.cs:476) runs with Mode = TeamDeathmatch.
- **Fix idea:** make the reviver in Mode 11 for team lobbies with the variant on. That also needs the cut-short path: vanilla
  TeamReviver (PC TeamReviver.cs:430) and TF.State's TeamReviver patch (:113) both test `Mode == TeamDeathmatch` and call
  TeamDeathmatchRoundLogic.CheckForWin, which NetplayRoundLogic doesn't have. Or block TEAM REVIVE online until then.
- **Still so in v0.28.0, from the official binaries only (2026-10-30; no port involved):** the release zip's 12 TF.EX / TF.State /
  TF.Replay DLLs (ilspycmd -il) construct `TowerFall.TeamReviver` in one place, TF.State's `LoadTeamRevivers` (a saved reviver
  rebuilt on a rollback load: never the first one); TF.EX.Patchs calls `ApplyNetplayMode` twice (the netplay menus), which stores
  `MatchSettings.Mode` = the registered Netplay mode; FortRise's patch_PlayerCorpse has no reviver code; and the official PC
  TowerFall.exe (FortRise-patched IL) makes a TDM reviver in PlayerCorpse.Added only after `ldfld Mode; ldc.i4.5; bne.un`. Upstream's
  TEAM REVIVE work (#211, 60ac432, 2026-08-17) tracked the reviver in TF.State and allowed 3-player TDM; its scenario (ScenarioCatalog
  `team-reviver`) sets `Mode = Modes.TeamDeathmatch`, which an online match never has. A check in TF.EX's own harness: that scenario
  with the Netplay mode instead (as a lobby match) should fail its Expect (no reviving TeamReviver).
  Nothing puts Mode back to TeamDeathmatch for the match: the release DLLs store MatchSettings.Mode in ApplyNetplayMode (Netplay),
  ClearNetplayMode (LastManStanding, leaving netplay), TFCommands.StartGame (the dev console / scenarios) and VersusModeButtonPatch's
  ModeSelection.Restore (the offline VERSUS menu) only; FortRise only in patch_VersusModeButton (the menu); NetplayVersusMode.OnStartGame
  is empty. The lobby's mode reaches the game through NetplayVersusMode.IsTeamMode (FortRise's TeamMode getter) and NetplayRoundLogic,
  so code that asks TeamMode sees a team match, while code that reads the Mode field (PlayerCorpse.Added, vanilla TeamReviver's
  cut-short check) sees the Netplay mode.
- **Fix (2026-10-29, not pushed):** branch `team-revive-netplay` (d39831d): a PlayerCorpse.Added postfix makes the TDM reviver when
  `Mode.IsNetplay() && MatchSettings.TeamMode && Variants.TeamRevive`; TF.State's cut-short check runs `TeamCheckForRoundOver` on any
  team round logic (MatchSettings.TeamMode) and sets `Level.Ending`. Run on both ports (STATUS.md): the reviver appears and stays in
  sync; a revive itself is untested. The Switch needed one game-version gap for it (pcgameplay.cpp: the 1.4.1.6 TeamReviver sets
  Depth -99, the PC doesn't).

### 4. A mod archer's stand-in can take the archer a joined player is on (found 2026-10-30; not reported)
- **What happens:** a player's ArcherMods is filled only when that player readies (RollcallElement EnterJoined: GetInstalledArcherMods),
  so until everyone has readied a custom archer is "not shared" (ArcherData.IsSharedByAllPlayers needs the tag in every player's
  list) and ResolveArchers gives it PickUnusedVanilla: the first vanilla index no lobby record claims. A joined player who hasn't
  readied yet has the record's ArcherIndex (0), not the archer under its cursor, so the stand-in can be exactly that archer
  (TFGame.Characters[remote seat] = it, MatchmakingService's lobby update), and the game's roll call won't let the player ready on an
  archer another seat shows: that player has to move off it.
- **Seen:** our Switch-vs-browser harness (the Switch readied first on a mod archer both sides have; the page stayed on its default,
  Blue, and its Confirm did nothing, runs 101325 and earlier). A person would move to another archer; the stand-in also changes
  once all are ready (the archer becomes shared), so it is a confusing moment rather than a stuck lobby.
- **Fix idea:** send ArcherMods with the join (they are known then), or have PickUnusedVanilla skip the local seats' current
  TFGame.Characters.

### 5. A wide (WiderSet) lobby stops at 4 players, and TF.State spawns only seats 0-3 online (found 2026-11-02; not reported)
- **Where:** `LobbyVersusPlayerCountButton.MAX_PLAYERS = 4` (LOBBY SIZE 2-4 even while WiderSet is wide, though `Constants.MAX_SEATS` is 8
  and `InputService` already sizes controller slots `WiderSet ? 8 : 4`); TF.State's `RoundLogic_SpawnPlayersFFA` / `SpawnPlayersTeams`
  prefixes (`Vector2[4]`, `for j < 4`, `CountTeam` / `SpawnTeam`) run instead of the game's while capture is on, so a 5+ seat netplay
  match would spawn 4 archers; `NetplayRoundLogic.OtherPlayerCouldWin`, `ApplyTeamsToMatchSettings`, `MatchVariants.EnableForAll` and
  the replay teams also loop to 4. The server (`MAX_PLAYERS = 4`) caps every lobby too.
  Also on a wide map with any player count: those prefixes compare spawns against the narrow screen's centre `160f` (the FFA
  "two leaders face each other" rule, the team side split), where WiderSet makes the game's own methods use 210, so wide netplay
  spawns differ from wide offline play.
- **Our change:** belacmu/TF.EX `eight-players` (88d54a0, on fb377d5): LOBBY SIZE to 8 while WiderSet is wide, the loops to
  `TFGame.Players.Length` (84e1358 the `Seats` entry, b29593c the roll-call icon for seat 5+, f10e659 the spawn centre
  `WrapMath.AddWidth.X / 2`); the server fork's `eight-players` branch: 8 seats in a wide lobby. Plan: docs/PLAN-8-PLAYERS-2026-11-02.md.
- **Status:** deferred (the user: upstream PRs later). Evidence for a PR: `tools/smoke.sh wide5` (5 players, Switch + 3 pages).

### 6. WiderSet (not TF.EX): the last kill of an 8-player team match marks only seats 0-3 as winners (found 2026-11-03; not reported)
- **Where:** `RoundLogic.FinalKillTeams` loops `i < 4` (GotWin, the spotlight), and so does `Session.EndRound`'s per-player stats loop
  (rounds survived, SurvivedWithNoKills, ...); WiderSet's transpilers widen the other seat loops to
  `IsWide ? 8 : 4` but not this one, so winners in seats 4-7 get no GotWin (match stats, awards) and no spotlight.
- **The Switch:** plays the PC's 4 online (pcgameplay kGaps, FinalKillTeams), since any difference desyncs MatchStats.
- **Status:** deferred, a WiderSet issue (Teuria.WiderSet), noted only.

### 7. Local VERSUS can't reach a game mode registered after TF.EX's (found 2026-11-05; not filed: latent on the PC, 2026-11-06)
- **The code:** TF.EX's `VersusModeButtonPatch` (TF.EX.Patchs Entity/MenuItem/VersusModeButton.cs) `Update` postfix puts the previous
  mode back (`ModeSelection.Restore`) whenever the GAME MODE scroll lands on TF.EX's Netplay mode. A mode registered after TF.EX's sits
  beyond it and can't be reached.
- **On the PC it doesn't happen with released mods (checked 2026-11-06):** FortRise sorts mods by `Priority` (ModuleManager's ModOrder)
  and delays one until its dependencies are in. TF.EX (Low) depends on TF.State (High), TF.Replay and TF.InputDisplayer, so it loads
  after the ordinary mods, and their modes come before its Netplay mode. Official build (browser repo's published TF.EX v0.29.0, worktree
  tfex-0281) with `?mods=TF.EX,MoveOrDie`: load order TF.State, MoveOrDie, TF.InputDisplayer, TF.Replay, TF.EX; GAME MODE scrolls to
  MOVE OR DIE. None of the catalog's mode mods (Soccer, Respawn, Move or Die, BaronMode, Bartizan, Arcade, PlayTag) depends on TF.EX.
- **It would happen** for a mode mod that depends on TF.EX (it then loads after it): our Move or Die fork (optional TF.EX + TF.State,
  for online play), and any mod built on mod modes online (Fcornaire/TF.EX#314). Worth raising with #314, not as an issue on its own.
- **On the Switch it happened because of our order:** the Switch's registry boots TF.EX before the mods (not FortRise's priority +
  dependency order), so Soccer's and Move or Die's modes landed after the Netplay mode. Our side to fix: register code mods in
  FortRise's order.
- **Fix (our fork, belacmu/TF.EX `game-mode-scroll` f4ae1eb, kept for #314):** step over the Netplay mode in the direction the player
  moved, restore only when nothing lies beyond.

## Investigated, not an upstream issue
- **TF.State's round-intro replay on load (2026-10-24).** `LevelExtensions.LoadState` recreates `VersusStart` and replays its coroutine
  `HUD.VersusStart.CoroutineState` times; a profile showed 58 `Coroutine_Update` calls per load (~24% of a load) and it looked as if the counter
  was never reset. It is: those calls only happen during the intro phase (the average over a whole test match fell to ~12 per load), and
  a Switch-side skip once `RoundStarted` never fired. The counter is fine; the intro-phase cost is candidate 1 above.
