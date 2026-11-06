# TF.EX f4ae1eb → v0.29.0: update summary

`tools/tfex/new-tag.sh v0.29.0 f4ae1eb`: vendor/tfex f4ae1eb → 3d98d2e, 0 commits; 90 changed members in 30 files (members 7504 → 7456). Full report: [f4ae1eb-v0.29.0.md](f4ae1eb-v0.29.0.md).

## Verdict

- **Simulation (A, lockstep): 23 changed members; the port is behind on 0 (manifest), 0 have no ledger entry.** Nothing to port for lockstep.
- Protocol (B): 1 changes (server messages, models: the report section 7)
- UI (C): 0; no rule (?): 6 to triage; not ours (D): 13
- Coverage: ? → **1977/2140 applicable (2129/2327 all)**
- Generated code (tfex/gen, binding.json): 1284 files changed: `tfexgen_Aj_MyLevel.cpp`, `tfexgen_Aj_MyLevel.hpp`, `tfexgen_Aj_MyTFGame.cpp`, `tfexgen_Aj_MyTFGame.hpp`, `tfexgen_Aj_TFModFortRiseAiSimpleModule.cpp`, `tfexgen_Aj_TFModFortRiseAiSimpleModule.hpp`, `tfexgen_An_MyTFGame.cpp`, `tfexgen_An_MyTFGame.hpp`, `tfexgen_An_TFModFortRiseAiNovaModule.cpp`, `tfexgen_An_TFModFortRiseAiNovaModule.hpp`, `tfexgen_ArcherDataPatch.cpp`, `tfexgen_ArcherDataPatch.hpp` …
- Module build: **FAILED (see .cache/tfex/new-tag-f4ae1eb-v0.29.0.log)**
- Ledger keys that match nothing at v0.29.0: 4
  - skip: no member at v0.29.0: network/TF.EX.Domain/CustomComponent/LightPauseMenu.cs LightPauseMenu.KeyboardPressed
  - skip: no member at v0.29.0: network/TF.EX.Domain/Services/TF/InputService.cs InputService..ctor
  - replace: no member at v0.29.0: network/TF.EX.Domain/Services/TF/InputService.cs InputService._supportsTestInputs
  - replace: no member at v0.29.0: network/TF.EX.Domain/Services/UpnpService.cs *

## Our places this touches (for the hand port)

**Ledger replacements to re-check** (overrides.toml [replace]):

- [-] `network/TF.EX.Domain/Context/GameContext.cs *` (gap) → tfexgen_services.cpp the archer copies
- [-] `network/TF.EX.Domain/Services/TF/InputService.cs InputService._supportsTestInputs` (S-tools) → xplay.cpp NpLocalInput (vs0/vs1.txt scripts)
- [-] `network/TF.EX.Domain/Services/TF/InputService.cs InputService.PushTestInputs` (S-tools) → xplay.cpp NpLocalInput (vs0/vs1.txt scripts)
- [-] `network/TF.EX.Domain/Externals/GGRSFFI.cs *` (S-transport) → ../../../../ggrs-switch/src/lib.rs; ping_measurement_* = net.cpp pm + ggrs-switch/src/ping_measurement.rs (direct only)
- [-] `network/TF.EX.Domain/Interop/ITfStateApi.cs *` (S-rollback) → rollback.cpp; rnd.cpp
- [?] `network/TF.EX.Core/TFEXModModule.cs *` (S-native) → main.cpp

**Translated units** (translate.toml; regenerated above, check the diff and run the flow they serve):

- NetplayRoundLogic: `NetplayRoundLogic..ctor`, `NetplayRoundLogic.OnLevelLoadFinish`, `NetplayRoundLogic.matchmakingService`, `NetplayRoundLogic.replayService`
- NetplayVersusMode: `NetplayVersusMode.GetMinimumPlayers`, `NetplayVersusMode.IsTeamMode`, `NetplayVersusMode.OnCreateRoundLogic`, `NetplayVersusMode.OnStartGame`
- LobbyPanel: `LobbyPanel.UpdateInfo`, `LobbyPanel.UpdateMode`
- MatchmakingService: `MatchmakingService.HandleLobbyUpdate`, `MatchmakingService.KickSpectatorIfMissingVariants`, `MatchmakingService.PublishSettingsChanges`
- NetplayManager: `NetplayManager.SpawnFirstRound`
- LobbyVersusModeButton: `LobbyVersusModeButton..ctor`, `LobbyVersusModeButton.AvailableModes`, `LobbyVersusModeButton.IndexOfCurrentMode`, `LobbyVersusModeButton.Render`, `LobbyVersusModeButton.SelectMode`, `LobbyVersusModeButton.Update`
- LobbyVersusPlayerCountButton: `LobbyVersusPlayerCountButton.MinPlayers`
- PlayerCorpsePatch: `PlayerCorpsePatch.<type>`, `PlayerCorpsePatch.PlayerCorpse_Added_Postfix`
- RollCallElementPatch: `RollCallElementPatch.RollcallElement_NotJoinedUpdate_Prefix`
- VersusModeButtonPatch: `VersusModeButtonPatch.VersusModeButton_Update_Postfix`
- State_Level: `LevelExtensions.GetState`, `LevelExtensions.LoadState`
- ScenarioLaunch: `TFCommands.StartGame`
- State_TeamReviver: `TeamReviverPatch.FinishReviving`
- TfexModule: `TFEXModModule.RegisterAndLoad`
- St_ModExports: `ModExports.RegisterEntityStateEvents`, `ModExports.RegisterStateEvents`, `ModExports.RegisterVariantStateEvents`, `ModExports.UnregisterEntityStateEvents`, `ModExports.UnregisterStateEvents`
- Ex_ModExports: `ModExports.NetplayVersusMode`, `ModExports.OnNetplayLevelLoadFinish`, `ModExports.RegisterNetplayVersusMode`, `ModExports.UnregisterNetplayVersusMode`

**Service map** (overrides.toml [service]: the hand-written Switch side may need the new behaviour):

- `TF.EX.Domain.Ports.IReplayService.*`: `IReplayService.SetCustomMode`

**New or unported, no ledger entry** (0 A/B, 5 C/?): port, or add a skip with its reason:

- [?] changed `TfStateApi.RegisterStateEvents` (state/TF.State.Core/Api/TfStateApi.cs)
- [?] removed `IAPIManager.GetEntityStateEvents` (state/TF.State.Domain/Ports/IAPIManager.cs)
- [?] removed `IAPIManager.RegisterEntityStateEvents` (state/TF.State.Domain/Ports/IAPIManager.cs)
- [?] signature `IAPIManager.RegisterStateEvents` (state/TF.State.Domain/Ports/IAPIManager.cs)
- [?] removed `IAPIManager.UnregisterEntityStateEvents` (state/TF.State.Domain/Ports/IAPIManager.cs)

## Next

1. Read the verdict; port A first (lockstep), then B, then the places above. Ask tfex-adopt for the hand-written ones.
2. After porting: `python3 tools/tfex/manifest.py fill v0.29.0 [key glob]`, then `manifest.py check v0.29.0`.
3. Commit vendor/tfex, tools/tfex/reports, binding.json and tfex/gen together; a lobby match (smoke test) before merging.
