# Team Deathmatch and Head Hunters: what the Switch must do to match TF.EX's `NetplayRoundLogic` (RE, 2026-10-16)

Read-only RE. Sources: TF.EX 0.19.2 (`.cache/tfex-0.19.2/src`), PC 1.3.3.3 vanilla (decompiled to `.cache/pc-src/*.cs`,
whole assembly in `.cache/pc-src/full/`), FortRise 5.5.0-beta.3 MonoMod patches (`TowerFall.FortRise.mm.dll`, decompiled to
`.cache/pc-src/fortrise/`, whole module in `.cache/pc-src/fortrise/full/`), PC IL `.cache/towerfall-pc.il`, Switch
`tools/re/main.flat` (1.4.1.6). Nothing was built or run.

**Short answer.** The three Switch round-logic classes do the same thing as PC vanilla and as `NetplayRoundLogic`. The
differences are elsewhere:
1. TF.EX runs every netplay match with `MatchSettings.Mode = 11` (FortRise custom). So every PC site that compares `Mode`
   with 4 or 5 *explicitly* takes its "other mode" branch, while `TeamMode` still answers the lobby's mode.
2. `RoundEndCounter.ghostWaitCounter` starts at 150 on the Switch and at 90 on the PC.

Keep Mode 3/4/5 on the Switch and add five instruction patches plus one spawn port (section 5).

---

## 1. Facts

