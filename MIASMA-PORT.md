# Miasma port: TF.EX's closed-form controller on the Switch

Status: header + golden test done and bit-exact (`hw-probe/exlaunch-overlay/source/program/miasma_seq.hpp`, `hw-probe/tests/miasma-test.cpp`
vs `hw-probe/tests/miasma-ref.txt`). The game hooks that would use it are NOT written yet (section 6).

Why: TF.EX replaces the vanilla Miasma's coroutine/tween motion with a state machine advanced by `Engine.TimeMult` per tick. The Switch build still
runs the vanilla coroutine, so the miasma kills at a different tick (first observed gameplay divergence: scripted-match tick 4016).

Paths. `DEC` = `/private/tmp/claude-501/-Users-crab-Code-Towerfall-Switch-Mods/a86873af-5865-44d3-bb29-d370464237eb/scratchpad/tfex/dec` (decompiled TF.EX 0.19.2, scratch, may vanish).
`PC` = `~/Code/Towerfall Browser/reference/decompile` (vanilla 1.3.3.3). All `file:line` below are in the decompiled copies.

## 1. What TF.EX does (all sites; I grepped every decompiled assembly for "miasma", this is the complete list)

| # | Site | Where | Effect |
|---|---|---|---|
| 1 | `MiasmaPatch.Miasma_Update` (Harmony **prefix** on `Miasma.Update`, original still runs after) | `DEC/TF.State.Patchs/TF.State.Patchs.Entity.LevelEntity/MiasmaPatch.cs:15-49` | The per-tick state machine, section 3. Runs only if `StateFlags.IsCaptureActive && !IsRestoring` (:19). |
| 2 | `Miasma_Update_FreezeCosmetics` / `..._UnfreezeCosmetics` (prefix/postfix on `Miasma.Update`) | `MiasmaPatch.cs:51-63` | `CosmeticFreeze.Freeze(entity)`: cosmetic RNG/sine freeze. No gameplay effect, not needed for tick parity. |
| 3 | `Miasma_Dissipate` (prefix on `Miasma.Dissipate`, original still runs) | `MiasmaPatch.cs:65-80` | If capture active and not already dissipating: `IsDissipating = true; DissipateTicks = 0; DissipateStartPercent = entity.Percent; DissipateStartSideWeight = entity.SideWeight`. |
| 4 | `CoroutinePatch.Coroutine_Update` (prefix on `Monocle.Coroutine.Update`) | `DEC/TF.State.Patchs/TF.State.Patchs.Component/CoroutinePatch.cs:24-31` | `if (coroutine.Entity is Miasma) return !StateFlags.IsCaptureActive;` i.e. the vanilla coroutine never runs while capture is active (so no tweens are ever created, `Collidable` stays whatever the patch set, and the vanilla `dissipating` field is never set). |
| 5 | `RoundLogicPatch.RoundLogic_OnUpdate_Prefix` ("miasma bookkeeping") | `DEC/TF.State.Patchs/TF.State.Patchs.RoundLogic/RoundLogicPatch.cs:282-306` | Capture active only. Before the original `RoundLogic.OnUpdate`: `RoundLogic.miasma = (miasmaCounter.Value > 0) ? null : Level.Get<Miasma>()` (private field, via DynamicData). See 1.2. |
| 6 | `Session_LevelLoadStart` (postfix on `Session.LevelLoadStart`) | `DEC/TF.State.Patchs/TF.State.Patchs/SessionStatePatch.cs:32-40` | `if (!IsRestoring) session.Miasma = Miasma.Default()`. |
| 7 | `LayerPatch.Layer_Remove_Entity` (postfix on `Layer.Remove(Entity)`) | `DEC/TF.State.Patchs/TF.State.Patchs.Layer/LayerPatch.cs:46-50` | `if (entity is Miasma) session.Miasma = Miasma.Default()`: any removal of a Miasma entity (dissipate finished, level delete, restore's `level.Delete<Miasma>()`) resets the state. Note `Default()` also restores `Counter = 1500f`. |
| 8 | Session resets | `SessionService.Reset` `DEC/TF.State.Domain/TF.State.Domain.Services/SessionService.cs:31-41`, `StateContext.NewSession` `.../TF.State.Domain.Context/StateContext.cs:43-52`, `StateContext.Reset` `:272-278`, `GameState.Session` default `.../Models/GameState.cs:20-28`; callers `SessionStatePatch.Session_StartGame` (`SessionStatePatch.cs:15-31`, calls `StateContext.Reset()`), `NetplayManager` reset (`DEC/TF.EX.Domain/TF.EX.Domain.Services/NetplayManager.cs:771` -> `TfStateApi.ResetSession`, `TfStateApi.cs:320-323`) | Every one sets `Miasma = Miasma.Default()`. |
| 9 | `LevelLoaderXMLStatePatch` ctor postfix `Reset` | `DEC/TF.State.Patchs/TF.State.Patchs.Scene/LevelLoaderXMLStatePatch.cs:39-59` -> `Level.ResetState` `DEC/TF.State.TowerFallExtensions/.../LevelExtensions.cs:159-165` | **Does not touch the miasma** (it deletes arrows/chests/pickups/corpses and resets the LMS round-end counters). Listed because it was asked: the miasma state is reset by #6 (each round's `LevelLoadStart`) instead. |
| 10 | Save (rollback snapshot) | `LevelExtensions.AddSessionState` `LevelExtensions.cs:964-1013` (miasma fields copied at :996-1008, `Counter` read from `RoundLogic.miasmaCounter.counter` at :980-987) | Snapshot = the 9-field `Miasma` model. |
| 11 | Restore (rollback load) | `LevelExtensions.LoadState` `LevelExtensions.cs:236-`, session at :330-340, miasma at :408-447; helpers `AddMiasmaToGameplayLayer` :1226-1240, `RestoreMiasmaVisualPhase` :1242-1260, `TransplantMiasmaSpriteState` :1262-1295 | Writes `miasmaCounter.counter`, deletes the live Miasma entity, and if the snapshot was dissipating re-creates one (`new Miasma(Versus)`, depth = `ActualDepth` = -1000000.0), applies `EvaluateDissipate` to it, `Collidable=false`, `NervesOfSteelCheck = Evaluate(mode, Ticks, Dir).NervesOfSteelCheck`, calls `entity.Update()` once (the prefix is a no-op because `IsRestoring`), restores the cosmetic sine phase; else if `Ticks > 0` re-creates it with `Evaluate(...)` applied; else (Ticks == 0) leaves no miasma. |
| 12 | Model + constants | `DEC/TF.State.Domain/TF.State.Domain.Models/Miasma.cs:5-41` (keys 0-8: Counter, Mode, Ticks, Dir, IsDissipating, DissipateTicks, DissipateStartPercent, DissipateStartSideWeight, ActualDepth), `Constants.cs:17,21` (`DEFAULT_MIASMA_COUNTER = 1500f`, `MIASMA_CUSTOM_DEPTH = -1000000.0`) | `Miasma.Mode` is **never set** to anything but 0 by TF.EX (no assignment exists outside the restore copy), so a Versus game is always `ModeNormal`. A boss Miasma (`new Miasma(CataclysmBoss/TheAmaranthBoss)`, `PC/TowerFall/CataclysmEye.cs:1326`, `AmaranthBoss.cs:293`) created under capture would be driven as Versus; irrelevant, those modes are not netplay. |
| 13 | Controller | `DEC/TF.State.TowerFallExtensions/TF.State.TowerFallExtensions.Entity.LevelEntity/MiasmaSequenceController.cs:28-97` | Ported 1:1, section 2. |

`ScenarioCatalog.cs:846,864,1414-1429` only mentions the miasma in TF.EX's own test scenarios. `KillStats.MiasmaKills` / `MatchStats.KillsDuringMiasma` are stats mirrors (the vanilla stat code, `PC/TowerFall/Player.cs:3406-3409`, reads `Miasma.NervesOfSteelCheck`, which is why the patch still writes it).

### 1.1 Where the miasma starts (unchanged vanilla, TF.EX does not patch it)

- `RoundLogic` ctor, `PC/TowerFall/RoundLogic.cs:53-78`: `miasmaCounter = new Counter(0)` if the mode cannot have a miasma (`canHaveMiasma: false`: DarkWorld, Quest, Trials, LevelTest; `true`: LastManStanding, HeadHunters, TeamDeathmatch, Warlord), `Counter(60)` with SuddenDeath, `Counter(1500)` normally, `Counter(0)` with NoTimeLimit. A new `RoundLogic` (hence a new counter) is built per level in `Session.LevelLoadStart` (`PC/TowerFall/Session.cs:112-120`).
- `RoundLogic.OnUpdate`, `RoundLogic.cs:90-105`: nothing happens until `RoundStarted`. Then `if (CanMiasma && (bool)miasmaCounter)` (`Counter` truthiness = `counter > 0f`, `PC/Monocle/Counter.cs:42-45`): `miasmaCounter.Update()` (`counter -= Engine.TimeMult`, `Counter.cs:22-25`); when it falls to <= 0: `Session.CurrentLevel.Add(miasma = new Miasma(Miasma.Modes.Versus))` (:101). With TimeMult 1.0000019f the 1500 counter reaches <= 0 on the 1500th started-round tick.
- `Level.Add` only queues the entity. `Scene.Update` (`PC/Monocle/Scene.cs:78-91`) calls `UpdateEntityLists` first, so the Miasma is inserted into layer 0 and gets its **first `Update` in the same tick it was created** (`Layer.UpdateEntityList`, `PC/Monocle/Layer.cs:148-175`); first closed-form step: `Ticks = 0 + TimeMult`.
- Ctor (`PC/TowerFall/Miasma.cs:44-91`): depth -600, `Collidable = false`, `Percent = -0.1f`, `SideWeight = 0`, hitboxes, coroutine + sprites + sines, `sfx_miasmaLoop`. Not patched by TF.EX.
- `RoundLogic.OnPlayerDeath`, `RoundLogic.cs:122-125`: `if ((bool)miasma && killerIndex != -1 && FFACheckForAllButOneDead()) miasma.Dissipate();` (vanilla; only caller in Versus. Boss code calls `Dissipate()` too: `PC/TowerFall/DarkWorldBoss.cs:152`).

### 1.2 What the bookkeeping (#5) means for gameplay

Vanilla keeps `miasma` pointing at the created entity forever. TF.EX re-derives it every tick (so a restored game, whose Miasma entity was re-created, is wired up). Without rollback the two are gameplay-equivalent: the only reader is `OnPlayerDeath`'s `Dissipate()` call and a `Dissipate()` on a removed/dissipating miasma is a no-op in both (vanilla: `dissipating` is true; TF.EX: prefix guards on `IsDissipating`, and the original body only re-seeds a coroutine that never runs). For a rollback-capable Switch build it must be done on restore (re-point `RoundLogic.miasma` at the re-created entity, null it if the counter is still > 0).

## 2. The controller (ported in `miasma_seq.hpp`)

All floats are binary32 with C# operation order. Helpers and where they come from:

| Helper | Source | Definition used |
|---|---|---|
| `Ease.CubeIn` | Monocle (TowerFall.exe), `PC/Monocle/Ease.cs` | `t * t * t` (left-assoc) |
| `Ease.CubeOut` | same | `Invert(CubeIn)`: `1f - CubeIn(1f - t)` |
| `Ease.CubeInOut` | same | `Follow(CubeIn, CubeOut)`: `t <= 0.5f ? CubeIn(t*2f)/2f : CubeOut(t*2f - 1f)/2f + 0.5f` |
| `MathHelper.Lerp` | FNA `src/MathHelper.cs:239-242` | `v1 + (v2 - v1) * amount` |
| `MathHelper.Clamp` | FNA `MathHelper.cs:151-164` | `if (v > max) v = max; else if (v < min) v = min;` |
| `Math.Max(float,float)` | .NET | NaN-propagating, +0 over -0 |
| float `%` | C# | IEEE fmod (exact) -> `std::fmod` |
| `(int)(60f * x)` | C# | truncating conversion (saturating on .NET 9/ARM64) |

`DeterministicMath` is NOT used by the controller (no sin/cos/pow), so `dmath.hpp` is not needed.

Versus (`ModeNormal`), `ticks` = `Miasma.Ticks` (float, += TimeMult each tick):

| ticks | Percent |
|---|---|
| < 540 | `Lerp(-0.1f, 0.33f, CubeOut(ticks/540f))` |
| < 720 | `Lerp(0.33f, 0.15f, CubeOut((ticks-540f)/180f))` |
| < 1380 | `Lerp(0.15f, 1.05f, CubeInOut((ticks-720f)/660f))` |
| < 1620 | `Lerp(1.05f, -0.1f, CubeIn((ticks-1380f)/240f))` |
| else | `-0.1f` |

`SideWeight = 0`, `Collidable = ticks >= 120f`, `NervesOfSteelCheck = ticks >= 720f`. `ModeAmaranthBoss` and `ModeCataclysmBoss` are ported too (`SideWeight` sweep `(ticks-180f) % 960f`, direction roll at `ticks >= 180f`); they are never reached in Versus.

Dissipate: `num = Math.Max(20f, (int)(60f * startPercent)); e = CubeInOut(Clamp(ticks/num, 0f, 1f)); Percent = Lerp(startPercent, -0.1f, e); SideWeight = Lerp(startSide, 0f, e); remove = ticks >= 100f`.

## 3. Order of operations per tick (capture active, not restoring)

`Level.Update` (`PC/TowerFall/Level.cs:312-361`):

1. `Session.OnUpdate` (`Level.cs:340`, `PC/TowerFall/Session.cs:214-218`) -> `RoundLogic.OnUpdate`:
   1. (TF.EX prefix #5) `miasma = counter.Value > 0 ? null : Level.Get<Miasma>()`.
   2. original: `miasmaCounter.Update()` (-TimeMult) and, when it reaches <= 0, `Level.Add(miasma = new Miasma(Versus))` (queued).
2. `base.Update()` -> `Scene.Update`: `UpdateEntityLists` (the new Miasma enters layer 0), then entities of the layer in **descending `actualDepth`**:
   - Players (depth -100, `Player.cs:580`) update BEFORE the Miasma (-600). `Player.Update` does `CollideDo(PlayerCollider, ...)` (`PC/TowerFall/Player.cs:1381`) -> `Miasma.OnPlayerCollide` -> `Hurt(DeathCause.Miasma)` using the hitbox widths and `Collidable` computed in the **previous** tick's `Miasma.Update`. A death can call `RoundLogic.OnPlayerDeath` -> `miasma.Dissipate()` (#3) in the middle of the player update pass.
   - `Miasma.Update` (`PC/TowerFall/Miasma.cs:145-158`), as Harmony does it: prefix #1 -> prefix #2 -> original -> postfix #2.
     - prefix #1:
       - if `IsDissipating`: `DissipateTicks += TimeMult`; `EvaluateDissipate(DissipateTicks, startPercent, startSide)` -> `entity.Percent`, `entity.SideWeight`; `entity.Collidable = false`; if `shouldRemove` (`DissipateTicks >= 100f`) `entity.RemoveSelf()`; **return** (`Ticks` and `NervesOfSteelCheck` untouched).
       - else: `Ticks += TimeMult`; if `ShouldRollAmaranthDir(Mode, Ticks, Dir)` (never in Versus) the RNG roll (section 4); `r = Evaluate(Mode, Ticks, Dir)`; write `entity.Percent`, `entity.SideWeight`, `entity.Collidable`, `entity.NervesOfSteelCheck`.
     - original `Update` (still runs, also on the tick `RemoveSelf` was called): `base.Update()` (components; the coroutine's `Update` is skipped by #4), loop volume `SetVolume`, `leftHitbox.Width = max(0, (Percent+SideWeight)*160 - 5)`, `rightHitbox.Width = max(0, (Percent-SideWeight)*160 - 5)`, `rightHitbox.Right = 320`, sprite x positions.
3. The Miasma kills in tick N+1's player pass using the state written in tick N. `Collidable` is first true after the update whose `Ticks >= 120f` (with TimeMult 1.0000019f that is the 120th Miasma update).

`Dissipate()` (#3) is called from inside the player pass; it does NOT touch `Collidable` / `Percent`: those change at the next `Miasma.Update` (prefix #1's dissipating branch). The original `Dissipate` body then runs too (`if (!dissipating) coroutine.Replace(DissipateSequence())`, `PC/TowerFall/Miasma.cs:176-182`), which only re-arms a coroutine that #4 never runs; the vanilla field `dissipating` therefore stays false under TF.EX. Harmless; the Switch hook may call or skip the original.

`TimeMult` = `Engine.TimeMult`, 1.0000019f (0x3f800010) in TF.EX sessions (already set on the Switch, `xplay.cpp` ~637). `Ticks`/`DissipateTicks` accumulate it as a float, so the thresholds (`>= 120f`, `>= 720f`, `>= 100f`) are hit on the same update as a plain integer count except where the 1.0000019 drift crosses a boundary; use the header's float accumulation, never an integer counter.

## 4. RNG

- Versus: **none**. `ModeNormal` never rolls; the vanilla Versus `Sequence()` draws nothing either (`PC/TowerFall/Miasma.cs:93-125`).
- Only `ModeAmaranthBoss` rolls (never reached in Versus because TF.EX never sets `Mode`): in `Miasma_Update`, after `Ticks += TimeMult` and before `Evaluate`, in the first update where `Mode == 1 && Dir == 0 && Ticks >= 180f`: `CalcPatch.RegisterRng(); Dir = Calc.Choose<int>(Calc.Random, new[]{1,-1}); CalcPatch.UnregisterRng();` (`MiasmaPatch.cs:38-43`). `Calc.Choose` (`PC/Monocle/Calc.cs:382-385`) = `choices[random.Next(2)]` on the gameplay stream: **one `Next(2)` draw, index 0 -> +1, index 1 -> -1**, once per miasma. Vanilla equivalent: `PC/TowerFall/Miasma.cs:220` (`Calc.Random.Choose<int>(1, -1)`, drawn at coroutine tick ~180 and with the non-gameplay Random). `miasma_seq::Step` takes a callable that must perform exactly that `Next(2)`.
- `GAMEPLAY-RNG-PORT.md` row A13 agrees.

## 5. What vanilla does instead (why the tick differs)

`PC/TowerFall/Miasma.cs:93-143`: a `Coroutine(Sequence())` that starts `Tween`s (`Tween.Create(Oneshot, ease, duration, start:true)`, `Percent = Lerp(...t.Eased)`) and waits with `yield return 120 / 420 / 180 / 660`. Differences from the closed form: (a) tweens are `Add`ed while the entity is `Updating`, so they are queued and their first update is one tick later; (b) `Tween.Update` (`PC/Monocle/Tween.cs:102-130`) uses `FramesLeft -= TimeMult; Percent = 1 - max(0,FramesLeft)/Duration`, a different float accumulation than `ticks/540f`; (c) the coroutine waits with `Counter.Update` per tick, and `Coroutine.OneFrameDelay` (set to 1 every session on the Switch, `xplay.cpp` ~1285; TF.EX sets it) adds a one-tick delay per `yield return` (`PC/Monocle/Coroutine.cs:39-81`); so `Collidable = true`, `NervesOfSteelCheck = true` and each tween segment start drift by a few ticks and accumulate, and `Dissipate` uses `coroutine.Replace` + a tween of `Math.Max(20, (int)(60f * Percent))` frames + `yield return 100` then `RemoveSelf`. This is derived from reading the sources, not measured on device.

## 6. What the Switch hook has to do

Game methods to intercept (all gated on "capture active", i.e. netplay/test mode):

1. **`Miasma.Update`**: pre-hook, then call the original. Pre-hook: `out = miasma_seq::Step(state, Engine.TimeMult, rngNext2)`; write `entity.Percent`, `entity.SideWeight`, `Entity.Collidable`, and (if `out.SetNerves`) `entity.NervesOfSteelCheck`; if `out.RemoveSelf` call `Entity.RemoveSelf` (still run the original afterwards, as TF.EX does). `rngNext2` is only ever invoked for `Mode == 1`; wire it to the existing gameplay-stream `Next(2)` inside `rnd::RegisterRng/UnregisterRng`. Skip when restoring.
2. **`Miasma.Dissipate`**: pre-hook `miasma_seq::BeginDissipate(state, entity.Percent, entity.SideWeight)`; call or skip the original (equivalent, see section 3).
3. **`Coroutine.Update`**: when the coroutine's owning entity is a `Miasma` (and capture is active) return without running it. (Already hooked on the Switch for logging at `Coroutine.Update` +0x920d10, `xplay.cpp:714`; the type check is on `Component.Entity`.)
4. **`RoundLogic.OnUpdate` (miasma part)**: nothing to change for the creation logic (vanilla). Add the #5 bookkeeping only if rollback restore is implemented (re-point `RoundLogic.miasma`).
5. **`Miasma` ctor**: no hook (TF.EX has none). The state is global (`miasma_seq::State`), not per entity.
6. **State resets** to `State::Default()`: `Session.LevelLoadStart` (not restoring), removal of a Miasma entity (`Layer.Remove(Entity)` with a Miasma; or detect it from `Step` returning `RemoveSelf`), `Session.StartGame`, netplay reset.
7. **Rollback** (if used): snapshot/restore the `State` struct (+ `Counter` mirror of `RoundLogic.miasmaCounter`), and re-create/delete the entity as in `LevelExtensions.cs:408-447`.

The per-entity field offsets of `Miasma` (`Percent`, `SideWeight`, `NervesOfSteelCheck`) and the method addresses are not in this doc (nothing was disassembled; no device or Ryujinx was touched).

## 7. Test

`hw-probe/tests/miasma-test.cpp` -> must equal `hw-probe/tests/miasma-ref.txt` (4,863 lines, 210 KB):

```
cd hw-probe/tests && c++ -std=c++17 -O2 -ffp-contract=off -I../exlaunch-overlay/source/program miasma-test.cpp -o /tmp/ms && /tmp/ms | diff - miasma-ref.txt
```

Reference generator (scratch, outside the repo): `.../scratchpad/miasma-golden/` (`golden.csproj`, `Program.cs`, net10). It runs the REAL `TF.State.TowerFallExtensions.dll::MiasmaSequenceController` and the real `TF.State.Domain.dll::Miasma` from `DShad.TF.State`, with the real `Monocle.Ease` from the Steam `TowerFall.exe` and `MathHelper` from the Steam `FNA.dll` (both are x86 PE32 images; the copies in `lib/` only have the `32BITREQUIRED` CLR flag cleared so the arm64 runtime loads them; IL untouched). Coverage: Evaluate for modes 0-3 x dirs {0,+1,-1} x {4001 integer ticks, 4000 ticks accumulated by 1.0000019f, 20,000 random floats in [0,4100), 185 ulp-neighbours of every breakpoint} as FNV hashes plus explicit lines (edges and every 20th tick); ShouldRoll over the same sets; EvaluateDissipate (16 x 4 x 20 explicit lines, plus a 155 x 68 x 421 hashed grid built from reachable Percent/SideWeight values); and a full per-tick simulation (TimeMult 1.0000019f, 1.0f, 0.5f; modes 0/1/2; a dissipate call after every possible tick for mode 0) through `Step`/`BeginDissipate` against a hand transcription of `MiasmaPatch` onto the real model.

Result: the C++ output is byte-identical to the .NET output at `-O0`, `-O2`, `-O3`, `-Os`, with `-ffp-contract=off` and with clang's default `-ffp-contract=on`. It is NOT identical with `-ffp-contract=fast` (FMA changes `Lerp`), which confirms the build must keep `-ffp-contract=off`; the header's `#pragma clang fp contract(off)` is a safety net for the default mode only.

## 8. Uncertain / not verified

- The Harmony-patch logic (`Step`, `BeginDissipate`) is verified against my C# transcription of `MiasmaPatch`, not against the live patch (Harmony cannot run offline). The controller math is verified against the real DLL.
- The golden uses the Steam PC build of `TowerFall.exe`/`FNA.dll`; TF.EX's DLL references FNA 26.6.0.0 and the Switch build uses its own MathHelper. `Lerp`/`Clamp` are the same formulas in the vendored FNA source at `~/Code/Towerfall Browser/vendor/FNA/src/MathHelper.cs` (the one I read; I did not compare it with the Steam `FNA.dll` line by line), and the Switch's AOT code is a straight compile of them, but I did not diff the Switch binary.
- Section 5 (vanilla timeline) is from source reading, not a measured trace; the exact vanilla-vs-closed-form tick offsets were not computed.
- The 4016 divergence was not reproduced here (offline work only).
