# TF.EX wide mode on the Switch (investigation, 2026-10-16)

Branch `claude/tfex-wider-mode-support-011cb0`. Question: TF.EX supports a "wide" mode; the Switch has its own 6-player wide mode.
How different are they, and how could the Switch play TF.EX's wide lobbies without touching its local (offline) 6-player mode?

## 1. What TF.EX's "wide" actually is

TF.EX has no wide mode of its own. It interoperates with the FortRise mod **Teuria.WiderSet** (by Terria) through
`IWiderSetModApi { bool IsWide; float UIXOffset; }` (`.cache/tfex-0.19.2/src/TF.EX.Common/TF.EX.Common.Interop/IWiderSetModApi.cs`).

- **Player count: 2-4.** TF.EX lobbies never go above 4 (`LobbyVersusPlayerCountButton.cs:24,40`: `MaxPlayers > 4` refused;
  the browser's tfex-server `MAX_PLAYERS = 4`, `cloudflare/tfex-server/matchmaker.js:28`). WiderSet itself allows 8 players in
  local play (`WiderSetModule` ctor: `TFGame.Players = new bool[8]`), but netplay stays at 4. So TF.EX "wide" = **2-4 archers on
  420x240 levels**; the Switch's wide = 5-6 archers on 420x240 levels.
- **Screen: 420x240**, the same as the Switch's. `WiderSetModule.IsWide` sets `WrapMath.AddWidth = 420` (`WiderSetModule.cs`, setter);
  UI elements shift by `UIXOffset = 50` ((420 - 320) / 2).
- **Levels: WiderSet's own `Content/WideLevels/Versus/<tower>/*.oel`.** They are derived from the Switch/console 6-player set
  (same 16 towers, same file names, `tower.xml` identical apart from whitespace, WiderSet even ships the console's
  `sixPlayerBGData.xml`), but **every level file differs**. Compared with the Switch's `Content/Levels/6P-Versus` (romfs dump at
  `~/Roms/Switch/towergall extract`), 193 shared files:
  - entities differ in all 193: no `ChalicePad` (a Switch-only entity), and spawns topped up to 8 (`PlayerSpawn` 6 -> 8,
    `TeamSpawnA/B` 5 -> 6..8), with many spawns, `RandomBlock`s, `CrumbleBlock`s, orbs moved;
  - `Solids` differ in 67 of 193, `BG` in 28;
  - one extra file, `00 - Sacred Ground/a00.oel`.
  
  So the Switch's 6P levels cannot be used for a TF.EX wide match: the level files must be WiderSet's.
- **Version.** The browser runs WiderSet **0.9.0** (`Towerfall Browser/mods/overrides.json`, catalog `teuriawiderset-090.zip`).
  Both versions are decompiled (git-ignored): 0.8.0-beta.1 in `.cache/widerset-0.8.0b1/`, and 0.9.0 (downloaded from GameBanana
  dl/1754984, sha256 9299e85d... = the catalog's) in `.cache/widerset-0.9.0/` (mod files) and `.cache/widerset-0.9.0-src/`.
  **0.8 -> 0.9:** the whole `Content/` folder (all wide levels, tower.xml, sixPlayerBGData) is byte-identical; every gameplay hook
  of section 3 is unchanged except **how the wide tower is chosen** (section 3.1). New in 0.9.0, menu only: `MenuButtonsHooks`
  (MenuButtons.Update `4->8`), `MenuInputHooks` (MenuInput.UpdateInputs `5/4 -> 9/8`), `CustomLevelCategoryButtonHooks` (level-set
  list filtered by IsWide); the other changed files (ScreenHooks, MainMenuHooks, VersusPlayerMatchResultsHooks) differ only in menu
  code or generated field-accessor numbering.
  GameBanana marks WiderSet as **not redistributable** (catalog `"redistributable": false`): shipping its level files in our romfs
  needs Terria's permission, or the files are copied from the user's own WiderSet download at install time (like `tools/pc-levels.sh`
  does for the PC levels).

### Lobby/matchmaking side (already documented, LOBBY-PROTOCOL.md 4.4)
- A lobby carries `Mods: [{Name: "Teuria.WiderSet", Data: {IsWide: "True"|"False"}}]`. `WiderSetModApiData.CanUseWiderSet`: a wide
  lobby needs a wide joiner ("WIDERSET MOD ON WIDE REQUIRED"), a wide joiner refuses a narrow lobby ("WIDERSET MOD NOT WIDE REQUIRED").
- Quick play: `EnterQuickPlay { Name, IsWide }`; the server matches only equal `IsWide` (`matchmaker.js:559-568`); `QuickPlayStatus`
  reports `SearchingStandard` / `SearchingWide`.
- Menu flow on the PC: NETPLAY -> WiderSet's StandardSelection picker (STANDARD / WIDE) -> NetplaySelect (`WiderSetMenu.cs`,
  `IsNetplayRequested`). The roll-call lobby uses other label offsets when wide (`RollcallLayout.cs`: ping (-16,-40), name (0,40),
  team (0,-20)).