### Enums
| | value | evidence |
|---|---|---|
| `Modes` 0..6 | Quest 0, DarkWorld 1, Trials 2, LastManStanding 3, HeadHunters 4, TeamDeathmatch 5, Warlord 6, the same as on PC | `get_TeamMode` 0xaba740 `cmp w8,#5`; `get_IsInOvertime` 0xb44048 `cmp w8,#4`; GoalScore jump table 0x38f3118 (cases 3,4,5,6) |
| `Modes` >= 7 | The Switch has only 0..7: `GetRoundLogic` 0xb37f64 and `PauseMenuType` 0xabaa24 do `cmp #7; b.hi`; `SoloMode` mask 0x87 = {0,1,2,7}; `GetLevels` tests 7 where the PC tests LevelTest 9. The PC has 7 EditorTest, 8 EditorPreview, 9 LevelTest, and FortRise adds 10 Custom (TF.EX's mode is registered as 11) | |
| `Allegiance` | Neutral -1, Blue 0, Red 1 (same as PC) | `TeamCheckForRoundOver` 0xb3a424 stores -1, 0xb3a440 stores 0, 0xb3a454 stores 1 |
| `MatchTeams[-1]` | Red (1), as on PC | jump table 0x38f31d8, entry -1 -> 0xabc454 returns w8 = 1 |

### Layouts (Switch)
| object | field | offset |
|---|---|---|
| MatchSettings | LevelSystem / Mode / MatchLength / Teams (MatchTeams) / Variants | +0x28 / +0x30 / +0x34 / +0x38 / +0x40 |
| MatchTeams | player1..6Team (int each) | +0x10, +0x14, +0x18, +0x1c, +0x20, +0x24 (`get_Item` 0xabc420, `set_Item` 0xabc870). It has 6 slots; PC has 4 |
| Session | MatchSettings / MatchStats[6] (stride 0x2a8) / **Scores int[]** / OldScores int[] / kept ArrowList[6] (Switch only) / CurrentLevel / RoundIndex | +0x20 / +0x28 / **+0x30** / +0x38 / +0x40 / +0x68 / +0x60 |
| Session.Scores length | `TeamMode ? 2 : 6` (ctor 0xb43b70, `csel w8, 2, 6`). PC: `TeamMode ? 2 : 4` | Team mode: 2 on both. FFA: 6 vs 4; every reader is equivalent (sec. 4) |
| RoundLogic | Session / ScoreGained / ScoreLost / Events | +0x10 / +0x20 / +0x28 / +0x48 |
| LMS / HH / TDM RoundLogic (same layout) | roundEndCounter / done / wasFinalKill | +0x68 / +0x70 / +0x71 |
| RoundEndCounter | session / endCounter / ghostWaitCounter | +0x10 / +0x18 / +0x1c |
| Level | Ending / CanEnd / OrbLogic | +0x88 / +0x89 / +0x80 |
| Player | Allegiance | +0x70 |
| PlayerCorpse | Revived | +0x122 |
| Arrays | data +0x10, length via pointer at +0x20 (`ldr x,[a,#0x20]; ldr w,[x]`) | |

### MatchSettings getters
- `get_TeamMode` 0xaba740 is `Mode == 5`. The Switch inlines the same test into `GetPlayerAllegiance` 0xabb5c0, `GetMaxTeamSize`
  0xabb5f0 (int[2]), `GetPlayerTeamSize` 0xabb910, `IsFriendly` 0xabb880, `CanPlayThisMode` 0xabaa50 and `GoalScore`.
  No other mode counts as team mode, and every vanilla team helper uses exactly 2 teams (`TeamCheckForRoundOver` bool[2],
  `GetMaxTeamSize` int[2]).
- `GetPlayerAllegiance(i)` = `Mode == 5 ? Teams[i] : -1` (0xabb5c0, the same as PC).
- `get_GoalScore` 0xabae50, `ceil(n * GoalMultiplier[MatchLength])` with multipliers {0.01, 0.5, 1, 2} (0x38e2240):
  - LMS n = {3p 4, 4p 3, 5p 3, 6p 3, else 5} (0x38e2210)
  - HH/Warlord n = {3p 8, 4p 10, 5p 11, 6p 12, else 5} (0x38e2280)
  - TDM n = 5
  - For 2-4 players this is the PC vanilla table. **FortRise's custom getter (used for Mode 11) is the HH table for every mode.**
    For 2 players every mode gives 5 x multiplier on both sides, so it is equal. With 3-4 players, LMS and TDM would differ.

## 2. TF.EX / FortRise facts that matter

- `NetplayRoundLogic` (TF.EX) is the same code as PC vanilla `LastManStanding`/`Headhunters`/`TeamDeathmatchRoundLogic`,
  branched on the *lobby* mode (`IsTeamMode = mode==5`, `IsHeadHunters = mode==4`). The only behavioural differences are:
  (a) `SessionStats.TimePlayed +=` comes after `base.OnUpdate()` instead of before (a stat, no effect);
  (b) HH `else AddScore(killer, 1)` in place of `else if (killer != -1)`, which is equivalent;
  (c) round 0 spawns on session sync and not in `OnLevelLoadFinish`. This is already RNG-equivalent for LMS
  (GAMEPLAY-RNG-PORT.md:173) and applies the same way to TDM and HH.
  There is no ghost, revive or score change of its own.
- The round logic object is rebuilt every round on both sides (`Session.LevelLoadStart`), so `done`, `wasFinalKill` and the
  counter reset each round.
- FortRise `patch_MatchSettings.get_TeamMode`: `IsCustom` -> `NetplayVersusMode.IsTeamMode` (lobby mode == 5). So `TeamMode`,
  `GetPlayerAllegiance`, `GetScoreIndex`, `Scores.Length` (2 in TDM), `CanHurtPlayer` and the rest behave as in real TDM.
- FortRise does **not** patch `Session.IsInOvertime/WasInOvertime`, `PlayerCorpse.Added` or `VersusTowerData.GetLevels`.
  They read `Mode` directly, so on PC netplay they always take the "not HH / not TDM" branch.
- `RoundLogic.GetRoundLogic` -> FortRise -> `NetplayVersusMode.OnCreateRoundLogic` -> `NetplayRoundLogic`.

## 3. Mode-comparison sites (PC IL `ldfld MatchSettings::Mode`) during a versus match

| PC site | compares | PC with Mode 11 | Switch with Mode 3/4/5 | relevance |
|---|---|---|---|---|
| `Session.get_IsInOvertime` | ==4 | always false | true in HH once a score >= goal (tie at the goal) | **SYNC** (spawns, chests, shields) |
| `Session.get_WasInOvertime` | ==4 | always false | as above, on old scores | music (`CancelFinalKill`), results; cosmetic |
| `PlayerCorpse.Added` | ==5 && TeamRevive | never adds a TeamReviver | adds one (0xafae20) | **SYNC** if TEAM_REVIVE is on |
| `TeamReviver.*` (MoveNext, line 430) | ==5 | never runs (no reviver) | only runs if a reviver exists | follows the row above |
| `VersusTowerData.GetLevels` | ==9, ==5 | FFA filter `PlayerSpawns >= PlayerAmount` | TDM filter `TeamSpawns >= GetMaxTeamSize` (0xbc21bc) | **SYNC** (level list, so level order) |
| `RoundLogic.GetRoundLogic` | switch | FortRise custom path, NetplayRoundLogic | vanilla class (equivalent, sec. 4) | none |
| `MatchSettings.get_GoalScore` | switch | FortRise: HH table, all modes | per mode | equal for 2 players (see 1) |
| `get_AutobalanceLosingAmount` | ==3 && PlayerAmount>2 | 3 | LMS with 3+ players: 2 | equal for 2 players |
| `get_PauseMenuType`, `get_SoloMode`, `CleanSettingsVersus`, `get_CanPlayThisMode` | | Versus pause, not solo; menu only | same | none |
| `Session.StartGame` (==1), `LevelLoaderXML` (==2, ==9), `TreasureChest.SceneBegin`, `GemPickup`, `LoopPlatform`, `SwitchBlockControl`, `Level.HandlePausing` (==2), `Player.TargetInvisibleOpacity`/`DoHatPickupCheck` (==0/1), DarkPortals/PurpleArcher (==9) | non-versus modes | false | false | none |
| `VersusStart` intro (3/4/5 title), `VersusRoundResults` (==4: skull sprites, colours, sound), `VersusAwards`, `VersusCoinButton`, `VersusModeButton` | | no mode title, coin sprites | mode-specific visuals | cosmetic. LMS is already in sync with the same mismatch, so the intro timing does not depend on it |

**`get_TeamMode` callers** (PC IL): MatchSettings helpers, `Session` (.ctor Scores length, CanHurtPlayer, GetScoreIndex,
GetLowestScore, TeamHasCrown/TeamHadCrown, GetSpawnArrows `TeamStartArrows`), `RoundLogic.OnPlayerDeath` (DeathType.Team),
`Player.Die`, `PlayerCorpse.Added`, `LevelLoaderXML` (LevelRandomItems/Treasure `teamMode` arg), `Brambles`, `ShieldPickup`,
`SensorBlock`, Chalice*, VersusRoundResults/Start/Awards/PlayerMatchResults, TreasureSpawner draft. On PC these answer
lobby==5. On the Switch with Mode=5 they answer true (inlined `cmp #5`). **They are equal for Mode 3/4/5. No forcing needed.**

### What Mode 11 on the Switch would need
- `RoundLogic.GetRoundLogic` 0xb37f64 (`cmp #7; b.hi`) leaves the table and goes to the throw path. It needs a hook.
- `get_GoalScore` 0xabae7c (`sub 2; cmp 5; b.hi` -> throw) also needs a hook.
- `PauseMenuType` 0xabaa28 returns 0, the wrong menu.
- Every inlined TeamMode test (`cmp w8,#5` at 0xaba744, 0xabaac8, 0xabb5c4, 0xabb628, 0xabb8c8, 0xabb944, plus `get_TeamMode`
  callers across the binary) would have to be forced true for team lobbies. That is 7+ patches, plus all other Switch-only
  Mode readers, which I did not enumerate (6-player UI, HUD).

So Mode 11 needs more patches, and riskier ones (section 6).

## 4. Per-mode comparison

### LMS (sanity)
`NetplayRoundLogic` LMS == PC `LastManStandingRoundLogic` == Switch (OnUpdate 0xa76110, OnPlayerDeath 0xa76250; same
fields, same `FinalKill`, `wasFinalKill` not cleared on cancel in all three). TF.State's `LastManStandingRoundLogicPatch`
(skip `OnLevelLoadFinish` on rollback frames) targets a class TF.EX never runs in netplay, so it is dead on PC. There are no
round-logic hooks in `hw-probe/exlaunch/source/program` (grep of RoundLogic/RoundEndCounter/0xa761/0xb686/0xa566: none).
Rollback is raw-memory, so it covers +0x68/+0x70/+0x71.

### Team Deathmatch (mode 5)
| item | PC / TF.EX | Switch | evidence | relevance | fix |
|---|---|---|---|---|---|
| OnLevelLoadFinish | VersusStart + SpawnPlayersTeams, Players = PlayerAmount | same | 0xb685c0 -> 0xbbc500, 0xb39070, 0xb37f30 | none | none (B2 ported) |
| OnRoundStart | base + SpawnTreasureChestsVersus | same | 0xb68670 -> 0xb39790 | none | none |
| OnUpdate: counter, done, AddScore(Players[0].Allegiance, 1), InsertCrownEvent, EndRound | | same | 0xb686a0 (Allegiance +0x70) | none | none |
| OnPlayerDeath: cancel when wasFinalKill && Living==0; TeamCheckForRoundOver; Ending; FinalKillTeams when Scores[s] >= Goal-1 | | same | 0xb68860 | none | none |
| TeamCheckForRoundOver | bool[2], living players + revived corpses | same, 2 teams | 0xb3a200 | none | none |
| FinalKillTeams | loop 4 | loop 6, gated by TFGame.Players | 0xb3a9f0 (`cmp x23,#6`) | none (2-4 players) | none |
| AddScore / InsertCrownEvent / GetWinner | | same | 0xb39db0 / 0xb39b60 / 0xb453b0 | none | none |
| Session.EndRound | stats, results | same, plus it stores surviving players' arrows in Session+0x40 (Switch-only kept-arrows variant, MatchVariants+0x1a0) | 0xb44b78..0xb44c2c | dormant (variant off) | keep +0x1a0 off |
| **Level list** | FFA filter (Mode 11) | TDM filter | 0xbc21bc/0xbc21c0 | **SYNC** | patch 0xbc21c0 `b.eq` (54000260) -> `nop` (d503201f) |
| **TopUpSpawns** | TF.State fills A/B from PlayerSpawn when a team has too few | not ported (rnd.cpp:591, it only logs) | | **SYNC once the FFA filter lets such levels in** | port TopUp (sec. 5) |
| **TeamReviver** | never created (Mode 11) | created if TeamRevive | 0xafae20/0xafae24 | **SYNC** with TEAM_REVIVE | patch 0xafae24 `b.ne` (54fff3c1) -> `b 0xafac9c` (17ffff9e), or refuse the variant |
| **RoundEndCounter ghost wait** | 90 | 150 | ctor 0xb37978, Reset 0xb379e4 (`movk x8,#0x4316,lsl 48`); PC IL `ldc.r4 90` twice | **SYNC** with RETURN_AS_GHOSTS (all modes) | both insns f2e862c8 -> f2e85688 (`movk #0x42b4`, 90.0) |
| GoalScore | FortRise (5,8,10) | TDM 5 | | equal for 2 players | none for 1v1 |
| FinalKill SFX pan | 160 | 1e8 | PARITY-TRIAGE | sound | none |

### Head Hunters (mode 4)
| item | PC / TF.EX | Switch | evidence | relevance | fix |
|---|---|---|---|---|---|
| OnLevelLoadFinish / OnRoundStart / OnUpdate | vanilla HH (no score at round end) | same | 0xa56440 / 0xa564d0 / 0xa56500 | none | none |
| OnPlayerDeath | AddScore(-1 self/env, +1 killer); GetWinner; cancel+Ending=false+counter.Reset; FFA all-but-one; OtherPlayerCouldWin | same order | 0xa56600 | none | none |
| OtherPlayerCouldWin | loop 4 | loop 6 (TFGame.Players bool[6], bounds-checked) | 0xa56790 | none | none |
| Scores length 6 vs 4 | | GetWinner, GetHighestScore, GetScoreLead, GetLowestScore, TeamHasCrown: extra slots are 0 and gated, so equal; CrownChangeEvent bool[6] is display only (same as LMS) | | none | none |
| **IsInOvertime** | always false (Mode 11) | true on a tie at the goal | 0xb44048/0xb4404c; callers: ShouldSpawn, SpawnPlayersFFA, SpawnTreasureChestsVersus, GetSpawnShield, VersusStart lambda, LosePointOverflow, Background | **SYNC**: the Switch would spawn only the leaders, skip chests and drop the autobalance shield | patch 0xb4404c `b.ne` (54000461) -> `b 0xb440d8` (14000023) |
| WasInOvertime | false | as above | 0xb4419c; callers CancelFinalKill (music), results | cosmetic, but patch it with IsInOvertime | 0xb4419c 54000461 -> 14000023 |
| GoalScore | FortRise HH table | HH table | | equal | none |
| RoundEndCounter ghost wait | 90 | 150 | as for TDM | SYNC with ghosts | as for TDM |

## 5. Recommended implementation (keep Mode 3/4/5)

Netplay-only instruction patches, added to the `gaps[]` table in `xplay.cpp` StartLobbyMatch. They should be restored at
EndLobbyMatch, because they change offline HH/TDM:
1. `0xb37978` and `0xb379e4`: `f2e862c8` -> `f2e85688` (ghost wait 150 -> 90). This applies to all modes, LMS included.
2. `0xb4404c`: `54000461` -> `14000023` (`IsInOvertime` = false).
3. `0xb4419c`: `54000461` -> `14000023` (`WasInOvertime` = false).
4. `0xafae24`: `54fff3c1` -> `17ffff9e` (no TeamReviver, as with Mode 11).
5. `0xbc21c0`: `54000260` -> `d503201f` (`GetLevels`: FFA filter in TDM, as with Mode 11).

Port:

6. **TF.State `TopUpSpawns`** in the B2 path. This is required once 5 is in, because levels without enough TeamSpawnA/B now
   reach `SpawnPlayersTeams` (0xb39070). TF.State does the top-up **before** the sort: for team A, PlayerSpawn with X <= 160,
   then all PlayerSpawn; for B, X > 160, then all; skip positions already used; stop at the team's member count. It runs only
   when `|A| < blueCount || |B| < redCount`. On the Switch the lists are sorted before `TeamsBeforeHook`, so either:
   (a) move an inline hook to just before the two `List.Sort` calls (comparators 0xb39750/0xb39770) and `List<Vector2>.Add`
   the top-up positions there; or
   (b) replace 0xb39070 in netplay with a full port of `RoundLogicPatch.RoundLogic_SpawnPlayersTeams`: GetXMLPositions,
   TopUp, Sort, OwnShuffledIndexes, ApplyOrder, `Player` ctor + `Level.Add` (Blue loop, then Red), `Players = PlayerAmount`.
   Option (b) is safer, because the Switch body's filtered `GetXMLPositions(name, teamSize)` and its 6-slot loops would no
   longer be involved.

Nothing else is needed for TDM/HH round logic: no `NetplayRoundLogic` port. Keep the Switch-only variants (+0x1a0,
+0x1d8) off. For 3-4 players, also patch `AutobalanceLosingAmount` (0xaba9c4) and the LMS/TDM `GoalScore` paths to the
FortRise formula. For 1v1 these are equal.

## 6. Mode 11 vs Mode 3/4/5

- **Keep 3/4/5 (recommended):** 6 one-instruction patches plus 1 spawn port. Every `TeamMode` reader already matches, and
  no jump table sees an unknown value.
- **Mode 11:** hooks on `GetRoundLogic` (create the right vanilla class), `GoalScore` and `PauseMenuType`; force-true on
  the inlined `cmp #5` TeamMode tests in 7+ places for team lobbies. Overtime, TeamReviver and GetLevels would come for
  free. But any Switch-only code that switches on `Mode` (6-player and HUD paths, not fully enumerated) could throw or
  misbehave, and the ghost-wait patch and TopUp port are still needed. It needs more patches, and riskier ones.

## 7. Mode 11 on the Switch: every Mode reader (2026-10-29)

Decision (the user, 2026-10-29): the Switch moves to FortRise's custom Mode 11 for online matches, as TF.EX runs them (also the base for
mods' game modes), instead of keeping 3/4/5 and imitating Mode 11's branches. Scan: every `ldr`/`ldrsw w, [x, #0x30]` followed within
four instructions by `cmp w, #2..9` on the same register in the game's .text (scratchpad modescan.py; names from tools/re/callmap.py),
plus the known jump tables (GoalScore 0xabae7c). Readers of MatchSettings.Mode, by what Mode 11 does there:

| site | Switch | under Mode 11 | needs |
|---|---|---|---|
| RoundLogic.GetRoundLogic 0xb37f64 | `cmp #7; b.hi` throw | throws | FortRise's patch_RoundLogic.GetRoundLogic (custom -> the mode's OnCreateRoundLogic) |
| MatchSettings.get_GoalScore 0xabae7c | jump table, throw | throws | FortRise's patch_MatchSettings.get_GoalScore |
| MatchSettings.get_TeamMode 0xaba744 | Mode == 5 | false | FortRise's get_TeamMode (custom -> IsTeamMode = lobby TDM); its callers call it (Session ctor Scores, CanHurtPlayer, ...) |
| MatchSettings.get_CanPlayThisMode 0xabaa9c/ac8/aec | Mode == 5 / solo | menu only | FortRise's get_CanPlayThisMode |
| GetPlayerAllegiance 0xabb5c4, GetMaxTeamSize 0xabb628, IsFriendly 0xabb8c8, GetPlayerTeamSize 0xabb944 | TeamMode inlined (Mode == 5) | not a team match | Switch fix: PC's bodies call TeamMode (FortRise's), so these four ask get_TeamMode |
| MatchSettings.get_PauseMenuType 0xabaa24 | `cmp #7; b.hi` -> 0 | 0 = VersusPause on the Switch (e_PauseMenu_MenuType), the PC's default arm | none (section 3's "wrong menu" misread it) |
| Session.get_IsInOvertime / WasInOvertime 0xb44048/0xb44198, PlayerCorpse.Added 0xafae20, VersusTowerData.GetLevels 0xbc21bc | == 4 / == 5 | the PC's branch | pcgameplay.cpp's imitation patches (overtime, TeamReviver) go; GetLevels stays (it loads the PC's level files) |
| get_AutobalanceLosingAmount, get_SoloMode, get_PlayerLimit, CleanSettingsVersus, VersusStart / VersusRoundResults / VersusAwards / VersusModeButton, Level / LevelLoaderXML / GemPickup / LoopPlatform / SwitchBlockControl / TreasureChest / TeamReviver.DrawLight / quest and dark-world code | == 2..7 | the PC's branch (the same tests on the PC) | none |
| ArcherPortrait.ShouldFlip 0x9702ac | Session.MatchSettings.Mode == 4 (Switch-only; the PC's ShouldFlip reads no Mode) | false, as the PC | none (cosmetic) |

No other Switch-only reader was found (no 6-player or HUD code compares Mode). Three hits outside the game's code (0x2ea9xx,
0x30fexx, 0x3ffexx: runtime) are other fields.
