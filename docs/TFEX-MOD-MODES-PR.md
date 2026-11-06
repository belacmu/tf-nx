# Mod game modes online: the issue, the PRs and the message (drafts, not posted)

Texts for handover step 3 (docs/PLAN-MOD-MODES-ONLINE-2026-10-31.md): an issue on FCornaire/TF.EX with the context and a guide for
mod authors, the TF.EX PR and the server PR linking to it, and the user's message to TF.EX's author sent with them. Branches:
belacmu/TF.EX `mod-modes` (4 commits on upstream main 6bc7cb8), belacmu/tf-ex-matchmaking-server `custom-mode` (1 commit on
upstream main). The private package for him: `tools/modmodes/share-package.sh` -> `.devserve/share/TF.EX-mod-modes-for-review.zip`.
Order: the issue first (the PRs name its number), then both PRs, then the message.

---

## Issue

**Title:** Mod game modes online

**In short: do for mods' game modes what TF.EX already does for mods' variants.** A mod's custom variant can be played online
today: the lobby carries it, a player without its mod can't join, and the mod puts the variant's state into the rollback state
through TF.State's `RegisterVariantStateEvents`. This brings the same to a mod's versus game mode: the lobby carries the mode, the
same four checks keep out players without its mod, and the mod puts the mode's state into the rollback state through TF.State.
The one thing a mode needs beyond a variant is the round itself: a mode can bring its own round logic, so TF.EX lets the mod
spawn the players once the session is up.

**Tested with real mods.** Three published mod game modes were patched for this and played online: Move or Die, Respawn and
Soccer (a rule on Last Man Standing, a mode with lives and respawns, a team mode with a ball). They stayed in sync, with TF.State's
saved state identical on both sides on every compared frame. The patched mods aren't ours to publish, so they're shared privately
with the maintainer, ready to drop in, with each mod's changes as a patch and how to try them. What each one needed is below.

### The problem

FortRise mods can add versus game modes (`IVersusGameMode`): Move or Die, Respawn, Soccer, TowerBall... Online, TF.EX only plays its
own Netplay mode (Last Man Standing, Headhunters, Team Deathmatch), so none of them can be played online, and mod authors have no
way in.

### The idea

The session stays TF.EX's Netplay mode, and a mod's mode runs inside it. TF.EX only provides general extension points; each mod takes
full responsibility for its own rules online: its state goes into TF.State, its randomness comes from the gameplay RNG. Nothing in
TF.EX knows any particular mod, so TF.EX never has to fix one.

- The lobby carries the mode's FortRise id (`GameData.CustomMode`, "ModName/ModeName"); `GameData.Mode` stays the built-in mode it
  plays like. GAME MODE lists registered mod modes after the built-in ones.
- A player without the mode's mod, or with another version than the host's, can't play it ("NEEDS <MOD> <VERSION>"), checked in the
  same four places as missing variants: the lobby list, joining, ready, spectating.
- The round: TF.EX spawns the players once the session is up (as it does for its own round logic), through a callback the mod gives.
  A mode without a round logic of its own plays on TF.EX's round logic.
- The state: two new TF.State calls, for a mod's own state (not bound to a variant) and for a mod's own entities.
- Replays record the mode and play it back. Spectators need nothing.
- The sync test (`test`) runs a mod mode by its id.

### The API

TF.EX (`TF.EX.API`):
- `RegisterNetplayVersusMode(Mod mod, string name, Action<RoundLogic> spawnPlayers)`: offer the mode online. `name` is the one given
  to `RegisterVersusGameMode`. `spawnPlayers` spawns a round's players and sets `RoundLogic.Players`; null for a mode without a
  round logic of its own.
- `OnNetplayLevelLoadFinish(RoundLogic roundLogic)`: called in the mode's `OnLevelLoadFinish`; true when the round is online, and
  then TF.EX spawns the players, so the round logic must not.
- `NetplayVersusMode()`: the mod mode being played online (its FortRise id), empty otherwise. Online `MatchSettings.Mode` is the
  Netplay mode, so a mode's own checks look at this too.
- `UnregisterNetplayVersusMode(Mod mod, string name)`.

TF.State (`TF.State.API`):
- `RegisterStateEvents(Mod mod, string name, Func<byte[]> save, Action<byte[]> load)`: a mod's own state (a mode's rules). Like
  `RegisterVariantStateEvents` without the variant: saved whenever `save` gives bytes.
