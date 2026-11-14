# Should we have ported FortRise first? (research, 2026-10-17)

Read-only research for the coordinator. Nothing was built or run.

**Question (the user):** "We went straight to making TF.EX work on Switch, but TF.EX is made to work on FortRise on PC. Should we
actually have been looking more at FortRise first? Or is it not necessary?"

**Short answer.** Porting FortRise as a layer is not necessary, and it would cost a lot for little gain. Most of what TF.EX
takes from FortRise is a loader and mod API: Harmony, the DI-style module ctor, registries for textures, variants, archers,
commands and settings, and mod-to-mod interop. On the Switch, exlaunch and hand-written glue already stand in for all of it.
None of that is gameplay.

There is a real gap, though, and it is narrower than "FortRise". FortRise is a set of **MonoMod patches to TowerFall.exe**
(`TowerFall.FortRise.mm.dll`, 88 `patch_*` classes in TowerFall plus 17 in Monocle). The browser runs
**vanilla + FortRise's patches + TF.EX**, and that combination is the reference the Switch has to match.

The project already accounts for the FortRise behaviour it found: the extra arrow-type draw, the GoalScore table, TeamMode under
mode 11, the level list and order, and the WiderSet tower registration. It found these one at a time, while chasing desyncs.
**No one has audited FortRise's patches systematically**: `docs/PARITY-TRIAGE.md` compared the Switch with
`.cache/towerfall-pc.il`, which is vanilla TowerFall.exe and has no FortRise code (0 `orig_`/FortRise symbols). Section 2 is that audit.

The recommendation (section 3): don't port or translate FortRise. Keep the stand-ins, and make "PC + FortRise" the parity
reference instead of "PC vanilla". In practice:
- re-run the parity triage against `TowerFall.Patch.dll`, the patched assembly the browser actually runs;
- treat the versus-relevant `patch_*` classes as a short port list;
- point the translator's reference at the patched assembly, so FortRise members (`orig_*`, `IsCustom`, `CustomVariants`,
  `GenCataclysm`) resolve instead of showing as `Invalid`.

Sources:
- TF.EX v0.21.0 (`vendor/tfex`, built against FortRise 5.5.0-beta.2: `vendor/tfex/libs/README.md`,
  `src/network/TF.EX.Core/meta.json:9`).
- The browser's FortRise 5.5.0-beta.3 (`~/Code/Towerfall Browser/tools/fetch-fortrise.sh:12`,
  `vendor/fortrise/version.txt`).
- FortRise's patch module, decompiled at `.cache/fortrise-browser-mm/` in the main checkout (cited below as `mm/`).
- Its two built-in mods, `FortRise.Content` and `FortRise.WorkshopFixes`, decompiled by me with ilspycmd.
- The browser's default mod set: `web/wwwroot/mods.js:46`, where `DEFAULT_ENABLED = ["TF.EX"]` plus dependencies TF.State
  and TF.Replay.

---

## 1. How TF.EX depends on FortRise, and what the Switch does

TF.EX is four FortRise mods: TF.EX, TF.State, TF.Replay and TF.InputDisplayer, 38 files that name FortRise. Each one:
- derives `FortRise.Mod` and takes `(IModContent, IModuleContext, ILogger)`;
- applies its own Harmony patches with `context.Harmony.PatchAll`: about 351 patch methods on 110 types, and only one of
  them targets a FortRise type (`ModIO.LoadXml`);
- talks to the others through FortRise interop (`GetApi`, `Interop.LoadedMods`, MonoMod `ModInterop`).

