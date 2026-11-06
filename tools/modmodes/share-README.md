# Mod game modes online: the mods, for review

Shared privately with you to go with the PR. Move or Die, Respawn and Soccer are ebe1.kenobi's mods, not ours: they're here
patched only so you can see what a mod adds to play online and try it yourself. Please don't pass them on; we'll offer the changes
to their author separately, later.

## What's inside
- `Mods/`, ready to drop into FortRise's `Mods/` folder:
  - `DShad.TF.EX`, `DShad.TF.State`, `DShad.TF.Replay`, `DShad.TF.InputDisplayer`: built from the PR branch (`@BRANCH@` at
    @TFEX@), without `Unmanaged/` (keep your installed one: the native netplay library).
  - `MoveOrDie`, `Ebe1.Respawn`, `Soccer`: the three mods with their online patch.
  - `NetplayModeTest`: our small test mode (a round logic of its own, an entity of its own, a counter). Source in `source/`.
- `patches/`: each mod's change, one diff against its author's `dev/fortrise5` branch.

## Trying it
1. Back up `Mods/`, then copy these folders over it (replacing TF.EX's files; keep `DShad.TF.EX/Unmanaged/`).
2. The matchmaking server needs the PR's one field (`GameData.custom_mode`). An older server drops it, and the player who joins
   would then play the built-in mode the mod mode is based on.
3. NETPLAY > LOBBIES > create. GAME MODE: past TEAM DEATHMATCH come the mod modes, by their registered names.
   In a team mode (Soccer) each player picks a team with UP/DOWN in the roll call, as in Team Deathmatch.
4. A player without the mode's mod, or with another version of it, sees the lobby as "NEEDS <MOD> <VERSION>" and can't join it.
5. The sync test runs a mod mode by its FortRise id (`_` for a space): `test <Mod>/<Mode> <start level> <map> <seed> <check
   distance>`, e.g. `test Soccer/SOCCER 0 0 42 2`, `test MoveOrDie/Move_or_Die 0 0 42 2`, `test Ebe1.Respawn/Ebe1.Respawn 0 0 42 2`.

## What each mod needed (all in the mod; nothing in TF.EX knows any of them)
Every mod: TF.EX and TF.State as optional dependencies in `meta.json` (nothing changes without them), its registration in
FortRise's `OnInitialize` (every mod loaded, so TF.EX's and TF.State's exports exist), its own "is my mode playing" checks also
true online (`NetplayVersusMode()`), and the same rule numbers for every player online (the settings' defaults, not each
player's own). About 90 to 240 lines each.
- **Move or Die** (a rule on Last Man Standing, no round logic of its own): registered with no spawn callback, so it plays on
  TF.EX's round logic. Its stationary timers go through `RegisterStateEvents`, empty while the rule isn't in play.
- **Respawn** (its own round logic, built on the game's Headhunters; lives; mid-round respawns): asks TF.EX first in
  `OnLevelLoadFinish` (Headhunters spawns there), and gives TF.EX a spawn callback. Lives, immunity and pending respawns go through
  `RegisterStateEvents`; its lives HUD is an entity through `RegisterEntityStateEvents`. Its respawn was an `Alarm` lambda on the
  corpse, which a rollback loses; now it's a counted step in the saved state.
- **Soccer** (a team mode with its own round logic, a ball, no arrows): the ball through `RegisterEntityStateEvents`; the goal,
  the shot charges and the hops through `RegisterStateEvents`. Its round end needed nothing: its fields have the game's names
  (`roundEndCounter`, `done`), which TF.State already restores. Two fixes: a steal compared the players' moves from
  `TFGame.PlayerInputs` (online that's the local controller only; now each player's own `input`), and its pitch kept the walls'
  cobwebs, which throw in `Cobwebs.SceneBegin` when the level begins after its load (TF.EX loads it first online).

## How they were tested (two players online, 40 ms delay so the matches roll back)
- The sync test (rolling back every frame), 60 to 90 s each: no desync.
- Two players online, 60 to 90 s: no desync, and TF.State's saved state identical on both sides on every compared frame
  (140 to 365 frames), each mod's state in every one. From the saved states: Respawn's lives going 3 to 0 with respawns, then the
  next round; Soccer's ball taken, carried, kicked into a goal, the round reset.
- TowerBall (FortRise's example mod): patched too but not ready (its sync test still finds a gap), so not included.
