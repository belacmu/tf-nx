/* xplay.cpp's internals, shared with the files split out of it (hwconfig.cpp, lobbymatch.cpp, netplay_gate.cpp): the headers they all
 * need, HWLOG, the types and the state that more than one of them uses (namespace xplay). Definitions are in xplay.cpp unless marked. */
#pragma once

#include "tfex/runtime/tfexgen_state.hpp"   /* the TF.State measurement (hw.txt statemeasure) */
#include "lobbymatch.hpp"
#include "tfexgen_NetplayManager.hpp"   /* TF.EX's NetplayManager (generated): the session's driver */
#include "tfexgen_ffi.hpp"
#include "lib.hpp"
#include "lib/hook/nx64/impl.hpp"
#include "pools.hpp"
#include "loggers.hpp"
#include "nn/fs.hpp"
#include "hwlog.hpp"
#include "rnd.hpp"
#include "rumble.hpp"
#include "devupdate.hpp"
namespace tfex {
                 void OnLobbyResultsFinished(int winnerSeat, const int* scores, int nScores);
                 namespace EndOfMatch { unsigned long TestButtons(); } }
namespace pcgameplay { void Set(bool on); void SetWideMatch(void* matchSettings, bool wide); }
namespace levelitems { void RunPc(void* self, void* solids, void* entities, bool teams); void* RunPcGeometry(void* rects, void* baseData);
    void* RunPcBGBitData(void* solids); void* RunPcBGTileData(void* baseSolids, void* bg, void* finalSolids, void* entities); }
namespace pcgameplay { bool Wide(); }
#include "tfex/MainMenuPatch.hpp"
#include "tfex/NetplayOptions.hpp"
#include "tfex/EndOfMatch.hpp"
#include "tfex/MatchmakingService.hpp"
#include "tfex/NetplayScreens.hpp"
#include "tfex/Notification.hpp"
#include "tfex/MapScenePatch.hpp"
#include "tfex/runtime/tfexgen_services.hpp"
#include "tfex/gen/tfexgen_Auto_Lobby.hpp"
#include "tfex/runtime/tfexgen_glue.hpp"
#include "tfex/gen/tfexgen_hooks.hpp"
#include "tfex/gen/tfexgen_SessionPatch.hpp"
#include "tfex/gen/tfexgen_State_Api.hpp"   /* TF.State's TfStateApi: StepOwnedControllers, SynchronizeSfx, ClearSfx from the Level.Update postfix */
#include "tfex/gen/tfexgen_Auto_ServiceCollections.hpp"   /* the sound log: TF.State's StateContext's playing sounds */
#include "tfex/gen/tfexgen_Auto_StateContext.hpp"
#include "tfex/gen/tfexgen_Auto_SoundEffectPlaying.hpp"
#include "tfex/gen/tfexgen_Auto_ExFlags.hpp"
#include "tfex/gen/tfexgen_binding.hpp"
#include "tfex/gen/tfexgen_NetplayManager.hpp"
#include "patch/code_patcher.hpp"
#include <lib/util/sys/modules.hpp>
#include <common.hpp>

#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <algorithm>

/* Lines that matter on the console go to the shared log (the SD card) as well as the guest log. */
#ifndef HWLOG
#define HWLOG(...) do { char hwb_[700]; snprintf(hwb_, sizeof hwb_, __VA_ARGS__); Logging.Log("%s", hwb_); hw::Append(hwb_); } while (0)
#endif

