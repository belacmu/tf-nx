# TF.State's gameplay patches in the translator (evaluation rec. 8, step 1; v0.21.1; 2026-10-18)

Step 1 brings TF.State's gameplay code into the translator's scope and generates it, NOT wired: the hand ports in rnd.cpp / xplay.cpp /
sfx.cpp stay in use (adopting them needs a test day, with the lockstep compares per tower, for determinism).
- **Scope** (overrides.toml [scope]): `state/TF.State.Patchs/`, `state/TF.State.Domain/` and `state/TF.State.TowerFallExtensions/` are
  in. Out: `state/TF.State.Core/` (the module bootstrap and API) and `state/TF.State.Domain/Models/` (MessagePack serialization). The
  [skip] S-rollback keys (GetState / LoadState, the snapshot services) keep the serialization parts out of the count.
- **Generated:** `tools/tfex/translate.sh coverage` translates every in-scope type and compiles it with the Switch toolchain, in
  `tools/tfex/translator/test/out/compile` (not in gen/, not built into the module).

## Result

**113 of 219 applicable methods (1366 of 3548 lines) translate** (step 1: 60 / 808; step 2: 105 / 1083; step 3: 111 / 1315). Whole
report: 932 of 1145 methods, 12400 of 18141 lines (tools/tfex/reports/translator-coverage-v0.21.1.md; network/ alone: 827 of 926, 89%).

Step 2 (this commit) added these [service] rules, so the generated code uses the Switch's own pieces:
- CalcPatch.RegisterRng / UnregisterRng / IgnoreToRegisterRng / UnignoreToRegisterRng / Reset / IsGameplayRngActive -> rnd.cpp's
  RegisterRng / UnregisterRng / IgnoreRng / UnignoreRng / ResetRng / Overriding. These unblocked 45 methods.
- StateFlags.IsCaptureActive -> rnd::Capture.
- IRngService.GetSeed / SetSeed / Reset -> rnd.cpp's stream seed.
- ISFXService.AddSoundEffect / GetSoundEffectName -> sfx.cpp's keying (no name table).

## What blocks the other 159 (a method can have several)

| blocker | methods | what it takes |
|---|---|---|
| unbound game members | 75 | 48 private game fields the binder can't resolve (translate.toml [pin]s, verified in the disassembly as the hand ports did), 12 methods not found (DeterministicMath's Math calls among them: a `gen::` math helper, as the Switch's dmath.hpp), 11 FortRise / Harmony / ChaliceGhost types (ledger rules) |
| TF.State's services, no unit (IStateContext, ISessionService, IHUDService, ISFXService, IRngService) | 49 | [service] rules onto the Switch's existing pieces: rnd.cpp (rng streams, seed), sfx.cpp (desired / current SFX), miasma_seq.hpp (session) |
| generated C++ doesn't compile | 22 | translator fixes (mostly DeterministicMath) |
| generic game method / Traverse.SetValue / user-defined conversion Variant -> bool / TypeOf | 6 / 4 / 3 / 3 | translator constructs (the same kinds network/ needed) |
| LocalSettingsGuard.Neutralize, UnlockDataPatch.SkipInExSession, IcicleOwnedState, QuestSpawnPortal tweens | 2-4 each | follow once the rng / service rules are in |

