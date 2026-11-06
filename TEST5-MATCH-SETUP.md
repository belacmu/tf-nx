# TEST 5: the scripted 2-player versus match, exactly as TF.EX's test mode sets it up

Goal of the test: the same scripted 2-player Last Man Standing match runs in the browser (real TF.EX 0.19.2 under
FortRise, .NET WebAssembly) and natively on the Switch, and the per-tick logs are compared. This document says how
`test lms 0 0 42 2 #NONE# 2` builds that match, field by field, and what the Switch side has to reproduce.

Everything below was read from a decompile of the vendored TF.EX v0.19.2 DLLs (`vendor/tfex/DShad.TF.EX-v0.19.2.zip`
in the browser repo; ilspy output is not kept), the PC 1.3.3.3 decompile (`reference/decompile/`), and **measured**
in the browser harness (`xplay2`, browser branch `exp/xplay-versus`; log lines quoted here are from its runs). Where a
statement is measured it says so; where it is only read from code it says "(code)". Facts are for seed 42, map 0,
2 players, no variants.

Naming: "PC decompile" names are `TowerFall.*` / `Monocle.*` from the PC game; "TF.EX" names are TF.EX's own classes.

## 0. The ten things a native implementation must get right

1. **First to 2 round wins, not 5.** In test mode TF.EX patches `MatchSettings.PlayerGoals` to return 2, so
   `GoalScore = ceil(2 * 1.0) = 2` (MatchLength Standard). (`TF.EX.Patchs.MatchSettingsPatch.MatchSettings_PlayerGoals`,
   `if (ExFlags.IsTestMode) __result = 2`.) Last-kill logic uses `GoalScore - 1`.
2. **Gameplay RNG = xoshiro256\*\* seeded with splitmix64(42), reseeded again at `Session.StartGame`, and a fixed
   sequence of draws before tick 0** (section 6: 11 draws, with the call sites).
3. **Level order is forced:** round 0 = `00.oel` of Sacred Ground, then 05, 06, 03, 08, 09, 04, 01, 02, 07 (shuffle of
   the eligible list with .NET `new Random(42)`), section 7.
4. **Spawn positions come from the gameplay RNG shuffle**, not `new Random()`, and p1 is mirrored from p0 (section 7).
5. **Timing is TF.EX's, not the Switch game's own fixed step:** `Engine.TimeMult = 1.0000019` (`0x3f800010`),
   `Engine.DeltaTime = 0.0166667f` (`0x3c88889a`), `Engine.DeltaTicks = 166667`, `Scene.FrameCounter = (float)ggrsFrame`
   (global, 1-based, not reset per round) and `Coroutine.OneFrameDelay = true` (section 8). `TimeMult` is NOT 1.0.
6. **The intro does not wait for a button:** TF.EX makes every `MenuConfirm` return true while the session is up, and
   forces the level system's `ShowControls` / `ShowTriggerControls` false. Round 0 then takes **265 ticks** before the
   archers can act (section 9).
7. **Tick 0** is the first tick whose `Player.Update` reaches the `GetState()` line with `State != Frozen`, which is
   Level.Update number 266 (section 9). The rule differs by one tick from the old `CrossplayTest.cs` rule (section 9.3).
8. **Several TF.State patches change vanilla behaviour** (e.g. `Arrow.EnforceLimit` is switched off, `Chain` and others
   draw from the gameplay RNG, DeterministicMath replaces sin/cos/atan2/pow in listed methods; section 11).
9. **Save-data unlocks are neutralised** inside TreasureSpawner / OrbPickup calls (section 11).
10. The browser trajectory is the one TF.EX's rollback sync test produces: every tick is saved, and after each tick
    the last 2 ticks are rolled back and resimulated (check distance 2). A faithful restore makes that identical to a
    plain forward run; it is the caveat of the whole comparison (section 12).

## 1. The command

`test lms 0 0 42 2 #NONE# 2` is TF.EX's dev-console command (`TF.EX.Core.TFCommands.LaunchTestMode`). Arguments, in
the order the code reads them:

| arg | value | meaning |
|---|---|---|
| 0 | `lms` | `ParseMode`: LMS -> `Modes.LastManStanding` (3). Others: `hh` HeadHunters, `tdm` TeamDeathmatch, anything else LMS |
| 1 | `0` | **start level**: `ParseStartLevel(args, 1, map)`, clamped to the tower's level count |
| 2 | `0` | **map** (versus tower index): `ParseMap(args, 2)`. 0 = `00 - Sacred Ground` |
| 3 | `42` | seed (int) |
| 4 | `2` | GGRS sync-test check distance (clamped to <= 7) |
| 5 | `#NONE#` | variants: split on `,` after trimming `#`, `_`->space, upper-cased: one variant, "NONE". It matches no variant: TF.EX only logs `[sweep] unknown variant 'NONE'` (a warning in the log, harmless). Result: no variants |
| 6 | `2` | player count, clamped to 2..4 |

(Both arg 1 and arg 2 are 0 here, so the order mix-up between "map" and "start level" in the task text does not matter.)

`LaunchTestMode` calls `StartTestMode(mode, map, startLevel, seed, checkDistance, variants, playerCount, levelPaths: null,
fps: 240 default)`. The harness runs the identical code with TF.EX's session fps forced to 60 (`TfexFps`, `?tfexfps=60`):
the only thing that changes is `GGRSConfig.Fps` (60 instead of `Constants.NETPLAY_FPS` = 240).

## 2. Order of operations (`TFCommands.StartTestMode` then `StartGame`)

Decompiled from the vendored TF.EX 0.19.2 DLLs (`StartTestMode` / `StartGame` match the v0.19.1 source clone line for line). Names are TF.EX unless prefixed `TowerFall.`.

