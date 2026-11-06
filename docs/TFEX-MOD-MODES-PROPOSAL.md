# Mod game modes online in TF.EX: a proposal (draft, not posted)

For TF.EX's author: a design to agree on before any PR. The code exists in our fork (belacmu/TF.EX, branch `mod-modes-online`, 11
commits on v0.28.1: 26 files, +514 / -62) so every point below has run, but the shape is open to change.

## The problem
FortRise mods can register versus game modes (`IVersusGameMode`): TowerBall, Move or Die, Respawn, Teams+. Online, TF.EX plays only
its own Netplay mode (Last Man Standing, Headhunters, Team Deathmatch), so none of them can be played online, and mod authors have
no way in.

## The idea
The session stays TF.EX's Netplay mode; a mod's mode runs inside it. TF.EX provides a few general extension points, and each mod
takes full responsibility for its own rules online (its state, its randomness). Nothing in TF.EX knows any particular mod.

## What a mod author adds (Move or Die, a real mod: about 90 lines)
```csharp
// meta.json: TF.EX and TF.State as optional dependencies; registration in OnInitialize (every mod loaded: their exports exist)
OnInitialize = _ => {
typeof(TfExAPIModImports).ModInterop();
TfExAPIModImports.RegisterNetplayVersusMode?.Invoke(this, "Move or Die", null);        // no round logic of its own: plays as LMS
TfStateAPIModImports.RegisterStateEvents?.Invoke(this, "Timers", SaveTimers, LoadTimers);   // empty while not in play
};
// a mode with its own round logic passes a spawn callback instead of null, and in its OnLevelLoadFinish (first, when it inherits
// a game round logic that spawns there, as Respawn's Headhunters):
if (TfExAPIModImports.OnNetplayLevelLoadFinish?.Invoke(this) == true) return;          // online, TF.EX spawns the round
// the mode's own hooks: "is my mode playing" also when TF.EX plays it
settings.Mode == Entry.Modes || TfExAPIModImports.NetplayVersusMode?.Invoke() == Entry.Name
```
The contract, like TF.State's for variants: state that affects play registered with TF.State (RegisterStateEvents, RegisterEntityStateEvents) and
saved only while in play;
randomness from the gameplay RNG; no callbacks (Flash/Alarm lambdas) carrying play across frames (a counted pending step in the
saved state instead: Respawn's respawn, TowerBall's timers); input read from the player's own state (its `input`), not the
controller (`TFGame.PlayerInputs` is only the local player's online); a round's level may begin after its load online (TF.EX loads it
first), so what a mod clears or adds in OnLevelLoadFinish must hold before the level's entities begin (Soccer's pitch left the
walls' cobwebs, which throw then); the rule's numbers the same for
every player online (not each player's own settings).

## TF.EX changes
- **EX-API** (TF.EX.API/ModExports): `RegisterNetplayVersusMode(Mod, name, Action<RoundLogic> spawnPlayers)` (null: plays on TF.EX's
  round logic as its built-in mode), `UnregisterNetplayVersusMode`, `OnNetplayLevelLoadFinish(RoundLogic)`, `NetplayVersusMode()`.
  Also a fix: TF.EX never called `typeof(ModExports).ModInterop()`, so its exports (IsPlayingOnline too) weren't reachable.
- **Round loading**: NetplayRoundLogic's netplay part moved unchanged into one helper (NetplayVersusModes.OnLevelLoadFinish) that a
  mod's round logic goes through too; NetplayVersusMode delegates IsTeamMode / OnStartGame / OnCreateRoundLogic / GetMinimumPlayers
  to the selected mode. The `is IRoundPlayerSpawner` lines stay as they are; a mod's spawn goes beside them.
- **Lobby**: `GameData.CustomMode` (the FortRise id, "ModName/ModeName"; GameData.Mode stays the built-in mode it plays like). The
  GAME MODE button lists registered modes after the built-in ones (not in a series); the lobby list shows the mod mode's name; a team
  mod mode's own minimum lobby size (GetMinimumTeamPlayers: TowerBall is one against one).
- **Required mod**: as for missing custom variants, in the same four places (lobby list, join, ready, spectator): a player without
  the mode's mod, or with another version than the host's, can't play it ("NEEDS MOVEORDIE 1.1.0"). Unlike a variant's mod (a version
  mismatch only warns), the version must match: a mode's rules change between versions. CatalogLobbyMods adds the mode's mod.
- **Replays**: TF.Replay records the CustomMode (ReplayInfo key 18) and plays it back; TF.Replay already refuses a replay whose mod
  is missing. **Spectators**: nothing needed.
- **The sync test** (`test`) takes a mod mode by its id: the level system asked as the map screen asks it (a mode's own levels).

## TF.State changes
- **A mod's own state**: `RegisterStateEvents(Mod, name, save, load)` (and `UnregisterStateEvents`): `RegisterVariantStateEvents`
  without the variant, for a mode's rules (a variant's state is saved only while its variant is on; a mode plays without one).
  Saved whenever `save` gives bytes.
- **A mod's own entities in the rollback state**: `RegisterEntityStateEvents(Mod, name, Func<Entity, bool> owns, save, load,
  Func<Entity> create)`. On a save each owned entity gives its bytes; on a load each is found again by its depth and given its bytes,
  made again through `create` when gone (extras removed). For a mod's arrow type (TF.State remakes arrows), `create` is null: only the
  mod's own fields are given back. Carried in the mods' data: no change to the state's format.
- **A mod not in play leaves the state as it is without the mod**: empty saves are omitted (a player with an idle mod and one
  without it stay in sync).

## Server
One field: `#[serde(default)] custom_mode: String` in GameData (our fork: belacmu/tf-ex-matchmaking-server, `mod-modes-online`).

## How it was tested
Browser (two to three pages), our own test mode (its own round logic, entities and a counter):
- lobby, required mod (missing and another version), replay, spectate (early, late), and the sync test (rolling back every frame):
  passes, and fails as it should when the mode's state isn't registered;
- the Switch (our port) against a page: in sync, the mode's entities included.
Built-in modes against the fork (the Switch with the fork's TF.EX, a page with it): Last Man Standing, Headhunters, Team Deathmatch,
the Switch joining from the lobby list, a rematch: in sync. (A player with a mod loaded but not in play and one without it: in sync
since empty mod saves are omitted; before that fix, every built-in match desynced against a page without the mod.)
Move or Die (a real, published mod, with our patch): the sync test (rolling back every frame) and two pages online with 40 ms
delay for 60 s: in sync, the saved states identical on all 133 compared frames, its timers in the state on every one.
Respawn (a second real mod: its own round logic, inheriting the game's Headhunters; lives; mid-round respawns; its own lives
HUD through TF.State's entity API): the sync test and two pages online for 90 s with deaths (lives 3 -> 0, respawns, the next round):
in sync, the saved states identical on all compared frames.
Soccer (a third real mod: a team mode with its own round logic, a ball entity, no arrows, nobody dies): TF.EX's team roll call
(UP/DOWN) worked for it unchanged; the sync test (60 s) and two pages online for 90 s (goals scored, rounds reset, the ball taken,
carried, kicked, bouncing): in sync, the saved states identical on all 319 compared frames.
A real PC: to do. TowerBall: patch written; its sync test still finds a gap (a ball released mid-frame).

## Open questions for you
- Names: `RegisterNetplayVersusMode` / `OnNetplayLevelLoadFinish`, `RegisterStateEvents`.
- Should a version mismatch of a mode's mod refuse (as here) or warn (as for variants)?
- TF.State: restore a neutral arrow's allegiance (`AllegiancePlusOne > 0` skips it; TowerBall's ball hit it).
- Seen while testing, not ours: a spectator removed for missing variants logs KeyNotFoundException "LeaveLobby-success".
