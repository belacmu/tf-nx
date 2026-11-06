# Workstream tfex-services (2026-10-17): handed over

The translator's service rules (tools/tfex/overrides.toml `[service]`, tfex/runtime/tfexgen_services.{hpp,cpp}): what TF.EX's
generated code gets when it calls IMatchmakingService, INetplayManager, IInputService, IArcherService, GGRSFFI and ExFlags. Branch
`ws/tfex-services` (everything merged into master).

## Status at the hand-over (owner from now on: tfex-adopt, "integration")

**How it works.** A `[service]` key is `"<Namespace.Type>.<Member>"` (a trailing `*` = every member). The rule is `cpp = "<C++>"` ($0,
$1... = the arguments; `set = "<C++ with $value>"` for a property assignment), `value = "<literal>"`, or `noop = true`; `key` = the
ledger reason (S-start, S-rollback, S-input, D, todo ...), `why` = one line. `todo` rules are listed apart in tfexgen_report.txt.
The C++ side is `tfex::gen::svc` in hw-probe/exlaunch/source/program/tfex/runtime/tfexgen_services.{hpp,cpp} (compiled in place since 2026-10-18), written over
the hand services. Conventions the translator expects:
- Lobby models cross as the generated structs (ws::Lobby, ws::Player ...), converted from/to the hand models through their JSON (exact).
- A model collection is returned as a pointer (`List<ws::Lobby,16>*`); a TF.EX value type by value (`st::Input`); tuples as
  `Snap<Tup2<...>>` by value; strings as managed strings (ManagedUtf8).
- TF.EX Actions are managed delegates (void*): kept alive in the Keep() ring until called, invoked through slot 0x58; a delegate with an
  argument (Action<Lobby>/<int>/<string>) gets a ws::Lobby* / int32_t / managed string.
- "The local seat" is never a scalar where TF.EX compares seats: LocalSeatMask / IsLocalSeat / LocalHandleMask / IsLocalInputIndex
  (several local players per device planned, docs/research-couch-online.md section 5). GetLocalSeat = "our main seat" only.
- The own lobby is writable: svc::GetOwnLobby returns a live copy, written back to the hand lobby before the hand service reads or sends
  it, reconverted when the hand service replaced it (section "A writable own lobby" below).