namespace xplay {
/* tfex/MatchPause.cpp (TF.EX's mid-match pause overlay, generated LightPauseMenu; LevelPatch's HandlePausing prefixes are generated hooks) */
void tfex_MatchPauseLogChange(); bool tfex_MatchPauseIsOpen();
unsigned long tfex_MatchPauseTestButtons(); void tfex_MatchPauseSetTest(int frame, int resume); void tfex_MatchPauseFrame();
namespace fld {
    constexpr uint32_t Entity_PositionX = 0x14, Entity_PositionY = 0x18;
    constexpr uint32_t Player_PlayerIndex = 0xe4;
    constexpr uint32_t Player_SpeedX = 0x2c8, Player_SpeedY = 0x2cc;
}
inline uintptr_t Base() { return exl::util::GetMainModuleInfo().m_Total.m_Start; }
struct FrameCost { double tick, rb, ggrs, resim, gate, save; int resimN; };   /* reset at the start of each FrameHook */
struct CostTimer { double* into; uint64_t t0; explicit CostTimer(double* d) : into(d), t0(svcGetSystemTick()) {} ~CostTimer() { *into += (svcGetSystemTick() - t0) / 19200.0; } };
inline uintptr_t Slot(uintptr_t off) { return *reinterpret_cast<uintptr_t*>(Base() + off); }
extern "C" {
struct TfxInput { int32_t jc, jp, sc, sp, ac, ap, dc, dp, arrow, mx, my; float ax, ay, rx, ry; int32_t disc; };
/* TF.EX's ggrs_ffi (ggrs-ffi-switch; docs/ws-ggrs-ffi.md): the session TF.EX's generated NetplayManager drives. These are the reads the
 * Switch's own parts (rollback seam, logs) need beside it; netplay_switch_confirmed_frame is the Switch's export (TFEX-DIFF). */
int32_t netplay_current_frame();
int32_t netplay_switch_confirmed_frame();
int32_t netplay_switch_pending_requests(int32_t* kinds, int32_t cap);
}
/* ggrs_ffi's frame counters for the Switch's logs, or -1 while Connect waits in netplay_init (on the worker thread, which holds ggrs_ffi's lock
 * for up to ConnectionTimeoutMs): TF.EX's GGRSFFI.IsInInit; a game-thread call then would wait for the whole connection */
inline int NpFrame() { return tfex::gen::ffi::InInit() ? -1 : netplay_current_frame(); }
inline int NpConfirmed() { return tfex::gen::ffi::InInit() ? -1 : netplay_switch_confirmed_frame(); }
inline void NpCopy(TfxInput* to, const TfxInput* from) { for (int i = 0; i < kNpSeats; i++) to[i] = from[i]; }
inline double Ms(uint64_t ticks) { return ticks / 19200.0; }
constexpr int kRbWindow = 64;           /* frames kept per frame-keyed ring (the inputs, the bookkeeping): beyond any rollback GGRS asks for */
struct SideAll {   /* the frame loop's own bookkeeping per frame (TF.State's state carries the game's) */
    void* seatPlayer[kNpSeats]; int seatTick[kNpSeats];
    int vsOrigin, lvTick;
};

/* ---- state and functions shared by xplay.cpp, hwconfig.cpp, lobbymatch.cpp and netplay_gate.cpp (defined in xplay.cpp unless noted) ---- */
extern bool g_captureForced, g_npOn;
extern bool g_testSession; extern int g_testMismatches;   /* TF.EX's test-mode session (BeginTestSession, lobbymatch.cpp) */
void PrepareTestSession();   /* before TF.EX's test launch (lobbymatch.cpp) */
void BeginTestSession(void* nm, int players);
extern int32_t g_hwSeed; extern bool g_hwSeedSet;   /* hw.txt "rngseed n": the seed of a scripted native (non-lobby) match */
extern int g_npWaitSkip;
extern int g_npSeat, g_npDelay, g_npPred, g_npPad;
int SessionHz();   /* the session's ticks per second: TF.EX's GetSessionFps (its 240; a test mode's own rate) */
int64_t SessionStepTicks();   /* the session's Game.TargetElapsedTime as on the PC: round(1e7 / SessionHz()) */
extern int g_shiftFrom, g_shiftN, g_vsStop, g_vstart;
extern FrameCost g_fc;
extern bool g_lobbyStart;
extern int g_lobbyPlayers;
extern int g_lobbyPred, g_lobbyScript, g_resultsFrame;
extern uint64_t g_syncWaitSince;
extern void* g_session;
extern bool g_matchFinished;
extern uint64_t g_interruptedSince;
extern bool g_remoteGone, g_vsLogDone;
extern int g_goal;
extern void* volatile g_game;
extern bool g_vsStarted, g_vsOn, g_vsPending;
extern int g_matchPads;
extern int g_lvTick;
extern int g_vsOrigin;
extern void* g_seatPlayer[kNpSeats];
extern void* g_npSession;
extern int g_npFrame;
extern int g_npN;
void NpCurrentInputs(TfxInput* out);   /* TF.EX's GameContext current inputs, per seat */
int ResimBegin();               /* a resimulated frame's bookkeeping around its Level.Update (LevelUpdateShell): returns the frame */
void ResimEnd(int frame);
void FrameOwnBegin();           /* the frame's own update's bookkeeping, before its Level.Update (LevelUpdateShell) */
bool MatchEndFrame();           /* the Switch's match end before TF.EX's TFGame.Update prefix (true: skip the update) */
void BeforeTfexUpdate();        /* the sync wait's start and the session's input delay, before TF.EX's prefix */
void AfterPoll(void* nm);       /* the Switch's per-event logs after TF.EX's Poll */
tfex::gen::Arr<uint8_t> SeamCapture();          /* TF.EX's StateApi.CaptureGameState through the rollback seam (netplay_gate.cpp) */
int SeamRestore(tfex::gen::Arr<uint8_t> bytes); /* TF.EX's StateApi.RestoreGameStateBytes through the rollback seam */
extern bool g_resim;
extern int g_npEvents[8];
extern bool g_rbReal;
extern int g_rbLoads, g_rbMaxDepth, g_rbResimTicks;
void AllocWatch(bool on);   /* hw.txt allocwatch (netplay_gate.cpp) */
extern int g_remoteChanges, g_rbLoadsAtStart, g_matchFirstTicks, g_remotesGone;
extern TfxInput g_lastRemote[kNpSeats];
extern int g_commitNext;
extern int g_stateFrame;
extern volatile bool g_resultsDone;
void LoadVsScripts();
void VsFlush();
void VsEvent(const char* what);
void CallVirt0(void* obj, uint32_t slot);
void LogCommit(int upTo);
void LogPlayersArray(const char* when);
TfxInput NpLocalInput(int seat, int F);
void ScriptFeed();
void ScriptFeedFrame();
void ScriptFeedSeat(int seat);
void SideSave(SideAll* s);
void SideLoad(const SideAll* s);
void GfxRecScan();
/* tfstate_ports.cpp (Cataclysm's GenerateData / AddItems) */
void InstallTfStateHooks();
/* hwconfig.cpp */
void LoadHwConfig();
/* lobbymatch.cpp (the lobby match's start; lobbymatch.hpp has StartLobbyMatch / EndLobbyMatch / LobbyStartSession) */
extern int g_lobbyMatchNo;
extern int g_lobbyMode;
void ClearFilledSlots();
void MaybeNativeStart();
/* netplay_gate.cpp */
void DesyncRemoteDeath(void* pl, int cause, int killer, bool brambled, bool laser);

}
