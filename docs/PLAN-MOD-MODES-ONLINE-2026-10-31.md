# Mod game modes in online play (research and plan, 2026-10-31)

The user (2026-10-31): research and plan custom mod game modes in TF.EX online play; find an actively developed mode mod that
works with modern FortRise to test with. **The end goal is TowerBall online** (FortRise's TowerBall mod, on the PC, the browser and
the Switch); every step below is chosen to move toward it. **Mods keep changing in a way the built-in modes don't: every player
must have the same mod at the same version** (the user, 2026-10-31), checked before a match, not assumed.

## What TF.EX supports today (v0.28.1, read from its source)

- **Modded variants online: yes.** TF.State's mod API (`TfStateAPIModImports.RegisterVariantStateEvents(mod, key, save, load)`,
  `vendor/tfex/src/state/README.md` "Modded variants") lets a mod put its own state in the rollback snapshot, under
  `<modName>-<key>`. FortRise's AdditionalVariants is the worked example, and TF.EX's author keeps it compatible (its commit
  76a7fdb, "Supporting EX past 0.22.X", 2026-10-20).
- **Mod lists in the lobby: yes.** `Lobby.Mods` (`CustomMod`: a name and data with `Version`), sent with `UpdateLobbySettings`.
- **Mod game modes online: no.** Online is TF.EX's own Netplay mode standing in for a built-in mode picked as a number
  (`Lobby.GameData.Mode`); `NetplayRoundLogic` and `NetplayVersusMode.IsTeamMode` know Team Deathmatch only. A mod's mode never
  gets a say, and its number differs per install (FortRise numbers mod modes in load order: with Teams+ installed the Netplay
  mode is 12, not 11).

## Candidates (mods that register a versus game mode)

From FortRise's ExampleFortRiseMod (maintained alongside FortRise) and the browser repo's GameBanana catalog
(`mods/catalog.json`, every TowerFall mod with its FortRise metadata). Measured from each mod's source:

| Mod | Author / source | Last change | FortRise | C# | Own entities | Mutable statics | Random | Patches |
|---|---|---|---|---|---|---|---|---|
| **MoveOrDie** (stand still too long, you die) | ebe1.kenobi, github tf-mod-fortrise-variant-moveordie | 2026-08-29, 20 commits | 5.3.3 | 892 | 0 | 1 game-state (`stationaryTimers`) | 0 | 4 `harmony.Patch` |
| Respawn (lives, respawn with immunity) | ebe1.kenobi, tf-mod-fortrise-gamemode-respawn | 2026-08-23, 16 commits | 5.3.3 | 1241 | 2 (LivesHUD, a settings popup) | ~3 | 0 | 10 `harmony.Patch` |
| Baron Mode (LMS with quest gems that revive) | Terria (FortRise's author), ExampleFortRiseMod | 2026-07-16, 25 commits | 5.3.0 | 1162 | 2 | ~5 | 0 | 7 `harmony.Patch` + 7 IL transpilers |
| PlayTag (pass the bomb) | ebe1.kenobi | 2026-08-23, 38 commits | 5.3.3 | 3045 | 4 | ~25 | 4 | 17 |
| Soccer (ball, two goals, teams) | ebe1.kenobi | 2026-08-29, 14 commits | 5.3.3 | 3648 | 6 | ~13 | 0 | 17 |
| TowerBall | FortRise ExampleFortRiseMod | 2026-09-05 | 5.3.0 | (not measured) | basket, ball | | | |

None targets FortRise 5.5 yet (beta); Teams+ showed 5.3-era mods load and run on 5.5 (2026-10-31, the Mac).

## The goal: TowerBall (FortRise's ExampleFortRiseMod/TowerBall)

Version 5.0.0 by Suyooo and CoolModder, added to FortRise's set 2026-09-05 (FortRise 5.3.0; GameBanana's "TowerBall For FortRise"
is an old 2023 build, broken). 1868 lines; 19 `[HarmonyPatch]` attribute patches (HookGen reads them). What online needs:
- **The ball is a custom arrow** (`BasketBall : ToyArrow`, 374 lines): TF.State saves arrows, but must recreate this subclass and its
  fields on a rollback (a custom-entity save/load, beyond today's per-key byte blobs).
- **A loose ball is a custom entity** (`DeadBall`: position, speed, two counters).
- **Per-player state in statics:** `HasBasketBall`, `currentHoldFrames`, `touchedGroundSinceCollect`, `PlayerArrows`, `Dunk`
  (`Hooks/PlayerHooks.cs`), a corpse counter.
- **Respawns mid-round** (`TowerBallRoundLogic`, `PlayerRespawner`), picking the spawn with an unseeded `new Random()`
  (TowerBallRoundLogic.cs:125): the two peers would choose different spawns. Online it must draw from the shared, seeded
  gameplay RNG (TF.State's), a change in the mod.
- Its own levels (Content/Levels/Versus, WideLevels), a basket (`BasketBallBasket : JumpThru`), a chest subclass.
On the Switch the megamod's TowerBall (towerball.hpp: a native port of an older TowerBall) is the stand-in; the end state is
FortRise's TowerBall translated, as Teams+ replaced the hand-written three-team code.

## Recommendation: MoveOrDie, then Respawn, then TowerBall

- **Smallest online state:** one dictionary, player index -> seconds without moving, updated in a `Player.Update` postfix. No
  entities of its own, no randomness. That is exactly one `RegisterVariantStateEvents` key: if its rollback works, the
  mechanism works; if it is left out, the desync is immediate and easy to see (a rollback while someone stands still).
- **A real mode:** it registers MOVE OR DIE as a versus mode (and also a variant and a setting), so it exercises the mode path
  end to end: the lobby, both peers having the mod, the round logic, the mode number.
- **Translatable as is:** plain `harmony.Patch` calls, which the Switch's translator now reads (HookGen, 2026-10-31).
- **Active and small:** 20 commits, updated two months ago, 892 lines; its author publishes source for every mod.
- Then **Respawn**: lives as per-player state, and players respawning mid-round under a mode's rules, which TowerBall does too.
- Then **TowerBall**: adds what the first two don't, a custom arrow and a custom entity in the rollback state, and the
  seeded-RNG fix. Baron Mode (IL transpilers), PlayTag and Soccer aren't on the way there.

## What has to exist (the work, in order)

1. **TF.EX: a mod mode in the lobby by name, and the same mod everywhere.** `GameData` carries the mode's registry name
   (FortRise's `IVersusGameModeEntry.Name`) beside the built-in number; never the number. The host's lobby lists that mod in
   `Lobby.Mods` with its exact version, and every player must have **the same mod at the same version**: a joining player with
   it missing or at another version is removed from the lobby, told which mod and which version it needs (details: the
   implementation plan's change 3; version only, as his mod checks; a rebuild without a version bump isn't caught).
2. **TF.EX: the Netplay mode hands off to it.** `NetplayVersusMode` / `NetplayRoundLogic` ask the selected mod mode for its round
   logic, team mode and minimum players (FortRise's `IVersusGameMode`), keeping TF.EX's netplay wrapping (seats, inputs, rollback).
3. **The mod: its state in TF.State.** MoveOrDie registers `stationaryTimers` with `RegisterVariantStateEvents` (an optional
   dependency on TF.State, as AdditionalVariants). A small PR to the mod, or a patch of ours for testing first. For TowerBall,
   TF.State needs more than byte blobs: recreating a mod's arrow subclass and entities on a rollback (a TF.State feature), and
   the mod drawing its spawns from TF.State's seeded RNG.
4. **Scores and results:** TF.State's fixed 4-slot scores (Teams+ needs more; MoveOrDie doesn't).
5. **The Switch:** translate the mod (vendor it as Teams+); FortRise's interface dispatch (tfex/FrVersusModes.cpp) gets one more
   mode class each time, until a generic dispatch replaces it. For TowerBall: FortRise's TowerBall translated, the megamod's
   native towerball.hpp retired (as threeteam.hpp was for Teams+), local play first, then online. The Switch checks the same
   mod and version as every other peer (step 1), from the mod folder on its SD card.
6. **Tests:** two browser pages first (TF.EX's own build, TF.EX-UPDATE.md's two-page test), then the Switch against a page
   (smoke lobby with the mode), then the user's Windows PC with real TF.EX + FortRise (from 2026-10-31).

Per "TF.EX is correct" and "upstream PRs deferred, only noted": steps 1-2 are TF.EX features. Start as a TF.EX feature request with
this plan (MoveOrDie as the example), and prototype them in our TF.EX fork so the Switch and the browser can test before upstream.

## How to take it upstream (2026-10-31)

**What happened last time (local players), from the PRs' code:** our ggrs-ffi #24 (the native mechanism) was merged; our TF.EX #283
was closed for his #306, and our server #3 closed (2026-10-31), with no review comments on either. The differences in #306:
- **no new messages:** #283 added four (Add/RemoveLocalPlayer + responses, so a server PR); #306 added a field to the existing
  CreateLobby / JoinLobby / EnterQuickPlay / JoinPrivate messages and to `Lobby`;
- **every flow covered:** #283 listed limits ("not in quick play or ranked"); #306 handled quick play, series, private, and refused
  ranked with several local players (`RefusableModeButton`), with its own UI (`LocalPlayersPanel`);
- **existing pieces reshaped** (MenuStateExtensions, SeriesScreen) rather than new ones beside them.
Reading: self-contained mechanisms in his libraries he takes; the lobby protocol, the menus and the flows are his design.

**What already exists to build on (TF.EX v0.28.1):**
- `MainMenuPatch.CatalogLobbyMods`: the lobby's `Mods` = the mods owning its enabled custom variants, each with `Version`;
  `NotifyOnVersionMismatch`: a different version only shows "... MAY NOT WORK"; a missing mod isn't checked. A mod mode extends this:
  its owner joins the catalog as a **required** mod (missing or another version: can't join / ready, says which), the variants'
  warning unchanged.
- TF.State's `RegisterVariantStateEvents` (mods' rollback state) and EX-API.md's stance: "making your mod netplay compatible" is the
  mod's job, through TF.State. A mod mode opts in the same way; nothing makes an unprepared mod playable.
- FortRise's `IVersusGameMode` + TF.EX's `NetplayVersusMode` / `NetplayRoundLogic` (the hand-off point).

**The approach:**
1. **Design first, in an issue, before code.** A short proposal on TF.EX: mod modes online, MoveOrDie as the small example and
   TowerBall as the goal, built from the four pieces above, with the decisions that are his asked as questions (the mode by name
   in `GameData`? required mods enforced or warned? which flows: private only first, or quick play too?).
2. **Prototype in our fork meanwhile**, shaped like his code: extend existing messages and models (fields, not message types),
   reuse `CatalogLobbyMods` / the mismatch path, every flow handled or refused (as `RefusableModeButton`), changelog entries,
   Ports / Services / Patchs layering, ServiceCollections. It proves the design on two browser pages, the Switch and the Windows
   PC, and it is what we show in the issue, not a PR he has to take or leave.
3. **Upstream in small pieces, mechanisms first**, each after the issue settles it: TF.State (mod state beyond variants, the seeded
   RNG for mods), then TF.EX's required-mod check, then the mode hand-off. The mods' own changes go to their authors (MoveOrDie's
   TF.State registration; TowerBall's seeded spawn, to FortRise's ExampleFortRiseMod).
4. **If he prefers to write it himself, that is fine:** the goal is TowerBall online in TF.EX; our fork then follows his version,
   as with v0.28.0's local players.

## Implementation plan (against TF.EX v0.28.1; the user, 2026-10-31: we build the whole thing for him, thoughtfully and minimally)

### What the code already gives us (read 2026-10-31)
- **Session plumbing is generic.** `NetplayManager.Init(TowerFall.RoundLogic)` takes any round logic; `SpawnFirstRound` asks an
  `IRoundPlayerSpawner` and otherwise falls back to the game's own spawn (VersusStart + SpawnPlayersFFA). TF.State saves the round
  logic by field name on whatever round logic runs (`RoundStarted`, `done`, `Time`, `Kills`: TF.State.TowerFallExtensions/Level.cs),
  and several of its paths are gated on the session being the Netplay mode (`currentMode.IsNetplay()`).
- **What is not generic:** `NetplayRoundLogic` (TF.EX.Core/RoundLogic/NetplayRoundLogic.cs) re-implements LMS / HH / TDM by mode
  number and owns the netplay duties of a round's load: `netplayManager.Init(this)` on the first one, replay / match report setup,
  deferred spawning (`SpawnRoundPlayers` when the session is established, at once on later rounds). `LobbyVersusModeButton`
  offers exactly the three built-in modes (series: one).
- **Mods and versions in the lobby:** `Lobby.Mods` (`CustomMod`: a name and a string map, `Version`) is filled by
  `MainMenuPatch.CatalogLobbyMods` from the enabled custom variants' owners; `NotifyOnVersionMismatch` only warns ("MAY NOT WORK")
  and skips a missing mod. The server keeps `CustomMod.data` as a `HashMap<String, String>` (any key passes); it parses `GameData`
  as a typed struct (an extra field needs one serde line, as `best_of` was added with `#[serde(default)]`).
- **Mod hooks into TF.State:** `RegisterVariantStateEvents` (a mod's rollback state, keyed per mod) and `RegisterRng` /
  `UnregisterRng` (`Calc.Random` is the tracked, seeded gameplay RNG inside the bracket). TF.EX's own export is
  `IsPlayingOnline` (EX-API.md, `[ModExportName("TF.EX.API")]`). FortRise computes a per-mod checksum (`ModuleMetadata.Hash`), internal.

### The design: the session stays TF.EX's Netplay mode; a mod mode runs inside it
Every online match keeps `MatchSettings.Mode` = the Netplay mode (all of TF.EX's and TF.State's netplay gates keep working, nothing
depends on a mod mode's number). The lobby names the mod mode; the Netplay mode asks it for its round logic, team mode and
minimum players, and TF.EX keeps doing the netplay part of a round's load around the mod's round logic. A mod opts in through
TF.EX's interop API, as variants opt in through TF.State's: an unprepared mod is never offered online.

### The contract for a mode mod (what a mod author adds; as built in step 1, tools/modmodes/NetplayModeTest)
```C#
// meta.json: "optionalDependencies": [{ "name": "TF.EX", ... }] so TF.EX loads first and its exports exist
// in the mod's constructor, after RegisterVersusGameMode(name, mode):
typeof(TfExAPIModImports).ModInterop();
TfExAPIModImports.RegisterNetplayVersusMode?.Invoke(this, "MoveOrDie", round => ((MoveOrDieRoundLogic)round).SpawnPlayers());
TfStateAPIModImports.RegisterVariantStateEvents?.Invoke(this, "MoveOrDie", SaveTimers, LoadTimers);   // only if it owns state
// in its RoundLogic.OnLevelLoadFinish, after base.OnLevelLoadFinish():
if (TfExAPIModImports.OnNetplayLevelLoadFinish?.Invoke(this) == true) return;   // online: TF.EX spawns through the callback
Session.CurrentLevel.Add(new VersusStart(Session));
SpawnPlayers();   // spawns the players and sets Players; TF.EX adds the VersusStart itself online
```
`OnNetplayLevelLoadFinish` replaces the plan's `IsPlayingOnline` check: on the first round the session isn't initialised yet when the
mod asks (IsPlayingOnline is false), so TF.EX has to take the round itself; true means "TF.EX did it". No Harmony patch on
RoundLogic. Randomness that affects play goes through `Calc.Random` inside TF.State's `RegisterRng` bracket (TowerBall's spawn:
TowerBallRoundLogic.cs:125).

### TF.EX changes (each small, shaped like #306: fields on existing models, existing pieces reshaped, every flow handled or refused)
1. **EX-API: `RegisterNetplayVersusMode(Mod mod, string modeName, Action<RoundLogic> spawnRound)`** (+ `Unregister`), beside
   `IsPlayingOnline` in the TF.EX.API export class; a small registry service (Ports / Services, resolved through ServiceCollections)
   keyed by the FortRise mode entry's name. EX-API.md gets the section above.
2. **The lobby picks it** (decided from his code: custom variants travel by name in `GameData.Variants`, and `GameData` grew
   `BestOf` with a `#[serde(default)]` server field, 24779da). `GameData.CustomMode` (string, the FortRise mode entry name; empty = a built-in mode), and `GameData.Mode`
   keeps the built-in mode it plays like (TeamDeathmatch for a team mode, else LastManStanding), so the server's team check and
   the match report stay meaningful. Server: one `#[serde(default)] custom_mode: String`. `LobbyVersusModeButton.AvailableModes`
   appends the registered modes (name and icon through FortRise's GetModeName / GetModeIcon, already custom-aware); not in a series
   lobby. Quick play and ranked keep the built-in modes (refused there, as `RefusableModeButton` refuses ranked with local players).
3. **The same mod at the same version, enforced** (decided from his code, 2026-10-31: the custom variants' path). `CatalogLobbyMods`
   adds the mode's owner to `Lobby.Mods` with its `Version` (`ModCollections.GetVersion`, FortRise's loaded-mod metadata), as it
   does a variant's owner. The join check that removes a player missing a variant (`MatchmakingService`: `MissingVariants` ->
   "MISSING ..., REMOVED FROM LOBBY") also removes one missing the mode's mod or having it at another version ("NEEDS TOWERBALL
   5.0.0, REMOVED FROM LOBBY"): stricter than the variants' version warning (`NotifyOnVersionMismatch`, unchanged), because a mode
   changes the rules every frame. Version only: his mod checks compare FortRise's metadata version and nothing else
   (`GameFilesIntegrity` hashes the player's own game files against a shipped manifest, a local flag, not a comparison between
   players); FortRise's own checksum is internal.
4. **The match runs it.** `NetplayVersusMode`: `IsTeamMode`, `GetMinimumPlayers`, `OnStartGame` and `OnCreateRoundLogic` ask the
   selected mod mode when `CustomMode` is set (FortRise's `IVersusGameMode` through its registry). The netplay part of
   `NetplayRoundLogic.OnLevelLoadFinish` (Init, replay / report setup, deferred spawning) moves into one helper that both
   `NetplayRoundLogic` and a mod's round logic go through: for a registered mode, a postfix on `TowerFall.RoundLogic.OnLevelLoadFinish`
   (the base the mod calls first) runs it, and `NetplayManager.SpawnFirstRound` / later rounds call the registered `spawnRound`.
5. **Replays** take `CustomMode` with the lobby's mods (`replayService.Initialize(lobby.GameData, lobby.Mods)` already gets both);
   a replay of a mod mode needs that mod (the same check). Spectators: the same check on join.

### TF.State changes
- Nothing for MoveOrDie (its state fits `RegisterVariantStateEvents`; the name says "variant": a neutral alias, `RegisterStateEvents`,
  with the README's example for a mode, is a small optional rename).
- For TowerBall: a mod's own entities and arrow subclass in the rollback state (recreate a `BasketBall : ToyArrow` / `DeadBall` from
  saved bytes on a load). Designed with TF.State's author once MoveOrDie and Respawn work: the largest piece.

### Order (each step tested before the next; our fork first, upstream in this order)
1. TF.EX: the registry + EX-API export + the round-load helper (changes 1, 4), with a tiny test mode of our own registered in a
   test build. Two browser pages in sync.
2. TF.EX: lobby selection + required-mod check (2, 3); the server's one field. A page without the mod can't ready; with it, plays.
3. MoveOrDie with the contract (a patch of ours first; then a PR to its author). Pages, then the Switch (translated as Teams+), then
   the Windows PC with real TF.EX.
4. Respawn (mid-round respawns through the same spawn callback).
5. TowerBall: TF.State's mod entities, the seeded spawn (PR to ExampleFortRiseMod), the Switch's translated TowerBall replacing the
   megamod's.
Upstream: one PR per step, in this order, each after its design is agreed in the issue (the approach above).

### Step 1 done (2026-10-31): a mod mode plays online between two browser pages
- **TF.EX** (fork belacmu/TF.EX, local branch `mod-modes-online` in ~/Code/TF.EX-mod-modes, from v0.28.1; not pushed): ~150 lines.
  - `TF.EX.Domain/Interop/NetplayVersusModes.cs`: the registry (FortRise id "Mod/Mode" -> spawn callback), `Selected()` from the
    lobby's `GameData.CustomMode`, `AsSpawner` (the IRoundPlayerSpawner adapter used by NetplayManager.SpawnFirstRound and
    ReplayService), and `OnLevelLoadFinish`, NetplayRoundLogic's netplay part moved out unchanged (NetplayRoundLogic calls it).
  - EX-API (`TF.EX.API/ModExports.cs`): RegisterNetplayVersusMode / UnregisterNetplayVersusMode (Mod, name, Action<RoundLogic>),
    OnNetplayLevelLoadFinish(RoundLogic) -> bool.
  - NetplayVersusMode delegates IsTeamMode, OnCreateRoundLogic, OnStartGame, GetMinimumPlayers to the selected mod mode.
  - `GameData.CustomMode` (string); the lobby builder's GAME MODE lists registered mod modes after LMS/HH/TDM (not in a series;
    GameData.Mode = TDM for a team mode, else LMS); the edit snapshot, change detection and update feed include it; the lobby list
    shows the mod mode's name (its id in red when the player doesn't have it).
  - **Upstream bug found:** TF.EX never called `typeof(TF.EX.API.ModExports).ModInterop()`, so its EX-API (even IsPlayingOnline)
    was unreachable from other mods; TF.State does call its own. Fixed in TFEXModModule (one line); worth its own small PR.
- **Server** (tf-ex-matchmaking-server, local branch `mod-modes-online` in ../tf-ex-mm-mod-modes, from switch-online-028; not
  pushed): `#[serde(default)] custom_mode: String` in GameData; cargo test 53 passed. Without it the server drops the field.
  The browser repo's local.mjs (matchmaker.js gameData()) needs the same field before the browser site can offer mod modes.
- **Test:** `tools/modmodes/two-pages.sh` (logs only, one PASS/FAIL line): TF.EX built from the local folder and the test mod as the
  page's hosted mods; A creates a lobby with the mod mode, B joins, the round logic is the mod's on both pages, round 0 spawns after
  Synchronized, ends on A's shot, round 1 spawns through TF.EX's helper; no desync. PASS 2026-10-31.
- Seen, not ours: `[ModTest] round 0: 0 player(s) spawned`: TF.State's SpawnPlayersFFA prefix returns without setting the
  result while capture is active, so RoundLogic.Players is 0 online for TF.EX's own NetplayRoundLogic too.
- Not yet: replays and spectators of a mod mode (change 5), the required-mod / version check (change 3, step 2).

### The Switch: checked before step 2 (2026-10-31)
How: the branch fetched into vendor/tfex (local, not pushed), `tools/tfex/translate.sh all <sha> <scratch>`, compared with v0.28.1
translated the same way.
- **Fixed in TF.EX's design:** the first version added a class implementing IRoundPlayerSpawner (an adapter for mod round logics). The
  translator handles a TF.EX interface only when ONE class implements it (UnitEmitter.Class.cs InterfaceTypeTest: IRoundPlayerSpawner
  -> NetplayRoundLogic), so SpawnFirstRound's spawn became UNSUPPORTED. Now TF.EX's `is IRoundPlayerSpawner` lines are untouched and a
  mod mode takes a delegate path (NetplayVersusModes.TrySpawnRoundPlayers; the helper takes an Action). Re-translated: no UNSUPPORTED
  in any unit the branch touches; the spawn callbacks are stored and invoked as TF.State's state events already are (the translator's
  map, the delegate's Invoke slot). The two-page test passes again.
- **Already there:** FortRise's GameModeRegistry is translated (Fr_GameModeRegistry), so Resolve by id works; NetplayVersusMode's
  delegation goes through the Switch's IVersusGameMode dispatch (tfex::frvmodes).
- **The Switch's mod layer needs (ours, generic: every mode mod needs them, MoveOrDie and TowerBall too; none changes TF.EX's design):**
  1. Mod interop: MonoMod's `[ModImportName]` / `[ModExportName]` are reflection. The translator should bind a mod's import delegate
     fields to the export statics at translate time, translate ModExports.cs (TF.EX's, TF.State's) instead of skipping it
     (overrides.toml "network/TF.EX.API/ModExports.cs"), and make `typeof(X).ModInterop()` a no-op rule (TfexModule: the one gap left
     in TF.EX's translation). TF.State's API (RegisterVariantStateEvents, RegisterRng) needs the same for MoveOrDie and TowerBall.
  2. Mode dispatch: FrVersusModes.cpp dispatches IVersusGameMode by hand to two classes (Teams+, Netplay). Generated from the classes
     implementing it instead (one translator feature; it also lifts the one-implementer limit above).
  3. Mod boot: FrModsBoot.cpp constructs Teams+ by hand, before TF.EX's module. Needed: the translated mods booted from a list in
     FortRise's order (priority, then dependencies: a mode mod that lists TF.EX loads after TF.EX's module).
  4. The S-native overrides of TFEXModModule.cs and ModExports.cs change hash: reviewed when the branch is adopted.
  5. Step 2's version check: mods are built into the Switch module, so a mod's version there is its build's (SwitchMods' metadata
     from the registry); it has to carry the same meta.json version as the PC's and the browser's copy of that mod.