- Netplay state comes from xplay.cpp's real GGRS/lobby state, never rnd::Capture: NpSessionForStats, NpStateForServices (resim, delay,
  remotes gone), NpInputsForServices (each simulated frame's inputs). Each flag logs its changes (`[tfex] svc IsServerMode -> 1`).
**Test** a rule: `tools/tfex/translate.sh coverage v0.21.0` before and after on the same tree (a fresh run; the committed report can be
stale), the call site in tools/tfex/translator/test/out/compile/program/tfex/gen/tfexgen_Cov_*.cpp, `tools/build-probe.sh` after
`translate.sh all` (then `git checkout` gen/ and binding.json), and a lobby run with the full hw.txt set (caveat below).
**Done:** every service call the coverage reaches is mapped except those below (groups 1-6, then GetArchers, the IsWide setter, the
writable own lobby). Last measured: 377 of 435 applicable methods (master 9051807 + GetArchers).
**Open (need a Switch feature, not a mapping):** the input lock (IInputService Disable/EnableAllControllers,
DisableAllControllerExceptLocal = "except the local seats", IsInputLocked), ResetPolledInput (v0.20.1 #273), Set/ClearSessionInputDelay
(no write path into xplay's session delay; only the unported InputDelayAdvisor calls them), spectators (IsSpectatorMode, SetSpectatorMode,
AddSpectators, catch-up, notices, SpectatorInputDisplay, GGRSFFI.netplay_frames_behind), skins (PublishCustomSkin), series
(IsSeriesPicker, PickSeriesMap, SeriesContinueChoice), ShowMatchStartWarnings, the roll call reconcile. Also: nothing on the Switch calls
NetplayManagerAbortToVersusOptions, so HasFailedInitialConnection / ConsumeAbortToVersusOptions stay false (STATUS.md backlog); EnterQuickPlay's
onQueued is never called (the hand service has no queued callback).
**Not tested at run time** (compile-checked only, no module unit called them yet): the lobby Task calls, GetCurrentInput, GetArchers,
IsInit / IsSynchronized / IsCaptureActive / IsRollbackFrame, IsDisconnected -> 1. Run-tested: IsServerMode / GetNetplayMode /
IsDisconnected -> 0 through GameplayLayerPatch, and the writable own lobby's self-test (lobby runs, 0 differing ticks).

## Harness caveat (for every session): lobby-vs-browser's hw.txt lines replace its defaults

`tools/lobby-vs-browser.sh "vsstop 600" "genpatch X"` drops the defaults (`rngcapture 1`, `lobbyhost 1`, `lobbyscript 1`, `npdelay 2`,
`lobbypred 8`, `vsstop 4380`: line 24, `[ $# -gt 0 ] || set -- ...`). Without `lobbyhost 1` / `lobbyscript 1` the Switch never hosts:
it sits on the main menu (`menu poll ... scene MainMenu state 2`), the local server sees no lobby, and the harness reports "the page
didn't join the lobby in 150 s", which looks like the known page-key flaw but isn't. Pass the whole set:
`LVB_PREKEYS=c tools/lobby-vs-browser.sh "rngcapture 1" "lobbyhost 1" "lobbyscript 1" "npdelay 2" "lobbypred 8" "vsstop 600" <your lines>`.

## Group 1 (1bbc619): flags from the real state, lobby calls

Coverage (compile-checked, v0.21.0): master 347/542 -> 348/542 (MatchEndChoices 5/6 -> 6/6). The committed
reports/translator-coverage-v0.21.0.md still says 325: it predates tfex-bind's merge, which already made it 347.

| rule | was | now |
|---|---|---|
| INetplayManager.IsInit | rnd::Capture | GGRS session exists (xplay NpSessionForStats) && !IsDisconnected |
| INetplayManager.IsSynchronized | rnd::Capture | that session's `tfx_state` == Running |
| INetplayManager.IsServerMode | rnd::Capture | own lobby non-empty or a session (TF.EX: SetRoomAndServerMode .. ResetMode) |
| INetplayManager.GetNetplayMode | 0 | Server (4) while IsServerMode, else Uninitialized (0) |
| ExFlags.IsCaptureActive | rnd::Capture | TF.EX PublishCaptureFlag: mode set && !(init && disconnected) |
| INetplayManager.GetNameForSeat | null | the session player at that GGRS handle (players by Seat), own = NetplayPreferences.Name |
| INetplayManager.GetFailedConnectionMessage | null | tfex/NetplayManager.cpp's message (null when empty) |
| GGRSFFI.netplay_current_frame / local_player_handle / IsInInit | 0 | tfx_current_frame / the local seat / false |
| GGRSFFI.* (the rest) | todo no-op | S-rollback no-op (xplay.cpp drives GGRS); netplay_frames_behind stays todo (spectators) |
| IMatchmakingService CreateLobby, JoinLobby, ExitQuickPlay, RematchChoice, ArcherSelectChoice, ResetLobbies, GetLobbies (both overloads) | unmapped | the hand MatchmakingService |
| IMatchmakingService IsLobbyFull, GetEndGameStatus | unmapped | from the own lobby, as TF.EX |
| IMatchmakingService RequestRollcallReconcile, ReconcileRollcallIfPending | unmapped | todo no-ops (the hand port has no roll call reconcile) |

rngcapture 1 no longer turns these on in the menus. Each of IsServerMode / IsInit / IsSynchronized / IsCaptureActive logs its changes
(`[tfex] svc IsServerMode -> 1`), so a run shows what TF.EX's generated code saw.

Tests: module build (tools/build-probe.sh) OK; coverage before/after above. Ryujinx: see group 2.

## Group 2 (f8d6a05): disconnect, rollback frame, input delay through a new xplay.cpp accessor

xplay.cpp + `NpStateForServices(resim, inputDelay, remotesGone, remoteGone)` (read-only, coordinator's OK): g_resim, g_npDelay and the
existing per-match g_remotesGone (GGRS disconnects before the results) / g_remoteGone (the remote stopped after them). No new counter.
- `IsDisconnected` = a session and (remoteGone, or every remote gone): the condition xplay.cpp ends a lobby match on (a 3-4 player
  FFA goes on when one leaves, so that isn't "disconnected"). Feeds IsInit and IsCaptureActive too.
- `ExFlags.IsRollbackFrame` / `HasFramesToReSimulate`, `INetplayManager.IsRollbackFrame` / `HaveFramesToReSimulate` = g_resim.
- `GetEffectiveInputDelay` = the session's delay (hw.txt npdelay).
Coverage stays 348 (these were fixed values before, already counted).

Ryujinx (shared instance), lobby-vs-browser with the full hw.txt set (`rngcapture 1`, `lobbyhost 1`, `lobbyscript 1`, `npdelay 2`,
`lobbypred 8`, `vsstop 600`, `notif 1`, `genpatch GameplayLayerPatch`), LVB_PREKEYS=c: joined t+65 s, synchronized, **0 differing
ticks of 599**, 10 rollback loads. The generated GameplayLayerPatch read `[tfex] svc IsServerMode -> 1` (through GetNetplayMode) and
`IsDisconnected -> 0` in the match. (Two earlier runs passed only my extra lines: lobby-vs-browser then drops its defaults, so the
Switch never hosted. Pass the whole set.)
Not checked at run time: IsDisconnected -> 1 (nobody left), IsInit / IsSynchronized / IsCaptureActive / IsRollbackFrame and the lobby
calls (no unit in the module reads them yet: compile-checked), anything on hardware.

## Groups 3-6 (2026-10-17)

- Group 3: local seats as a mask (svc::LocalSeatMask / IsLocalSeat / LocalHandleMask / IsLocalInputIndex; docs/research-couch-online.md
  section 5). tfex-translator emits `x == GetLocalPlayerInputIndex()` as `svc::IsLocalSeat(x)`.
- Group 4: quick-play counts / IsQuickPlayStarting (tfex/MatchmakingService), the failed-connection flags (tfex/NetplayManager.cpp; nothing
  calls AbortToVersusOptions on the Switch yet: STATUS.md backlog), UpdateLobbySettings, NotifyMatchEnded; model collections returned
  as pointers (translated code holds them by address).
- Group 5: PublishInputDelay (own player through the hand UpdatePlayer), UpdatePolledInput no-op, GetCurrentInput from xplay.cpp's
  g_npIn (NpInputsForServices: the inputs of the frame being simulated, each advance and resimulation).
- Group 6: JoinPrivate / EnterQuickPlay. Delegates with an argument: Invoke (slot 0x58) forwards a ws::Lobby* / int32_t / managed string
  to the generated lambda. onQueued is never called (TFEX-DIFF). JoinPrivate's success lambda is still blocked on the translator side.
Each lobby run (shared Ryujinx, full defaults, vsstop 600) after groups 2, 4 and 5: 0 differing ticks.

## Still todo, and why (2026-10-17, after group 6)

Features the Switch doesn't have (need a design, not a mapping): the input lock (IInputService Disable/EnableAllControllers,
DisableAllControllerExceptLocal: "except the local seats", IsInputLocked), ResetPolledInput (v0.20.1 #273), Set/ClearSessionInputDelay
(no write path into xplay's session delay; only the unported InputDelayAdvisor calls them), spectators (IsSpectatorMode, AddSpectators,
catch-up, notices, SpectatorInputDisplay, GGRSFFI.netplay_frames_behind), skins (PublishCustomSkin), series (IsSeriesPicker,
PickSeriesMap, SeriesContinueChoice), ShowMatchStartWarnings, the roll call reconcile (Request/ReconcileRollcallIfPending).
Not mapped on purpose: GetCurrentInputs (spectator feed only), GetPolledInput, AdvanceFrame / LoadGameState / ConsumeNetplayRequest
(TF.EX's own rollback loop in TFGamePatch, replaced by xplay.cpp).

## A writable own lobby (2026-10-17)

TF.EX's LobbyBuilder buttons write through ownLobby (`ownLobby.GameData.Mode = mode`, MaxPlayers ...). svc::GetOwnLobby rebuilt its copy
from the hand lobby on every call, so writes were lost. Now:
- tfex/MatchmakingService: `OwnLobbyVersion()` (bumped at every replace/clear of g_ownLobby: UpdateOwnLobby, ResetLobby, the server
  update and the abandoned-room clear in DrainGameThreadActions), `WriteOwnLobby(lobby)` (a plain store, none of UpdateOwnLobby's side
  effects), `SetOwnLobbySync(fn)`: GetOwnLobby, CreateLobby and DrainGameThreadActions run it first (UpdateLobbySettings through GetOwnLobby).
- services: g_own is the live copy, converted again only when the version changed (a server update replaces pending generated edits,
  as TF.EX's HandleLobbyUpdate replaces its ownLobby); edits (g_own differs from its shadow) are written back before the hand service
  reads or sends the lobby. So writes are immediate for every reader, as with TF.EX's single object.
- Seeding TF.EX's ownLobby before CreateLobby (the builder's settings): MatchmakingService::UpdateOwnLobby / WriteOwnLobby; reading it
  back at START: MatchmakingService::GetOwnLobby.
- Self-test, once per boot in the first own lobby: a Name written through the generated copy reaches the hand lobby, survives the next
  read and is restored (logged `[tfex] svc own lobby self-test: ...`; nothing is sent).
Ryujinx lobby run (full defaults, vsstop 600): 0 differing ticks of 599; self-test "reached yes, survived yes, restored yes"; the only
write-backs were the self-test's two.
Cost: while generated code holds the lobby, each hand GetOwnLobby compares the ~36 KB copy with its shadow (memcmp).