**Order to go further** (each a build-only step; wiring stays for a test day):
1. ~~The CalcPatch rng rules~~ (done).
2. TF.State's service rules: rng and SFX naming done. Left: ISessionService.GetSession / GetRoundData (Miasma, round state: TF.State's Session model, which is excluded with Models/), IHUDService (HUD coroutine state), and ISFXService.AddDesired (TF.State's SFX model). These need either Models/ brought in for those types, or rules onto miasma_seq.hpp / sfx.cpp that take the fields.
3. ~~DeterministicMath~~ (done: 12 of 12; the translator now maps double/float.IsNaN / IsInfinity to compiler builtins, NaN / infinity
   literals, and `%=` on doubles to fmod).
4. The translator constructs: a game type's conversion operator (Variant -> bool) now calls its body (done). Left: 48 private game
   fields (pins, each verified in the disassembly), generic game methods (6), Traverse.SetValue (4), ChaliceGhost / FortRise types
   (ledger rules), DeterministicRandom (its own C# ulong state: a unit or the Switch's xoshiro.hpp), 16 compile errors.

## Per file (methods translating / applicable, lines)

| file | types | methods ok / all | lines ok / all |
|---|---|---|---|
| state/TF.State.Domain/Context/StateFlags.cs | StateFlags | 3 / 3 | 14 / 14 |
| state/TF.State.Domain/DeterministicMath.cs | DeterministicMath | 12 / 12 | 305 / 305 |
| state/TF.State.Domain/Extensions/VersusLevelSystemExtensions.cs | VersusLevelSystemExtensions | 0 / 2 | 0 / 44 |
| state/TF.State.Domain/Randomness/DeterministicRandom.cs | DeterministicRandom | 2 / 15 | 9 / 114 |
| state/TF.State.Domain/Services/RngService.cs | RngService | 1 / 7 | 4 / 27 |
| state/TF.State.Domain/Services/SFXService.cs | SFXService | 3 / 15 | 34 / 160 |
| state/TF.State.Patchs/Calc/Calc.cs | CalcPatch, CalcPushRandomPatch | 7 / 8 | 53 / 70 |
| state/TF.State.Patchs/Component/ArrowCushion.cs | ArrowCushionPatch | 2 / 2 | 12 / 12 |
| state/TF.State.Patchs/Component/Coroutine.cs | CoroutinePatch | 0 / 1 | 0 / 45 |
| state/TF.State.Patchs/DarkPortalsVariantSequence.cs | DarkPortalsVariantSequencePatch | 1 / 2 | 8 / 91 |
| state/TF.State.Patchs/DeterministicMathPatch.cs | DeterministicMathPatch | 0 / 2 | 0 / 36 |
| state/TF.State.Patchs/Entity/LevelEntity.cs | LevelEntityPatch | 2 / 2 | 15 / 15 |
| state/TF.State.Patchs/Entity/LevelEntity/Arrow.cs | ArrowPatch | 6 / 7 | 41 / 52 |
| state/TF.State.Patchs/Entity/LevelEntity/Bat.cs | BatPatch | 7 / 7 | 203 / 203 |
| state/TF.State.Patchs/Entity/LevelEntity/BrambleArrow.cs | BrambleArrowPatch | 0 / 2 | 0 / 43 |
| state/TF.State.Patchs/Entity/LevelEntity/Chain.cs | ChainPatch | 2 / 2 | 12 / 12 |
| state/TF.State.Patchs/Entity/LevelEntity/ChaliceGhost.cs | ChaliceGhostPatch | 2 / 4 | 26 / 105 |
| state/TF.State.Patchs/Entity/LevelEntity/Hat.cs | CrownRngPatch, SteelHatRngPatch, WoodenHatRngPatch | 6 / 6 | 18 / 18 |
| state/TF.State.Patchs/Entity/LevelEntity/Icicle.cs | IciclePatch | 0 / 6 | 0 / 136 |
| state/TF.State.Patchs/Entity/LevelEntity/Lava.cs | LavaPatch | 2 / 2 | 12 / 12 |
| state/TF.State.Patchs/Entity/LevelEntity/LavaControl.cs | LavaControlPatch | 6 / 6 | 36 / 36 |
| state/TF.State.Patchs/Entity/LevelEntity/Miasma.cs | MiasmaPatch | 0 / 2 | 0 / 68 |
| state/TF.State.Patchs/Entity/LevelEntity/MoonGlassBlock.cs | MoonGlassBlockPatch | 0 / 1 | 0 / 11 |
| state/TF.State.Patchs/Entity/LevelEntity/Orb.cs | OrbPatch | 1 / 2 | 6 / 17 |
| state/TF.State.Patchs/Entity/LevelEntity/OrbPickup.cs | OrbPickupPatch | 1 / 2 | 6 / 12 |
| state/TF.State.Patchs/Entity/LevelEntity/Pickup.cs | PickupPatch | 2 / 2 | 29 / 29 |
| state/TF.State.Patchs/Entity/LevelEntity/Player.cs | PlayerStatePatch | 4 / 4 | 24 / 24 |
| state/TF.State.Patchs/Entity/LevelEntity/PlayerCorpse.cs | PlayerCorpsePatch | 3 / 8 | 20 / 191 |
| state/TF.State.Patchs/Entity/LevelEntity/PlayerGhost.cs | PlayerGhostPatch | 6 / 6 | 134 / 134 |
| state/TF.State.Patchs/Entity/LevelEntity/QuestSpawnPortal.cs | QuestSpawnPortalPatch | 1 / 6 | 6 / 67 |
| state/TF.State.Patchs/Entity/LevelEntity/SensorBlock.cs | SensorBlockPatch | 0 / 3 | 0 / 89 |
| state/TF.State.Patchs/Entity/LevelEntity/Slime.cs | SlimePatch | 2 / 2 | 61 / 61 |
| state/TF.State.Patchs/Entity/LevelEntity/Spikeball.cs | SpikeballPatch | 0 / 2 | 0 / 55 |
| state/TF.State.Patchs/Entity/LevelEntity/TeamReviver.cs | TeamReviverPatch | 3 / 5 | 21 / 98 |
| state/TF.State.Patchs/Entity/LevelEntity/TechnoMage.cs | TechnoMagePatch | 4 / 7 | 50 / 158 |
| state/TF.State.Patchs/Entity/LevelEntity/TreasureChest.cs | TreasureChestPatch | 4 / 8 | 64 / 195 |
| state/TF.State.Patchs/Entity/TreasureSpawner.cs | TreasureSpawnerPatch | 5 / 10 | 40 / 95 |
| state/TF.State.Patchs/Layer/Layer.cs | LayerPatch | 0 / 1 | 0 / 41 |
| state/TF.State.Patchs/LocalSettingsGuard.cs | LocalSettingsGuard | 1 / 3 | 15 / 51 |
| state/TF.State.Patchs/ModIO.cs | ModIOPatch | 1 / 1 | 9 / 9 |
| state/TF.State.Patchs/Monocle.cs | MonoclePatch | 0 / 1 | 0 / 27 |
| state/TF.State.Patchs/OrbLogic.cs | OrbLogicPatch | 8 / 10 | 48 / 126 |
| state/TF.State.Patchs/RoundLogic/RoundLogic.cs | RoundLogicPatch | 3 / 8 | 31 / 233 |
| state/TF.State.Patchs/SFX/SFX.cs | SFXPatch | 0 / 2 | 0 / 51 |
| state/TF.State.Patchs/SFX/SFXInstanced.cs | SFXInstancedPatch | 0 / 1 | 0 / 41 |
| state/TF.State.Patchs/Scene/LevelLoaderXML.cs | LevelLoaderXMLStatePatch | 0 / 1 | 0 / 23 |
| state/TF.State.Patchs/Session.cs | SessionStatePatch | 0 / 2 | 0 / 29 |
| state/TF.State.Patchs/UnlockData.cs | UnlockDataPatch | 0 / 5 | 0 / 34 |
| state/TF.State.Patchs/VersusLevelSystem.cs | VersusLevelSystemPatch | 0 / 1 | 0 / 29 |