- The Switch today: always sends `IsWide False`, and `WhyNotJoinable` refuses wide lobbies (`tfex/MainMenuPatch.cpp:248`).

## 2. What runs on the PC in a wide netplay match

Two layers patch the same vanilla methods:
1. **WiderSet** (Harmony transpilers/prefixes on vanilla methods, active when `IsWide`): see section 3.
2. **TF.State** (TF.EX's rollback layer): `[HarmonyPrefix] ... return false` *replacements* of RoundLogic.SpawnPlayersFFA /
   SpawnPlayersTeams, OrbLogic.DoSpaceOrb / DoOffsetWorldVariant / DoDarkOrb, Miasma.Update, etc., active while
   `StateFlags.IsCaptureActive`. A prefix that returns false **skips the original body, including WiderSet's transpiled version of it.**
   These replacements hard-code the 320-wide constants and do **not** consult WiderSet:
   - `RoundLogicPatch.SpawnPlayersFFA`: `list[0].X != 160f` before using `WrapMath.Opposite` (`TF.State.Patchs.RoundLogic/RoundLogicPatch.cs:99`);
     loops `j < 4`.
   - `RoundLogicPatch.SpawnPlayersTeams`: `TeamSpawn` split at `X <= 160` and TopUpSpawns split at 160 (`:141,157-158`). On a 420
     level the centre is 210, so the PC's wide team spawns are split off-centre. The Switch must copy that, not "fix" it.
   - `OrbLogicPatch.DoSpaceOrb`: `Calc.Snap(x, 320, OffsetWorld ? 160 : 0)` and +-320 choices (`TF.State.Patchs/OrbLogicPatch.cs:42-47`);
     `DoOffsetWorldVariant`: +-160 (`:101-104`).
   
   Our port already reproduces these bodies (teamspawn.hpp, the TF.State ports), so in wide they stay as they are. What changes is
   everything *underneath* them that reads the screen width (WrapMath, Opposite, etc.).
3. Vanilla code the two layers leave alone keeps its PC 320 literals unless WiderSet transpiles it.

## 3. WiderSet's gameplay hooks

Paths relative to `.cache/widerset-0.8.0b1/Teuria.WiderSet/` (identical in 0.9.0 unless noted). Almost every patch is a transpiler that rewrites IL constants when
`IsWide`: `320->420`, `160->210`, tile columns `32/31/30->42/41/40`, half-widths `16/15->21/20`, player loops `4->8`.

**Simulation-affecting, wide only** (each must be equal on the Switch in wide):
| area | WiderSet hook | change | Switch status (to verify per method) |
|---|---|---|---|
| tower / level list | 0.9.0: no hook; WiderSet's 16 towers are FortRise-registered towers (section 3.1) | levels, treasure, `arrows` 0.6, `arrowShuffle`, `procedural` from `WideLevels/Versus/<dir>` | redirect to PCWide files; tower.xml equal to the Switch's 6P ones |
| level filter | vanilla `VersusTowerData.GetLevels` on the registered tower | `PlayerSpawns` = count of `PlayerSpawn`, `TeamSpawns` = min(count `TeamSpawnA`, count `TeamSpawnB`) (plain `TeamSpawn` not counted; FortRise `ModTowers.cs:139-160`), FFA `PlayerSpawns >= PlayerAmount`, TDM `TeamSpawns >= GetMaxTeamSize()` | same filter as pcgameplay's GetLevelsHook, on PCWide files; level order = the folder's file order (`00..NN`, then `a00` in Sacred Ground): to confirm, it feeds OwnGenLevel |
| load seed | `GetNextRoundLevel`: seed = sum of the level path's chars; the path is the FortRise RootPath of the mod file | LoadSeed only drives visuals (GAMEPLAY-RNG-PORT.md:164) | nothing, unless a later check finds a gameplay reader |
| level grid | LevelLoaderXMLHooks (`32->42`) | 42x24 grids | the Switch reads 420 levels natively |
| BG | LevelSystemHooks (GetBackground/Foreground from sixPlayerBGData) | built after `Calc.Random = new Random(LoadSeed)`: **open** whether it draws from Calc.Random | check the Switch's 6P background build |
| procedural geometry | LevelRandomGeometryHooks | 32/31/30/16 -> 42/41/40/21 | Cataclysm-style towers: check |
| items | LevelRandomItemsHooks (Collide, AddItems: many constants, `"310"->"410"`, `"160"->"210"`) | changes draw counts | **our C++ port (levelitems_core.hpp) is 320-only: needs a wide variant** + pcgen.py reference |
| treasure | LevelRandomTreasureHooks (16/160/320/15/14 and BigTreasureSpotValue `110->160`), TreasureSpawnerHooks (`160->210`, shield loop `4->8`) | | Switch uses grid width/2 (PARITY-TRIAGE); BigTreasureSpotValue: check |
| spawns | RoundLogicHooks (`4->8`, first `160->210`) | **dead in netplay**: TF.State's prefix replacements skip the original (section 2) | keep the TF.State ports with 160 |
| arrays / rules | SessionHooks, MatchSettingsHooks, VariantHooks (`4->8`) | sizes only; equal for <= 4 players | Switch is 6-wide already |
| wrapping | WrapMathHooks (all), WrapHitboxHooks, ActorHooks, LevelEntityHooks.EnforceScreenWrap, EnemyHooks.GetHorizontalPlayer, `WrapMath.AddWidth = 420` | 320->420, Opposite 160->210 | Switch: width static 420 (PARITY-TRIAGE "W" list): likely equal, verify |
| hazards | MiasmaHooks (ctor 320/330, Update 160/320/332, OnPlayerCollide 160), LavaHooks (320, 370->470, 305->405, 310->410), LoopPlatformHooks | | Switch: Miasma table 210, Lava W-15: verify; TF.State's MiasmaPatch is a void prefix (the original still runs): check our Miasma port in wide |
| orbs | OrbLogicHooks (CancelVariants, DoSpaceOrb, DoOffsetWorldVariant, EndScroll) | DoSpaceOrb / DoOffsetWorldVariant are **dead in netplay** (TF.State replacements keep 320/160) | CancelVariants, EndScroll: verify |

### 3.1 How the wide tower is picked (0.9.0): TF.EX's behaviour, to reproduce as is
- 0.8.0-beta.1 swapped the tower inside `VersusLevelSystem`'s ctor (`VersusLevelSystemHooks`, `MapEntry[levelID]`) and remapped
  `GetLevels` (`VersusTowerDataHooks` / `WideTowerManager`). **0.9.0 removed all three.**
- 0.9.0 registers each `WideLevels/Versus/<dir>` as its own versus tower in the set `<Teuria.WiderSet>/TowerFall`
  (`WiderSetModule.cs`, `RegisterVersusTower(text, "<Teuria.WiderSet>/TowerFall", ...)`). In wide, the map shows only that set
  (`MainMenuHooks`: `MapScene.TowerSet = "Teuria.WiderSet/<Teuria.WiderSet>/TowerFall"`; `MapSceneHooks.InitVersusButtons_Postfix`
  keeps only towers whose TowerSet contains `<Teuria.WiderSet>`).
- FortRise gives a registered tower `ID = (GameData.VersusTowers.Count, 0)` at registration (`ModTowers.cs:131`), so the wide towers
  are **IDs 16..31**, not 0..15. Its TreasureMask/TreasureChances are built by FortRise's registration (`ModTowers.cs:157-185`), not
  by vanilla `VersusTowerData` loading: to compare with the official towers' masks.
- TF.EX picks the lobby's tower in `MapScenePatch.Begin_Postfix` (`TF.EX.Patchs.Scene/MapScenePatch.cs:79`): the button with
  `Data.ID.X == lobby.GameData.MapId`, else `VersusRandomSelect`. The lobby builder offers MapId -1..15 (`LobbyVersusMapButton.cs:43`).
  TF.EX's behaviour is the spec (it works as intended); the Switch supports it as it is. Read from the code: since the wide buttons
  carry IDs 16..31, a wide match goes through `VersusRandomSelect`: `GetRandomVersusTower` postfix (`:95-134`) keeps the
  `VersusMapButton`s whose title is in `NETPLAY_SAFE_MAP` (the 16 official names; the wide towers reuse the official themes, so all
  16 pass), drops locked ones without the Dark World DLC, then `Calc.Shuffle(list, new Random(StateApi.GetSeed()))` and takes
  `list[0]`. The lobby list/panel show the official tower's name for MapId (`LobbyPanel.cs:236`).
- **Confirmed on a running page** (2026-10-16, harness page with WiderSet 0.9.0 wide, lobby seed 109144726, MapId 0): the page
  started `LevelSystem.ID={X:24}` SUNKEN CITY, levels `mod:Teuria.WiderSet/Content/WideLevels/Versus/08 - Sunken City/*.oel`
  (GetLevels: 00..10; 11.oel has fewer PlayerSpawns than 2), i.e. the RANDOM path above; the shuffle model
  (`tools/re/pcgen.py` NetRandom) gives Sunken City for that seed. The Switch's `tfex::Tower::SelectWide` picks the same tower.
- Without wide (the harness's first attempt, where TF.EX's main-menu reset had turned wide off again) the page played the official
  tower 0: TF.EX only reaches WiderSet's towers when IsWide is on when the match starts.

### 3.2 The code audit: PC + WiderSet vs the Switch with SixPlayer (2026-10-16)
Method: every reader of the Switch's WrapMath width static (`[0x4792130]`: 83 sites, listed with their methods by scanning for
`adrp 0x4792000; ldr x, [x, #0x130]`, not xref.py, which prints only 40) and of `WrapMath.get_CenterX` / `get_AddWidth`, against
WiderSet's hook list and the exact IL constants it rewrites ("all" vs "first" occurrence, int vs float matchers), with TF.State's
netplay replacements (which win over WiderSet's transpilers) on the PC side. Outcome:

| method | PC (TF.EX + WiderSet wide) | Switch (SixPlayer) | fix |
|---|---|---|---|
| Player.SceneBegin, Player.SideBouncePlayer, CrackedWall/CrumbleBlock/CrumbleWall/HotCoals/SwitchBlock ctors, FakeWall.SceneBegin, Ice.BuildTiles | not patched by WiderSet: 160 / 280 / 320 | width (420, 380, 210) | the width load -> `mov w, #320` (pcgameplay kGaps; a no-op at width 320) |
| RoundLogic.SpawnPlayersFFA (`list[0].X == 160`), SpawnPlayersTeams (split at 160), OrbLogic.DoOffsetWorldVariant (+-160) | TF.State's replacements: 160 | width/2 | same |
| OrbLogic.DoSpaceOrb | TF.State: snap to 320, +-320 | width (4 reads, pointer live) | the width static is 320 for the call (rnd.cpp DoSpaceOrbHook, wide only) |
| Actor.MoveTowardsWrap | WiderSet remaps only the first 320: first test 420, second 320 | both tests width | second load -> 320 |
| LoopPlatform.Added (horizontal walls) | `i < 42`, `(i + 41) % 42` | fixed 32, `(i - 1) & 31` | post-hook redoes the scan over 42 columns (pcgameplay LoopPlatformAddedHook) |
| LevelRandomTreasure.AddRandomTreasure, centre big chest | `solids[15]` / `solids[21]` (the int 15 isn't remapped) | `half - 1`, `half` | `sub w23, w8, #1` -> `mov w23, #15` |
| BigTreasureSpotValue / CompareBigTreasureSpots | `y <= 160` | `y <= 110` | rodata load -> a 160.0f in the same page (kWideGaps, only during a wide match) |
| LevelRandomGeometry.GenerateData (Cataclysm) | 32/31/30/16 -> 42/41/40/21 | derives from baseData's width; structure differs | the PC's ported (levelgeom_core.hpp) and used in wide; tools/test-levelgen.sh: 150/150 wide, 140/140 standard vs pcgen.py |
| LevelRandomItems.AddItems | remapped constants | (already the C++ port) | K() in levelitems_core.hpp (20 x 16, 4 x 15, 8 x 32, 4 x 31, ...) |
| WrapMath.* (incl. Vec, Shortest, AbsDiffX, SignX via the hooked ApplyWrapX/DiffX), WrapHitbox.BuildHitList, LevelEntity.EnforceScreenWrap, Miasma (Update's wide branch 210/420/432; the ctor's 320 is overwritten before any collision), Lava ctor (right side `X = width + 50`; the side fire hitbox height 240 vs 320/420 covers the screen either way) and Update (`width - 15`, `width - 10`), LoopPlatform ctors/Update, OrbLogic.CancelVariants/EndScroll, TreasureSpawner.GetChestSpawnsForLevel (CenterX) | remapped | width | equal |
| Level/LevelTiles/LevelBGTiles/Background/GameplayLayer/SFX pan/Spikeball.Render and the like | | width | render/audio only, **except** LevelTiles.ctor's SolidTiles read (32 -> 42): its overrides include lit tiles, whose AddTileIDLights light sources are depth-0 entities in the saved state (Layer.ActualDepthLookup); pcgameplay.cpp kWideGaps, 2026-10-27 |
| RoundLogic.ctor: miasma timer | Counter(1500) | `SixPlayer ? 1200 : 1500` (Switch-only, keyed on the flag, not the width) | 1200 -> 1500 online (kGaps; no-op standard) |
| VersusTowerData.FixedFirst (Sacred Ground, Ascension) | FortRise's RegisterVersusTower never sets it: false | tower.xml `<fixedFirst/>`: true | cleared on the wide tower for the match (SetWideMatch), restored after |

Readers of `MatchSettings.SixPlayer` (+0x20) on the Switch, found by scanning for `ldr x, [x, #0x20]` (Session.MatchSettings) +
`ldrb w, [x, #0x20]`, and for flag tests in gameplay methods: Session.StartGame (width), Miasma (210 table, equal), LevelLoaderXML
(42 columns, equal), RoundLogic.ctor (miasma timer, fixed); the rest UI (GunnStyle, VersusSeedDisplay) or Dark World bosses.
None in Arrow, Player, Actor, LevelEntity.

Already handled for every mode (not wide-specific): BRUTE's `Math.Round(double)` (+0xaca0) is `floor(x + 0.5)`, but xplay.cpp's
boot fixes rewrite it to `frintn` (ties to even, as .NET), and dm.cpp routes Math.Sin/Cos/Atan2/Pow to TF.EX's DeterministicMath, so
arrow seeking's trigonometry and actor movement rounding are bit-identical on both sides.

**Not visual (corrected 2026-10-27):** BG tiles / details. They run inside Push/PopRandom after the items, so they draw no gameplay
randomness, but what they place is gameplay: `LevelRandomBGDetails.GenerateTileData` appends `BGLantern` entities (BGTorches, which
set arrows on fire and are extinguished by dodges) and lit tiles whose `AddTileIDLights` light sources are depth-0 entities in the
saved state. The Switch's own versions placed them differently in wide (the wide Cataclysm desync), so a wide match runs the PC's,
ported with WiderSet's exact per-occurrence constants: `levelbg_core.hpp` (GenerateBitData + GenerateTileData; WiderSet skips some
IL occurrences, documented there), hooked in tfstate_ports.cpp beside the geometry and items ports.

**How to recheck the whole list:** `python3 tools/re/widecheck.py <WiderSet decompiled dir>` lists every IL constant WiderSet's
transpilers rewrite (about 90 methods) and whether the Switch's same method still holds the narrow value. Its 2026-10-27 run: the
LevelTiles SolidTiles read (fixed, kWideGaps), BigTreasureSpotValue (kWideGaps), LoopPlatform.Added (post-hook), LevelRandomItems /
LevelRandomGeometry (PC ports), Miasma (wide branch audited), RoundLogic spawns (TF.State's replacements), the rest player-count `4`s
(UI / arrays, equal for <= 4 players; the 8-player work) or drawing. A method the Switch implements differently (no constant to find)
needs a port, as above. End-to-end: `tools/parity-sweep.sh tools/parity/wide-towers.txt` (every wide tower by RANDOM) and
`tools/parity/wide-dreadwood.txt`.

Visual only: Level/LevelTiles ctor sizes (not the SolidTiles read: see the table), menus, HUD, results, replays,
SwitchBlock (DrawSolid only), roll call.

**Always active, even with wide off** (matters for the *current* standard-set parity if the browser player has WiderSet loaded):
- `Session.TeamStartArrows = {3,3,3,3,2,2,2}` (WiderSetModule.cs:184; vanilla `{3,3,2}`): a team of 3 (3v1 TDM) starts with 3 arrows,
  not 2.
- MatchTeams indexer finalizers (MatchTeamsHooks.cs:175-191): `get_Item` always returns a **static** dictionary entry (Neutral if never
  set through the indexer; `Teams[-1]` gives Neutral, not Red), while `GetPlayersOfAllegiance` etc. read the fields.
- `TreasureSpawner.ChestChances` grows to 7 rows; rows for 2-4 players unchanged.
- TFGame arrays / inputs sized 8, window 420: no simulation effect.

These two first points mean "PC with WiderSet installed, standard set" is not the same game as "PC without WiderSet" in 3-4-player
team modes. Worth checking which one the browser runs before the 3-4 player work lands.

## 4. The Switch's own wide mode (v1.4.1.6)

- Flag: `MatchSettings+0x20` (the Switch-only `SixPlayer` field; name tokens `get_SixPlayer` / `set_SixPlayer` / `WideModeButton` /
  `WideGameMode` / `GameData.SixPlayerTowers` exist in the binary).
- `Session.StartGame` (+0xb44530): `SixPlayer ? 420 : 320` into the WrapMath width static `[0x4792130]`, and resizes the screen
  (`+0x949290(w, 240)`). Every Switch gameplay reader of the width goes through that static (docs/PARITY-TRIAGE.md, "W" entries:
  WrapMath.*, Actor.MoveTowardsWrap, LevelEntity.EnforceScreenWrap, WrapHitbox, LoopPlatform, Lava, OrbLogic, SwitchBlock,
  CrackedWall, Crumble*, FakeWall, Ice, Enemy.GetHorizontalPlayer, Player.SceneBegin / SideBouncePlayer, RoundLogic.SpawnPlayersFFA,
  TreasureSpawner.GetChestSpawnsForLevel). Miasma uses table 0x38e1630 = {160, 210} by the same flag.
- Flag confirmed: `Session.StartGame` reads `ldrb w20, [MatchSettings, #0x20]` (+0xb4451c).
- Levels: `GameData.Load` (+0xa46740) builds two `List<VersusTowerData>` the same way: `GameData.VersusTowers` (static slot
  `0x47926b8`, +0xa47360) and the 6P-Versus towers (static slot `0x4794320`, +0xa475bc; the 16 towers, in folder order, with the
  official themes, so their titles match TF.EX's NETPLAY_SAFE_MAP).
- `MatchSettings.get_PlayerLimit` (+0xaba780) is per-Mode only (table 0x38e4d40: {1,1,1,4,4,4,4,1}), not per SixPlayer: where the
  6-player limit comes from is still open.
- `RoundLogic.SpawnPlayersTeams`, `MatchTeams` (6 slots), `Session.Scores` (6 in FFA), `MatchSettings.GoalScore` (PlayerGoals tables
  extended to 6) are already 6-player aware; all are equal to the PC for 2-4 players (docs/TEAM-MODES-RE.md, PARITY-TRIAGE.md).

## 5. What the Switch does now (branch claude/tfex-wider-mode-support-011cb0)

The Switch already does the expensive part natively: a 420-wide screen, WrapMath width 420, Miasma 210 and the wide camera all follow
`MatchSettings.SixPlayer`. What was added follows the pcgameplay.cpp rule: PC levels and PC rules, only while online.

- **Lobby (tfex/MainMenuPatch.cpp, Models.cpp, MatchmakingService.cpp, LobbyTest.cpp):** the Switch acts as a TF.EX player with
  WiderSet installed. Hosted lobbies carry `{Teuria.WiderSet: {IsWide: "True"|"False", Version: "0.9.0"}}` (TF.EX's
  AddWiderSetLobbyMod); the lobby list applies `CanUseWiderSet` as TF.EX's does (`WiderSetRefusal`: "WIDERSET MOD ON WIDE REQUIRED",
  "... NOT WIDE REQUIRED", "... ON OFF REQUIRED" for a mod-less lobby when wide, "... MISSING VALUE"); quick play sends `is_wide`.
  `Lobby::WiderSetIsWide` parses the entry like `bool.TryParse`.
- **Choosing wide (TF.EX's way, decided 2026-10-16):** NETPLAY opens the Switch's own STANDARD / WIDE screen (VERSUS's state 7,
  CreateVersusSub +0xaa1e20) with WiderSet's IsNetplayRequested set; its buttons (StandardModeButton / WideModeButton.MenuAction
  +0xb63890 / +0xbce530) then only record the choice and go to NetplaySelect, as TF.EX's SelectionButton_MenuAction does, leaving
  the local versus settings alone. NetplaySelect's back goes to that screen (MenuReturn.NetplayEntry). VERSUS clears the request
  (FightButtonPatch); the main menu clears it and turns wide off while in netplay mode or holding a lobby (MainMenu_CreateMain).
  Lobbies of the other kind are refused with TF.EX's texts ("CAN'T JOIN - WIDERSET MOD ON WIDE REQUIRED"). hw.txt `lobbywide 1`
  presets the choice for tests that host without menus, and makes the debug driver (lobbyui) pick WIDE.
- **Tower (tfex/NetplayManager.cpp `Tower::SelectWide`):** TF.EX's MapScene rules over WiderSet's towers (IDs 16..31, kSafe order),
  mapped by title onto the Switch's 6P-Versus towers; `LobbyLevelSystem(tower, wide)` takes the 6P tower's level system.
- **Match (xplay.cpp, pcgameplay.cpp `SetWideMatch`):** at lobby start, `MatchSettings.SixPlayer` = the lobby's wide, for every lobby
  match (a local 6-player game may have left it on); the value it replaced is given back when online mode is left (`Set(false)`).
- **Levels (pcgameplay.cpp GetLevelsHook, widelevels.hpp, tools/pc-levels.sh):** in a wide match, GetLevels lists WiderSet's files
  for the tower in FortRise's order (zip entries sorted by name: `00..NN`, then `a00` in Sacred Ground) from romfs
  `Content/Levels/PCWide/<tower>/`, with the same `PlayerSpawns >= PlayerAmount` filter. pc-levels.sh copies them from the user's
  WiderSet download (`XPLAY_WIDERSET`, default `.cache/widerset-0.9.0` of this or the main checkout; GameBanana doesn't allow
  redistribution) and refuses files that differ from widelevels.hpp. The Switch's own `Levels/6P-Versus` stays for local play.
- **Cataclysm items (levelitems_core.hpp):** AddItems with WiderSet's IL constant remap (each `K()` is one IL constant: 20 x 16,
  4 x 15, 8 x 32, 4 x 31, one each of 11/17/29/30, plus "310"/"160"), on the 42-column grid; tools/test-levelitems.sh still 560/560
  for the standard set.
- **Offline untouched:** the PC AddItems only runs in versus tests / netplay, the wide level list needs online mode and a wide match,
  SixPlayer is only written at lobby-match start and restored on leaving online; no other Switch instruction changes.

### 5.1 Harness (browser `switch-harness` worktree, local commits) and tools
- `xlobby wide=1`: WiderSet's IsWide on (what its WIDER button does), again after TF.EX's main-menu reset (`MainMenu_CreateMain`
  turns it off while in netplay mode), and `widerset_wide=` in the match log.
- `xlobby wide=1` also drops FortRise's `PlatformNotSupportedException` from `Level.InitializeModdedLevel`: it ends by creating a
  `FileSystemWatcher` on the mod level's folder (hot reload), which the browser's .NET doesn't support. **On the live site every mod
  tower's level (all of WiderSet's) stops the game ("Error in MainLoop()")**: the site needs the same fix (FortRisePatcher.BrowserFixups
  or a finalizer); that is browser-repo work.