| # | Dependency | Where in TF.EX | What it is | Switch today | Switch pointer |
|---|---|---|---|---|---|
| 1 | Mod base class, ctor injection, `OnInitialize` / `OnUnload` | `TF.EX.Core/TFEXModModule.cs:10-27,127`; `TF.State.Core/TFStateModule.cs:9-31` | loader lifecycle | **replaced**: exlaunch `main.cpp` installs hooks; services are singletons | TFEX-BASE.md S-native; `tools/tfex/overrides.toml:56-77` (DI ctors skipped) |
| 2 | Harmony (`context.Harmony.PatchAll`) | `TFEXModModule.cs:133`; `TFStateModule.cs:24-25`; `TFReplayModule.cs:64-65` | the patch engine (FortRise ships Harmony and MonoMod.Core) | **replaced**: exlaunch trampolines by offset; generated `tfexgen_*` patches use the same mechanism | `xplay.cpp`, `tfex/gen/`, `tools/tfex/patch-targets.sh` (248 targets, 205 found on 1.4.1.6) |
| 3 | Custom versus game mode: `Registry.GameModes.RegisterVersusGameMode(new NetplayVersusMode())`, giving `Modes` 11 and `IsCustom`; FortRise routes `GetRoundLogic`, `TeamMode`, `GoalScore`, `OnStartGame` and `GetMinimumPlayers` to it | `TFEXModModule.cs:108-109`; `TF.EX.Core/RoundLogic/NetplayRoundLogic.cs:13-64`; `TF.EX.Domain/Extensions/VersusMode.cs:5,17-25`; `TF.State.Domain/Extensions/ModesExtensions.cs:20` | **gameplay-relevant** | **replaced on purpose**: the Switch keeps Mode 3/4/5 and patches the sites where Mode 11 behaves differently: GoalScore (FortRise HH table), no HH overtime, no TeamReviver, ghost wait 90, PC level filter | TFEX-BASE.md S-start / S-mode; docs/TEAM-MODES-RE.md; `pcgameplay.cpp:44-51`; `xplay.cpp:1594-1596`; `tfex/NetplayManager.cpp:326` |
| 4 | Custom variant registry: `Registry.Variants.RegisterVariant("RightStickShot", PerPlayer)`; `MatchVariants.CustomVariants` (a FortRise field) | `TFEXModModule.cs:92-97`; `TF.EX.Domain/Extensions/MatchVariants.cs:23-61`; `TF.EX.Patchs/MatchVariants.cs`; `PlayerInput/RightStickShot.cs` | lobby variant titles; an input feature | **missing / skipped**: no custom variants. `ApplyVariants` fails on an unknown title, so a browser lobby with RIGHT STICK SHOT on would be refused | `tools/tfex/overrides.toml:47-53,92-93`; `tfex/NetplayManager.cpp:89-106`; `tfex/PlayerPatch.cpp:4` (S-input) |
| 5 | FortRise variant-preset types (`ModVariantPreset`, `VariantPresetCustom`, `VariantPresetAdd`) | `TF.EX.Patchs/VariantButton.cs:19-22` | menu UI | **not applicable**: the Switch has no preset buttons | – |
| 6 | Subtexture registry (`Registry.Subtextures.RegisterTexture`) for the netplay icon and the variant icon | `TFEXModModule.cs:90,102-105` | UI asset loading | **replaced**: drawn with game atlases or Draw.Rect | `tfex/NetplayScreens.cpp`, TFEX-BASE.md section 4 C |
| 7 | Archer registry (`IModArchers`, `IArcherEntry`, `ArcherEntryType`, `HairInfo`, `SpriteConfiguration`, `Registry.Sprites.RegisterSprite`): custom archers and streamed skins | `TF.EX.Domain/Interop/ArcherRegistryApi.cs`; `Extensions/ArcherData.cs:36-80`; `Services/SkinStreamService.cs:221-300`; `SkinOverlayService.cs:49-366` | cosmetic, plus a lobby check | **skipped (S-native)**: vanilla archers only | `tools/tfex/overrides.toml:87-91,100` |
| 8 | Mod registry and interop (`Interop.LoadedMods`, `GetApi<T>`, `ModInterop`): TF.EX talks to TF.State, TF.Replay and InputDisplayer, and warns about desync-risk mods | `TF.EX.Core/Api/ModCollections.cs:27-114`; `TF.State.Core/Api/ModExports.cs`; `TF.EX.Patchs/Engine/TFGame.cs:521-571` | plumbing | **replaced**: one module, direct calls; no mod list | `tools/tfex/overrides.toml:30-31,101-106` |
| 9 | Settings (`ModuleSettings`, `CreateSettings`) | `TF.EX.Core/NetplaySettings.cs:11`; `TFEXModModule.cs:29` | persisted preferences | **replaced**: `sd:/xplay/netplay.txt` | TFEX-BASE.md S-settings |
| 10 | Commands (`Registry.Commands.RegisterCommands`) | `TF.EX.Core/TFCommands.cs:42-80` | dev console | **replaced**: hw.txt and test tooling | TFEX-BASE.md S-tools, section 4 D |
| 11 | `RiseCore.GameRootPath`, `FortRiseVersion`, `WillRestart`; AutoUpdater | `TF.EX.Domain/ServiceCollections.cs:34-37`; `TF.EX.Patchs/Engine/TFGame.cs:433`; `TF.EX.Common/AutoUpdater.cs` | install and update | **not applicable** | TFEX-BASE.md S-update |
| 12 | `FortRise.ModIO.LoadXml` path fix (`\` to `/`) | `TF.State.Patchs/ModIO.cs:6-17` | the one TF.EX patch on a FortRise method | **equivalent**: Switch paths keep `/` | TFEX-BASE.md S-fspath |
| 13 | `FortRise.Logger`, `ILogger` injection | `TF.EX.Domain/ServiceCollections.cs:21` | logging | **replaced**: `netws::Log`, `hw::Log` | overrides.toml:111 |
| 14 | `PickupsRegistry` / `IPickupEntry` (mod arrow types and pickups) in `TreasureSpawnerPatch` | `TF.State.Patchs/Entity/TreasureSpawner.cs:43-97` | **gameplay**: the list fed to `Calc.Choose` | **equivalent with no mods**: the list is just `[orig]`. `IsModuleSafe` skipped | `rnd.cpp:431,515-520`; overrides.toml:147 |
| 15 | FortRise-added game members that TF.EX calls: `TreasureSpawner.orig_GetRandomArrowType`, `LevelRandomBGDetails.GenCataclysm` (FortRise renames vanilla's body), `MatchSettings.IsCustom` / `CustomVersusModeName`, `MatchVariants.CustomVariants` | `TreasureSpawner.cs:41`; `DeterministicMathPatch.cs:41`; `VersusMode.cs:22-25`; `VersusModeButton.cs` | **gameplay** for the first two | `orig_GetRandomArrowType` is the vanilla body, hooked (`rnd.cpp` ArrowTypeHook). For GenCataclysm, the Switch's DeterministicMath covers Math.* globally while capturing (`dm.cpp:107`). The rest is mode-11 UI, replaced | the translator reports these as `Invalid` (14), because its reference is vanilla TowerFall.exe: `tools/tfex/translate.toml:5`, `docs/ws-tfex-update.md:207` |
| 16 | WiderSet interop (FortRise-registered towers, `Teuria.WiderSet` menu buttons) | `TF.EX.Patchs/Scene/WiderSetMenu.cs`; `Scene/MainMenu.cs:1750` | gameplay (tower IDs, level order) | **replaced**: built-in wide towers, FortRise's IDs (16..31) and level order reproduced | docs/ws-wide.md; `widelevels.hpp:1-26`; `tfex/NetplayManager.cpp:193` |
| 17 | FortRise's built-in mods `FortRise.Content` (content loaders for mod folders) and `FortRise.WorkshopFixes` (Steam-only finalizer) | browser `vendor/fortrise/data/Internals/` | content pipeline for mods | **not applicable**: they load nothing without content mods. WorkshopFixes only patches `WorkshopDiscovery.Load` on Steam | – |
| 19 | `Engine.EnableFixedTimeStep` (FortRise-added; vanilla `set_IsFixedTimeStep` is relinked to it): fixed step at 1/240 s | `TF.EX.Patchs/Engine/TFGame.cs:758-788` (forces fixed step in Level and LevelLoaderXML, then sets `TargetElapsedTime` to the session fps); `Scene/Level.cs:76-79` (`TimeMult = TargetElapsedTime * TimeRate * 60`) | **timing**: TF.EX's 240 Hz tick comes from this FortRise method | **replaced**: S-60hz. The Switch sets TF.EX's TimeMult/DeltaTime itself (`xplay.cpp:1435-1440`), and the browser plays the Switch at 60 Hz | `mm/Monocle/patch_Engine.cs:143-160`; LOBBY-PROTOCOL.md section 5 |
| 18 | **FortRise's own MonoMod patches to vanilla** (`patch_*`) | `mm/TowerFall/patch_*.cs`, `mm/Monocle/patch_*.cs` | changes the game TF.EX runs on | **partly reproduced, case by case; never audited as a whole** | section 2 |

What the table shows:
- Rows 1-2 and 5-13 are loader, UI and mod-ecosystem features. The Switch has its own answer for each of them, or doesn't need one.
- Rows 3, 14, 15, 16, 18 and 19 are where FortRise reaches gameplay or timing. The project has already met 3, 14, 15, 16 and 19.
- Row 18 is the open one.

---

## 2. FortRise's changes to vanilla that TF.EX relies on

Method: every `patch_*` class that can run in a versus match was read against the vanilla PC decompile
(`~/Code/Towerfall Browser/reference/decompile/`). I also checked FortRise's own module: it registers only a quest enemy, quest
events and menu states, and applies no Harmony patches of its own. In the browser's default set
(TF.EX + TF.State + TF.Replay + FortRise's two internal mods), nothing else registers pickups, arrows, variants, entities or modes.

### 2.1 Changes that are real with only TF.EX loaded

| # | FortRise change | Vanilla | Effect in a TF.EX netplay match | Switch today |
|---|---|---|---|---|
| F1 | `TreasureSpawner.GetRandomArrowType` = `orig` + `Calc.Choose(Random, [item] + mod arrows)` (`mm/TowerFall/patch_TreasureSpawner.cs:229-247`) | one Choose | **one extra `Next(1)` on the spawner's Random per call.** In capture, that is TF.State's gameplay stream; TF.State's own replacement (`TreasureSpawner.cs:41-53`) reproduces the same draw | **ported**: `rnd.cpp:515-520` adds the extra `Next(1)` (GAMEPLAY-RNG-PORT R-FR, B4) |
| F2 | `TreasureSpawner.GetArrowShufflePickups` = `orig` + **`Calc.Shuffle(list)` again** (`patch_TreasureSpawner.cs:251-265`) | one shuffle | Only with the ArrowShuffle variant or a tower with `arrowShuffle`. In netplay, TF.State wraps the call in `RegisterRng` (`TF.State.Patchs/Entity/TreasureSpawner.cs:65-86`), so **both** shuffles draw from the gameplay stream: about one extra `Next(2)`, and possibly a different 2-pickup pair order | **missing**: `rnd.cpp:524` (ArrowShuffleHook) applies only the settings guard, with no RegisterRng (A27 open in TFEX-BASE.md) and no second shuffle. Dormant until a lobby turns ArrowShuffle on, which lobbies can do |
| F3 | `MatchSettings.GoalScore` for `IsCustom`: `ceil(PlayerGoals(5,8,10) * GoalMultiplier[len])`, the Head Hunters table for every mode, plus `MatchLengths.Custom = 4` with `CustomGoal` (`patch_MatchSettings.cs:9-16,50-61`) | per mode | goal is 5 x multiplier for 2 players in every mode; it differs for LMS/TDM with 3-4 players | **ported**: `xplay.cpp:1594-1596`, `tfex/NetplayManager.cpp:326`; docs/TEAM-MODES-RE.md section 1 |
| F4 | `MatchSettings.TeamMode`, `CanPlayThisMode`, `CleanSettingsVersus` route to the custom mode (`patch_MatchSettings.cs:63-135`); `RoundLogic.GetRoundLogic` returns `NetplayRoundLogic` (`patch_RoundLogic.cs:16-40`); `Session.StartGame` calls `OnStartGame` (`patch_Session.cs:113-162`) | mode switch | everything that compares `Mode` directly sees 11 and takes the "other" branch: no HH overtime, no TeamReviver, PC level filter | **ported** as instruction patches with Mode 3/4/5 kept: `pcgameplay.cpp:44-51`, docs/TEAM-MODES-RE.md section 3 |
| F5 | `Modes.Custom = 10`; `EnumPool` gives the first registered versus mode **11** (`patch_Modes.cs:3-16`, `FortRise/EnumPool.cs:20-48`). The value depends on registration order across all loaded mods | 0..9 | the mode number on the wire / in replays is 11 only with the default mod set | not needed on the wire: the lobby carries the vanilla mode (`lobby.GameData.Mode`, `NetplayRoundLogic.cs:44-48`) |
| F6 | `LevelLoaderXML.Load` is replaced with **no `yield`s**: a level loads in one frame, with the same RNG order (`patch_LevelLoaderXML.cs:39-129`) | about 10 + N frames | TF.EX's `SkipLevelLoaderIfNeeded` also runs the loader through in one tick; on the PC that is one coroutine step | **ported**: `xplay.cpp:1293-1319` runs the loader to completion and the new Level's first Update in the same GGRS frame |
| F7 | `VersusLevelSystem.GenLevels` (MonoModReplace) **ignores `FixedFirst` in a custom mode** unless the mode answers `IsRespectFixedFirst` (default false; `patch_VersusLevelSystem.cs:59-81`, `FortRise/IVersusGameMode.cs:41`) | first level fixed | **no effect in netplay**: TF.State's prefix replaces GenLevels with `OwnGenLevel` while capture is active (`TF.State.Patchs/VersusLevelSystem.cs:15-44`), and OwnGenLevel honours FixedFirst (`VersusLevelSystemExtensions.cs:26`). It matters only for offline custom-mode play | equivalent: the Switch ports OwnGenLevel (`pcgameplay.cpp` GetLevelsShell). For WiderSet's registered towers, FortRise never sets FixedFirst, which the Switch mirrors (`pcgameplay.cpp:383-390`) |
| F8 | `Engine`: `EnableFixedTimeStep` at 1/240 s; `TimeMult = delta * 60` (`mm/Monocle/patch_Engine.cs:143-221`; setting off by default, `FortRise/FortRiseModuleSettings.cs:9`) | 60 Hz | TF.EX turns it on in Level / LevelLoaderXML (row 19) | replaced (S-60hz) |
| F9 | `LevelRandomBGDetails.GenerateTileData` renamed to `GenCataclysm`, with a forwarding wrapper | – | same draws; TF.State's DeterministicMath transpiler targets the new name (`DeterministicMathPatch.cs:41`) | covered: Math.* is redirected for every caller (`dm.cpp:1-10`); Cataclysm runs 0 of 2598 (STATUS.md) |

### 2.2 Changes that are no-ops with only TF.EX loaded (registry plumbing)

The following are mod hooks with empty registries in the default set: `MatchVariants` (custom variants appended **after** the
vanilla ones; vanilla order, count and indices unchanged; vanilla exclusions appended twice, which is harmless),
`Pickup.CreatePickup`, `ArrowTypePickup`, `Arrow` and the `*Arrow` subclasses, `ArrowList`, `TreasureSpawner` ctor and arrays
(`TreasureChances` is only set for mod towers), `GameData.Load` (same sorted tower and level order), `VersusLevelSystem` ctor and
`GetNextRoundLevel` (same path-char seed), `Session.LevelLoadStart` (tags and Dark World temp variants), `Level` (events, shaders,
`EntityRegistry` lookups; **no entities added**), `Background.ReadLayers` (same `Calc.Random.Chance` draws), `Player`,
`PlayerCorpse`, `PlayerHair` (hair data, render), `JumpPad`, `SwitchBlockControl`, `TFGame.Update` (Before/After events, which
TF.EX's GGRS loop hangs off), `Entity.RemoveSelf` (null guard), `Calc` (`LoadXML` through ModIO; **`Calc.Random`, `Choose` and
`Shuffle` unchanged**). Scene/Entity update order is untouched.

Cosmetic only: `VersusStart.VariantsSequence` (the variant-card tween-out runs as its own coroutine), `Player.InitHead` (team hat
texture), `Level.HandlePausing` (`NoAutoPause`).

### 2.3 What this means

- **F2 is the one open gameplay gap** that FortRise causes and the Switch doesn't reproduce. It is dormant, because it needs
  ArrowShuffle. It pairs with TF.State's A27 RegisterRng, also missing. The fix is small: in `ArrowShuffleHook`, add a RegGuard
  and one more Fisher-Yates over the returned list.
- **F1 and F3-F9 are already handled.** The project found each of them through a desync or a team-mode RE pass, not through an
  audit. So the method was "vanilla PC + TF.EX", corrected after the fact. The reference should be "PC + FortRise + TF.EX" from
  the start.
- **Mods in the reference.** Anything another loaded mod registers (pickups, arrow types, variants, towers) feeds straight into
  F1, F2, `MatchVariants` and the level lists. The browser's default set adds none. A player with extra mods on the page is
  outside parity, and TF.EX's `GetDesyncRiskMods` warning (`TFGame.cs:521-571`) is skipped on the Switch. The Switch can't see the
  peer's mod list today; the lobby's `Mods` field (`MainMenu.cs:1600-1640`) is where it could check.
- **The translator's reference** is vanilla TowerFall.exe (`tools/tfex/translate.toml:5`), so TF.EX's references to FortRise
  members (`orig_GetRandomArrowType`, `IsCustom`, `CustomVersusModeName`, `CustomVariants`, `GenCataclysm`,
  `EnableFixedTimeStep`) come out as `Invalid` (14 operations, `docs/ws-tfex-update.md:207`). With the patched
  `TowerFall.Patch.dll` as the reference, each one would resolve to a known FortRise member. `orig_X` maps to the Switch's X,
  and the rest map to a TFEX-DIFF key (S-mode, S-60hz, S-native). They would stop being unknowns.

---

## 3. Recommendation

### Options

| option | what | cost | risk | verdict |
|---|---|---|---|---|
| A. Port FortRise as a layer | re-create the loader, registries, mode/variant/archer/pickup registries and the 105 MonoMod patch classes on the BRUTE build | very high: the patch module alone is ~13,000 decompiled lines, mostly menus, quest, Dark World, Trials, content loading, the editor and audio; it is MonoMod-specific (`orig_`, `MonoModReplace`, IL relinks) and the BRUTE build has no IL | high: a large surface with no gameplay benefit beyond F1-F9; every FortRise bump (weekly-ish, `docs/FORTRISE.md` in the browser repo) becomes Switch work | **no** |
| B. Translate FortRise with the TF.EX translator | feed `mm/` to `tools/tfex/translator` | high: the translator handles C# source with a Roslyn model, but FortRise's patches are MonoMod constructs that replace method bodies by name; they would have to become hooks again, which is what hand ports already are. The useful part (F1-F9) is under 200 lines of logic | medium | **no**, except as the reference assembly (below) |
| C. Keep stand-ins, change the reference | keep the S-native / S-mode / S-60hz stand-ins; treat "PC + FortRise" as the parity reference; port the few versus-relevant `patch_*` behaviours | low: F2 is about 20 lines; the parity re-run is one triage pass | low | **yes** |
| D. Ignore FortRise | carry on as before | none now | medium: the next FortRise-caused gap (F2-style) is found by a desync on hardware instead of by reading | no |

### What to do (option C), in order

1. **F2:** RegisterRng plus FortRise's second shuffle in `ArrowShuffleHook` (`rnd.cpp:524`). Then a lockstep run with the
   ArrowShuffle variant on, Switch against the browser, to confirm.
2. **Parity reference = the patched assembly.** Re-run `tools/re/parity.py` (and the PARITY-TRIAGE pass) against
   `TowerFall.Patch.dll` as the browser builds it, rather than `.cache/towerfall-pc.il` (vanilla). The patched assembly can be
   taken from the browser's OPFS cache or rebuilt with the browser's `FortRisePatcher`. Any method whose body changed (only the
   F rows above, by this audit) then shows up as a diff instead of being missed.
3. **Translator reference = the patched assembly** (`tools/tfex/translate.toml [refs]`), so FortRise members bind, and add
   `orig_X → X` plus explicit TFEX-DIFF skips for `IsCustom` / `CustomVariants` / `EnableFixedTimeStep` in `overrides.toml`.
   This turns the 14 `Invalid` operations into known cases.
4. **Mod set check:** decide whether a Switch lobby should refuse peers whose lobby `Mods` lists anything beyond TF.EX's own
   (TF.EX's desync-risk warning has no Switch equivalent). If it should, that's lobby work for B/C, not FortRise work.
5. **Pin the reference.** TF.EX 0.21.0 targets FortRise 5.5.0-beta.2, and the browser runs beta.3. When either bumps, re-diff the
   versus-relevant `patch_*` list in section 2.1 (`TreasureSpawner`, `MatchSettings`, `MatchVariants`, `VersusLevelSystem`,
   `LevelLoaderXML`, `Engine`, `Session`, `RoundLogic`). That list is the whole of FortRise that matters for parity.

**FortRise pieces that matter, by weight:** custom game mode routing (F3-F5, done) > TreasureSpawner wrappers (F1 done, F2 open) >
level loading and timing (F6, F8, done) > GenLevels / FixedFirst (F7, equivalent) > MatchVariants append order (no-op today, but
it matters as soon as any variant mod is on the browser). Everything else in FortRise (loader, registries, content, settings,
commands, UI, quest/Dark World/Trials, audio) is outside versus parity.

**Answer to the user:** looking at FortRise first would not have changed the plan. The Switch can't run FortRise, and most of
it is a mod platform the Switch replaces with exlaunch. What it would have changed is the **reference** the project compared
against: "PC vanilla" was never quite what the browser runs. Rows F1-F9 are the whole difference. Eight are handled already and
one (F2, ArrowShuffle) is open.