1. `netplayManager.HasSetMode()` must be false (otherwise the command refuses).
2. `netplayManager.SetTestMode(checkDistance=2, playerCount=2, fps)`: `_netplayMode = Test`;
   `GGRSConfig = GGRSConfig.DefaultTest(2, 2, fps)` = `{ InputDelay = 2, Fps = fps, Name = "TEST", NumPlayers = 2, Test = {CheckDistance = 2} }`;
   `ExFlags.IsTestMode = true` (this is what `TF.State`'s `StateFlags.IsTestMode` reads); `IsCaptureActive` becomes true
   (`NetplayManager.PublishCaptureFlag`: mode != Uninitialized). (`Init` later overwrites `InputDelay` with the preference
   based `GetEffectiveInputDelay()`; irrelevant for scripted inputs, section 9.)
3. `StateApi.Current.SetSeed(42)` (`TF.State.Core.Api.TfStateApi.SetSeed` -> `RngService.SetSeed`):
   `StateContext.SetSeed`: `_seed = 42; _gameplayRandom.Seed((ulong)42)`, and `Calc.Random = new System.Random(42)`
   (the cosmetic RNG; see section 6).
4. `replayService.Initialize()` (starts a TF.Replay recording; no gameplay effect).
5. `TFGame.PlayerInputs[i] ??= new FakeController()` for i = 0..1 (only fills empty slots; section 5).
6. `StartGame(mode, netplayManager, map=0, startLevel=0, variants, playerCount=2, levelPaths=null)`:
   1. `TFGame.Players[i] = (i < 2)` for i in 0..3.
   2. `MatchSettings ms = TowerFall.MainMenu.VersusMatchSettings ?? TowerFall.MatchSettings.GetDefaultVersus();`
      (`VersusMatchSettings` is set by `TFGame.Load`: `GetDefaultVersus()` = `new MatchSettings(VersusTowers[0].GetLevelSystem(),
      Modes.HeadHunters, MatchLengths.Standard)`; **the existing object is reused and mutated**, so whatever the player
      had picked in the menu (match length, variants) would carry over; in the harness it is the fresh default).
   3. `ms.LevelSystem = GameData.VersusTowers[0].GetLevelSystem()` (a new `VersusLevelSystem`);  `ms.Mode = LastManStanding`.
   4. `MainMenu.CurrentMatchSettings = ms`.
   5. (`TeamMode` is false for LMS: the `Teams[i] = i%2==0 ? Blue : Red` block is skipped; Teams stay `Neutral`.)
   6. `ms.Variants.ApplyVariants([ "NONE" ])` -> `MatchVariants.DisableAll()` (every `Variant.Value = false`; per-player
      variants: `playerValues[i] = false`) then the unknown "NONE" is ignored.
   7. `StateApi.Current.GenerateVersusLevels(ms, 0, 0)`: `VersusLevelSystem.levels = OwnGenLevel(...)`;
      `levelSystem.StartOnLevel(0)` (section 7).
   8. `StateApi.Current.SetVersusLevels(ms, null)`: with no paths it only `ScenarioLevels.Clear()`.
   9. `new TowerFall.Session(ms)`; `netplayManager.Init(session.RoundLogic)` (RoundLogic is null yet; test mode runs
      `NativeInit()` synchronously: `netplay_init` starts the GGRS **SyncTestSession** (2 players, check distance 2,
      `with_input_delay`); `StateApi.SetFrameDriver("TF.EX")`; `_isInit = true`). `StateApi.ResetSfx()`.
   10. `session.StartGame()` (`TowerFall.Session.StartGame`), see below. Then `Commands.Open = false`.

`TowerFall.Session.StartGame` is patched by two prefixes (both in test mode):
`TF.State.Patchs.SessionStatePatch.Session_StartGame`: `CalcPatch.Reset(); rngService.Reset()` (**re-seeds the gameplay
xoshiro from the stored seed 42, discarding anything drawn since step 3**); `StateContext.Reset()` (new State session);
`ServiceCollections.ResetState(); PurgeCache();` and, because the test flag is set,
**`MatchSettings.RandomLevelSeed = rngService.GetSeed()` = 42**. And `TF.EX.Patchs.SessionPatch.Session_StartGame`
(`StateApi.SetInstantReplay(isExMatch && ...)`, `InstantReplayFootage.ResetMatch()`; replay UI only).
The PC body then does `SaveData.Instance.Stats...++` (irrelevant), `TreasureSpawner = LevelSystem.GetTreasureSpawner(this)`
and `Engine.Scene = new LevelLoaderXML(this)`.

Then the level loads over several frames (`LevelLoaderXML.Load` coroutine; these frames are not netplay ticks), and the
first `Level` becomes the scene.

## 3. MatchSettings after setup (measured, `[xplay2setup]` lines from a run)

Dumped at the end of `Session.StartGame`:

| field | value | note |
|---|---|---|
| `Mode` | `LastManStanding` (int 3) | vanilla mode; **not** TF.EX's custom netplay mode, so the round logic is vanilla `LastManStandingRoundLogic` |
| `MatchLength` | `Standard` (2) | from `GetDefaultVersus()`; multiplier 1.0 |
| `GoalScore` | **2** | `PlayerGoals` forced to 2 in test mode (vanilla would be 5 for 2-player LMS) |
| `AutobalanceLosingAmount` | 3 | vanilla: LMS with 2 players -> 3 |
| `TeamMode` / `SoloMode` / `PlayerLimit` | false / false / 4 | |
| `Teams` | Neutral x4 | |
| `RandomVersusTower` | false | |
| `RandomLevelSeed` | 42 | set by `Session_StartGame` prefix. Only used for procedural towers; Sacred Ground is not procedural |
| `QuestHardcoreMode`, `DarkWorldDifficulty` | false, Normal | defaults |
| `TreasureRates` | null | default |
| `Variants` | all off (90 entries in the browser build incl. DLC and custom variants; none on) | vanilla defaults + `DisableAll()` |
| `LevelSystem` | `VersusLevelSystem`, `ID = {0,0}`, theme "SACRED GROUND", `Procedural = false` | |
| `LevelSystem.ShowControls` / `ShowTriggerControls` | **false** (vanilla: `ShowControls = (ID.X == 0)` is true for Sacred Ground) | forced by `LevelLoaderXMLStatePatch.LevelLoaderXML_ctor` whenever capture is active |
| `VersusTowerData.FixedFirst` | true (`tower.xml`: `<fixedFirst/>`) | first level stays first |
| `MainMenu.CurrentMatchSettings == VersusMatchSettings` | same object | |

## 4. Teams / colours / skins

No teams (FFA). Archer = seat index: `TFGame.Characters = {0,1,2,3}` (Green, Blue, Pink, Orange: set at TFGame start-up, `TFGame.Load`),
`TFGame.AltSelect = {0,0,0,0}` (no alt skins). Nothing in test mode changes them (TF.EX's lobby path does: `ArcherService.ApplyToGame`).
`Player` is constructed with `Allegiance.Neutral` (-1) for both allegiance arguments.

## 5. Players and inputs

- `TFGame.Players = {true, true, false, false}`, `TFGame.PlayerAmount = 2`.
- `TFGame.PlayerInputs` in the browser run: `[KeyboardInput, FakeController, null, null]` (slot 0 is whatever the page assigned,
  slot 1 was empty so TF.EX put a `FakeController`). `MenuInput.MenuInputs = [KeyboardInput, null, ...]`.
  **None of this matters for gameplay in the scripted run**, because the harness replaces the input state after
  `GetState()`; but it matters for the intro (section 9: `MenuInput.Confirm`).
- How TF.EX normally feeds inputs: `KeyboardInput.GetState` / `XGamepadInput.GetState` have Harmony postfixes
  (`KeyboardInputPatch.KeyboardInput_GetState`, `XGamePadInputPatch.XGamepadInput_GetState`) that replace the result with
  the netplay input array's entry for the seat (`InputService.GetCurrentInput(seat)`, converted `Input.ToTFInput()` then
  `RightStickShot.Apply`); `FakeController.GetState` returns that entry only when `InputScripter.Enabled && IsTestMode`
  (else a zero `InputState`). The array is filled each tick by `NetplayManager.AdvanceGameState` (GGRS inputs, delayed by
  the input delay, restored on rollback). So `PlayerInputs[i].GetState()` in `Player.Update` already returns the netplay
  array under TF.EX's frame driver (`RightStickShot.Apply` is the identity unless TF.EX's custom variant `RIGHT STICK SHOT` is on; it is off, and the only other reader of the
  array in `Player.Update` is TF.EX's postfix that kills a player whose array entry says `disconnected`, always 0 here). **Overriding the result of that call at the call site (what the Switch probe does) is
  therefore a valid point**, and it wins over the array. It must be a pure function of the tick (section 9.2), otherwise the
  sync test's resimulated ticks would see different input than the first pass.
- TF.EX also has its own scripted-input path for scenarios (`InputScripter`: `GetPolledInput` pushes scripted `Input` for
  all seats into GGRS via `netplay_set_test_inputs`), which the `scenario` command uses. It is time-origin and input-delay
  dependent, so the harness does not use it.

## 6. Seeding and the gameplay RNG

TF.State's gameplay RNG (`TF.State.Domain.Randomness.DeterministicRandom`, `System.Random` subclass):

```
Seed(ulong seed):  x = seed; s0..s3 = splitmix64(&x) four times; if all zero, s0 = 0x9E3779B97F4A7C15
splitmix64(x): x += 0x9E3779B97F4A7C15; z = x; z = (z ^ (z>>30)) * 0xBF58476D1CE4E5B9; z = (z ^ (z>>27)) * 0x94D049BB133111EB; return z ^ (z>>31)
NextULong():  xoshiro256**: result = rotl(s1*5, 7) * 9; t = s1 << 17; s2 ^= s0; s3 ^= s1; s1 ^= s2; s0 ^= s3; s2 ^= t; s3 = rotl(s3, 45)
Sample() / NextDouble():  (NextULong() >> 11) * 2^-53
Next(max) = (int)(Sample() * max);   Next(min, max) = (int)((long)(Sample() * ((long)max - min)) + min)
```

Seed 42 gives `s = bdd732262feb6e95 28efe333b266f103 47526757130f9f52 581ce1ff0e4ae394`, fold (s0^s1^s2^s3) `8a7657bd80c8e350`.
**The harness log's `rng=` is that fold.**

Timeline of the seed (measured; fold after each checkpoint, and the number of draws it equals, found by stepping a Python xoshiro):

| when | fold | draws since seed |
|---|---|---|
| `SetSeed(42)` in `StartTestMode`, then `Session.StartGame` prefix `Reset()` (same state again) | `8a7657bd80c8e350` | 0 |
| after `Session.LevelLoadStart` (round 0) | `511be9f7f91d765c` | 2 |
| after `Session.OnLevelLoadFinish` (round 0) | `68393fba09e21e52` | 10 |
| after `Session.StartRound` (round 0), i.e. the tick before tick 0 | `69710fca3ad62567` | 11 |

and the same fold is the log's value from T=-265 (10 draws) .. T=-1, and `69710fca3ad62567` from T=0 until something draws
again (nothing does for the first 1,900+ ticks of the test script, so it is also the value through round 0's fight).
The 11 draws, in order, with their callers (from `rngtrace`, call stacks, first pass only):

| draw | caller |
|---|---|
| 1 | `TowerFall.TreasureSpawner.orig_GetRandomArrowType` -> `Calc.Choose(Random, ...)` (called by `Session.LevelLoadStart` for `RoundRandomArrowType`; `TreasureSpawner.Random` is replaced by the gameplay RNG in `LevelLoaderXML` ctor) |
| 2 | `TF.State.Patchs.Entity.TreasureSpawnerPatch.TreasureSpawner_GetRandomArrowType_Prefix` -> `Calc.Choose<ArrowTypes>(Random, list)` (TF.EX re-picks from [orig result + safe modded arrows]; with no mods the list has one element but the draw still happens) |
| 3-7 | `TowerFall.Chain..ctor` (`Level.LoadEntity` of the level's `Chain`; 5 draws; `ChainPatch` registers the gameplay RNG around the ctor) |
| 8-10 | `Calc.Shuffle` of the PlayerSpawn list: `TF.State.TowerFallExtensions.CalcExtensions.OwnVectorShuffle` in `RoundLogicPatch.RoundLogic_SpawnPlayersFFA` (3 draws for 4 spawns: `Calc.Shuffle` = `while (--n > 0) swap(list[n], list[random.Next(n+1)])`) |
| 11 | `TowerFall.TreasureSpawner.GetChestSpawnsForLevel` -> `Calc.Chance` (from `RoundLogic.SpawnTreasureChestsVersus`, in `Session.StartRound` -> `LastManStandingRoundLogic.OnRoundStart`) |

Which parts of the game draw from the gameplay RNG: `Calc.Random` is swapped to the gameplay generator only while a
registration is open (`CalcPatch.RegisterRng`), with `StateFlags.IsCaptureActive`. The classes whose patches open a registration
(`grep RegisterRng` over the decompiled TF.State/TF.EX patch DLLs; each is a prefix/postfix pair around the named methods, whose
exact method lists are in the decompile): `TreasureSpawner` (`GetChestSpawnsForLevel`, `GetArrowShufflePickups`; `GetRandomArrowType` uses the
spawner's own `Random` = the gameplay RNG), `RoundLogic.SpawnPlayersFFA` / `SpawnPlayersTeams`, `OrbLogic`, `Orb`, `TreasureChest`, `Crown.Update`,
`WoodenHat.Update`, `SteelHat.Update`, `Player.DropArrow` / `Player.LoseHat`, `Arrow` (`Drop`, `EnterFallMode`), `ArrowCushion`, `PlayerCorpse`, `Chain`
ctor, `DummyHead`, `Spikeball`, `Miasma`, `Lava` / `LavaControl`, `MatchVariants.Randomize`, `DarkPortalsVariantSequence`. `ParticleSystem.Emit/Update` and
`Lava.BubbleComplete` do the opposite (`IgnoreToRegisterRng`: cosmetic). `Calc.PushRandom()` (no seed argument) inside an open registration
pushes `Calc.Random.Next()` of the gameplay RNG (`CalcPushRandomPatch`).

Everything else uses the **cosmetic** `Calc.Random`, which is not synchronized in netplay. It is `new Random(42)` after
`SetSeed`, and `LevelLoaderXML.Load` replaces it per level: `Calc.Random = new Random(Level.LoadSeed)` where `LoadSeed` is the
sum of the UTF-16 code units of the level path string (`VersusLevelSystem.GetNextRoundLevel`). Measured: 4102 for
`Content/Levels/Versus/00 - Sacred Ground/00.oel` with `/` separators (a Windows path with `\` would give 4282). It only
drives cosmetic things (tile variants via `Calc.PushRandom(Level.LoadSeed)` in `LevelTiles`/`LevelBGTiles`, intro tweens) and
does not need to match.

## 7. Level selection, spawns

`VersusTowerData.GetLevels(ms)` (LMS): every `.oel` of `Content/Levels/Versus/00 - Sacred Ground/` (files sorted with `Array.Sort`)
whose `PlayerSpawn` count >= `TFGame.PlayerAmount` (2). Files: `00..10.oel`; `10.oel` has no PlayerSpawn, so the eligible list is
`00 01 02 03 04 05 06 07 08 09` (measured).

`OwnGenLevel` (`TF.State.Domain.VersusLevelSystemExtensions`, called from `TfStateApi.GenerateVersusLevels` at step 6.7 and from the
`VersusLevelSystem.GenLevels` prefix `VersusLevelSystemPatch` whenever the list runs empty): `levels = GetLevels(ms)`; `FixedFirst &&
lastLevel == null` -> remove element 0, `Calc.Shuffle(levels, new System.Random(seed))` (the .NET seeded generator, **not** the xoshiro),
insert it back at 0. Then `StartOnLevel(0)`: `path = VersusTowerData.Levels[0].Path; levels.Remove(path); levels.Insert(0, path)`
(`VersusLevelSystem.lastStartedLevel = 0`). Measured result for seed 42, consumed one per round by
`VersusLevelSystem.GetNextRoundLevel`:

`00, 05, 06, 03, 08, 09, 04, 01, 02, 07` (the dump printed `levels` = `05 06 03 08 09 04 01 02 07` right after `StartOnLevel(0)`; the `[xplay2setup] after Session.LevelLoadStart` lines name each round's file as `level=`).

(Vanilla uses `new Random()` here, and vanilla `GenLevels` runs lazily on the first `GetNextRoundLevel`.)

Spawns of `00.oel`: `PlayerSpawn` (60,210) (260,210) (60,90) (260,90); `LevelLoaderXML` loads the level's entities in file order. At
`SpawnPlayersFFA` (TF.State replacement, `RoundLogicPatch.RoundLogic_SpawnPlayersFFA`): positions = `GetXMLPositions("PlayerSpawn")`
(topped up from TeamSpawnA/B if fewer than PlayerAmount, not needed here); `CalcPatch.RegisterRng(); list = OwnVectorShuffle(list)`
(gameplay RNG, 3 draws); for each seat j in 0..3 with `Session.ShouldSpawn(j)`: if `num2 == 0 && PlayerAmount == 2 && list[0].X != 160`
and `WrapMath.Opposite(list[0])` (= `(160 + (160 - x), y)`) is in the list, `list[1] = Opposite(list[0])`; position
`list[num2] + (0,2)`; `new Player(j, pos, Neutral, Neutral, GetPlayerInventory(j), GetSpawnHatState(j), frozen: true, flash: true, indicator: true)`.
Measured for seed 42: p0 at (60, 212), p1 at (260, 212) (float bits `42700000 43540000`, `43820000 43540000`).
Round 0 inventories: 3 arrows each, no shield, normal hat (`Session.GetPlayerInventory`: scores equal; autobalance needs a lead of 3).

## 8. Frame driver, time step, and what TF.EX overrides every tick

TF.EX replaces `TFGame.Update` while a session is active (`TF.EX.Patchs.Engine.TFGamePatch.TFGameUpdate_Patch`, "frame driver TF.EX"):

- `ManageTimeStep` each update: `EnableFixedTimeStep(true)`; **`Coroutine.OneFrameDelay = true`** (restored to
  `!SaveData.Options.VerticalSync` when the session ends); `Game.TargetElapsedTime = TimeSpan.FromTicks(Round(10^7 / fps))` =
  **166667 ticks** at 60 (measured `target_elapsed_ticks=166667`; at the very start it is still 41666 = 240 Hz until the first
  Level update).
- It advances game ticks from a wall-clock accumulator in steps of `TargetElapsedTime` (so real time = 60 ticks/s; not relevant to
  determinism), one GGRS request loop per step (`TFGamePatch.NetplayLogic`): `SaveGameState` (capture, `StateApi.CaptureGameState`) / `LoadGameState`
  (`RestoreGameStateBytes`) / `AdvanceFrame`. Every `AdvanceFrame` does `netplayManager.AdvanceGameState()` (the Rust side increments the frame counter and
  fills the input array), then `Scene.FrameCounter = (float)netplay_current_frame()` (**set by assignment**). The ticks re-run after a rollback call
  `MInput.Update()` and `level.Update()` directly; the final (new) tick of a step goes through `TFGame_Update_orig`, i.e. the vanilla `TFGame.Update` -> `Engine.Update` -> scene update.
- `TF.EX.Patchs.Scene.LevelPatch.Level_Update__Prefix`, before every `Level.Update` when capture is active:
  ```
  float num = (float)Engine.Instance.TargetElapsedTime.TotalSeconds * Engine.TimeRate;   // (float)0.0166667 * 1f = 0.0166667f (0x3c88889a)
  Engine.TimeMult   = num * 60f;                                                          // 1.0000019 (0x3f800010)
  Engine.DeltaTime  = num;
  Engine.DeltaTicks = TargetElapsedTime.Ticks;                                            // 166667
  ```
  Measured: `timemult=3f800010` in every checksum line. The PC `Engine.Update` would set `TimeMult = 1f * TimeRate`,
  `DeltaTime = 1/60f * TimeRate` in fixed-step mode (and the Switch's 1.4.1.6 `TimeMult = TimeRate`): **these must be overridden to the
  values above before each Level.Update**, otherwise every `* Engine.TimeMult` term (physics, counters) differs by 1.9e-6 per tick.
  `Engine.LastTimeMult` is set by `Engine.Update` (= previous tick's TimeMult) and is only used by `Scene.OnInterval`.
- `Scene.Update` then does `FrameCounter += Engine.TimeMult` (PC code), so during `Level.Update` the counter reads
  `(float)ggrsFrame + 1.0000019f`. `ggrsFrame` is **1 for the first Level.Update after the match starts and increases by one per
  Level.Update; it is not reset between rounds** (a new `Level` object gets the global number: the netplay frame counter only advances in
  Level scenes, so the loading screens between rounds do not count). `Scene.OnInterval` uses `FrameCounter` (`SceneIntervalPatch`
  only rescales it when the tick rate is above 60, so at 60 Hz it is vanilla).
- Sync test (GGRS `SyncTestSession`, check distance 2): after each new tick N the session requests a load of N-2 and a
  resimulation of N-1 and N, comparing state checksums (a mismatch ends the test with `Sync test desync detected`). Measured: ~2
  resimulated `Level.Update` calls per tick (`resim=` in the checksum lines).
- TF.State patches that exist only for state capture (restore of entities, coroutines, SFX, particles, backgrounds, ...) are
  not relevant to a forward-only run, except for the behaviour changes listed in section 11.

## 9. The intro and tick 0

### 9.1 Round 0: 265 ticks before anyone can act (measured)

`LastManStandingRoundLogic.OnLevelLoadFinish` adds a `VersusStart` HUD and spawns both archers `Frozen` (`SpawnPlayersFFA`). `VersusStart.SetupSequence`
(a coroutine, updated once per Level.Update through `Coroutine.Update`), round 0, with TF.EX's patches in place:

1. `IntroControls` (Sacred Ground's controls screen, waits for `MenuInput.Confirm`) and `TriggerArrowScreen` are **skipped**: `ShowControls` /
   `ShowTriggerControls` were forced false (section 3).
2. `SessionIntroSequence` runs (title card "SACRED GROUND / LAST MAN STANDING": `yield return 30`, a 10-tick and a 30-tick tween, then
   **`while (!MenuInput.Confirm) yield return null`**, then a 30-tick fly-out tween). In a session `KeyboardInput.get_MenuConfirm` (and the `XGamepadInput` one)
   is patched (`KeyboardInputPatch.Intercept`, `if (netplayManager.IsInit()) return true`) to return **true on every call**, so the wait falls through at
   once. On the Switch `MenuInput.Confirm` must therefore be forced true for the intro (or the wait skipped); a real
   button press would also work but is not reproducible.
3. `IntroSequence`: "ROUND 1" (30), arrows in (20-tick tween, 30), "FIGHT!!" (10), 50, 10, 30; then `Session.StartRound()` (`RoundLogic.OnRoundStart` ->
   `SpawnTreasureChestsVersus` (draw 11), `Player.StopFlashing()`, `Player.Unfreeze()` for every player: `State = Normal`) and the HUD removes itself.
   With `Coroutine.OneFrameDelay = true` each timed `yield return N` costs one extra tick (`Coroutine.Update`: after a wait counter reaches 0 a `delay = 1`
   is applied).

Measured: `Session.StartRound` is called during Level.Update **#265** (GGRS frame 265, in the HUD's update, after the players' own updates of that tick),
and the players' first `Player.Update` with `State != Frozen` is Level.Update **#266**. So:
**tick 0 = Level.Update #266 = GGRS frame 266**; the 265 earlier ticks are `T = -265 .. -1` in the log. During them both archers are `Frozen`
(positions fixed at the spawn, speed 0, RNG fold `68393fba09e21e52` = 10 draws; at T=-1 the StartRound draw is not yet done: the fold changes
from T=-1 to T=0).

The check that the Switch intro is the same: it must call `Session.StartRound` in its 265th `Level.Update` of the match.

Later rounds (new `LevelLoaderXML` + `Level`): `VersusStart` has no `SessionIntroSequence` (it is `RoundIndex == 0` only), just `IntroSequence` (160 ticks of
waits plus the one-frame delays). Measured with the test script (log `U` lines = first non-Frozen `Player.Update` per seat and round): round 1's first Level.Update is
T=2154 (`A ... round_start`) and both archers are first non-Frozen at **T=2321**, i.e. 167 ticks later; round 0's round ended (`round_ending`) at T=1942, so the gap from
the last kill's `round_ending` to the next round's first tick was 212 ticks (end counter, results screen, level load). Script input keeps applying through all of it (section 9.2).

### 9.2 Script semantics (what the harness does; the Switch must match)

A script is `frame:flags:moveX:moveY` steps joined by commas, each held until the next step (the last step is held forever); `flags` hex: `1` jump, `2` shoot,
`4` dodge, `8` alt shoot, `0x10` arrows. `-` is no input. For each seat s and tick `T >= 0`:

```
cur  = step of script[s] with the largest Frame <= T        (none -> no override at all)
prev = the same for T-1                                      (none before tick 0)
InputState: JumpCheck = cur.flag1; JumpPressed = cur.flag1 && !prev.flag1; same for Shoot (2), Dodge (4), AltShoot (8);
            ArrowsPressed = cur.flag(0x10);                  (a held flag, true every tick)
            MoveX = cur.moveX; MoveY = cur.moveY;
            AimAxis = (moveX, moveY), each multiplied by 0.70710677f when both are non-zero
```

It is applied in `TowerFall.Player.Update` to the value of `TFGame.PlayerInputs[PlayerIndex].GetState()` for seats 0 and 1, **on every tick
`T >= 0` including the frozen intro ticks of later rounds** (frozen archers still turn to face `MoveX`: `if (State == Frozen) Facing = Sign(moveAxis.X)`), and not at all for `T < 0`
(there the game's own, neutral input stands). It is a pure function of the tick: `pressed` is derived from the script at T and T-1, never from "the previous call",
so a rollback re-run, a dead archer, or a respawned `Player` object changes nothing. `T` counts Level.Update ticks since tick 0 (section 8) across all rounds.

### 9.3 Difference to the old `CrossplayTest.cs` (`xplay`) rule

The single-player harness set its origin at the first `NormalUpdate` *call* and counted `Override` calls of that one `Player` instance, so its "frame 0" is the
**second** `Player.Update` with a non-frozen state (the first one's override ran before `NormalUpdate` set the origin): **old frame f = this document's T = f+1**, and
the input of T=0 can never be applied. It also stops counting while that player is dead and keeps a per-call `prev*` state. The Switch probe should use the definition
of section 9.2 (stateless in T, origin = first non-Frozen `Player.Update`); if it keeps the old rule, give both scripts a neutral first step of 1 tick and shift
every other frame number by -1.

## 10. What differs between test mode, a normal online match, and a vanilla Switch menu match

| aspect | TF.EX test mode (this test) | TF.EX online match (lobby) | vanilla Switch menu match |
|---|---|---|---|
| entry | `LaunchTestMode` -> `StartGame` -> `Session.StartGame`; no menu scenes | rollcall/archer select/`MapScene.StartSession` -> `Session.StartGame` | same menus, no TF.EX |
| `Mode` / RoundLogic | `LastManStanding` / `LastManStandingRoundLogic` | TF.EX custom netplay mode (`Modes.IsNetplay()`), `NetplayRoundLogic` (adds the same `VersusStart`, own end-of-round code) | chosen mode |
| `GoalScore` | 2 (`PlayerGoals` forced) | from lobby `MatchLength` (5 for std 2-player LMS; `Local` mode 10) | from menu |
| `MatchLength`, variants | defaults (Standard, none): the `VersusMatchSettings` object as it is | from the lobby (`ApplyVariants`, `MatchLength` from `GameData`) | from menu, persists between matches |
| seed | the test argument (42) | lobby `GameData.Seed` (server-chosen; `MainMenuPatch` lines `SetSeed(...)` at lobby create/update/rematch) | none: `new Random()` everywhere |
| gameplay RNG | seeded + reseeded at `Session.StartGame` | same mechanism | vanilla `Calc.Random` (time-seeded) |
| level order | `OwnGenLevel(seed)`; first level = `StartOnLevel(startLevel)` | `OwnGenLevel(seed)`; map from lobby (`MapId`, -1 random: `GetRandomVersusTower` shuffled with the seed) | `GenLevels` with `new Random()` |
| `RandomLevelSeed` | seed | seed (`Session_StartGame` prefix) | `MapScene.CustomSeed` |
| controls/intro screens | forced off (`ShowControls=false`), Confirm forced true | same patches | shown, wait for a press |
| inputs | scripted (harness) / `FakeController` + GGRS sync-test inputs | real, via GGRS | local controllers |
| characters / skins | `Characters={0,1,2,3}`, `AltSelect=0` (TFGame defaults) | from the lobby (`ArcherService.ApplyToGame`) | archer select |
| frame driver | TF.EX's (accumulator, GGRS requests, `TimeMult` override, `FrameCounter` assignment, `OneFrameDelay=true`) | same | `Engine.Update` fixed step |
| state saves / rollback | every tick saved; sync test rolls back 2 ticks every tick | saves every tick, rolls back on mispredicted input | none |
| unlocks | neutralised inside TreasureSpawner/OrbPickup (section 11) | same | the save's own |

What a Switch match started from the normal menu must override to follow this test: mode LMS + Standard + goal 2; no variants; tower 0 with levels
`00,05,06,03,08,09,04,01,02,07`; spawn shuffle/treasure/chain RNG from the xoshiro gameplay RNG seeded 42 and reseeded at match start; archers 0 and 1 (Green,
Blue), no alt skins, players 2 only; intro screens skipped and Confirm true; `OneFrameDelay` true; the per-tick `TimeMult/DeltaTime/DeltaTicks/FrameCounter`
overrides; the vanilla-behaviour patches of section 11.

## 11. Other behaviour changes active in test mode (partial audit)

Read and confirmed to change vanilla gameplay behaviour in this scenario:

- `Arrow.EnforceLimit` is skipped while capture is active (`ArrowPatch.Arrow_EnforceLimit`: `return !IsCaptureActive`): no vanilla limit on lying arrows.
- `Calc.PushRandom()` redirect, `RegisterRng` sites (section 6); `TreasureSpawner.Random` = gameplay RNG; `GetRandomArrowType` is re-picked with 2 draws.
- `DeterministicMathPatch` replaces `Math.Sin/Cos/Atan2/Pow` by `DeterministicMath.*` in `Calc.AngleToVector`, `Calc.Angle` (both overloads), `SineWave.Update/ValueOffset/StartUp/StartDown`,
  `Wiggler.Update`, `Particle.Update`, `Arrow.ArrowUpdate`, `Player.DodgingUpdate`, `LevelRandomGeometry.GenerateData`, `LevelRandomItems.AddItems`, `LevelRandomBGDetails.GenCataclysm` and the
  Ease functions `ExpoIn`, `SineIn`, `SineOut`, `SineInOut`.
- `LocalSettingsGuard.Neutralize()` around `TreasureSpawner` ctor, `GetRandomArrowType`, `GetArrowShufflePickups`, `OrbPickup.OnPlayerCollide`: `Options.RemoveScrollEffects = false`,
  `Unlocks.SunkenCity = Unlocks.TowerForge = Unlocks.Ascension = true`, `GameData.NoDarkWorldOverride = false` (restored afterwards). Measured save state in the browser: those three
  unlocks are false, so this changes which arrows/pickups exist inside those calls.
- `Pickup` ctor/`FinishUnpack`: TF.State replaces the unpack tween (30 ticks, `Ease.BackOut`) and adds `FinishedUnpack`/`TargetPosition`; `Session.LevelLoadStart`: `Session.Miasma = Miasma.Default()`.
- `RoundLogicPatch.OnUpdate` prefix: miasma bookkeeping; `LastManStandingRoundLogic.OnRoundStart` (test mode only) writes an entity dump file (no gameplay effect).
- **Vanilla kill-cam replay is off.** `TF.EX.Patchs.Scene.LevelPatch.Level_ctor` sets `Level.ReplayRecorder = null` in test/netplay/replay sessions (vanilla creates it in the `Level`
  ctor unless `Options.ReplayMode == Off`), so `Session.EndRound` goes straight to `CreateResults()` instead of the rewind replay (`ReplayViewer.Watch`). A native run must not play it either.
- **TF.EX's instant replay** (`InstantReplayFootage`, on when the saved `Options.ReplayMode != Off`, i.e. by default; it needs ~60 ticks of round results first, then bakes footage from state
  keyframes and plays it while game ticks are paused) is outside the simulation: measured, a run with `replay=off` has exactly the same `T`/`A`/`U` lines as the default run. In the console it shows
  as bursts of `GotoNextRound/LevelLoadStart/OnLevelLoadFinish` `[xplay2setup]` lines at one GGRS frame (the baking); they are not rounds.
- `LevelLoaderXMLStatePatch.Reset(session)` each round: `Screen.Offset = 0`, state caches reset, `RoundEndCounter` counters reset to `Constants.INITIAL_END_COUNTER`, `done = false`, `Level.Ending = false`.

The remaining TF.State/TF.EX Harmony patches (about 140 patch classes; list: `grep HarmonyPatch` over the decompiled `TF.State.Patchs` and `TF.EX.Patchs`) are mostly
state capture/restore, rendering or menu code; they were **not read line by line**. Entities with their own patch classes (`Bat`, `Slime`, `Icicle`, `Spikeball`, `MovingPlatform`, `LoopPlatform`,
`RotatePlatform`, `ShiftBlock`, `SensorBlock`, `TechnoMage`, `ChaliceGhost`, `PlayerGhost`, `QuestSpawnPortal`, `TeamReviver`, `YellowArcherStatue`, `HeavyPhysicsObject`, ...) are not exercised by
Sacred Ground's LMS rounds except `Chain`, `Player`, `Arrow`, `PlayerCorpse`, `Pickup`, `TreasureChest` (if the spawner places any) and the round logic. If the logs diverge, the log's RNG column says at which tick a draw
happened or was missing, and `rngtrace=N` names the callers of the first N draws.

## 12. Caveats

- The browser trajectory is TF.EX's own test-mode run: every tick is captured and, after each tick, the last 2 ticks are restored from the state and re-run before the game moves on.
  The harness logs the first pass of each tick (the high-water mark of GGRS frames), but the game *continues* from the resimulated objects. That is by design identical to a plain forward
  run if state restore is lossless (the sync test checks exactly that with a state checksum, and passed for every run here). Anything the checksum does not cover and restore loses would show up as a
  browser-vs-Switch divergence that is not a Switch bug.
- Cosmetic randomness (`Calc.Random` outside registrations) is deliberately not compared.
- `Level.LoadSeed` and everything derived from the level path string differs with the path format (section 6).
- Only seed 42, map 0 was exercised; seeds change the draws and the level order (the order list above is for 42).

## 13. The harness (browser repo, branch `exp/xplay-versus`)

`xplay2 <script0> <script1> [seed=42] [maxticks=6000] [rngtrace=N] [impure=1] [replay=off]` (needs `?fortrise&tfexfps=60`, TF.EX enabled; once per page load; `?command=xplay2%20...` runs it when the menu is up). It installs the patches and calls TF.EX's
`LaunchTestMode(["lms","0","0",seed,"2","#NONE#","2"])`. Log lines (one console line each; `[xplay2]` lines are first pass only):

```
[xplay2] T <tick> rng=<s0^s1^s2^s3 of the gameplay xoshiro, 16 hex> p0 <x> <y> <sx> <sy> alive=<0|1> p1 <x> <y> <sx> <sy> alive=<0|1>
```
x y = `Player.Position`, sx sy = `Player.Speed`, as float bits (8 hex). Values are read at the **start** of the tick (a `Level.Update` prefix, before any entity runs and before the
tick's input is applied) and printed after it. A seat with no living `Player` entity (dead, or not yet spawned) prints `00000000` four times and `alive=0`.
`[xplay2pre] T <negative> ...` is the same line for the intro ticks before tick 0 (up to 400 of them, printed once tick 0 is known).
`[xplay2] origin ggrs_frame=<n>` once. `[xplay2] cfg ...` (target elapsed ticks, OneFrameDelay, first GGRS frame, `Level.LoadSeed`).
`[xplay2] U <tick> seat=<s> round=<r>`: first non-Frozen `Player.Update` of a seat in a round.
`[xplay2] A <tick> round=<r> [round_start|round_ending] scores=<4 ints> arrows=<n> sumx=<16 hex> sumy=<16 hex> quivers=<n0>,<n1> timemult=<8 hex> resim=<n>`:
at every 300th tick and at each round start/end: the number of live `Arrow` entities, the sums of their `Position.X` / `Position.Y` **bit patterns** (uint, summed mod 2^64; order independent), each archer's arrow count
(`-` if no `Player`), `Engine.TimeMult` bits, and how many `Level.Update` calls so far were resimulations. `[xplay2setup]` lines are the one-time dump of section 3/5/6.

### Evidence (browser runs, headless Chrome, `tools/netplay-driver.mjs`, the same 2 scripts of 5,200 ticks, seed 42, 5,000 logged ticks, two rounds and the match end)

- The log is bit-identical across independent runs (a fresh browser process each time): three pairs of runs with the final scripts (r4/r5, r6/r7, r9/r10: all lines, including setup and U/A lines, same `sha1`), plus a pair
  with the first, shorter script; later builds of the harness (r11, r12) and a run with the game's Replay option off give the same `T`/`A`/`U`/pre lines as r9. No `Sync test desync` message in any of them;
  ~2 resimulated `Level.Update` calls per tick (`resim=` 12,383 at T=4800), i.e. the rollback sync test ran, and passed, with the harness on.
- Negative control (`impure=1`, script indexed by call count instead of tick): TF.EX's sync test reports `Sync test desync detected : GGRSError : Detected checksum mismatch during rollback on frame 277` at T=11, with
  `Players[1].Speed (-1.5, 0) != (-1.0000019, 0)` in the diff (that second number is also a direct view of `TimeMult`). So the check is live, and the pure-function input is what keeps it quiet.
- Match flow with the test scripts: round 0 ends at T=1942 (p0 wins, 1-0), round 1 starts at T=2154 on `05.oel`, round 1 ends at T=4016 (2-0, match over, results screens until the end of the log).
- Golden values (first ticks, final scripts): T=-1 `rng=68393fba09e21e52` both archers at (60,212) / (260,212) = `42700000 43540000` / `43820000 43540000`; T=0 `rng=69710fca3ad62567` same positions, speeds 0;
  T=1 p0 speed `be1999ad 00000000`, p1 position `43818000 43510000`, speed `bf26666b c04ccccd`; T=1000 p0 `42300000 43310000 3e23d71f 3ed66d20`, p1 `437e0000 43380000 3ff0a3d4 bfffffeb`.
- Files (scratch, not in a repo): `/private/tmp/claude-501/-Users-crab-Code-Towerfall-Switch-Mods/a86873af-5865-44d3-bb29-d370464237eb/scratchpad/evidence/` (script strings, generator, run script, full logs, the 800-line sample, the negative control);
  browser worktree `.../scratchpad/wt`, branch `exp/xplay-versus` (commits `7f4d734` = the 60 Hz diff, `c3dee1e` = the harness); the harness diff is `browser-60hz/xplay-versus-harness.diff` next to this file.