- `tools/lobby-vs-browser.sh`: `LVB_MODS` (the page's mods) and `LVB_PREKEYS` (keys before the usual ones; `c` passes the "What's new"
  box a changed mod set shows). A wide run:
  `LVB_PREKEYS=c LVB_SEED=<seed> LVB_MODS=TF.EX,Teuria.WiderSet LVB_XLOBBY_EXTRA=wide=1 tools/lobby-vs-browser.sh "rngcapture 1" "lobbyhost 1" "lobbyscript 1" "npdelay 2" "lobbywide 1" "vsstop 2600"`.
  Seed -> tower: TF.EX's shuffle over the 16 wide towers (`tools/re/pcgen.py` NetRandom).
- Screenshots of the Switch's lobby browser, no browser page: `XPLAY_RJSLOT=B tools/panel-check.sh` (tools/lobby-host.mjs hosts a
  WiderSet wide lobby, `-n` a narrow one; the Switch walks to LOBBIES, selects it, presses A; shots in the run's out dir). Ryujinx runs
  as a normal window behind the front app there (a hidden one draws nothing), captured with `screencapture -l` (tools/winid.swift).
  2026-10-16: the refused wide lobby's panel showed map, mode, then the reason alone in red, as TF.EX's LobbyPanel.

### 5.2 Other points checked (2026-10-16)
- **Quick play count:** "N PLAYERS SEARCHING" now uses searching_wide when the search is wide (TF.EX's GetSearchingCount); it used the
  standard count always.
- **WiderSet's always-on hooks in standard lobbies** (a PC player who enabled WiderSet; the browser enables only TF.EX by default,
  `web/wwwroot/mods.js` DEFAULT_ENABLED): `Session.TeamStartArrows = {3,3,3,3,...}` only changes a team of three (3v1); the
  MatchTeams finalizers (static per-index values, `Teams[-1]` Neutral) don't change gameplay for 2-4 players: the gameplay readers of
  `Teams[i]` guard -1 (`Session.CanHurtPlayer`, ChalicePad) or are colours, CanHurtPlayer reads teams only in team modes, and TF.EX
  writes the teams through the indexer at every match start (MapScenePatch -> ApplyTeamsToMatchSettings). The Switch adds
  `{IsWide: "False"}` to standard lobbies like the browser site does.
