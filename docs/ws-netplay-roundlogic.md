# TF.EX's NetplayRoundLogic on the Switch (2026-10-26)

**Status:** switched over. A lobby match's round logic is TF.EX's own `NetplayRoundLogic`, generated
(`tfex/gen/tfexgen_NetplayRoundLogic.*`) as a real subclass of the game's `TowerFall.RoundLogic`, and created where the PC creates
it. The hand start path it replaces is deleted. Test results: below.

## How the PC does it

- `TFEXModModule.RegisterAndLoad` (vendor/tfex/src/network/TF.EX.Core/TFEXModModule.cs:108):
  `context.Registry.GameModes.RegisterVersusGameMode(new NetplayVersusMode())`. FortRise gives it the next free `Modes` value
  (11, `VersusModeExtensions.NetplayModeValue`), and TF.EX's menus put `MatchSettings.Mode = 11`, `IsCustom = true`
  (`ApplyNetplayMode`).
- FortRise's MonoMod patch `patch_RoundLogic.GetRoundLogic` (vendor/fortrise/TowerFall.FortRise.mm/Patches/RoundLogic.cs):
  `if (session.MatchSettings.IsCustom && CustomVersusGameMode != null) return gamemode.OnCreateRoundLogic(session)`, else vanilla.
- `NetplayVersusMode.OnCreateRoundLogic` = `new NetplayRoundLogic(session)`. Its `mode` is `NetplayVersusMode.ResolveMode()` (the
  replay's, else the lobby's GameData.Mode); it branches TDM / HH / LMS on it.
- `NetplayRoundLogic.OnLevelLoadFinish`: base; the first time `NetplayManager.Init(this)`, the replay and the match report, else
  `SpawnRoundPlayers` (VersusStart + the mode's spawn). `NetplayManager.SpawnFirstRound` spawns round 0 through
  `roundLogic is IRoundPlayerSpawner` once the session is synchronized.
- TF.State reads the round logic's private state by name (`DynamicData.For(roundLogic)`: `done`, `wasFinalKill`,
  `roundEndCounter`): on the PC these are NetplayRoundLogic's own fields.

## What the Switch's 1.4.1.6 has

- `RoundLogic.GetRoundLogic(Session)` +0xb37f50: `ldr x8,[x0,#0x20]; ldr w8,[x8,#0x30]` (Session.MatchSettings.Mode), `cmp w8,#7;
  b.hi` -> throw, else a jump table (0x38f369c) to the eight vanilla constructors. Its only caller is `Session.LevelLoadStart`
  +0xb446f0 (`bl 0xb37f50` at +0xb44720, result stored in Session.RoundLogic +0x58), as on the PC.
- No `MatchSettings.IsCustom` / `CustomVersusGameMode` (FortRise additions; translate.toml `[fortrise]`), no mode registry. Mode >= 8
  also throws in `GoalScore` and `PauseMenuType`, and `TeamMode` is inlined as `Mode == 5` in 7+ places (docs/TEAM-MODES-RE.md
  section 3). So the lobby match keeps the lobby's vanilla Mode 3/4/5 (unchanged; S-start), and only the round-logic creation is
  FortRise's.
- `RoundLogic` (size 0x68, vtable 0x48 bytes): `OnLevelLoadFinish`, `OnRoundStart`, `OnUpdate`, `OnPlayerDeath` are virtual; the
  generated class checks each slot against the base body before overriding it (bind: `v_RoundLogic_*`, `m_RoundLogic_*`).

## The switch-over

- translate.toml: `[[unit]] NetplayRoundLogic` (the class: `MakeClass` from RoundLogic's runtime type, its four overrides, its
  fields after the base's 0x68 bytes) and `NetplayVersusMode` (members `ResolveMode` only: the rest of the class is FortRise's
  registration). The `RoundSpawner` glue unit (translator/glue/RoundSpawner.cs) is deleted.
