# Gameplay RNG port: TF.EX determinism -> native Switch mod

Desk inventory only (nothing built or run on device). Goal: reproduce, bit for bit, the gameplay RNG stream that the
browser's TF.EX produces for a 2-player VERSUS match (LMS / TDM) on the netplay-safe maps at 60 ticks/s.

## 0. Sources, confidence, and how the two TF.EX copies compare

Paths. `TFEX` = `/private/tmp/claude-501/-Users-crab-Code-Towerfall-Browser--claude-worktrees-quickplay-mode-deep-link-cd26a5/44a5571c-b2ed-4dab-9857-d775d693a538/scratchpad/tfex-src/src`
(clone). `PATCHS` = `TFEX/state/TF.State.Patchs`. `PC` = `~/Code/Towerfall Browser/reference/decompile` (PC 1.3.3.3).
File:line cites below are the **clone**. The vendored v0.19.2 DLLs were decompiled with `ilspycmd` 9.1 into
`/private/tmp/claude-501/-Users-crab-Code-Towerfall-Switch-Mods/a86873af-5865-44d3-bb29-d370464237eb/scratchpad/dec/`
(`patchs/`, `domain/`, `core/`, `tfx/`, `exCore/`, `expatchs/`, `exdomain/`; scratch, may vanish). The Harmony attribute
argument lists for constructors do not decompile ("Could not decode attribute arguments"), so ctor parameter types come from the clone.

**Clone vs 0.19.2 DLL, RNG surface: no difference found.**
- Same set of `[HarmonyPatch]` classes in TF.State.Patchs (76 in both, zero name differences).
- Identical regex counts of `RegisterRng` / `UnregisterRng` / `IgnoreToRegisterRng` / `UnignoreToRegisterRng` in TF.State.Patchs (32/32/4/4 in both; the first count also matches the `IgnoreTo...` substring and the definitions).
- I read the DLL bodies of: CalcPatch, CalcPushRandomPatch, DeterministicRandom, RngService, StateContext, Rng, StateFlags, SessionStatePatch, LevelLoaderXMLStatePatch, VersusLevelSystemPatch, VersusLevelSystemExtensions, RoundLogicPatch, TreasureSpawnerPatch, ParticleSystemPatch, MiasmaPatch, SpikeballPatch, MatchVariantsPatch, OrbLogicPatch, DarkPortalsVariantSequencePatch, ArrowPatch, ChainPatch, Crown/SteelHat/WoodenHat, DummyHeadPatch, LavaPatch, LavaControlPatch, VersusMatchResultsPatch, CalcExtensions, NetplayRoundLogic, Constants.NETPLAY_SAFE_MAP. All logically identical to the clone.
- Not a full textual diff of the two trees; only the RNG-relevant surface was compared. Known non-RNG deltas: `Constants.CONNECTION_TIMEOUT_MS` exists only in the DLL.

**Important extra source (not in the brief): the browser's own FortRise patch DLL**
(`gamefiles/TowerFall/TowerFall.app/Contents/FortRise.app/Contents/MacOS/TowerFall.FortRise.mm.dll`, decompiled to `scratchpad/dec/mm/`).
The browser reference runtime = vanilla + FortRise + TF.EX, and FortRise changes RNG consumption in two places
(`patch_TreasureSpawner`, section 3 note R-FR). The Switch (no FortRise) must replicate the *combined* behaviour.

Not confirmable from here: whether the Switch binary equals PC 1.3.3.3 for every hooked method (signatures and bodies were checked only against the PC decompile).

---

## 1. How the gameplay RNG works

### 1.1 Generator
- `TF.State.Domain.Randomness.DeterministicRandom : System.Random` (`TFEX/state/TF.State.Domain/Randomness/DeterministicRandom.cs`; DLL identical).
- State = four `ulong` words `_s0.._s3`. Algorithm = **xoshiro256\*\*** (lines 83-96):
  `result = rotl(s1*5, 7) * 9; t = s1 << 17; s2 ^= s0; s3 ^= s1; s1 ^= s2; s0 ^= s3; s2 ^= t; s3 = rotl(s3, 45)`.
- Seeding `Seed(ulong)` (lines 49-58): four successive **SplitMix64** outputs fill s0..s3, in that order.
  SplitMix64: `x += 0x9E3779B97F4A7C15; z = x; z = (z ^ (z>>30)) * 0xBF58476D1CE4E5B9; z = (z ^ (z>>27)) * 0x94D049BB133111EB; return z ^ (z>>31)`.
  If all four words are 0, `_s0 = 0x9E3779B97F4A7C15` (cannot happen for real seeds).