- `RegisterEntityStateEvents(Mod mod, string name, Func<Entity, bool> owns, Func<Entity, byte[]> save, Action<Entity, byte[]> load,
  Func<Entity> create)`: a mod's entities. On a save each owned entity gives its bytes; on a load each is found again by its depth
  and given them back, made again through `create` when gone, and extras removed. `create` null: TF.State already remakes that
  entity (a mod's arrow type), and only its fields are given back.
- A mod with nothing to save leaves nothing in the state, so a player with the mod loaded but not in play and one without it stay
  in sync. Everything is carried in the mods' data: the state's format doesn't change.

### For mod authors: making a mode playable online

1. `meta.json`: TF.EX and TF.State as optional dependencies. Without them nothing changes; offline the mod works as before.
2. Import the APIs:
   ```csharp
   [ModImportName("TF.EX.API")]
   public static class TfExAPIModImports
   {
       public static Action<Mod, string, Action<RoundLogic>> RegisterNetplayVersusMode;
       public static Func<RoundLogic, bool> OnNetplayLevelLoadFinish;
       public static Func<string> NetplayVersusMode;
   }

   [ModImportName("TF.State.API")]
   public static class TfStateAPIModImports
   {
       public static Action<Mod, string, Func<byte[]>, Action<byte[]>> RegisterStateEvents;
       public static Action<Mod, string, Func<Entity, bool>, Func<Entity, byte[]>, Action<Entity, byte[]>, Func<Entity>> RegisterEntityStateEvents;
   }
   ```
3. Register in the module's `OnInitialize` (every mod is loaded then, so the exports exist whatever the load order):
   ```csharp
   OnInitialize = _ =>
   {
       typeof(TfExAPIModImports).ModInterop();
       typeof(TfStateAPIModImports).ModInterop();
       TfExAPIModImports.RegisterNetplayVersusMode?.Invoke(this, "My Mode", round => ((MyRoundLogic)round).SpawnPlayers());
       TfStateAPIModImports.RegisterStateEvents?.Invoke(this, "Rules", SaveRules, LoadRules);
   };
   ```
4. The round logic: move the players' spawn into its own method (the callback above), and let TF.EX take the round online:
   ```csharp
   public override void OnLevelLoadFinish()
   {
       base.OnLevelLoadFinish();
       // ...the level's own setup (the mode's entities, its pitch...)
       if (TfExAPIModImports.OnNetplayLevelLoadFinish?.Invoke(this) == true) return;   // online: TF.EX spawns the players
       SpawnPlayers();
   }
   ```
   When the round logic inherits a game round logic that spawns in its `OnLevelLoadFinish` (Headhunters), ask TF.EX before calling
   the base. A mode with no round logic of its own (its rules in hooks on a built-in one) passes null instead of a callback.
5. The mode's own checks ("is my mode playing") also true online: `settings.Mode == Entry.Modes ||
   TfExAPIModImports.NetplayVersusMode?.Invoke() == Entry.Name`.
6. Everything that affects play goes into the rollback state: the mode's fields through `RegisterStateEvents`, its entities through
   `RegisterEntityStateEvents`. Save nothing (an empty array) while the mode isn't in play.
7. The rules for code that runs during play:
   - randomness from the gameplay RNG (TF.State's `RegisterRng` / `UnregisterRng` around it);
   - no `Alarm` / `Flash` callbacks that carry play to a later frame: a rollback loses them. Keep a counter in the saved state and act
     when it runs out;
   - input from the player's own `input`, not `TFGame.PlayerInputs` (online that's the local controller only);
   - the same rule numbers for every player online: the settings' defaults, not each player's own settings;
   - online the level loads before it begins, so what `OnLevelLoadFinish` removes or adds must be valid before the level's entities
     begin (for example, a wall decoration left without its wall throws).
8. Test it: the sync test rolls back every frame and reports the first difference: `test <Mod>/<Mode> <start level> <map> <seed>
   <check distance>` (`_` for a space in the id), e.g. `test Soccer/SOCCER 0 0 42 2`. Then a lobby: GAME MODE past TEAM DEATHMATCH.

### Checked with real mods

Three published mods, patched as above (about 90 to 240 lines each), quite different from each other:
- **Move or Die**, a rule on Last Man Standing (no round logic of its own): its stationary timers in the state.
- **Respawn**, its own round logic built on Headhunters, with lives and mid-round respawns: lives and pending respawns in the state, a
  lives HUD entity. Its respawn was an `Alarm` callback on the corpse; it's now a counter in the state.
- **Soccer**, a team mode with its own round logic, a ball and no arrows: the ball as an entity, the goal and shot charges in the
  state.

Two players online with 40 ms delay so the matches roll back, 60 to 90 s each: no desync, and TF.State's saved state identical on
both sides on every compared frame (140 to 365 per run), each mod's state in every one. The sync test on each. With a small test
mode: the required mod (missing, another version), replays, spectators joining early and late. Built-in modes with these changes:
Last Man Standing, Headhunters, Team Deathmatch, joining from the lobby list, a rematch, all in sync.

### The PRs

- FCornaire/tf-ex-matchmaking-server: `GameData.custom_mode` (one field). It should be live first: an older server drops the field,
  and the player who joins would play the built-in mode instead.
- FCornaire/TF.EX: the changes above, in four commits (TF.State, TF.Replay, TF.EX, the test command).

### Open points

- The names (`RegisterNetplayVersusMode`, `OnNetplayLevelLoadFinish`, `RegisterStateEvents`).
- A mode's mod with another version is refused, where a variant's mod only warns: a mode's rules change between versions. Refuse
  or warn?
- Seen while testing, unrelated: a spectator removed for missing variants logs a KeyNotFoundException ("LeaveLobby-success").

🤖 Generated with [Claude Code](https://claude.com/claude-code)

---

## TF.EX PR

**Title:** Mod game modes online

Closes #<issue>. The context, the design and a guide for mod authors are in the issue.

## Why

FortRise mods can add versus game modes, but online TF.EX only plays its own Netplay mode, so none of them can be played online.

## What

What TF.EX already does for mods' variants, for mods' game modes: a mod's mode runs inside TF.EX's Netplay mode, the lobby
carries it and checks its mod, and the mod puts the mode's state into the rollback state. TF.EX provides general extension points
and each mod handles its own rules online.

- **TF.State** (`feat(state)`): `RegisterStateEvents` (a mod's own state, not bound to a variant) and `RegisterEntityStateEvents` (a
  mod's entities, by name and an ownership test). A mod with nothing to save leaves nothing in the state. The state's format doesn't
  change.
- **TF.Replay** (`feat(replay)`): a replay records the lobby's mod mode (`ReplayInfo` key 18) and plays it back.
- **TF.EX** (`feat: mod versus modes online`):
  - EX-API: `RegisterNetplayVersusMode`, `UnregisterNetplayVersusMode`, `OnNetplayLevelLoadFinish`, `NetplayVersusMode`; and
    `typeof(ModExports).ModInterop()` in the module, which was missing, so the exports (`IsPlayingOnline` too) weren't reachable.
  - Lobby: `GameData.CustomMode`; GAME MODE lists mod modes after the built-in ones; the lobby list shows its name; a team mod
    mode's own minimum lobby size.
  - The required mod in the four places missing variants are checked (lobby list, join, ready, spectator).
  - Round loading: `NetplayRoundLogic`'s netplay part moved as it is into one helper a mod's round logic goes through too.
- **The sync test** (`feat: the test command`): `test <Mod>/<Mode> ...` runs a mod mode.

Needs the server's `custom_mode` field (FCornaire/tf-ex-matchmaking-server PR), live first.

## Testing

- Three real mods patched for it (Move or Die, Respawn, Soccer) and a small test mode. Two players online with 40 ms delay: no
  desync, TF.State's saved state identical on both sides on every compared frame; the sync test on each.
- The required mod (missing, another version), replays, spectators (early, late).
- Built-in modes: Last Man Standing, Headhunters, Team Deathmatch, joining from the lobby list, a rematch: in sync.
- Each commit builds on its own.

🤖 Generated with [Claude Code](https://claude.com/claude-code)

---

## Server PR

**Title:** GameData.custom_mode: a mod's versus mode

For FCornaire/TF.EX#<issue>: mod game modes online.

## Why

TF.EX's mod game modes carry the mode in the lobby's `GameData`. Without the field the server drops it, and the player who joins
would play the built-in mode instead.

## What

`#[serde(default)] pub custom_mode: String` in `GameData`: the mode's FortRise id ("ModName/ModeName"), empty for the built-in
modes. Clients that don't send it get the default.

## Testing

`cargo test`: 53 passed. Used by the TF.EX PR's online tests.

🤖 Generated with [Claude Code](https://claude.com/claude-code)

---

## The message (from the user to TF.EX's author, with the links and the package)

Hi! I just opened an issue [link] and two PRs, TF.EX [link] and a one-field one for the matchmaking server [link], that do for
mods' game modes what TF.EX already does for mods' variants: a mod's versus mode plays online inside the Netplay mode, the lobby
carries it and checks its mod, and each mod takes care of its own rules, so TF.EX never has to know any particular mod. To show
it works I patched three real mods (Move or Die, Respawn, Soccer) and they play online in sync; they're ebe1.kenobi's, not mine,
so they're attached here just for you, ready to drop in with a README, and later I'd like to offer their author PRs for online
play. The issue has the whole picture and a guide for mod authors; the server field should go in first. I'm very open on the
shape, or to you writing it your own way. Thanks for TF.EX!