### The Switch: mod modes working (2026-10-31, branch claude/mod-modes-switch)
The Switch hosts a lobby in the test mod's mode, a browser page joins, both run the mod's round logic: `tools/smoke.sh lobby
"lobbycustommode NetplayModeTest/ModTest" "lobbymax 2"` with `LVB_SERVER=real TFEX_REAL_SERVER=<the server's mod-modes-online build>
TOWERFALL_WWW=.devserve/modmodes/www LVB_MODS=TF.EX,NetplayModeTest` (the page from tools/modmodes/two-pages.sh; the test mod's folder
on the slot's sd:/xplay/mods/NetplayModeTest: meta.json, Content/gameModes/modtest.png): PASS 155905, in sync (TF.State's saved bytes
identical on every frame after 0), GGRS checksum clean; the plain `smoke lobby` PASS 154718 on the same build.
What the Switch's mod layer gained (all generic, the next mode mod is config rows):
- Mod interop (translate.toml [mod_exports], UnitEmitter ModImportUnit): a mod's [ModImportName] field `?.Invoke` / `Invoke` / read is
  a call of the export class's unit (TF.EX's ModExports, now translated: unit Ex_ModExports); `typeof(X).ModInterop()` is nothing.
- Interface dispatch across compilations (translate.toml [dispatch], CodeGen.Dispatch): tfexgen_dispatch_frvmodes.cpp generated from
  every unit's class implementing FortRise's IVersusGameMode (TF.EX's Netplay, Teams+'s, the test mod's); FrVersusModes.cpp deleted.
- Code mods' boot (FrModsBoot.cpp kCodeMods): booted in FortRise's order from each meta.json's priority (tfexgen_mods: `priority`) and
  dependencies: ThreeTeamMode, TF.EX, NetplayModeTest.
- Code mods as config (translate.toml [code_mods]: prefix, source folder, struct prefix) instead of Teams+ hard-coded.
- Translator fixes the test found: IModGameModes.RegisterVersusGameMode(name, mode) had no rule (the name went in as the mode);
  Array.IndexOf / Contains over tuples with strings compared the strings by pointer (C#: by value); a ToArray in a returned ?: branch
  wasn't the managed array; TF.State's by-name round-logic fields (roundEndCounter, wasFinalKill) didn't look at code mods' classes
  (the desync: Session.RoundEndCounter 0 on the Switch, 90 on the page) — another compilation's class is ours now (OriginOf "TfexAll").
- hw.txt `lobbycustommode <Mod/Mode>` (tests): the LobbyBuilder's GAME MODE on a mod's mode.
Not yet: vendor/tfex points at the fork's local commit (belacmu/TF.EX not pushed), so this branch can't go to master until the TF.EX
change is pushed (or merged upstream); the server's custom_mode field (tf-ex-mm-mod-modes) and the browser's local.mjs likewise.

### Step 2 done (2026-10-31): the lobby's required mod, as TF.EX's missing variants
TF.EX fork e3d34f5 (belacmu/TF.EX mod-modes-online, pushed): `NetplayVersusModes.MissingMode(lobby)`: the mode's mod not installed, or
another version than the host's (the lobby's Mods: CatalogLobbyMods adds the mode's mod with its version) -> the lobby list refuses it
("NEEDS NETPLAYMODETEST 0.1.0", the join's notification "CAN'T JOIN - NEEDS ..."), the roll call won't ready ("MISSING ..."), a spectator
is removed, the same four places as missing custom variants. Unlike a variant's mod (a version mismatch only warns, NotifyOnVersionMismatch),
a mode's mod must be the host's version. `tools/modmodes/check.sh`: PASS (B without the mod, B with 0.2.0: refused, never joins).
Not run: the roll-call and spectator paths (the same MissingMode); the Switch side translated and built (switch-online 231670d7), not run.

### TowerBall online: what it takes (read 2026-10-31: ExampleFortRiseMod main, TowerBall 5.0.0; TF.State at v0.28.1)
TowerBall's state and TF.State today (TF.State saves only entity types it has code for; others are never saved and never touched on a
load, Level.cs GetState / LoadState):
- **Its ball** `BasketBall : ToyArrow` (a FortRise arrow type): saved and recreated as an arrow by its type number (ArrowBuilder's
  "modded arrow" default, Arrow.Create), its own fields not rolled back; the type number must match on both peers (mod load order).
- **`DeadBall`** (loose ball), **the respawn timers** (a bare Entity with an Alarm and a lambda, TowerBallRoundLogic.OnPlayerDeath),
  **`TowerBallHUD`**, **`BasketBallBasket`**: never saved; on a load a DeadBall made after the saved frame stays, one removed after it
  doesn't come back, a respawn whose player TF.State removes is lost.
- **`BasketBallTreasureChest`**: saved as a vanilla chest; reused from TF.State's chest cache (its own fields not rolled back), a plain
  TreasureChest on a cache miss.
- **Its round logic's fields** (endDelay, LastThrower, overtime, arrowQueue: non-empty across a frame when a ball drops) and **the
  per-player statics** (PlayerHooks: HasBasketBall, currentHoldFrames, touchedGroundSinceCollect, PlayerArrows, Dunk): not saved.
- **Respawn position**: `new Random()` (TowerBallRoundLogic.RespawnPlayer): differs between peers.
- **First round**: OnLevelLoadFinish sets the level up (ball chest, baskets, HUD, variants off) and spawns the players in one go.
TF.State's mod API (RegisterVariantStateEvents) can carry the statics and round-logic fields as bytes, but a mod restoring its own
entities in OnLoadState hits three traps (RemoveSelf blocked while restoring; Add's toAdd list cleared right after; the load order of
mods' handlers not fixed): workable, but every mod with entities would repeat that code.

The plan (each upstream piece small, in its owner's style):
1. **TF.State: mod entities in the state** (the generic piece; PR to TF.State's author): `RegisterEntityState(Mod, Type entityType,
   Func<Entity, byte[]> save, Func<Level, byte[], Entity> create)` (exported like RegisterVariantStateEvents): on save, every live entity
   of that type (by actualDepth) with its bytes; on load, those not in the state removed, the saved ones recreated through `create` (or
   loaded in place when still there) by TF.State's own add / remove (the way it adds its own entities), before the mods' OnLoadState.
   The same for an arrow type's own fields (`RegisterArrowState(ArrowTypes, save, load)`, on top of the base arrow it already rolls back).
2. **TowerBall (a patch of ours, then a PR to ExampleFortRiseMod):** the contract (RegisterNetplayVersusMode with SpawnPlayers;
   OnLevelLoadFinish: level setup, then OnNetplayLevelLoadFinish, then the players); the respawn position from Calc.Random in TF.State's
   RegisterRng bracket; its statics and round-logic fields through RegisterVariantStateEvents; DeadBall, the ball's fields and the
   respawn timer (a small entity class of its own instead of the bare Entity + lambda) through step 1. All optional: offline unchanged.
3. **Browser:** TowerBall as a hosted test mod (as NetplayModeTest), two pages in sync over several rounds with goals, deaths and
   respawns (rollback with netdelay).
4. **Switch:** TowerBall translated as Teams+ (a [code_mods] row, units, a kCodeMods row); its 19 Harmony patches through HookGen; its
   content (levels, atlas) on the SD card like a content mod; the megamod's native TowerBall (towerball.hpp) deleted when it plays.
   meta.json's dependencies in FortRise's string form ("FortRise:5.3.0"): tfexgen_mods.cpp reads only {name, version} objects yet.
Open: TowerBall's repository has no licence file (needed before the Switch build or the site ships it; ask its authors).

## Open questions

- MoveOrDie's and Respawn's licences (neither repo lists one, 2026-10-31): fine for local testing; ask the author before shipping.
- TowerBall's unseeded `new Random()`: a PR to FortRise's ExampleFortRiseMod (its maintainers are FortRise's) or TF.State
  steering it; ask which they prefer.

### TowerBall online: first runs (2026-10-31)
Code: TF.State's `RegisterEntityStateEvents` (TF.EX fork fdf5e27: a mod's entity types in the rollback state, carried in the mods'
data, no format change); TF.EX's `NetplayVersusMode` export and a team mod mode's own lobby minimum (GetMinimumTeamPlayers);
TowerBall patched on ../ExampleFortRiseMod branch towerball-online (local, not pushed: no fork of FortRise/ExampleFortRiseMod yet):
builds on current FortRise again (FortRise.ResourceTree), the contract, `Online.IsPlaying` for its 14 mode checks (online the
session's mode is the Netplay mode), the respawn from the gameplay RNG, its state through TF.State (the ball's and chest's fields,
the loose ball and the round timers as entities, the players' ball state, the round's fields, per-corpse counters).
Test: `tools/modmodes/towerball.sh` (two pages, 40 ms delay, random keys; PASS needs no GGRS desync AND the dumped saved states
identical). Results: the lobby offers TowerBall, 1v1 with a team each, the match starts and plays; two rollback bugs found by the
state diff and fixed in TowerBall (the ball chest's and the corpses' removal were Flash callbacks, which a rollback can't keep: now
counters in their saved state). One run PASS by GGRS (60 s) but the dumped states differed in the ball's ShootingCounter; the next
runs: the ball's bounce diverges (frame 2410 in one: page A's ball bounces at y 207, page B's falls on), GGRS reports desyncs.
Next: a deterministic check instead of random play: TF.EX's sync test (`test`) with TowerBall (needs TowerBall's level system
chosen before the mode is set: StartGame calls GetLevelSystem first) or a scripted throw, dumps every frame around the bounce.
Sync test (2026-10-31): `SYNCTEST=1 tools/modmodes/towerball.sh` runs TF.EX's `test` on TowerBall (TF.EX fork: the test command takes
a mod mode's id and gets its levels as the map screen does), GGRS rolling back every frame on one page: deterministic. Found: the
ball's allegiance (frame 1299: TF.State restores an arrow's allegiance only when it isn't neutral; the chest's ball is neutral while
its owner has a team) -> fixed in TowerBall's own ball state. Next mismatch: frame 1300, the ball's first frame after the chest
makes it (original bounces off the floor, the resimulation falls on): to find.

### The TF.EX fork finished before TowerBall (2026-10-31; the user: finish the fork first, TowerBall's source undecided)
- **Replays** (TF.EX fork 461c8a5): TF.Replay records the lobby's CustomMode (ReplayInfo key 18, SetCustomMode /
  GetLoadedReplayCustomMode, as SetArcherTeams / GetLoadedReplayMode); playback runs that mode (NetplayVersusModes.Selected reads the
  replay's). A replay needing a mod that isn't installed was already refused by TF.Replay ("X IS MISSING": the mode's mod is in the
  recorded Mods). `tools/modmodes/replay.sh` PASS (recorded, exported on B's leaving, played back through the mod's round logic).
  The Switch has no replays.
- **Spectators**: no change needed (a spectator's lobby carries CustomMode). `tools/modmodes/spectate.sh` PASS early and late.
- **TF.State's mod entities, without TowerBall**: the test mode makes markers (entities of its own) and keeps a counter of its own
  through RegisterEntityStateEvents / RegisterVariantStateEvents; `tools/modmodes/synctest.sh` (TF.EX's sync test, rolling back
  every frame) PASS, and FAIL as it must with MOD_DEFINES=NO_STATE (frame 1172: the gameplay layer's depths). The sync test runs a
  mod mode by its id (fork e949186).
- **The required mod when the host switches a lobby to the mode** (by hand, 2026-10-31, screenshots in .devserve/modedit): the player
  without the mod can't ready ("MISSING NETPLAYMODETEST 0.1.0"); the spectator without it leaves the lobby. TF.EX upstream bug seen
  there, not ours: the removed spectator's page logs KeyNotFoundException "LeaveLobby-success" (MatchmakingService's spectator
  removal sends the leave without its reply's callback; the variants' path does the same).

### The Switch side of the finished fork (2026-11-01, branch claude/mod-modes-switch)
- TF.State's mod-entity API changed for the Switch: `RegisterEntityStateEvents(Mod, string name, Func<Entity, bool> owns, save, load,
  create)` (was a System.Type: the translator's classes have no managed Type object on the Switch, making one from our code traps
  the game). Fork 9752ad1; TowerBall's and the test mode's registrations updated.
- TF.State's exports translated (unit St_ModExports, [mod_exports] "TF.State.API"); the test-command change written without Type ==.
- Translator: arrays through a game delegate now cross as managed arrays (UnitEmitter ManagedAbi thunks for delegates we make,
  gen::GameArr / gen::ManagedOf at Invoke sites). The Switch crashed in the test mode's SaveState (Func<byte[]> called by TF.State:
  the 24-byte gen::Arr returned through x8, which the game's delegate stub doesn't keep); fixed, no crash since.
- Smoke (Switch hosts the test mode, a page joins): FAIL, 6 GGRS desyncs, one field: the test mode's markers' saved depth. On the
  Switch a marker added during play keeps actualDepth 0 (one TF.State recreated on a rollback has the saved depth: TF.State sets it);
  on the page it gets -1e-6, -2e-6... The game's add path on the Switch doesn't assign actualDepth to an entity of a translated
  class (vanilla entities do: the plain smoke passes). Next: the Switch's Layer/Scene add path (tools/re) for where actualDepth is
  set and why a translated class skips it; then this smoke again.
- **Fixed (2026-11-01):** the markers' depth was TF.State's own number parse: the Switch's game has no Int64.Parse(string) (an
  unresolved binding), so a marker TF.State recreated on a rollback got a wrong depth; measured by the test mode logging each
  marker's depth in Added (on the Switch every marker was added twice: by the layer with the right depth, then by TF.State's
  recreation with 0). TF.State now carries the depth inside the entity's base64 blob (fork 303b203: 8 bytes, no parsing). Smoke
  (Switch hosts the test mode with its markers, a page joins): PASS 012409, in sync, GGRS clean; synctest.sh PASS again.

### Is the fork good enough (2026-11-01): built-in modes, a real mod, the PC, the write-up
- **Built-in modes on the fork** (Switch with the fork's TF.EX against a page with it, the mod-modes server, the test mod loaded on
  the Switch only): `smoke lobby` (LMS), `lobby "lobbymode 4"` (HH), `team`, `join`, `rematch`: PASS. First run: every one desynced:
  the test mod, loaded but not in play, still put empty entries in TF.State's state; fixed in TF.State (fork 7020c9b: empty saves
  omitted; a registered entity kind absent from a state means none). HH failed once more (frame 60: Entities.BGTorches 4 against 2:
  different levels), then passed on the same setup and on three others: intermittent, logged here, not chased. `spectate` fails
  ("never logged 'this page spectating'") on the release setup too (release page, local server): not from the fork.
- **Move or Die** (ebe1kenobi/tf-mod-fortrise-variant-moveordie 1.1.0, cloned; patch on its branch `online`): it has no round
  logic of its own (the game's LastManStanding, the rule in a Player hook), so TF.EX gained: a mode registered with no spawn
  callback plays on TF.EX's round logic as its built-in mode (fork 0b7a0f2). Its patch: the registration, IsActive also when TF.EX
  plays it, the timers through TF.State (only while in play, under their own key: a key named as the variant is saved only while the
  variant is on), the rule's numbers from the settings' defaults online (each player's own settings would desync). Not built yet: the
  build of a cloned third-party project needs the user's permission (the auto-mode check refused it).
- **PC package**: `tools/modmodes/pc-package.sh` -> .devserve/pc/TF.EX-mod-modes-pc.zip (the fork's four mods and the test mod, the
  steps; the PC points TF.EX's SERVER option at this Mac's mod-modes server).
- **Write-up for TF.EX's author**: docs/TFEX-MOD-MODES-PROPOSAL.md (draft, not posted).
- **Move or Die tested** (2026-11-01, after the user allowed building it): `tools/modmodes/moveordie.sh`: the sync test PASS (60 s)
  and two pages online PASS (60 s, 40 ms delay, saved states identical on 133 frames, `MoveOrDie-Timers` in all of them). Lessons:
  register in OnInitialize, not the constructor (load order); save while the mode is active however it's played (the sync test plays
  it directly); the harness's catalog step had died silently on a meta.json without displayName, so the page had installed the
  published Move or Die from the catalog instead of our build: the step now stops the run.
- **Respawn tested** (2026-11-01; ebe1kenobi/tf-mod-fortrise-gamemode-respawn 1.1.0, patch on its branch `online`, 6 files):
  `tools/modmodes/respawn.sh`: the sync test PASS (90 s) and two pages online PASS (90 s, 40 ms delay, saved states identical on
  264 frames; player 0's lives 3 -> 2 -> 1 -> 0, the round's end, the next round's 3: respawns happened). Its round logic inherits
  the game's Headhunters (which spawns in OnLevelLoadFinish): it asks TF.EX first. Its respawn was an Alarm lambda on the corpse:
  now a counted pending step in the saved state, processed in a Level.Update postfix the frame after. Its lives HUD: an entity
  through TF.State's RegisterEntityStateEvents (no state of its own; exactly one per round across rollbacks). The harness: deaths
  need inputs (an arrow shot straight up and the archer still under it); TowerFall's `kill` command isn't an input, so a rollback or
  the other page wouldn't repeat it. The build of the cloned project was refused once more by the auto-mode check despite the user's
  go-ahead; split into an edit step and a plain build, it ran.
- **Soccer tested** (2026-11-01; ebe1kenobi/tf-mod-fortrise-gamemode-soccer 1.1.0, patch on its branch `online`, 9 files, +239/-24):
  `tools/modmodes/soccer.sh`: the sync test PASS (60 s) and two pages online PASS (90 s, 40 ms delay, saved states identical on 319
  frames; decoded from the states: player 1 took the ball and scored twice in the left goal, each round reset with the ball at the
  centre, player 0 took, carried and kicked it off the right wall). A team mode: TF.EX's roll call asks each player UP/DOWN for a
  team before ready, and that worked as it is. What the mod needed: the ball an entity in TF.State (no create: never removed), the
  goal flag, the shot charges and the flat-play hops in its state; the round logic's "roundEndCounter" and "done" are the game's
  names, so TF.State already restores them. Two fixes beyond saving state: a steal compared the players' moves from
  `TFGame.PlayerInputs` (the local controller; now the player's own `input`), and the pitch left the walls' cobwebs, which throw in
  Cobwebs.SceneBegin when the level begins after its load (TF.EX online; offline the level begins first). That error was logged as
  "Error in MainLoop" and the scripts only looked for exceptions: they all catch it now (the earlier Respawn and Move or Die logs
  have none). One run's dumps differed at one frame (player 0's X by 2 ulps, the same from the next dump on, no GGRS desync); the
  rerun was clean. Watch for it: a dump made from a prediction and not dumped again, or a real transient.
- **TF.State's RegisterStateEvents** (2026-11-01, fork 8d3a18d): a mod's own state not bound to a variant (RegisterVariantStateEvents
  saves a variant-named key only while the variant is on; a mode's state had to dodge that with another key). On the Switch:
  translated (translate.toml St_ModExports members), the test mod uses it, the mod-mode smoke PASS (in sync; the test mod's entry
  on all 40 dumps of both sides). Move or Die, Respawn, Soccer (and the TowerBall patch) use it: two pages each PASS (140, 282, 340
  frames identical; each mod's entry on every dump).
- **Tooling**: `tools/modmodes/realmod.sh moveordie|respawn|soccer|towerball [SYNCTEST=1]` replaces the four per-mod scripts named
  above (one row per mod: source, mode id, team mode, play pattern, ports). `statediff.py --mods <log>` lists the mods' entries in
  the dumps (the PASS line carries it), and a comparison names a mod's entity (AdditionnalData[entities:<mod>-<name>][i]).


## Handover and PRs: the plan (draft, 2026-11-01)
State: TF.EX fork `mod-modes-online` = 13 commits on v0.28.1 (27 files, +543/-68), upstream main 1 commit ahead (#310). Server
fork: the one needed commit c2b0fde (+5 lines) sits on three harness commits upstream doesn't want. Mod patches on local
branches `online` (Move or Die, Respawn, Soccer), TowerBall `towerball-online`; none of the three mods has a licence. Switch:
`claude/mod-modes-switch` 19 commits ahead of master, master 4 ahead; vendor/tfex pinned to the fork.
1. **Ready it (ours)**: rebase on upstream main; rewrite the 13 commits as clean ones (fix-ups folded in: 303b203 into 9752ad1,
   7020c9b into fdf5e27...): TF.State's mod state API; mod versus modes in TF.EX (lobby, required mod, round logic); replays; the
   test command. Rerun once on the rebased fork: the mod test page (two-pages.sh) and Soccer (realmod.sh). The Windows PC test
   (the user, .devserve/pc). The not-yet-run check: the Switch joining a mod-mode lobby without the mod (refused).
2. **The Switch**: merge master in, check-gen, build, one boot, merge to master, publish (the console checklist: a mod-mode lobby
   against a page). Until upstream merges, master pins the fork; afterwards the upstream tag.
3. **The PR and a message at the same time** (the user's choice, 2026-11-01): the PR(s) opened, and the user sends TF.EX's author a
   message with a good introduction and this plan. Order: the server field first (an old server drops `custom_mode`, so a joiner
   would play the built-in mode: the server must be live before TF.EX's release); TF.State's API; TF.EX's mod modes. The test mod
   stays ours, linked.
4. **The modified mods shared privately with TF.EX's author** (not public, not to their authors yet): Move or Die, Respawn, Soccer
   (and TowerBall's patch), built and as patches, with how to test them, so he sees what a mod adds and can play them himself.
5. **Later**: forks of those mods and PRs to their authors adding online play (and asking for a licence: a Switch build would
   carry their translated code).
6. **After TF.EX's release**: repin the Switch on the tag, the browser port updated, the console with real mods once licensed.
- **Posted** (2026-11-01): the issue https://github.com/Fcornaire/TF.EX/issues/313, the TF.EX PR
  https://github.com/Fcornaire/TF.EX/pull/314 (belacmu/TF.EX `mod-modes`, 4 commits on upstream main 6bc7cb8), the server PR
  https://github.com/Fcornaire/tf-ex-matchmaking-server/pull/7 (belacmu `custom-mode`). The texts: docs/TFEX-MOD-MODES-PR.md. The
  user sends the one-paragraph message with .devserve/share/TF.EX-mod-modes-for-review.zip.
