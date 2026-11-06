# TF.EX fb377d5 → team-revive-modmodes-v0.29.0: update summary

`tools/tfex/new-tag.sh team-revive-modmodes-v0.29.0 fb377d5`: vendor/tfex fb377d5 → a8b8f89, 16 commits; 97 changed members in 12 files (members 7447 → 7504). Full report: [fb377d5-team-revive-modmodes-v0.29.0.md](fb377d5-team-revive-modmodes-v0.29.0.md).

## Verdict

- **Simulation (A, lockstep): 4 changed members; the port is behind on 0 (manifest), 0 have no ledger entry.** Nothing to port for lockstep.
- Protocol (B): no change
- UI (C): 2; no rule (?): 0 to triage; not ours (D): 5
- Coverage: ? → **1978/2141 applicable (2162/2352 all)**
- Generated code (tfex/gen, binding.json): 1092 files changed: `tfexgen_ArcherDataPatch.cpp`, `tfexgen_ArcherDataPatch.hpp`, `tfexgen_ArcherService.cpp`, `tfexgen_ArcherService.hpp`, `tfexgen_Auto_APIManager.cpp`, `tfexgen_Auto_APIManager.hpp`, `tfexgen_Auto_ActionExtensions.cpp`, `tfexgen_Auto_ActionExtensions.hpp`, `tfexgen_Auto_Alarm.cpp`, `tfexgen_Auto_Alarm.hpp`, `tfexgen_Auto_AlarmExtensions.cpp`, `tfexgen_Auto_AlarmExtensions.hpp` …
- Module build: **FAILED (see ~/Code/Towerfall Switch Mods/switch-online/.claude/worktrees/archer-performance-scaling-3777f2/.cache/tfex/new-tag-fb377d5-team-revive-modmodes-v0.29.0.log)**
- Ledger keys that match nothing at team-revive-modmodes-v0.29.0: 4
  - skip: no member at team-revive-modmodes-v0.29.0: network/TF.EX.Domain/CustomComponent/LightPauseMenu.cs LightPauseMenu.KeyboardPressed
  - skip: no member at team-revive-modmodes-v0.29.0: network/TF.EX.Domain/Services/TF/InputService.cs InputService..ctor
  - replace: no member at team-revive-modmodes-v0.29.0: network/TF.EX.Domain/Services/TF/InputService.cs InputService._supportsTestInputs
  - replace: no member at team-revive-modmodes-v0.29.0: network/TF.EX.Domain/Services/UpnpService.cs *

## Our places this touches (for the hand port)

**Ledger replacements to re-check** (overrides.toml [replace]):

- [-] `network/TF.EX.Domain/Context/GameContext.cs *` (gap) → tfexgen_services.cpp the archer copies
- [-] `network/TF.EX.Domain/Services/TF/InputService.cs InputService._supportsTestInputs` (S-tools) → xplay.cpp NpLocalInput (vs0/vs1.txt scripts)
- [-] `network/TF.EX.Domain/Services/TF/InputService.cs InputService.PushTestInputs` (S-tools) → xplay.cpp NpLocalInput (vs0/vs1.txt scripts)
- [-] `network/TF.EX.Domain/Externals/GGRSFFI.cs *` (S-transport) → ../../../../ggrs-switch/src/lib.rs; ping_measurement_* = net.cpp pm + ggrs-switch/src/ping_measurement.rs (direct only)
- [-] `network/TF.EX.Domain/Interop/ITfStateApi.cs *` (S-rollback) → rollback.cpp; rnd.cpp
- [-] `network/TF.EX.Core/TFCommands.cs *` (S-tools) → xplay.cpp LoadHwConfig (hw.txt)
- [-] `state/TF.State.Patchs/Entity/LevelEntity/TeamReviver.cs *` (S-mode) → pcgameplay.cpp version-gap table (no TeamReviver from corpses)

**Translated units** (translate.toml; regenerated above, check the diff and run the flow they serve):

- SeriesScreen: `SeriesScreen.CloseLeaveDialog`, `SeriesScreen.FORFEIT_LABEL`, `SeriesScreen.LeaveLabel`, `SeriesScreen.LeaveLobby`, `SeriesScreen.Removed`, `SeriesScreen.RequestLeave`, `SeriesScreen.Update`, `SeriesScreen.UpdateFace`, `SeriesScreen.UpdateLeaveDialog`, `SeriesScreen.UpdateRankedFinished`, `SeriesScreen.UpdateTowerPick`, `SeriesScreen.leaveDialog`
- LobbyInfos: `LobbyInfos..ctor`, `LobbyInfos.BASE_HORIZONTAL_SCALE_X`, `LobbyInfos.NAME_LEFT_MARGIN`, `LobbyInfos.NAME_RIGHT_X`, `LobbyInfos.Render`, `LobbyInfos._nameRightX`
- MatchmakingService: `MatchmakingService.HandleKickedFromLobby`, `MatchmakingService.HandleLobbyUpdate`, `MatchmakingService.HandleMessageContents`, `MatchmakingService.IsInUnfinishedRankedGame`, `MatchmakingService.NotifySeriesAbort`, `MatchmakingService.ReturnToMenu`
- LightPauseMenu: `LightPauseMenu.Attach`, `LightPauseMenu.Close`, `LightPauseMenu.CloseForfeitDialog`, `LightPauseMenu.FORFEIT_LABEL`, `LightPauseMenu.ForceClose`, `LightPauseMenu.HandleStartPress`, `LightPauseMenu.IsRankedPlayer`, `LightPauseMenu.KeyboardPressed`, `LightPauseMenu.MenuButton.<type>`, `LightPauseMenu.PadButtonPressed`, `LightPauseMenu.PadPressed`, `LightPauseMenu.Pressed`, `LightPauseMenu.RankedOptions`, `LightPauseMenu.Removed`, `LightPauseMenu.Render`, `LightPauseMenu.ShowForfeitDialog`, `LightPauseMenu.Update`, `LightPauseMenu.UpdateForfeitDialog`, `LightPauseMenu.forfeitDialog`, `LightPauseMenu.isForfeitDialogOpen`
- State_Icicle: `IciclePatch.Icicle_Update`, `IciclePatch.RefreshCannotHitArrow`

**Service map** (overrides.toml [service]: the hand-written Switch side may need the new behaviour):

- `TF.EX.Domain.CustomComponent.RawMenuInput.Pressed`: `RawMenuInput.Pressed`

**New or unported, no ledger entry** (0 A/B, 2 C/?): port, or add a skip with its reason:

- [C] added `RawMenuButton.<type>` (network/TF.EX.Domain/CustomComponent/RawMenuInput.cs)
- [C] added `RawMenuInput.<type>` (network/TF.EX.Domain/CustomComponent/RawMenuInput.cs)

## Next

1. Read the verdict; port A first (lockstep), then B, then the places above. Ask tfex-adopt for the hand-written ones.
2. After porting: `python3 tools/tfex/manifest.py fill team-revive-modmodes-v0.29.0 [key glob]`, then `manifest.py check team-revive-modmodes-v0.29.0`.
3. Commit vendor/tfex, tools/tfex/reports, binding.json and tfex/gen together; a lobby match (smoke test) before merging.