- Both ctor overloads `(int seed)` and `(long seed)`, and `StateContext.SetSeed(int)`, call `Seed((ulong)seed)` (unchecked): an **int seed is sign-extended** to 64 bits (`-1` -> `0xFFFFFFFFFFFFFFFF`). The browser matchmaker only issues non-negative seeds `floor(random()*0x7fffffff)` (`cloudflare/tfex-server/matchmaker.js:293-295`), so sign extension is academic, but keep it.
- Output mapping (every call consumes exactly ONE `NextULong`, even for `maxValue` 0 or 1; unlike .NET's Random):
  - `Sample()` / `NextDouble()` = `(double)(NextULong() >> 11) * 2^-53` (const 1.1102230246251565E-16).
  - `Next(int max)` = `(int)(Sample() * (double)max)`; throws if `max < 0`.
  - `Next(int min, int max)` = `(int)((long)(Sample() * (double)((long)max - (long)min)) + min)`; throws if `min > max`.
  - `Next()` = `Next(0, int.MaxValue)` = `(int)(long)(Sample() * 2147483647.0)`.
  - `NextBytes(byte[])` overridden (little-endian 8 bytes per ulong). The .NET 6+ virtuals `NextInt64`, `NextSingle`, `NextBytes(Span<byte>)` are NOT overridden (not used by the game).
- Game helpers on top (`PC/Monocle/Calc.cs:382-437`): `Choose(params/List)` = `choices[Next(count)]`; `Range(int min,int add)` = `min + Next(add)`; `Range(float min,float add)` = `min + add*NextFloat()`; `NextFloat()` = `(float)NextDouble()` (double rounded to float32, keep single precision from there on); `NextFloat(max)` = `NextFloat()*max`; `Chance(float p)` = `NextFloat() < p`; `Shuffle(list, rnd)` = Fisher-Yates from the end: `for n=Count-1..1: i = rnd.Next(n+1); swap(list[n], list[i])` (Count-1 draws); `NextAngle()` = `NextFloat()*2PI`; `Range(Vector2,Vector2)` = X then Y `NextFloat` (2 draws).

### 1.2 Who creates it / when it is (re)seeded / from what
- One instance for the process lifetime: `StateContext._gameplayRandom = new DeterministicRandom(0)` (`TFEX/state/TF.State.Domain/Context/StateContext.cs:54`). `StateContext.Reset()` (line ~272) does NOT recreate it and does NOT touch the seed. `RngService.Gameplay` returns it.
- Seed source: `lobby.GameData.Seed` (int, from the matchmaker; the browser server draws a fresh seed per match start `matchmaker.js:352` and per rematch `:388`, quickplay lobby creation `:571`). `StateApi.SetSeed(seed)` is called whenever the lobby seed is learned/changes: `network/TF.EX.Patchs/Scene/MainMenu.cs:564, 830, 1684`, `network/TF.EX.Domain/Services/MatchmakingService.cs:889` (only if `lobby.GameData.Seed != GetSeed()`).
- `RngService.SetSeed(int seed)` (`state/TF.State.Domain/Services/RngService.cs:29-34`) does TWO things: `StateContext.SetSeed(seed)` (stores `_seed`, reseeds the gameplay generator via `Seed((ulong)seed)`) AND `Monocle.Calc.Random = new System.Random(seed)` (the **cosmetic** stream becomes a seeded .NET Random).
- `RngService.Reset()` (line 41-44) = `SetSeed(GetSeed())` i.e. reseed from the *current stored seed* (and replace cosmetic `Calc.Random`). Callers:
  1. `MapScene.StartSession` postfix, netplay only (`network/TF.EX.Patchs/Scene/MapScene.cs:46-58`, `ResetRng` at :57). Vanilla `MapScene.StartSession` = `new Session(MainMenu.CurrentMatchSettings).StartGame()` (`PC/TowerFall/MapScene.cs:713-717`), so this runs just before StartGame.
  2. `Session.StartGame` prefix (`PATCHS/Session.cs:13-30`): in order `CalcPatch.Reset(); rngService.Reset(); StateContext.Reset(); ServiceCollections.ResetState(); PurgeCache();` then, if `Mode.IsNetplay() || IsTestMode`, `MatchSettings.RandomLevelSeed = rngService.GetSeed()`.
  3. `MapScene.GetRandomVersusTower` postfix (random-tower pick, `MapScene.cs:138`).
  4. Test/scenario commands (not relevant).
- **Granularity: one stream per match.** Seeded at `Session.StartGame`; NOT reseeded per round or per level. Rounds 2..N continue the same stream. (A rematch/series game calls StartGame again with the new lobby seed -> reseed.)
- Net effect at the first gameplay draw: state = `Seed((ulong)(long)lobbySeed)`, no draws consumed between reseed and the first level-load draw (see 3.4).

### 1.3 What is saved/restored each tick (rollback)
- `TF.State.Domain.Models.Rng` (`state/TF.State.Domain/Models/Rng.cs:7-29`, MessagePack): `Key0 int Seed`, `Key1..4 ulong S0..S3`. **Only the seed int + the 4 xoshiro words.** Snapshot: `StateContext.GetRng()` (:99-104), restore: `UpdateRng` (:106-110) which sets `_seed` and calls `Restore(state)`. Written into `GameState.Rng` by `Level.SaveState` (`state/TF.State.TowerFallExtensions/Level.cs:220`), read back at `Level.cs:822` (`rngService.LoadState`).
- NOT saved: cosmetic `Calc.Random`, `CalcPatch` depth counters and `_cosmeticBackup`, `SFXVaried.chooseFrom`, TreasureSpawner (it only holds a reference to the shared generator), `Arrow`/`Alarm`/`Tween` pools (flushed/cleared in capture mode instead). Useful for the Switch: `GameState.Rng` is logged every tick in the browser, so a per-tick RNG golden trace already exists on the browser side.

### 1.4 The swap: `CalcPatch` (`PATCHS/Calc/Calc.cs`; DLL `TF.State.Patchs.Calc.CalcPatch`)
State: `_gameplayDepth`, `_ignoreDepth` (ints), `_overriding` (bool), `_cosmeticBackup` (Random).
- `Apply()` (:14-31): `want = _gameplayDepth > 0 && _ignoreDepth == 0 && StateFlags.IsCaptureActive`.
  `if (want && !_overriding) { _cosmeticBackup = Calc.Random; Calc.Random = RngService.Gameplay; _overriding = true; }`
  `else if (!want && _overriding) { Calc.Random = _cosmeticBackup; _cosmeticBackup = null; _overriding = false; }`
- `RegisterRng()`: `_gameplayDepth++; Apply()`. `UnregisterRng()`: `if (_gameplayDepth>0) _gameplayDepth--; Apply()` (clamped at 0, unbalanced Unregister is silently ignored). `IgnoreToRegisterRng()` / `UnignoreToRegisterRng()` same with `_ignoreDepth` (clamped).
- Only **transitions** swap; nested sections are plain counters. While an Ignore section is open inside a Register section the cosmetic generator is restored, and re-captured (`_cosmeticBackup = Calc.Random` again) when the ignore closes.
- `IsGameplayRngActive => _overriding`.
- `Reset()` (:65-78): zero both depths, restore backup if overriding. Called from `Session.StartGame` prefix and from `TFGame.OnSceneTransition` Level->MainMenu via `StateApi.ResetRngOverride()` (`network/TF.EX.Patchs/Engine/TFGame.cs:77`; DLL `TfStateApi.ResetRngOverride` -> `CalcPatch.Reset`).
- **Anything that assigns `Calc.Random` while `_overriding` is true is lost** at the next Unregister (restored to the backup). Vanilla assigns it at `LevelLoaderXML.Load` (`PC/TowerFall/LevelLoaderXML.cs:62`, `Calc.Random = new Random(Level.LoadSeed)`), which happens outside any section.

### 1.5 What sets `IsCaptureActive`
- `StateFlags.IsCaptureActive` (`state/TF.State.Domain/Context/StateFlags.cs`) is written only by `TfStateApi.SetDriverFlags` (`state/TF.State.Core/Api/TfStateApi.cs:208-216`), called from `ExFlags.Push()` (`network/TF.EX.Domain/Context/ExFlags.cs:25-34`; skipped when another mod owns the frame driver).
- `NetplayManager.PublishCaptureFlag()` (`network/TF.EX.Domain/Services/NetplayManager.cs:725-729`):
  `IsCaptureActive = mode == Replay || (mode != Uninitialized && !(isInit && IsDisconnected()))`.
  Called at the top of **every `TFGame.Update`** (`network/TF.EX.Patchs/Engine/TFGame.cs:150`) and from the `_isInit` / `_netplayMode` setters (:37, :50).
- Consequence: capture is ON from the moment a netplay mode is chosen (lobby create/join: `SetRoomAndServerMode`, `network/TF.EX.Patchs/Scene/MainMenu.cs:562, 820, 1658`) through lobby, match and results, until `netplayManager.Reset()` on leaving to MainMenu or a disconnect after init. It is NOT scoped to Level.Update. The Switch equivalent: "capture = an online session is active", true before `Session.StartGame` and until the session is left.

### 1.6 `Calc.PushRandom()` prefix (`PATCHS/Calc/Calc.cs:79-94`)
`[HarmonyPrefix] PushRandom()` (no-arg overload only): `if (!IsGameplayRngActive) return true;` else `Calc.PushRandom(Calc.Random.Next()); return false;`.
- Vanilla `PushRandom()` = `randomStack.Push(Random); Random = new Random();` (unseeded); `PushRandom(int)` = push + `Random = new Random(seed)`; `PopRandom()` = `Random = randomStack.Pop()` (`PC/Monocle/Calc.cs:365-380`).
- With the patch inside an active section: ONE gameplay draw `Next()` (= `Next(0,int.MaxValue)`, see 1.1), then `PushRandom(int)` pushes the **gameplay generator** on the stack and installs a **seeded `System.Random`** (.NET legacy Knuth subtractive algorithm). Everything until `PopRandom` runs on that System.Random (exact .NET seeded sequence required), and PopRandom puts the gameplay generator back.
- Only caller reached in netplay: `MatchVariants.Randomize` (`PC/TowerFall/MatchVariants.cs:820-866`) -> menu-only (variants "RANDOMIZE" button, `VariantRandomize.cs:14`). Others (`VersusTreasureDraft:168` TreasureDraft variant, which TF.EX forbids via `UnauthorizedVariant`, `EditorTestRoundLogic:44`, `LevelTiles`/`LevelBGTiles`/`LevelLoaderXML:84` `PushRandom(int)` which is NOT patched) are non-gameplay or int-seeded.

### 1.7 Other RNG-related patches (not Register/Unregister)
- `LocalSettingsGuard` (`PATCHS/LocalSettingsGuard.cs:8-72`): neutralises local save state around the TreasureSpawner ctor, `GetRandomArrowType`, `GetArrowShufflePickups` (so every peer builds identical loot tables): sets `Options.RemoveScrollEffects=false`, `Unlocks.SunkenCity/TowerForge/Ascension=true`, `GameData.NoDarkWorldOverride=false`, restores afterwards. Only when `IsExSession()` = TestMode || ReplayMode || `VersusMatchSettings.Mode.IsNetplay()`. NOT neutralised: `GameData.DarkWorldDLC` (install-dependent; affects Trigger/Prism pickups & arrow lists: `PC/TowerFall/MatchVariants.cs:782-815`, `TreasureSpawner.cs:57-65,455-462`).
- `TreasureSpawnerPatch.UseDeterministRandom` (`PATCHS/Entity/TreasureSpawner.cs:11-14`): sets the spawner's `Random` property to the **shared gameplay generator instance** (not to `Calc.Random`). Called from `LevelLoaderXML` ctor postfix each round (`PATCHS/Scene/LevelLoaderXML.cs:17-37`, line 28). From then on every `Random.Chance/Range/NextFloat/Choose` inside TreasureSpawner draws the gameplay stream regardless of the depth counters.
- `VersusLevelSystemPatch.GenLevels` (`PATCHS/VersusLevelSystem.cs:13-41`) + `VersusLevelSystemExtensions.OwnGenLevel` (`state/TF.State.Domain/Extensions/VersusLevelSystemExtensions.cs:9-42`): level order via **`new System.Random(rngService.GetSeed())`** (a fresh seeded System.Random each time the list is regenerated, NOT the gameplay stream).
- `MapScene.GetRandomVersusTower` postfix (`network/TF.EX.Patchs/Scene/MapScene.cs:99-143`): random tower = `Shuffle(filtered buttons, new Random(seed))[0]` after `ResetRng()`.
- `ParticleSystemPatch` (`PATCHS/Entity/ParticleSystem.cs`) replaces particle position jitter with a private **unseeded** `static Random _random = new Random()` (cosmetic, nondeterministic by design); `LightningFlashLayerPatch` (`PATCHS/Layer/LightningFlashLayer.cs`) same with its own `new Random()` for lightning alarm lengths.
- `CalcExtensions.OwnShuffle` (`state/TF.State.TowerFallExtensions/Calc.cs:9-30`; DLL `CalcExtensions`): `Calc.Shuffle(list.ToList(), Calc.Random)` (used by the replaced spawn methods inside a Register).
- `ModExports.RegisterRng/UnregisterRng` (`state/TF.State.Core/Api/ModExports.cs:46-58`): public API for other mods; not used by the game path.

---

## 2. Complete table of RNG sites

Legend. Patch kind: Pre/Post = Harmony prefix/postfix; "Reg" = `RegisterRng`/`UnregisterRng` (depth++/--); "Ign" = Ignore depth++/--; "REPL" = prefix returns false and the TF.EX code replaces the original. "Draws" = what the PC method does with Random when it runs (`R` = `Calc.Random`). Scope tags: **REQ** = required for v1; **NR** = not reachable in LMS/TDM with the quickplay/default variants on the 16 maps; **COS** = cosmetic only.
Quickplay variants are `TournamentRules()` = `NoAutobalance` + `SymmetricalTreasure` ON, everything else off (see section 3.2), not "all off".

### 2A. Sites that swap `Calc.Random` (Register / Ignore)

| # | TF.EX file:line | PC type | Method (PC param types) | Kind | Effect on RNG | PC draws inside | Scope |
|---|---|---|---|---|---|---|---|
| A1 | `PATCHS/Entity/LevelEntity/Arrow.cs:77-88` | Arrow | `Drop(int fallSide)` (virtual) | Pre+Post Reg | depth++ / -- | `PC/Arrow.cs:987-995`: `Speed.Y = -2 - R.NextFloat()`; `Speed.X = fallSide*(0.5+R.NextFloat()*0.3)` = 2 `NextFloat`. Overrides `BombArrow.cs:112`, `SuperBombArrow.cs:108` call `base.Drop` (covered). | **REQ** (every corpse arrow drop, A23) |
| A2 | `Arrow.cs:98-109` | Arrow | `EnterFallMode(bool bounce=true, bool zeroX=false, bool sound=true)` (virtual) | Pre+Post Reg | depth++ / -- | `PC/Arrow.cs:939-971`: if `!squished && State!=Buried` and `bounce`: `Speed.Y = -2 - R.NextFloat()`; if `!zeroX`: `Speed.X = sign*(0.5+R.NextFloat()*0.3)` (so 1 or 2 `NextFloat`). `BoltArrow.EnterFallMode` override does NOT call base (no draws, not patched). | **REQ** (arrows bounce constantly) |
| A3 | `PATCHS/Component/ArrowCushion.cs:10-22` | ArrowCushion | `AddArrow(Arrow, float moveIn=0, bool drawHead=false)` | Pre+Post Reg | depth++ / -- | `PC/ArrowCushion.cs:98`: `R.Range(-22, 44)` = `-22 + Next(44)` = 1 draw. (`ReleaseArrows(Vector2,Vector2)` with `Range(Vector2)` draws but has zero callers.) | **REQ** (arrow sticks in corpse/entity) |
| A4 | `PATCHS/Entity/LevelEntity/Chain.cs:11-23` | Chain | `.ctor(Vector2 position, int height)` (`Chain(XmlElement)` chains to it) | Pre+Post Reg | depth++ / -- | `PC/Chain.cs:45-50`: for each link `i < height/10`: `R.Next(subtexture.Height/10)` = 1 draw per link. | **REQ** (Chains on 9 of 12 base maps + Amaranth; counts in 3.3) |
| A5 | `.../DummyHead.cs:11-23` | DummyHead | `.ctor(Vector2, Arrow, int xScale)` | Pre+Post Reg | depth++ / -- | `PC/DummyHead.cs:28,35`: `R.Range(-2f,-1.2f)` (NextFloat); if `arrow==null`: `R.Range(-2f,4f)` (NextFloat). | **NR** (Dummy/Trials only, `Dummy.cs:125`) |
| A6 | `.../Hat.cs:6-16` | Crown | `Update()` | Pre+Post Reg | depth++ / -- | `PC/Crown.cs:80-100`: draws only in collision callbacks while the hat moves: `ResetSpin()` = `Range(4,12)` = `4+Next(12)` (1 draw) on CollideH and CollideV; CollideV also `R.Range(-1f,1f)` (NextFloat) if `Speed.Y>=2`. | **REQ** (crown/hat physics runs every tick it exists) |
| A7 | `.../Hat.cs:18-28` | SteelHat | `Update()` | same | same | `PC/SteelHat.cs:76,91` same pattern | **REQ** (archers with steel hats) |
| A8 | `.../Hat.cs:30-40` | WoodenHat | `Update()` | same | same | `PC/WoodenHat.cs:76,91` same | **REQ** (archers with wood hats) |
| A9 | `.../Lava.cs:10-22` | Lava | `BubbleComplete(Sprite<int>)` (private) | Pre+Post **Ign** | ignore++ / -- (cosmetic generator restored inside) | `PC/Lava.cs:441-462`: `R.Next(surface.Length)`, `R.Range(0.8f,0.4f)`, `R.Next(13)` (cosmetic). Called 3x from the Lava ctor (`PC/Lava.cs:118,146,174,202`) which runs inside `LavaControl.Added` (A12). Without the Ignore these would eat gameplay draws. | **REQ** (LavaOrb pickup) |
| A10 | `.../LavaControl.cs:38-50` | LavaControl | `.ctor(LavaControl.LavaMode, int ownerIndex)` | Pre+Post Reg | depth++ / -- | `PC/LavaControl.cs:38-41`: only if `mode==Pickup`: `R.Range(600,300)` = `600+Next(300)`. | **REQ** (Pickup mode via LavaOrb; Variant mode via AlwaysLava is NR) |
| A11 | `.../LavaControl.cs:10-22` | LavaControl | `Extend(int ownerIndex)` | Pre+Post Reg | depth++ / -- | `PC/LavaControl.cs:117`: `R.Range(540,300)` = `540+Next(300)`. | **REQ** |
| A12 | `.../LavaControl.cs:24-36` | LavaControl | `Added()` | Pre+Post Reg | depth++ / -- | `PC/LavaControl.cs:44-76`: unless `CataclysmBoss`: `!blocksTopAndBottomLava && R.Chance(0.001f)` (NextFloat, skipped when `Tiles.BlocksTopAndBottomLava`); `else if (blocks || R.Chance(0.5f))` (NextFloat, skipped when blocks). Nested Lava ctors: Ignore A9. | **REQ** |
| A13 | `.../Miasma.cs:12-58` | Miasma | `Update()` | Pre (void, original still runs) | manual Reg/Unreg only around the roll: `if (MiasmaSequenceController.ShouldRollAmaranthDir(mode,ticks,dir))` (`mode==AmaranthBoss && dir unset && ticks>=180`) | `state.Dir = Calc.Random.Choose(1,-1)` = `Next(2)` once. Vanilla equivalent `PC/Miasma.cs:220` (only in `TheAmaranthBossSequence`). Versus miasma (`Sequence()`, `PC/Miasma.cs:93-125`) draws nothing. | **NR** (boss mode) |
| A14 | `.../Orb.cs:11-28` | Orb | `.ctor(Vector2, bool explodes)` (`Orb(XmlElement,bool)` chains to it) | Pre Reg / Post Unreg | depth++ / -- (Post also sets `ownerIndex=-1` when capture active; state hygiene, no RNG) | `PC/Orb.cs:46-81`: if `explodes`: `PlayEyeAnim()` = `Next(TotalAnimations-1)` then `Range(0.9f,0.2f)` (NextFloat); then always `sine.Randomize()` = `NextFloat()` (`PC/SineWave.cs:66`). Order: eye anim first, Randomize last. | **REQ** (Twilight Spire "Orb", Darkfang "ExplodingOrb") |
| A15 | `PATCHS/OrbLogic.cs:138-150` | OrbLogic | `Update()` | Pre+Post Reg | depth++ / -- around the **whole** OrbLogic.Update (every tick) | `PC/OrbLogic.cs:57-133`: `darknessEndCounter.Set(R.Range(600,300))`, `slowTimeEndCounter.Set(R.Range(300,60))` when start counters expire; plus the nested `DoSpaceOrb()` (A16) when `spaceCounter` expires. Anything else called inside (ScreenEffects, tween callbacks, counters) would also hit the gameplay stream; none draws. | **REQ** |
| A16 | `OrbLogic.cs:16-57` | OrbLogic | `DoSpaceOrb()` | Pre **REPL** (only if `!Level.Ending && IsCaptureActive`, else original) | manual Reg around: Snap math, `end += R.Choose(4 Vector2)` then Unreg; then re-builds the tween (`TweenMode.Oneshot` instead of Persist, adds `ScreenOffsetStart/End` to the tween's DynamicData) | `PC/OrbLogic.cs:282-302`: `R.Choose<Vector2>(4 values)` = `Next(4)` (1 draw). Same draw count/order as vanilla; only the tween bookkeeping differs. | **REQ** (SpaceOrb / ChaosOrb) |
| A17 | `OrbLogic.cs:58-95` | OrbLogic | `DoOffsetWorldVariant()` | Pre **REPL** (same condition) | same pattern | `PC/OrbLogic.cs:309-323`: `Choose` of 4 = `Next(4)`. Callers: `VersusStart.cs:328` (OffsetWorld variant), scenario sweeper. | **NR** (variant-gated) |
| A18 | `OrbLogic.cs:96-108` | OrbLogic | `DoDarkOrb()` | Pre+Post Reg | depth++ / -- | `PC/OrbLogic.cs:159-171`: if `darknessEndCounter` active: `SetMax(R.Range(600,300))` (1 draw), else none. | **REQ** (DarkOrb, ChaosOrb) |
| A19 | `OrbLogic.cs:110-122` | OrbLogic | `StartScroll()` (no-arg only, `new Type[]{}`) | Pre+Post Reg | depth++ / -- | `PC/OrbLogic.cs:325-328`: `Choose<ScrollDirections>(4)` = `Next(4)`. Callers: `Session.StartRound` (AlwaysScrolling variant), QuestControl. | **NR** (variant/Quest) |
| A20 | `OrbLogic.cs:124-136` | OrbLogic | `DoTimeOrb(bool delay)` | Pre+Post Reg | depth++ / -- | `PC/OrbLogic.cs:206-219`: if `slowTimeEndCounter` active: `SetMax(R.Range(300,60))` (1 draw). | **REQ** (TimeOrb, ChaosOrb) |
| A21 | `PATCHS/Entity/LevelEntity/Player.cs:21-33` | Player | `DropArrow()` (private) | Pre+Post Reg | depth++ / -- | `PC/Player.cs:2936-2944`: `Arrow.Create(..., R.Range(PI*-3/4, PI/2))` (1 NextFloat) then `arrow.Drop(...)` (A1: 2 more). Only caller: `Player.cs:1872`, guarded by `Variants.ClumsyArchers[i]`. | **NR** (ClumsyArchers variant) |
| A22 | `Player.cs:35-47` | Player | `LoseHat(Arrow arrow=null, bool updateHead=true)` (private) | Pre+Post Reg | depth++ / -- | `PC/Player.cs:2647-2667` -> `Hat.CreateHat` -> hat ctors, see A22a-d. Callers: `Player.cs:1402` (hat knocked off), `:2372` (`Die`, `LoseHat(null,false)`), `:3431`. | **REQ** |
| A22a | (inside A22) | Crown ctor | `PC/Crown.cs:33-43` | n/a | draws in this order: `Range(4f,12f)` (NextFloat), `Choose(1,-1)` (Next(2)), then if `arrow!=null`: `Range(-1f,-1f)` (NextFloat) else `Range(-1,2)` (Next(2)) then `Range(-3f,3f)` (NextFloat). C# left-to-right argument order matters. | REQ |
| A22b | (inside A22) | SteelHat / WoodenHat ctor | `PC/SteelHat.cs:41-50`, `WoodenHat.cs:41-50` | n/a | `Range(4f,12f)`, `Choose(1,-1)`, then arrow: `Range(-1f,-1.2f)`; no arrow: `Range(0.8f,0.8f)`, `Choose(1,-1)`, `Range(-1f,-1.2f)` | REQ |
| A22c | (inside A22) | DefaultHat ctor | `PC/DefaultHat.cs:47-55` | n/a | arrow: `Range(-1f,-1.2f)` else same for Speed.Y; then `new SineWave(R.Range(50,80))` (`50+Next(80)`), `sine.Randomize()` (NextFloat). `DefaultHat` has no Update draws and is not patched. | REQ |
| A22d | (inside A22) | `Hat.CreateHat` | `PC/Hat.cs:15-43` | n/a | null for NoHat; `Sounds.char_crownFall/char_hatFall.Play` (plain SFX, no draw). Which hat class: `ArcherData.Hat.Material` (content). | REQ |
| A23 | `.../PlayerCorpse.cs:197-209` | PlayerCorpse | `DropNextArrow()` (private, looping Alarm every 10 ticks) | Pre+Post Reg | depth++ / -- | `PC/PlayerCorpse.cs:307-322`: if `Squished==0 && Arrows.Count>0`: `Arrow.Create(..., R.Range(PI*-3/4, PI/2))` (1 NextFloat) + `arrow.Drop(...)` (A1: 2 NextFloat) = **3 draws per dropped arrow**. Alarm created in `StartDroppingArrows` (`PC/PlayerCorpse.cs:298-305`, TF.EX removes+recreates the alarm: `PlayerCorpse.cs:179-195`). | **REQ** |
| A24 | `.../Spikeball.cs:17-31` | Spikeball | `.ctor(Vector2 pos, Vector2 pivot, bool exploding)` (XmlElement ctors chain to it) | **Post only** (if capture active) | Reg; `spinDir = Calc.Random.Choose(1,-1)`; Unreg | Original ctor body (`PC/Spikeball.cs:52-97`) runs OUTSIDE any section: exploding -> `spr.Play(R.Next(TotalAnimations))` on the *cosmetic* stream; `spinDir = new Random().Choose(1,-1)` unseeded. The postfix then makes ONE gameplay draw `Next(2)` and overwrites `spinDir`. | **REQ** (Twilight Spire 1, Thornwood 1, Darkfang 1 SpikeBall) |
| A25 | `.../TreasureChest.cs:204-218` (called from Update postfix :129) | TreasureChest | (manual, `SpawnNextPickup`) | manual Reg/Unreg around `Session.TreasureSpawner.GetTreasureSpawn()` | Reg only for the call | `GetTreasureSpawn` = `Random.NextFloat(sum)` (1 draw) | **NR** (Bottomless chest variant) |
| A26 | `PATCHS/Entity/TreasureSpawner.cs:100-121` | TreasureSpawner | `GetChestSpawnsForLevel(List<Vector2> chests, List<Vector2> bigChests)` | Pre+Post Reg | depth++ / -- | `PC/TreasureSpawner.cs:145-289`, in order: `chestPositions.Shuffle()` (Count-1 draws), `bigChestPositions.Shuffle()` (Count-1 draws if Count>=2); if `!MaxTreasure`: `flag = AlwaysBig||Bottomless||Random.Chance(0.03f)` (1 NextFloat, always evaluated); big-chest branch (rare 3%: 1-3 `GetTreasureSpawn`, `bigChests.Shuffle(Random)`, `Random.Range(30, PlayerAmount*30)`); else loop `while (chests.Count>0 && CanProvideTreasure && CanSpawnAnotherChest(n))`: `Random.Chance(ChestChances[PlayerAmount-2][n])` (1 NextFloat) -> `Calc.Random.Range(10, PlayerAmount*60)` (`10+Next(PlayerAmount*60)`) -> `GetTreasureSpawn(list2)` (1 NextFloat(sum)); Symmetrical: mirrored chest removes its partner from the list and `n++` (no draw). `Random` here is the shared generator (1.7), `Calc.Random` is the swapped one, same state. Called from `RoundLogic.SpawnTreasureChestsVersus` at `OnRoundStart` (`PC/RoundLogic.cs:276-289`; skipped if `!CanSpawnTreasure || IsInOvertime`). | **REQ** |
| A27 | `TreasureSpawner.cs:65-86` | TreasureSpawner | `GetArrowShufflePickups()` | Pre Reg (+guard) / Post Unreg (+restore, then drop non-"safe" mod pickups) | depth++ / -- | `PC/TreasureSpawner.cs:470-515`: `list.Shuffle()` of up to 9 arrow pickups (n-1 draws) then trims to <=2. Only runs if tower `arrowShuffle` or `ArrowShuffle` variant. FortRise adds a second `Calc.Shuffle(list)` (R-FR). No versus `tower.xml` sets `arrowShuffle` (checked all 16). | **NR** |
| A28 | `PATCHS/DarkPortalsVariantSequence.cs:73,103` | (TF.EX `Step(Level)`) | manual | Reg around `Calc.Shuffle(spawnerPositions, Calc.Random)` and `Calc.Choose(Random, {"Bat","Slime"})` | | | **NR** (DarkPortals variant / Dark World) |
| A29 | `PATCHS/MatchVariants.cs:11-34` | MatchVariants | `Randomize()` | Pre: if `IsRollbackFrame \|\| HasFramesToReSimulate` -> `__state=false; return false` (skip original); else Reg, `__state=true`. Post: Unreg only if `__state` | `PC/MatchVariants.cs:820-866`: `Calc.PushRandom()` (patched, 1.6): 1 gameplay draw `Next()`, remaining draws (`canRandoms.Shuffle()`, `Range(3,3)`) come from the seeded System.Random | menu button only | **NR** |
| A30 | `PATCHS/Entity/ParticleSystem.cs:29-55, 59-84` | Monocle.ParticleSystem | `Emit(ParticleType,int,Vector2,Vector2,float)` and `Emit(ParticleType,int,Vector2,Vector2)` | Pre **REPL**: `if IsCosmeticFrozen return false; if !IsCaptureActive return true;` else Ign, loop `amount` times `Emit(type, _random.Range(pos-range, range*2) [,dir])` with the private unseeded Random, Unign, `return false` | ignore++ / -- | Vanilla `PC/ParticleSystem.cs:57-76`: 2 `NextFloat` per particle for the position, then `ParticleType.Create` draws 3-6 more (`PC/ParticleType.cs:97-109`), all from `Calc.Random`. The single-particle overloads `Emit(type,pos)`, `Emit(type,pos,Vector2)`, `Emit(type,pos,float)` are NOT patched (still draw from whatever `Calc.Random` is). I found no amount-`Emit` or any `ParticleType.Create` call inside the registered bodies A1-A26 (scan of the PC bodies listed, not an exhaustive callee walk). | **COS** (defensive; no gameplay effect found in default versus) |
| A31 | `PATCHS/Entity/ParticleSystem.cs:15-25` | ParticleSystem | `Update()` | Pre REPL when `IsCosmeticFrozen` | no RNG | rollback re-sim only | **COS** (not needed at 60 Hz without rollback) |

### 2B. Sites that replace a method/instance/seed without Register (still decide which stream is used)

| # | TF.EX file:line | PC type | Method | Kind | What it does to RNG | PC draws (vanilla) | Scope |
|---|---|---|---|---|---|---|---|
| B1 | `PATCHS/RoundLogic/RoundLogic.cs:13-105` | RoundLogic | `SpawnPlayersFFA()` (protected, returns int) | Pre **REPL** (when `IsCaptureActive \|\| IsReplayMode`, else original) | Whole method re-implemented. Reg only around `shuffled = CalcExtensions.OwnVectorShuffle(PlayerSpawn positions).ToList()` = `Calc.Shuffle(copy, Calc.Random)` (Count-1 draws). If `PlayerSpawn.Count < PlayerAmount` it first appends `TeamSpawnA/B` positions (not already present). Then the 2-player "opposite spawn" tweak (`xMLPositions[1] = WrapMath.Opposite(xMLPositions[0])` when `num==2 && [0].X != 160 && list.Contains(opposite)`) is done BEFORE creating the first player (vanilla did it after). All `Player` entities are created into a list first, then added to the level in a second loop (vanilla adds each immediately). `RoundLogic.Players = num` via reflection. | `PC/RoundLogic.cs:169-213`: `xMLPositions.Shuffle(new Random())` (unseeded) | **REQ** (LMS) |
| B2 | `RoundLogic.cs:107-169` | RoundLogic | `SpawnPlayersTeams()` | Pre **REPL** (same condition) | Re-implemented: `TeamSpawn` split by `X<=160` into A/B, top-up from `PlayerSpawn` if too few, `List.Sort(SortTeamSpawnsLeft/Right)` (comparators at :207-225), then Reg around `order = CalcExtensions.OwnShuffledIndexes(Math.Max(A.Count,B.Count))` = Shuffle of `0..n-1` (n-1 draws); `ApplyOrder` filters `index < spawns.Count` on both lists; `SpawnTeam` loops players Blue then Red. | `PC/RoundLogic.cs:215-256`: `seed = new Random().Next(); A.Shuffle(new Random(seed)); B.Shuffle(new Random(seed))` | **REQ** (TDM) |
| B3 | `PATCHS/VersusLevelSystem.cs:13-41` | VersusLevelSystem | `GenLevels(MatchSettings)` (private) | Pre **REPL** when `IsCaptureActive` (ScenarioLevels bypass is test-only) | `OwnGenLevel`: `levels = VersusTowerData.GetLevels(ms)`; if `FixedFirst && lastLevel==null`: remove [0], `Calc.Shuffle(levels, new Random(seed))`, insert at 0; else shuffle, and if `levels[0]==lastLevel` move it to the end. `seed = rngService.GetSeed()` (the lobby int). **System.Random (Knuth), not the gameplay stream.** Regenerated only when `levels` is empty (`PC/VersusLevelSystem.cs:49-56`) so each cycle yields the identical order (same seed). | `PC/VersusLevelSystem.cs:30-47`: `Shuffle(new Random())` | **REQ** (level order each round) |
| B4 | `PATCHS/Entity/TreasureSpawner.cs:30-63` | TreasureSpawner | `GetRandomArrowType(bool includeDefaultArrows)` | Pre **REPL** when `IsCaptureActive` (else original), with LocalSettingsGuard Neutralize/Restore | `arrowTypes = [ __instance.orig_GetRandomArrowType(include) ] + registry arrow pickups (mod pickups with ArrowType, not excluded, "module safe": name contains '/' and module has state events)`; `__result = Calc.Choose(__instance.Random, arrowTypes)`. **Two draws with no registry entries**: one inside `orig_` (`Random.Choose(list)` = `Next(N)`, N = number of allowed arrow types) and then `Next(1)` (returns 0, still consumes a sample). Called from `Session.LevelLoadStart` (`PC/TowerFall/Session.cs:112-120`) every round, outside any Register: it relies on the spawner's `Random` being the shared generator (1.7). `includeDefaultArrows` is always true there. | `PC/TreasureSpawner.cs:420-468` | **REQ** |
| B5 | `PATCHS/Scene/LevelLoaderXML.cs:15-37` | LevelLoaderXML | `.ctor(Session)` | Post (if capture) | `Reset(session)` (screen offset, `ResetState`, LMS round-end counters) + `UseDeterministRandom(session.TreasureSpawner)` (1.7) + `ShowControls=false`. Runs after the ctor's own `GetNextRoundLevel` (-> B3). | n/a | **REQ** |
| B6 | `PATCHS/Entity/TreasureSpawner.cs:17-28` | TreasureSpawner | `.ctor(Session,int[],float,bool)` | Pre/Post | only `LocalSettingsGuard.Neutralize/Restore` (no draw; ctor's `new Random()` is replaced later by B5) | `PC/TreasureSpawner.cs:52-101` (`GetArrowShufflePickups` only if arrowShuffle) | **REQ** (guard) |
| B7 | `PATCHS/Session.cs:13-30` | Session | `StartGame()` | Pre | `CalcPatch.Reset`, `RngService.Reset` (reseed + cosmetic `Calc.Random = new Random(seed)`), `StateContext.Reset`, `ResetState`, `PurgeCache`; `MatchSettings.RandomLevelSeed = GetSeed()` if netplay | n/a | **REQ** |
| B8 | `PATCHS/Entity/HUD/VersusMatchResults.cs:12-33` (line 29) | VersusMatchResults | `.ctor(Session, VersusRoundResults)` | Post (if capture) | `RandomLevelSeed = GetSeed()`; freezes level; adds `VersusSeedDisplay`. No draw. | `PC/VersusMatchResults.cs:111` | **COS** |
| B9 | `network/TF.EX.Patchs/Scene/MapScene.cs:99-143` | MapScene | `GetRandomVersusTower()` | Post (netplay) | `list = Buttons that are VersusMapButton && title in NETPLAY_SAFE_MAP && (DarkWorldDLC \|\| !Locked)`, drop `NoRandom` ones, `ResetRng()`, `Shuffle(list, new Random(seed))`, `__result = list[0]` | `PC/MapScene.cs:372-415`: `Shuffle(new Random())` | **REQ if** MapId=-1 ("random") is supported; the quickplay lobby uses MapId -1 (`matchmaker.js:571`) |
| B10 | `network/TF.EX.Patchs/Scene/MapScene.cs:46-58` | MapScene | `StartSession()` | Post (netplay) | `ResetRng()` (see 1.2) | n/a | **REQ** |
| B11 | `PATCHS/Layer/LightningFlashLayer.cs:13-54` | LightningFlashLayer | `SequenceB/C/D` | Post (if capture) | `alarm.Start(_random.Range(...))` with private unseeded Random, overwrites the alarm vanilla set from `Calc.Random` | background lightning | **COS** |
| B12 | `PATCHS/DeterministicMathPatch.cs` | (many) | transpiler | `Math.Sin/Cos/Atan2/Pow` -> `DeterministicMath` | known; not RNG | | known |
| B13 | `PATCHS/Calc/Calc.cs:79-94` | Monocle.Calc | `PushRandom()` | Pre REPL when `IsGameplayRngActive` | see 1.6 | | **NR** |
| B14 | `PATCHS/Entity/LevelEntity/Arrow.cs:14-19` | Arrow | `EnforceLimit(Level)` (static) | Pre REPL when capture | skips the 12-arrow decay rule (non-RNG, see 4.2) | `PC/Arrow.cs:260-273` | **REQ** (non-RNG) |

### 2C. PC code that draws Random in versus but TF.EX leaves on the cosmetic stream (for awareness; no port action unless a trace says otherwise)
Level load `LevelLoaderXML.Load` coroutine: `Calc.Random = new Random(LoadSeed)` where `LoadSeed` = sum of the level path chars (`PC/VersusLevelSystem.cs:58-62`) and every entity/background ctor after it (Lantern, BGMushroom, ShiftBlock face `Chance(0.5f)` `PC/ShiftBlock.cs:62,83,102`, Icicle, MovingPlatform, CrumbleBlock, PirateBanner, Player breath alarms `Player.cs:1063,1123`, Level screen shake `Level.cs:321`, VersusStart `:173,:334`, SFXVaried `PC/Monocle/SFXVaried.cs:47-52`, particles). They influence only visuals/audio. No Switch action needed, but the Switch's `Calc.Random` while no section is active must simply be *some* non-gameplay generator (TF.EX seeds it with the lobby seed at StartGame and LoadSeed per level; not required for gameplay determinism).
Unseeded `new Random()` sites that TF.EX handles: RoundLogic spawn (B1/B2), Spikeball (A24), GenLevels (B3), MapScene (B9), TreasureSpawner (B5). Unhandled and local-save dependent (new finding): `UnlockData.ShouldSelectSunkenCity` / `ShouldOpenPurpleArcherPortal` (`PC/UnlockData.cs:143,165`, unseeded `Chance`) used by `LevelLoaderXML.Load` (`PC/LevelLoaderXML.cs`, after the entity loop) to add `MoonBreakSequence` / `PurpleArcherUnlockSequence` entities on levels whose XML says `CanUnlockMoonstone="True"` (Sacred Ground 01,03,05,06,09,10) or `CanUnlockPurple="True"` (Twilight Spire 06), and only when the local save qualifies (`!Moonstone && PlayedAllDefaultLevels`; `RoundsPlayed>=100 && !PurpleArcherRevealed` then an unseeded `Chance`). See 4.2.

---

## 3. Scope for the first version

### 3.1 Definition
2-player VERSUS, `Modes.LastManStanding` (3) and `Modes.TeamDeathmatch` (5; HeadHunters=4 is handled by the same `NetplayRoundLogic` and uses the same hooks), 60 ticks/s, `TFGame.PlayerAmount = 2`, capture active for the whole online session.
TF.EX's netplay round logic is its own class, `NetplayRoundLogic` (`network/TF.EX.Core/RoundLogic/NetplayRoundLogic.cs`): `IsTeamMode = (mode==5)`, `SpawnRoundPlayers` = add `VersusStart` then `SpawnPlayersFFA` / `SpawnPlayersTeams`; `OnRoundStart` = `base.OnRoundStart(); SpawnTreasureChestsVersus();`. Round 0 does not spawn at `OnLevelLoadFinish`; it calls `netplayManager.Init(this)` and spawns when the session is synchronized (`NetplayManager.EstablishSessionIfSynchronized -> OnSessionEstablished -> SpawnFirstRound`, `network/TF.EX.Domain/Services/NetplayManager.cs:174-186, 276-312`); later rounds spawn immediately in `OnLevelLoadFinish`. RNG-wise this is still the vanilla order (level-load draws, then spawn shuffle, then chests at round start) because `Level.Update` returns early while awaiting sync (`network/TF.EX.Patchs/Scene/LevelPatch.cs`, `Level_Update__Prefix`), so no tick-time draws happen before the spawn. (Clone: `network/TF.EX.Patchs/Scene/Level.cs:63-108`, `IsAwaitingSynchronization`.)

### 3.2 Variants ("default")
The browser quickplay path applies `Variants.ApplyNetplayVariantRules()` = `NormalizeForNetplay()` (per-player variants cleared) + `TournamentRules()` (`network/TF.EX.Domain/Extensions/VersusMode.cs:53-74`, used for `IsQuickPlay` lobbies at `MainMenu.cs:1684ff`). In the PC game the 1v1 tournament preset is exactly `NoAutobalance` and `SymmetricalTreasure` (`[TournamentRule1v1]` on `PC/TowerFall/MatchVariants.cs:14,58`). **Match this, not "all variants off"**: `SymmetricalTreasure` changes chest counting in A26 (mirror chest increments the spawned count and removes its partner). Manual lobbies pass `lobby.GameData.Variants` instead. `TreasureDraft` is blocked in netplay (`network/TF.EX.Patchs/MatchVariants.cs:12-16`). With these variants, all variant-gated rows (A17, A19, A21, A25, A27, A28, A29) are not reached.

### 3.3 Netplay-safe maps (`NETPLAY_SAFE_MAP`, `network/TF.EX.Domain/Models/Constants.cs:15-32`; identical in the DLL)
16 titles: SACRED GROUND, TWILIGHT SPIRE, BACKFIRE, FLIGHT, MIRAGE, THORNWOOD, FROSTFANG KEEP, KING'S COURT, SUNKEN CITY, MOONSTONE, TOWERFORGE, ASCENSION, THE AMARANTH, DREADWOOD, DARKFANG, CATACLYSM.
RNG-relevant entity inventory from the browser's local game content (`gamefiles/TowerFall/.../Content/Levels/Versus/*` and `DarkWorldContent/Levels/Versus/12-15`; counts are totals over all level files of the tower; per-level counts differ):

| Map | levels | Chain | Orb | ExplodingOrb | SpikeBall | other hazards (non-RNG gameplay) | tower flags |
|---|---|---|---|---|---|---|---|
| 00 Sacred Ground | 11 | 10 | | | | | fixedFirst; treasure arrows .4, orbs: Time/Dark/Lava only; levels 01,03,05,06,09,10 have `CanUnlockMoonstone="True"` |
| 01 Twilight Spire | 12 | 21 | 29 | | 1 | | level 06 has `CanUnlockPurple="True"` |
| 02 Backfire | 10 | 4 | | | | | |
| 03 Flight | 12 | 30 | | | | | |
| 04 Mirage | 13 | 10 | | | | | |
| 05 Thornwood | 10 | 9 | | | 1 | 5 MovingPlatform | |
| 06 Frostfang Keep | 13 | 8 | | | | 32 Icicle | |
| 07 King's Court | 12 | 2 | | | | | |
| 08 Sunken City | 12 | | | | | 18 ShiftBlock | |
| 09 Moonstone | 13 | | | | | MoonGlass (cosmetic controller) | |
| 10 TowerForge | 11 | | | | | | |
| 11 Ascension | 12 | | | | | | fixedFirst |
| 12 The Amaranth (DLC) | 13 | 61 | | | | | |
| 13 Dreadwood (DLC) | 12 | | | | | | |
| 14 Darkfang (DLC) | 12 | | | 17 | 1 | | TriggerArrows in mask |
| 15 Cataclysm (DLC) | 14 | | | | | 200 RandomBlock | **`<procedural/>`** |

Every tower's treasure mask contains orbs (Sacred Ground: Time/Dark/Lava; others also Space/Chaos), so the OrbLogic/LavaControl/Space rows are reachable on all 16 maps. **No tower sets `arrowShuffle`.**
**Cataclysm is procedural**: `LevelLoaderXML.Load` (`PC/LevelLoaderXML.cs:80-91`) does `Calc.PushRandom(MatchSettings.RandomLevelSeed)` (-> seeded System.Random, `RandomLevelSeed` forced to the lobby seed by B7) and runs `LevelRandomGeometry.GenerateData`, `LevelRandomItems.AddItems`, `LevelRandomTreasure`, `LevelRandomBGTiles`, `LevelRandomBGDetails` on it (+ DeterministicMath in GenerateData/AddItems/GenCataclysm, `DeterministicMathPatch.cs:52-54`). It does not use the gameplay stream but needs exact seeded System.Random and exact generator code. Treat Cataclysm as a separate milestone; the other 15 maps need only the whitelist.
Level lists are filtered by `PlayerSpawns >= PlayerAmount` (LMS) or `TeamSpawns >= maxTeamSize` (TDM) (`PC/VersusTowerData.cs:74-104`); content order of `VersusTowerData.Levels` feeds `OwnGenLevel`, so level files/ordering must match the browser build's (note the browser install also has `Teuria.WiderSet` wide level sets under FortRise Mods; its quickplay queue has a `wide` flag, `cloudflare/tfex-server/matchmaker.js:561`: use `wide=false` for the Switch).

### 3.4 Draw timeline for one match (what the port must reproduce, in order)
1. Lobby: `SetSeed(seed)` (reseeds gameplay + cosmetic). `MapScene.StartSession` post and `Session.StartGame` pre each reseed again (idempotent, no draws between). Random map (MapId -1) -> B9 (System.Random, no gameplay draws).
2. `Session.StartGame`: `TreasureSpawner` ctor (no draws; guard). `new LevelLoaderXML`: B3 level order (System.Random, no gameplay draws), B5 swaps spawner `Random`.
3. `LevelLoaderXML.Load` (coroutine over many frames): `Session.LevelLoadStart` -> **B4: 2 gameplay draws** (`Next(N)`, `Next(1)`). Then `LoadEntity` in XML order: per `Chain` (A4) `height/10` draws; per `Orb`/`ExplodingOrb` (A14) 1 or 3 draws (explodes: `Next(anims-1)`, NextFloat, then NextFloat); per `SpikeBall` (A24) 1 draw (`Next(2)`) via the ctor postfix. (Cosmetic entities draw from the cosmetic stream.)
4. `OnLevelLoadFinish` (deferred for round 0 until sync): `VersusStart` added, then **B1** (LMS: Count-1 draws) or **B2** (TDM: n-1 draws).
5. Round starts (`Session.StartRound` after the VersusStart sequence): `OnRoundStart` -> **A26** chests (see row for exact order; skipped in overtime).
6. Every tick: `Level.Update` order (`PC/Level.cs:312-363`): cosmetic stuff, `Session.OnUpdate`, **`OrbLogic.Update` (A15, Register for the whole call)**, TeamReviver, then entity updates (arrows -> A2 on bounce, A3 on stick, hats -> A6-8, corpse alarms -> A23, deaths -> A22, orb pickups -> A18/A20/A16/A10-A12...).
7. Next round: back to step 3 (`GetRandomArrowType` again), **no reseed**.

### 3.5 Minimal ORDERED port list (implementation difficulty, easiest first)
Infrastructure (no game hooks beyond what is listed):
1. `DeterministicRandom` (xoshiro256** + SplitMix64) with the exact output mapping of 1.1; unit-test with the vectors in the appendix.
2. The substitution mechanism with `CalcPatch` semantics (1.4): depth counters, `Apply()` transitions, `_cosmeticBackup`, clamped Unregister, `Reset()`. Native-friendly design: instead of swapping the managed `Calc.Random` field, intercept the `System.Random` draw entry points the game uses via `Calc` (`Next()`, `Next(int)`, `Next(int,int)`, `NextDouble()`) and redirect to the xoshiro instance when the receiver is the current `Calc.Random` object AND `depth>0 && ignore==0 && capture`, or when the receiver is `session.TreasureSpawner.Random` (always, once B5 ran). All Choose/Range/Chance/Shuffle/NextFloat helpers then reproduce TF.EX's draw order automatically because they are the vanilla IL.
3. `IsCaptureActive` definition (1.5) and `Session.StartGame` prefix behaviour B7 (`CalcPatch.Reset`, reseed from the lobby seed, `RandomLevelSeed = seed`), plus reseed in `MapScene.StartSession` (B10) and `SetSeed` on lobby seed change.
4. `LocalSettingsGuard` around TreasureSpawner ctor / GetRandomArrowType / GetArrowShufflePickups (1.7).

Hooks (Register/Unregister pairs unless noted), cheapest first:
5. A1 Arrow.Drop, A2 Arrow.EnterFallMode, A3 ArrowCushion.AddArrow, A23 PlayerCorpse.DropNextArrow (+ keep the corpse alarm timing: A23 note), A22 Player.LoseHat, A6/A7/A8 Crown/SteelHat/WoodenHat.Update. (Highest frequency; pure wrappers.)
6. A4 Chain ctor, A14 Orb ctor, A24 Spikeball ctor **postfix** (with the explicit `spinDir` draw).
7. B7/B5 spawner `Random` replacement, **B4 GetRandomArrowType replacement** (orig draw + `Next(1)` extra draw; list = vanilla list only), B3 GenLevels (needs bit-exact seeded `System.Random`; verify the Switch BCL: `new Random(42).Next()` must be 1434747710, 302596119, 269548474).
8. B1 SpawnPlayersFFA / B2 SpawnPlayersTeams re-implementations (including player creation order and the deferred first-round spawn), A26 GetChestSpawnsForLevel.
9. Orbs/lava: A15 OrbLogic.Update (Register around the whole call), A18, A20, A16 (DoSpaceOrb replacement keeping draw order), A10, A11, A12, A9 (the Ignore on `Lava.BubbleComplete` is mandatory here).
10. Optional/defensive for v1: A30 ParticleSystem.Emit ignore, B9 random map select (if quickplay MapId=-1 is supported), Cataclysm procedural generation (separate milestone).
Not needed for v1 (variant/Quest/Trials/menu): A5, A13, A17, A19, A21, A25, A27, A28, A29/B13.

Non-hook behaviours to carry: seed lifecycle (1.2), PushRandom handling (only if A29 is ever enabled), replaced `new Random()` sites (B1, B2, A24, B3, B5, B9), the extra `Next(1)` in B4, LocalSettingsGuard, TournamentRules variants, `Arrow.EnforceLimit` skipped (B14), and the non-RNG items in 4.2.

---

## 4. Risks and subtleties

### 4.1 RNG
- **Nesting.** Counters, not flags; `Apply()` swaps only on 0<->1 transitions. Sections nest freely (e.g. LoseHat -> hat ctor; DropNextArrow -> Arrow.Drop; OrbLogic.Update -> DoSpaceOrb; LavaControl.Added -> Lava ctor -> BubbleComplete Ignore). Ignore beats Register while open.
- **Prefix+Postfix on exceptions.** Harmony postfixes do not run if the original throws -> depth leak -> `Calc.Random` stays the gameplay generator until `CalcPatch.Reset()` (Session.StartGame, Level->MainMenu). Manual pairs (A13, A16, A17, A25, A28, B1, B2) leak the same way. TF.EX has no finalizer for these. A native port with try/finally deviates only in already-broken (exception) situations.
- **Prefix returning false / replacing results** (the draw sequence of the replacement is what counts): B1, B2, B3, B4, A16, A17, A30, A29 (rollback only), B14, `SFX.Play`/`SFXInstanced.Play` (`PATCHS/SFX/*.cs`, capture + Level scene: queued instead of played; **`SFXVaried.Play` is a separate override and is NOT replaced**, it draws `Calc.Random.Next/Choose` and also returns early when `Audio.MasterVolume <= 0`, so a registered section that played an SFXVaried would make the draw count depend on volume; none of A1-A26 does, but keep it in mind when diffing traces), `Coroutine.Update` for Miasma/MoonGlassBlock (`PATCHS/Component/Coroutine.cs:25-33`).
- **Patch target is the exact method.** Virtual overrides that do not call base are not covered: `BoltArrow.EnterFallMode` (no draws anyway). Crown/SteelHat/WoodenHat each override `Update` and each is patched; `Hat` itself has no Update. `Chain(XmlElement)` and `Orb(XmlElement,bool)` and the `Spikeball(XmlElement...)` ctors chain into the patched ctors, so hook the inner ctor.
- **Methods used from gameplay and cosmetic contexts:** `Lava.BubbleComplete` (ctor inside LavaControl.Added = gameplay section; later via sprite callback = no section), `ParticleSystem.Emit`, `TreasureSpawner.GetTreasureSpawn` (chest round start; bottomless; NR), `Arrow.EnterFallMode`. Draw count is what matters, not the context.
- **Random objects created inside a section** do not get the gameplay stream: `new Random()` inside is unseeded; `Calc.PushRandom(int)` installs a seeded System.Random (1.6). TreasureSpawner is the only object bound to the shared generator by reference.
- **Coroutine/alarm driven code across ticks:** `LevelLoaderXML.Load` is a coroutine (draws spread over frames, but sequential); corpse arrow drops are an Alarm (`Looping`, every 10 ticks); TF.EX deletes coroutines/tweens of Spikeball (`Spikeball.Update` prefix, `PATCHS/Entity/LevelEntity/Spikeball.cs:34-72`), bottomless chests, Miasma, MoonGlass and replaces their timing with tick counters (no RNG in the replacements except A13/A25).
- **Numerics.** One `NextULong` per call always (`Next(0)`/`Next(1)` still consume). `Next()` is `Next(0,int.MaxValue)` (scaled by 2147483647.0, not 2^31). `NextFloat` = `(float)double`. Keep float32 arithmetic in `Range(float,float)` and `NextFloat(max)`. C# evaluates `a(R) * b(R)` and `new Vector2(f(R), g(R))` left to right (hats). `Calc.Shuffle` loop bounds as in 1.1.
- **Seeded `System.Random` dependency:** B3 level order, B9 random map, A29 Randomize, Cataclysm generation, `LevelTiles` etc. need the .NET legacy Knuth subtractive generator (`new Random(seed)`, `Next(int)` = `(int)(InternalSample()*(1.0/int.MaxValue)*max)`), identical across runtimes if the Switch BCL is a .NET/Mono-derived one; verify with the test vector above.
- **TreasureSpawner reference binding** only happens if capture is active when the `LevelLoaderXML` ctor postfix runs (B5); if capture is off at that moment the spawner keeps its unseeded `new Random()`.
- **`GetRandomArrowType` assumptions to verify with a browser trace:** (a) the reference session has no mod registering arrow pickups (`PickupsRegistry.GetAllPickups()` with `Configuration.ArrowType`) or the list grows; (b) `Exclusions`/unlock/DLC state equal on both sides (`GameData.DarkWorldDLC` is not neutralised).
- **Reconcile with FortRise (R-FR):** `patch_TreasureSpawner.GetRandomArrowType` already does `orig` + `Calc.Choose(Random, list)`, which is what TF.EX's replacement also reproduces; the extra `Next(1)` therefore exists in the browser reference and must exist on the Switch even though vanilla has none. `patch_TreasureSpawner.GetArrowShufflePickups` adds a second `Calc.Shuffle(list)` (only matters if arrow shuffle is ever enabled).

### 4.2 Determinism items that are NOT RNG (new findings, brief)
- **Local save/unlock state beyond TreasureSpawner:** `UnlockData` (`PC/UnlockData.cs:143-170`) with unseeded `Random().Chance` can add `MoonBreakSequence` / `PurpleArcherUnlockSequence` entities on levels with `CanUnlockMoonstone="True"` (Sacred Ground 01,03,05,06,09,10) / `CanUnlockPurple="True"` (Twilight Spire 06) in `LevelLoaderXML.Load` (`PC/LevelLoaderXML.cs`, after the entity loop); depends on the local save (`PlayedAllDefaultLevels`, `RoundsPlayed`) and unseeded `Chance`; not covered by `LocalSettingsGuard`. Neutralise for online matches. `GameData.DarkWorldDLC` must match on both peers.
- **Arrow limit disabled and pools drained in capture mode:** `Arrow.EnforceLimit` skipped (`PC/Arrow.cs:260-273`: vanilla starts decaying arrows when >12 exist), `Arrow.FlushCache` on every `Arrow.Removed`, `Alarm`/`Tween` cache cleared (`PATCHS/Component/Alarm.cs`, `Tween.cs`). Affects collisions with many arrows; replicate.
- **Entity-list flush at the end of every `Level.Update`** (`network/TF.EX.Patchs/Scene/Level.cs:139,209-218`: `UpdateEntityList()` on gameplay and HUD layers, only when capture active). Vanilla flushes at the start of the next `Scene.Update`. Draw order is unchanged (nothing draws between), but `Added()` callbacks (e.g. LavaControl.Added, A12) fire at end-of-tick instead of start-of-next-tick; snapshot/hash timing differs.
- **Coroutine/tween sequences re-implemented as tick counters:** Miasma (closed-form `MiasmaSequenceController`, `state/TF.State.TowerFallExtensions/Entity/LevelEntity/MiasmaSequenceController.cs`: Versus path percent segments 540/180/660/240 ticks, `Collidable` at ticks>=120, `NervesOfSteelCheck` at ticks>=720, dissipate duration `max(20,(int)(60*startPercent))`, removed at 100 ticks; miasma coroutine suppressed by `CoroutinePatch`), Spikeball exploding spin, PlayerCorpse prism/bramble/ghost counters (`PATCHS/Entity/LevelEntity/PlayerCorpse.cs:70-177`), bottomless chest, MoonGlass, DarkPortals. At 60 Hz the vanilla coroutines may or may not match these to the tick; kills by miasma (and thus corpse-arrow draws) depend on it. Verify against a browser trace rather than assume.
- **Spawn flow differences:** B1/B2 create-then-add ordering, round-0 deferred spawn at sync, `LastManStandingRoundLogic.OnLevelLoadFinish` skipped on rollback frames only.
- **Disconnect-as-death:** `PlayerPatch.Player_Update_Postfix` calls `Die(DeathCause 9, -1, false, false)` for a player whose controller reports disconnected (`network/TF.EX.Patchs/Entity/LevelEntity/PlayerPatch.cs`), so a dropped peer produces a death (corpse draws).
- **Variants:** quickplay preset is NoAutobalance + SymmetricalTreasure (3.2).
- **Reference runtime differences:** the browser install also loads FortRise mods (`ThreeTeamMode`, `Teuria.WiderSet`); make sure they are disabled/neutral when capturing golden traces.

---

## Appendix: golden vectors (computed by me from the decompiled algorithm in Python; NOT checked against a running TF.EX)

SplitMix64(0) outputs `e220a8397b1dcdaf 6e789e6aa1b965f4 06c45d188009454f f88bb8a8724c81ec` match the published reference, so seeding is right.

| seed (int) | state after `Seed()` s0,s1,s2,s3 | first 4 `NextULong` |
|---|---|---|
| 0 | e220a8397b1dcdaf 6e789e6aa1b965f4 06c45d188009454f f88bb8a8724c81ec | 99ec5f36cb75f2b4 bf6e1f784956452a 1a5f849d4933e6e0 6aa594f1262d2d2c |
| 12345 | 22118258a9d111a0 346edce5f713f8ed 1e9a57bc80e6721d 2d160e7e5c3f42ca | be6a36374160d49b 214aaa0637a688c6 f69d16de9954d388 0c60048c4e96e033 |
| -1 | e4d971771b652c20 e99ff867dbf682c9 382ff84cb27281e9 6d1db36ccba982d2 | 8f5520d52a7ead08 c476a018caa1802d 81de31c0d260469e bf658d7e065f3c2f |

Derived (fresh generator each row):
- seed 12345: `Sample` x3 = 0.7438081631565894, 0.13004553462783452, 0.9633344930128545; `Next(2)` x6 = 1,0,1,0,1,0; `Next()` x3 = 1597315866, 279270658, 2068745070; `NextFloat` x3 = 0.7438081502914429, 0.13004553318023682, 0.9633345007896423; `Shuffle([0..7])` = [1,3,4,2,6,7,0,5].
- seed 0: `Next()` x3 = 1291202458, 1605832635, 221233742; `Shuffle([0..7])` = [1,6,3,7,2,0,5,4].
- seed -1: `Next()` x3 = 1202360426, 1648054283, 1089411295; `Shuffle([0..7])` = [1,0,7,2,6,3,5,4].
- System.Random legacy check: `new Random(42).Next()` x3 = 1434747710, 302596119, 269548474; `new Random(1).Next(100)` = 24.

Suggested validation loop: log the 4 RNG words at: after `Session.StartGame`, after `LevelLoadStart`, after spawn, after chest spawn, and every tick, on the Switch and from the browser's per-tick `GameState.Rng` (`Level.cs:220`); first divergence names the missing/extra site.
