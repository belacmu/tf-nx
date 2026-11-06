/* The boundary between translated TF.EX code and the Switch's hand-written services (overrides.toml [service] maps TF.EX's interface
 * methods here). TF.EX resolves its services through DI (ServiceCollections.ResolveX()): here a service is a token and each method
 * of its interface is one of these functions. Lobby models cross as the generated structs (tfexgen_Models.hpp), converted from the
 * hand-written tfex::models through their JSON (exact: both are TF.EX's JSON, tools/tfex/translator/test/models-test.sh). */
#pragma once
#include "tfexgen_code_rt.hpp"
#include "tfexgen_Models.hpp"
#include "tfexgen_structs.hpp"

namespace tfex::models { struct Lobby; struct Player; struct GameData; }
namespace tfex::gen::svc {
/* TF.EX's generated MatchmakingService (hw.txt genmm 1, tfex/MatchmakingService.cpp): the object, null while the hand service runs */
void* GenMM();
/* the hand models <-> the generated structs (through their JSON; game thread: shared buffers) */
void* Service();                                /* ServiceCollections.ResolveX(): a non-null token */
/* FortRise's IModInterop.GetApi(name): a mod built into the module whose API the Switch provides (TF.State, Teuria.WiderSet: their API
 * interfaces' members are [service] rules) gives a non-null object; another mod, or one not loaded, null (as FortRise for a mod without
 * an API). Names compared ignoring case (TF.EX's ModCollections does). */
void* ModApi(const void* name);
/* TF.EX's ModCollections (generated), made once over FortRise's interop (the registry), kept in a GC root */
void* GenModCollections();
int32_t ModsMenuState();                       /* FortRise's MODS menu state on the Switch (tfex/ModsMenu.cpp: past TF.EX's MenuStates) */
/* IReplayService.Export with TF.Replay absent: no replay file (null), then ReplayService.Export's MatchReport.OnReplayExported(null) */
void* ExportReplay();
/* IMatchmakingService (tfex/MatchmakingService.hpp) */
ws::Lobby* GetOwnLobby();                       /* the own lobby, as the generated struct: writable (TF.EX writes through ownLobby); written back to
                                                 * the hand lobby before the hand service reads or sends it (SyncOwnLobby) */
bool IsSpectator();
int32_t GetLocalSeat();                         /* "our main seat" only (host Start, lobby editor); never compare seats with it: */
/* the local seats (docs/research-couch-online.md section 5: a device may own several lobby Players, all with its RoomPeerId). One
 * seat per device today. TF.EX's `seat == GetLocalSeat()` / `index == GetLocalPlayerInputIndex()` comparisons go through these. */
uint32_t LocalSeatMask();                       /* bit s: lobby Seat s is this device's (outside a lobby: TF.EX's default seat 0) */
bool IsLocalSeat(int32_t seat);
uint32_t LocalHandleMask();                     /* bit h: GGRS handle h (= rank of the Seat) is this device's */
bool IsLocalInputIndex(int32_t index);          /* IInputService's index: a handle in a session, a lobby seat in the menus */
void ExitQuickPlay();
void UpdateLobbySettings(int32_t maxPlayers, ws::GameData* gameData);   /* the mods: the own lobby's (the hand service re-adds WiderSet's) */
/* GetLobbies(onSuccess, onFail) requests the list; GetLobbies() is the last one received (both overloads: one rule). Model
 * collections are returned as pointers: translated code holds them by address (auto = the list's address, gen::FromModelList) */
List<ws::Lobby, 16>* GetLobbies(void* onSuccess = nullptr, void* onFail = nullptr);
/* GameContext's input store, for TF.EX's InputService (generated): IGameContext Get/SetLocalInput, Is/SetInputLocked */
void* GameContextObj();                         /* TF.EX's GameContext object (made at boot with the NetplayManager) */
void SetGameContextObj(void** root);
void* LocalInput();                             /* its local controller (GameContext.GetLocalInput) */
bool InputLocked();                             /* its input lock (GameContext.IsInputLocked: 5 s timeout) */
bool InputHeld();                               /* InputService.DisableAllControllers .. EnableAllControllers, whatever the timeout (S-input) */
void LogLocalInput(void* input);
void LogInputLocked(bool locked);
int32_t ControllerSlots();                      /* TF.EX's `WiderSet ? 8 : 4` controller slots: the game's (TFGame.PlayerInputs' length) */
int32_t InputIndex(void* input);                /* input's index in TFGame.PlayerInputs, or -1 */
void* AsPad(void* input);                       /* IsSameDevice's `is XGamepadInput`: a local JoyConInput, or null (S-input) */
bool SamePad(void* pad, void* otherPad);        /* ReferenceEquals(pad.XGamepad, otherPad.XGamepad): one JoyCon (S-input) */
void* NewRemoteSeatInput();                     /* EnsureRemoteControllers' `new KeyboardInput()`: a JoyConInput over the local JoyCon (S-input) */
void ClearRemoteSeatInputs();                   /* those out of TFGame.PlayerInputs again (lobbymatch.cpp ClearFilledSlots) */
void* GetRoomPeerId();
void SetRoomPeerId(void* id);                   /* MatchmakingService.peerId = id (null: reset) */
void ResetLobby();
int32_t GetSearchingCount();
int32_t GetStandardSearchingCount();
int32_t GetWideSearchingCount();
Opt<bool> IsRelayedWith(ws::Player* player);
Opt<int32_t> GetPingTo(ws::Player* player);                /* the lobby's ping measurement, by RoomPeerId (MatchmakingService::GetPingTo) */
Opt<int32_t> GetConnectionQualityTo(ws::Player* player);   /* ConnectionQuality: Good 0, Ok 1, Bad 2 */
/* INetplayManager: GGRSFFI's frame counter, names, the failed-connection message */
int64_t SyncWaitMs();                           /* ms since a lobby match started waiting for its first synchronization (-1: not waiting): TF.EX's _synchronizationTimer */
/* TF.EX's GameContext archer selections (IGameContext.GetArchers / GetPlayers / AddArcher / ResetArcherSelections / RemoveArcher): seat (or,
 * after ArcherService.CompactSeatsToHandles, GGRS handle) -> a copy of the lobby's Player (copies: the lobby's model storage moves on updates).
 * A fixed pool of kArcherSlots keys; a key beyond it is logged and dropped. */
Snap<Tup2<int32_t, void*>> ArcherSelections();          /* GetArchers: (key, "ArcherIndex-ArcherAltIndex") */
Snap<Tup2<int32_t, ws::Player*>> ArcherPlayers();       /* GetPlayers: (key, the stored Player) */
void AddArcher(int32_t index, ws::Player* player);
void ResetArcherSelections();
void RemoveArcher(int32_t index);
void* GenNM();                                   /* TF.EX's generated NetplayManager object (null before the first menu) */
/* NetplayManager / flags (xplay.cpp, rnd.cpp) */
bool InSession();                               /* a GGRS session exists (xplay.cpp): a netplay match runs */
void* SwitchMenuIcon(const char* name);      /* the Switch's MenuAtlas controls/buttons/<name> (FakeController's icons) */
/* INetplayManager.UpdatePlayers: TF.EX's own (generated), with the lobby's player lists at the generated lists' capacities */
void UpdatePlayers(void* players, void* spectators);
bool OnlineMode();
bool BuiltInVersusMode(int32_t mode);   /* FortRise's GAME MODE button: a built-in versus mode (the PC's three; local play: the megamod's TOWERBALL, 6) */
int32_t BuiltInVersusModeCount();   /* FortRise's BuiltInModeCount: 3, or 4 with the megamod's TOWERBALL (local play) */
void FortRiseStartGameTail(void* session);   /* FortRise's Session.StartGame tail: a custom mode's OnStartGame (tfex/FrVersusModes.cpp) */
void FortRiseArrowShuffleTail(void* list);   /* FortRise's GetArrowShufflePickups tail: list.Shuffle() (Calc.Random) once more */                              /* TF.State ModesExtensions.IsNetplay: the Switch's online mode (pcgameplay.cpp On), the PC's Mode 11 */                         /* ExFlags.IsRollbackFrame / HasFramesToReSimulate: xplay.cpp resimulates a rolled-back frame */
bool BoolTryParse(void* text, bool* result);   /* System.Boolean.TryParse(string, out bool): "true" / "false" in any case, whitespace and NULs
                                               * trimmed (.NET's); else false and *result = false */
void EnableFixedTimeStep(void* engine, bool on);  /* FortRise's Engine.EnableFixedTimeStep: IsFixedTimeStep, TargetElapsedTime 1/240 s on, 1/60 s off */
void PollForGate();                            /* INetplayManager.Poll: TF.EX's on the generated NetplayManager, then xplay's event logs (netplay_gate.cpp AfterPoll) */
gen::Arr<uint8_t> CaptureForSeam();            /* ITfStateApi.CaptureGameState: TF.State's, through xplay's rollback seam (netplay_gate.cpp) */
int32_t RestoreForSeam(gen::Arr<uint8_t> bytes); /* ITfStateApi.RestoreGameStateBytes: TF.State's, through xplay's rollback seam */
gen::Vector2 RightStickOf(void* input);           /* XGamepadData.GetRightStick: the controller's right stick (rightstick.cpp) */
void ScriptFeed();                              /* tests: the hw.txt script into GameContext's polled input ([after] ResetPolledInput) */
void ScriptFeedSeat(int32_t seat);              /* ... one seat ([after] UpdatePolledInput) */
bool IsCaptureActive();                         /* ExFlags.IsCaptureActive (NetplayManager.PublishCaptureFlag): mode set && !(init && disconnected) */
/* WiderSet (S-wide: the Switch's wide mode is built in) */
bool IsWide();
bool LightPausePressed(int32_t button);
/* LightPauseMenu.Pressed(MenuButton): Confirm 0, Back 1, Up 2, Down 3, Start 4 */
void SetWide(bool wide);                        /* IWiderSetModApi.IsWide = x (TF.EX: a spectator joining a lobby takes its wide flag) */
float UIXOffset();
void* PlatformCurrent();
void* SwitchOwnServer();                          /* NetplayPreferences.LocalServer: the Switch's own default server (no server on a console) */
/* logging: hwlog */
void Log(const void* managedText);
/* TF.EX's MatchmakingService (generated, translate.toml) on the Switch: its socket, ping measurement and relay bookkeeping stay the hand
 * service's (tfex/MatchmakingService.cpp); tools/tfex/reports/matchmakingservice-edge-rules.toml, mapped in overrides.toml [service] */
void PostServerMessage(void* msg);               /* TF.EX's listen loop: a received message for HandleMessageContents (game thread) */
int64_t TfexVersion();                             /* IAutoUpdater.GetCurrentVersion: TFEX_VERSION */
st::Status PingMeasurementStart(void* url, bool isRelay);   /* GGRSFFI.ping_measurement_start: net.cpp's measurement (direct only) */
void PingMeasurementStop(bool isRelay);
void PingMeasurementStats(void* peer, bool isRelayed, st::PingStats* out);   /* GGRSFFI.ping_measurement_stats: rtt -1, samples 0 when none */           /* the peers paired with ours in lobby.RelayedPairs */
} // namespace tfex::gen::svc