- **TeamStartArrows with 3-4 players** (master's 2-4 player lobbies): in a wide lobby every PC has WiderSet, so SetWideMatch makes the
  Switch's `Session.TeamStartArrows` (static +0x4796168, `{3, 3, 2, 2, 1}`) `{3, 3, 3, 3, 1}` for the match (a team of 3 starts with 3,
  as WiderSet's `{3, 3, 3, 3, 2, 2, 2}`), restored after. Standard lobbies stay vanilla: the Switch can't tell whether a PC peer has
  WiderSet loaded (the site always sends `IsWide: "False"`); a 3v1 between a PC with WiderSet and one without differs on TF.EX too.
- **Lobby editing** (master's UpdateLobbySettings): it sends the own lobby's mods (the WiderSet entry), as TF.EX re-adds it.
- **AddRandomTreasure** (the Switch's body in wide): every width use matches WiderSet's remaps (`half - 2`, the `j < half` bound,
  `X != half * 10`, the mirror `width * 10 - X`), plus the centre-column patch.
- **Lobby roll call:** WiderSet's RollcallLayout offsets belong to its 8-seat wide roll call; the Switch keeps its 4-seat lobby roll call
  in wide too (RollcallMode Versus), so its standard label positions are the right ones. No change.

### 5.3 Not done
- The greyed-out look of refused lobby rows (TF.EX's Enabled/DisabledTint; the refusal text on A works), spectators
  (TF.EX's ApplyLobbyWideForSpectator).
- Lobby matches with more than 2 players (the general lobby limit today, not wide-specific); 3-4 player wide is the interesting case.
- The site's FileSystemWatcher fix (above).
- AddRandomTreasure is the Switch's own body (plus the centre-column patch): its other width uses (`half - 2`, `half`, `half * 10`)
  match WiderSet's remaps; not ported or checked offline like GenerateData / AddItems.

## 6. Open questions
- Does the browser load WiderSet by default (TeamStartArrows / MatchTeams side effects in standard 3-4-player team games)?
- Where the Switch's 6-player limit lives (not needed for 2-4 players).
- Licensing of WiderSet's level files (today: copied from the user's own download).

## 7. Test results
- First real wide match (2026-10-16, before the audit): seed 109144726 -> Sunken City (tower 24 on the page, 6P tower 8 on the Switch),
  2 rounds, **0 differing ticks of 2597**. Mirage 0 of 2597.
- A sweep over more towers before the audit failed on Cataclysm, Sacred Ground, Twilight Spire, Backfire, Flight. Those runs had no
  `lobbypred` line, and the default is 8: they were **rollback** runs, not lockstep. A Twilight Spire rerun with the same seed passed,
  and its Switch tick 1340 had the RNG the page had at the failing run's tick 1320: the page took the same path both times, the Switch
  left it in one, which only network timing (whether a rollback happened) can change. The code gaps of 3.2 were found by reading code
  afterwards. Next: lockstep (`lobbypred 0`) per tower first, then rollback, as for the standard set.
- After the audit fixes, lockstep (`lobbypred 0`), 1700 ticks: Sacred Ground 0 of 1668, Twilight Spire 0 of 1699, Backfire 0 of
  1699, Flight 0 of 1699, Cataclysm 0 of 1699 (generated level, the GenerateData port); rollback (`lobbypred 8`) Cataclysm 0 of 1698.
  Earlier (before the set-A patches): Sunken City 0 of 2597, Mirage 0 of 2597.
- **Ascension (fixed 2026-10-16, not wide-specific):** 28 of 1699 from tick 1333: an arrow 1 px further left on the Switch from tick
  1330 hit p0 one tick early. One traced run (page `xlobby ptrace=F0-F1` now also logs every arrow's Position, Speed, Direction, State;
  Switch hw.txt `atrace F0 F1` dumps the arrows' raw words: Speed +0x140, Direction +0x154, Actor.counter +0xd8) showed speed and
  direction bit-identical every frame, but the Switch's arrow started with Actor.counter -0.446 (the PC's 0): a pooled arrow's leftover
  sub-pixel remainder (Arrow.Init doesn't reset it). TF.State's ArrowPatch.Arrow_Removed postfix calls Arrow.FlushCache while capture
  is active, so every PC shot is a new Arrow; the port lacked it (GAMEPLAY-RNG-PORT.md 4.2 lists it). Ported: xplay.cpp
  ArrowRemovedHook (Arrow.Removed +0x985d60 -> FlushCache +0x985620 under capture). After: Ascension wide 0 of 1499, standard 0 of 1699.
- **All 16 towers, lockstep (2026-10-16, with the arrow fix, ~1700 ticks):** Thornwood 0 of 1699 (seed 1034), Frostfang Keep 0 of 1699
  (1020), King's Court 0 of 1698 (1009), Moonstone 0 of 1499 (1006), TowerForge 0 of 1498 (1014), The Amaranth 0 of 1699 (1003),
  Dreadwood 0 of 1499 (1007), Darkfang 0 of 1699 (1000); with the runs above, every wide tower has had a 0-difference match.
- **Wide TDM, 4 players, rollback (2026-10-16):** `XPLAY_RJSLOT=B LVB_PAGES=3 LVB_TEAMKEY=ArrowDown LVB_SEED=1004 LVB_MODS=TF.EX,Teuria.WiderSet
  LVB_XLOBBY_EXTRA=wide=1 LVB_JOINMAX=240 LVB_MATCHMAX=300 tools/lobby-auto.sh -s 650` with hw.txt `lobbypred 8`, `lobbywide 1`,
  `lobbymode 5`, `lobbyteam 0`, `lobbymax 4`, `vsstop 1200`: the Switch alone (team 0) against three pages (team 1), teams 0 1 1 1,
  Sacred Ground, team spawns topped up to 1 and 3. Every run agreed with all three pages tick for tick (344 to 755 ticks compared),
  except each page's last 10-11 logged ticks in p0 (the Switch's own archer) when the harness cut a run short: all three pages show the
  same there, so they are the pages' predictions of the Switch's input. 3 of 4 runs crashed the Switch: two rollback restore gaps, fixed
  (ROLLBACK.md, status 2026-10-16 night); after the fixes 2 of 2 ran without a crash. No run reached vsstop: in Ryujinx a 4-player
  match runs at 3-7 ticks/s (Ryujinx's socket service, ~5 ms per UDP call), so a finished 4-player match is for the console.
  Also 2 players, wide, rollback, Sacred Ground: 0 of 1694 (10 rollbacks); standard 4-player TDM rollback: no crash.
- Offline: tools/test-levelgen.sh (C++ GenerateData + AddItems vs pcgen.py) standard 140/140, wide 150/150; tools/test-levelitems.sh
  560/560.