- xplay.cpp `GetRoundLogicHook` (+0xb37f50), TFEX-DIFF S-start: for a lobby match (`g_lobbyStart` and TF.EX's NetplayManager made:
  TF.EX's `Mode.IsNetplay()`), `NetplayRoundLogic::New(session)`, i.e. FortRise's patch + `OnCreateRoundLogic`; otherwise the game's.
  The class is made on the main menu (EndOfMatch.cpp `EnsureMatchClasses`, outside any rollback window).
- Deleted: xplay.cpp `GenStartHolds`, `BaseLoadFinish`, `GenStartLoadFinish`, `GenDisconnectedHolds`, `LmsLoadFinishShell` (the
  Wrap on TF.State's LMS patch) and the three trampolines on Headhunters / Warlord / TeamDeathmatch `OnLevelLoadFinish`
  (+0xa56440, +0xbca880, +0xb685c0); EndOfMatch.cpp `GenSetPending` (a hand copy of OnLevelLoadFinish's first-time block: Init,
  MatchReport.Begin), `SessionDisconnected`, and the unused `GenStartPending` / `GenEstablish`; overrides.toml's
  `NetplayRoundLogic.cs *` and `IRoundPlayerSpawner.cs *` [replace]s and the three IRoundPlayerSpawner [service] rules.
- Kept, TFEX-DIFF S-60hz: the session's input delay in 60 Hz ticks from TF.EX's GGRSConfig (EndOfMatch.cpp `SyncInputDelay`, now
  called by NetplayGate before EstablishSessionIfSynchronized instead of from the deleted GenSetPending).

Translator (general):
- a type test against a TF.EX interface with one implementing class of ours (`roundLogic is IRoundPlayerSpawner`) tests that
  class's runtime type, and the call goes to its method (NetplayManager.SpawnFirstRound, ReplayService);
- `DynamicData` members by name on a game type now also look at the classes of ours that configured units make as subclasses of it
  (TF.State's `done` / `wasFinalKill` / `roundEndCounter` on a NetplayRoundLogic read its own fields, as on the PC; before, they
  would have gone to the side table, and a rollback would not have restored them);
- a model list passed for a model-list parameter of another capacity (`lobby.SessionPlayers`, List<Player, 4>, for MatchReport.Begin's
  IEnumerable<Player>, List<Player, 16>) is copied (`gen::ModelListAs`).

Behaviour that changes with this (all toward the PC):
- TF.State's `LastManStandingRoundLogicPatch` (no VersusStart on a rollback frame) no longer applies to lobby matches; on the PC it
  doesn't either (TF.EX never runs that class in netplay).
- Warlord lobbies play NetplayRoundLogic's LMS branch, as on the PC (the Switch already played them as Mode 3).

## Tests (Ryujinx, private slot `subclass`, hidden, commit 092f09e)

- `smoke.sh lobby`: PASS, 0 differing ticks of 591. `[lobby] TF.EX's match classes made: ..., NetplayRoundLogic ok`,
  `[vs] round logic: TF.EX's NetplayRoundLogic 0x... (mode 3)`, `[tfex] Configuring and initializing current netplay session`,
  `[np] input delay 2 ticks at 60 Hz (TF.EX's GGRSConfig.InputDelay 8 at 240 Hz)`, `[tfex] Spawning players at netplay frame 0 (round 0)`.
  The page's own line: `round_logic=TF.EX.Core.RoundLogic.NetplayRoundLogic`.
- `smoke.sh rematch`: PASS (GGRS checksum clean). Round 1 spawned at netplay frame 2091, match over at 3157 with 155 rollbacks,
  `voted REMATCH!`, the rematch's round logic again NetplayRoundLogic.
- `smoke.sh team`: PASS, 0 differing ticks of 593; `round logic: TF.EX's NetplayRoundLogic ... (mode 5)`.
- Hardware checklist: the lines above; a TDM and an HH lobby once each to the end.
