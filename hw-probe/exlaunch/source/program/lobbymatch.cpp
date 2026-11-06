/* The lobby match's start, end and session setup (StartLobbyMatch, MaybeNativeStart, LobbyStartSession, NpStart, EndLobbyMatch) (split out of xplay.cpp, code unchanged; shared state: xplay_internal.hpp) */
#include "xplay_internal.hpp"
#include "tfex/gen/tfexgen_InputService.hpp"   /* TF.EX's EnsureEveryControllerSlot / RemoveFakeControllers */
#include "tfex/gen/tfexgen_State_Api.hpp"   /* TF.State's TfStateApi.SetSeed (a scripted match's seed) */

namespace xplay {
static void NpStart();
static int g_vsJoinedFrames = 0;

static int g_lobbyTower = -1;   /* the lobby's tower (GameData.VersusTowers index, TF.EX's MapScenePatch; tfex::Tower::Select), -1 = keep */
static bool g_lobbyWide = false;   /* WiderSet wide lobby: the 6P towers, MatchSettings.SixPlayer, WiderSet's levels (pcgameplay.cpp) */
int g_lobbyMode = 3, g_lobbyTeams[kNpSeats] = { -1, -1, -1, -1, -1, -1, -1, -1 };   /* the lobby's GameData.Mode and players' teams (by handle) */
static bool g_clearSlotsPending = false;
static bool SceneIsLevel() {
    uintptr_t sc = g_game ? *reinterpret_cast<uintptr_t*>(reinterpret_cast<uintptr_t>(g_game) + 0xe8) : 0;
    char tn[32] = ""; if (sc) rnd::TypeName(reinterpret_cast<void*>(sc), tn, sizeof tn);
    return !std::strcmp(tn, "Level");
}
/* TF.EX's InputService.RemoveFakeControllers (its TFGame OnSceneTransition, leaving the session): the match's FakeControllers out of
 * TFGame.PlayerInputs and MenuInput.MenuInputs once no Level runs (every frame). S-start: also on the way back to the lobby (TF.EX keeps
 * them there; the Switch's roll call gives the seats their controllers itself), so the menus never meet a FakeController. */
void ClearFilledSlots() {
    if (!g_clearSlotsPending || SceneIsLevel()) return;
    g_clearSlotsPending = false;
    tfex::gen::InputService::RemoveFakeControllers(tfex::gen::svc::Service());
    tfex::gen::svc::ClearRemoteSeatInputs();   /* ...and the remote seats' controllers (EnsureRemoteControllers) */
}
int g_lobbyMatchNo = 0;   /* lobby matches started this boot (a rematch is the next one) */
static bool g_lobbyRandomMap = false;   /* the map screen rolls RANDOM to the lobby's tower (tfex/MapScenePatch.cpp) */
void StartLobbyMatch(const LobbyMatch& m) {
    g_lobbyMatchNo++;
    rnd::SetLobbySeed(m.seed);
    g_npSeat = m.handle;
    g_npOn = true;
    g_npPred = g_lobbyPred;
    if (g_lobbyPred <= 0) HWLOG("[vs] hw.txt lobbypred %d ignored: TF.EX's ggrs_ffi session always rolls back (prediction 15)", g_lobbyPred);
    g_npPad = g_lobbyScript ? 0 : 1;
    g_goal = m.goal;
    if (m.players > kNpSeats) HWLOG("[vs] ERROR: a lobby match of %d players, more than the %d seats it holds (xplay::kNpSeats, a pools error): only the first %d play", m.players, kNpSeats, kNpSeats);
    g_lobbyPlayers = m.players < 2 ? 2 : m.players > kNpSeats ? kNpSeats : m.players;
    g_lobbyTower = m.tower;
    g_lobbyWide = m.wide;
    g_lobbyRandomMap = m.randomMap;
    g_lobbyMode = (m.mode >= 3 && m.mode <= 5) ? m.mode : 3;
    for (int i = 0; i < kNpSeats; i++) g_lobbyTeams[i] = m.team[i];
    g_lobbyStart = true;
    g_vstart = 1;
    HWLOG("[vs] lobby match requested: %d players, seed %d handle %d goal %d prediction %d input %s", g_lobbyPlayers, m.seed, m.handle, m.goal, g_npPred, g_npPad ? "controller" : "script");
}
/* TF.EX's NetplayManager.Reset for xplay's native lobby match: the session and every per-match setting, so the next StartLobby starts afresh. */
/* TF.EX's test mode (its dev console's `test`; hw.txt tfextest, tfex/TestMode: TFCommands.LaunchTestMode -> StartTestMode, the
 * NetplayManager in NetplayMode.Test with ggrs_ffi's SyncTestSession, Session.StartGame): the netplay gate drives that session as TF.EX's
 * NetplayLogic does in test mode (no Poll; every frame saved and loaded by the sync test through the rollback seam), without a lobby. */
bool g_testSession = false;
/* Before TF.EX's StartTestMode builds the session and the level (call it before TFCommands.LaunchTestMode): what a lobby match has before
 * its StartSession. TF.State's capture flag on (StartLobbyMatch: the gameplay RNG stream; TF.State's LevelLoaderXML patch gives the
 * TreasureSpawner the gameplay Random only while it's on) and its context made (NpStart: statert::BeforeMatch, the gameplay Random,
 * ResetMatch / ResetSession). Without it the spawner kept a System.Random of its own, which no load restores: the sync test's chests
 * diverged at the round start (frame 265, 2026-10-22). */
void PrepareTestSession() {
    tfex::gen::statert::BeforeMatch();
    HWLOG("[test] before TF.EX's test launch: TF.State's context made (capture comes on with TF.EX's SetTestMode)");
}
void BeginTestSession(void* nm, int players) {
    g_lobbyMatchNo++;
    g_npSession = nm; g_npOn = true; g_testSession = true;
    g_npN = players < 1 ? 1 : players > kNpSeats ? kNpSeats : players; g_npSeat = 0; g_npPred = 8;
    g_lobbyStart = false; g_vsStarted = true; g_vsPending = true; g_rbReal = true; g_matchFinished = false; g_resultsFrame = -1;
    g_lvTick = 0; g_vsOrigin = -1;
    LoadVsScripts();   /* seat 0's input without hw.txt nppad 1: the vs script (sd:/xplay/vs0.txt), so the sync test sees movement, shots and
                        * rollbacks; on the console the tester plays (nppad 1: the controller) */
    HWLOG("[test] TF.EX's test-mode session: %d players at %d Hz (ggrs_ffi's sync test; NetplayManager %p)", g_npN, SessionHz(), nm);
}
void StartTestSession(void* nm, int players, int localSeat) { BeginTestSession(nm, players); g_npSeat = localSeat; }   /* (tfexgen_glue's call) */
void EndLobbyMatch() {
    tfex::gen::statert::LogSummary("match end");
    {   /* TF.EX's NetplayManager.Reset (its fields, netplay_reset), whose native part (the netplay_reset rule) is this teardown */
        static bool inReset = false;
        void* nm = g_npSession;
        if (nm && !inReset && !tfex::gen::ffi::InInit() && tfex::gen::NetplayManager::get__isInit(nm)) {
            inReset = true;
            tfex::gen::NetplayManager::Reset(nm);
            inReset = false;
            return;
        }
    }
    if (g_npSession) HWLOG("[np] lobby match #%d over at frame %d: %d first-time ticks, remote input changes %d, rollbacks %d", g_lobbyMatchNo,
                           NpFrame(), g_matchFirstTicks, g_remoteChanges, g_rbLoads - g_rbLoadsAtStart);
    devupdate::RequestUpload("match-hw-log.txt", "sd:/xplay/hw-log.txt");   /* the whole log of the match to the dev server (when there is one) */
    rumble::MatchEnd();   /* vibration stopped, the controller object back in the other seats' slots */
    g_clearSlotsPending = true;   /* emptied once the Level is left (ClearFilledSlots): a level that runs on (every remote gone)
                                                * still reads every joined seat's controller (Level.HandlePausing: a null one crashed the
                                                * guest 0.3 s after the teardown, run 235306 of 2026-10-17) */
    ClearFilledSlots();
    g_npSession = nullptr;   /* (ggrs_ffi's session: netplay_reset, after this, in the rule) */
    if (g_lobbyStart) pools::Report("lobby match end");   /* (once per match: EndLobbyMatch clears g_lobbyStart below) */
    g_syncWaitSince = 0;
    g_rbReal = false;
    tfex::MapScenePatch::Reset();   /* a match dropped on its map screen: the screen's patches off */
    g_npOn = false; g_vsOn = false; g_vsPending = false; g_vsStarted = false; g_vstart = 0; g_lobbyStart = false; g_testSession = false;
    rnd::SetCapture(g_captureForced);
    g_resultsFrame = -1; g_session = nullptr; g_matchFinished = false; g_remoteGone = false; g_interruptedSince = 0;
    g_lvTick = 0; g_vsOrigin = -1; g_npFrame = 0; g_npWaitSkip = 0;
    for (auto& p : g_seatPlayer) p = nullptr;
    *reinterpret_cast<uint8_t*>(Slot(0x4791c30)) = 0;   /* Coroutine.OneFrameDelay back to the game's own */
    HWLOG("[vs] lobby match reset (NetplayManager.Reset)");
}
/* TF.EX's MapScene.StartSession postfix: InputService.EnsureEveryControllerSlot, a FakeController in every empty seat (its GetState is the
 * seat's GGRS input; the local controller's is TF.EX's XGamepadInput.GetState postfix, tfex/InputLock.cpp). TF.EX's Level_HandlePausing
 * prefix repeats it every frame while the session is up. */
static void FillControllers() {
    if (g_lobbyStart) {
        tfex::gen::InputService::EnsureRemoteController(tfex::gen::svc::Service(), g_lobbyPlayers);   /* TF.EX's StartSession postfix */
        tfex::gen::InputService::EnsureEveryControllerSlot(tfex::gen::svc::Service());
        rumble::MatchStart(g_npSeat, g_lobbyPlayers);   /* only the local seat's controller rumbles (rumble.cpp) */
    }
}
/* The match settings and the session (TF.EX's StartSession prefix and postfix): mode and teams, the input hooks, the GGRS session.
 * Right before MapScene.StartSession (new Session(CurrentMatchSettings).StartGame()). */
static void NativeSessionSetup(uintptr_t ms) {
    /* MatchSettings.Mode (+0x30): a lobby match keeps TF.EX's Netplay mode (11, FortRise's custom mode: ApplyNetplayMode since NETPLAY), as
     * on the PC; the lobby's own mode (3 LastManStanding, 4 HeadHunters, 5 TeamDeathmatch) is TF.EX's NetplayVersusMode's, which FortRise's
     * TeamMode / round logic ask (docs/TEAM-MODES-RE.md section 7). A native test match (no lobby) plays Last Man Standing.
     * Team mode: MatchSettings.Teams (+0x38, MatchTeams: set_Item +0xabc870) from the lobby, TF.EX's ApplyTeamsToMatchSettings */
    int mode = g_lobbyStart ? g_lobbyMode : 3;
    if (!g_lobbyStart) *reinterpret_cast<int32_t*>(ms + 0x30) = mode;
    if (mode == 5) {
        void* teams = *reinterpret_cast<void**>(ms + 0x38);
        for (int i = 0; teams && i < 6; i++)
            reinterpret_cast<void (*)(void*, int32_t, int32_t)>(Base() + 0xabc870)(teams, i, i < kNpSeats ? g_lobbyTeams[i] : -1);
        HWLOG("[vs] team mode: teams %d %d %d %d (MatchTeams %p)", g_lobbyTeams[0], g_lobbyTeams[1], g_lobbyTeams[2], g_lobbyTeams[3], teams);
    }
    HWLOG("[vs] mode: MatchSettings.Mode %d, the lobby's %d", *reinterpret_cast<int32_t*>(ms + 0x30), mode);
    {   /* the match's variants (TF.EX's ApplyVariants from the lobby's GameData.Variants): MatchSettings.Variants (+0x40) -> Variants (+0x2f8),
         * the ones on (Variant.get_Value +0xb9c570; Title +0x30): one line per match, to compare with the page's "MatchSettings.Variants.on" */
        uintptr_t mv = *reinterpret_cast<uintptr_t*>(ms + 0x40);
        uintptr_t arr = mv ? *reinterpret_cast<uintptr_t*>(mv + 0x2f8) : 0;
        uintptr_t* d = arr ? *reinterpret_cast<uintptr_t**>(arr + 0x10) : nullptr;
        int n = arr ? **reinterpret_cast<int32_t**>(arr + 0x20) : 0;
        char on[400] = ""; size_t at = 0; int count = 0;
        for (int i = 0; d && i < n; i++) {
            if (!d[i] || !reinterpret_cast<bool (*)(uintptr_t)>(Base() + 0xb9c570)(d[i])) continue;
            uintptr_t t = *reinterpret_cast<uintptr_t*>(d[i] + 0x30);
            int len = t ? *reinterpret_cast<int32_t*>(t + 0x10) : 0;
            const char16_t* c = t ? reinterpret_cast<const char16_t*>(t + 0x14) : nullptr;
            if (count++ && at < sizeof on - 1) on[at++] = '|';
            for (int k = 0; k < len && at < sizeof on - 1; k++) on[at++] = (char)c[k];
            on[at] = 0;
        }
        HWLOG("[vs] match variants on (%d of %d): %s", count, n, count ? on : "none");
    }
    uintptr_t ls = *reinterpret_cast<uintptr_t*>(ms + 0x28);
    if (ls) { *reinterpret_cast<uint8_t*>(ls + 0x20) = 0; *reinterpret_cast<uint8_t*>(ls + 0x21) = 0; } /* ShowControls / ShowTriggerControls off */
    {   /* the input objects' MenuConfirm */
        uintptr_t f2 = *reinterpret_cast<uintptr_t*>(Base() + 0x47951b0);   /* MenuInput.MenuInputs (static field slot) */
        uintptr_t a2 = f2 ? *reinterpret_cast<uintptr_t*>(f2) : 0;
        uintptr_t* data = a2 ? *reinterpret_cast<uintptr_t**>(a2 + 0x10) : nullptr;
        uintptr_t bodies[4] = {};
        int nb = 0;
        for (int i = 0; data && i < 4; i++) {
            if (!data[i]) continue;
            uintptr_t body = rnd::VtableBody(reinterpret_cast<void*>(data[i]), 0x38);
            char tn3[48] = "";
            rnd::TypeName(reinterpret_cast<void*>(data[i]), tn3, sizeof tn3);
            HWLOG("[vs] MenuInputs[%d] is %s, MenuConfirm body +%#lx", i, tn3, (unsigned long)(body - Base()));
            bool dup = false;
            for (int k = 0; k < nb; k++) dup |= bodies[k] == body;
            if (!dup && body > Base() && body < Base() + 0x2863000) {
                bodies[nb++] = body;
            }
        }
        if (nb > 1) HWLOG("[vs] WARNING: %d distinct MenuConfirm bodies, only the first JoyConInput's is hooked (tfex/InputLock.cpp)", nb);
    }
    if (!g_lobbyStart && g_hwSeedSet) {   /* a scripted native match (no lobby): its seed through TF.State's StateApi.SetSeed, where a lobby
                                           * match gets it from TF.EX's own SetSeed (MainMenu's create / join); TF.State reseeds from it at StartGame */
        if (!g_npOn && rnd::Capture()) tfex::gen::statert::BeforeMatch();   /* TF.State's gameplay Random and context, as NpStart makes them for a
                                                                         * netplay match (TreasureSpawner's GetRandomArrowType prefix draws from it) */
        tfex::gen::State_Api::SetSeed(nullptr, g_hwSeed);
        HWLOG("[vs] scripted native match: seed %d (hw.txt rngseed) through TF.State's SetSeed", (int)g_hwSeed);
    }
    LoadVsScripts();
    g_lvTick = 0; g_vsOrigin = -1;
    *reinterpret_cast<uint8_t*>(Slot(0x4791c30)) = 1;   /* Coroutine.OneFrameDelay (read in Coroutine.Update +0x920d10): TF.EX sets it every update of a session */
    pcgameplay::Set(true);   /* pcgameplay.cpp: the PC gameplay TF.EX plays (version gaps, hot coals, PC levels), until online mode is left */
    if (g_npOn) NpStart();
    g_vsPending = true;   /* g_vsOn from the first Level.Update on: forcing MenuConfirm earlier would make the rollcall's idle seats join */
    LogPlayersArray("before StartSession");
    HWLOG("[xplay] native versus start: mode %d, MapScene.StartSession", mode);
}
void MaybeNativeStart() {
    if (!g_vstart || g_vsStarted) return;
    auto arr = [](uintptr_t slot) -> uint8_t* {
        uintptr_t f = *reinterpret_cast<uintptr_t*>(Base() + slot);
        uintptr_t a = f ? *reinterpret_cast<uintptr_t*>(f) : 0;
        return a ? *reinterpret_cast<uint8_t**>(a + 0x10) : nullptr;
    };
    auto ArrLen = [](uintptr_t slot) -> int {
        uintptr_t f = *reinterpret_cast<uintptr_t*>(Base() + slot);
        uintptr_t a = f ? *reinterpret_cast<uintptr_t*>(f) : 0;
        return a ? **reinterpret_cast<int32_t**>(a + 0x20) : 0;
    };
    uint8_t* pl = arr(0x4793890);
    uintptr_t f = *reinterpret_cast<uintptr_t*>(Base() + 0x4793818);
    uintptr_t ms = f ? *reinterpret_cast<uintptr_t*>(f) : 0;
    int32_t* ch = reinterpret_cast<int32_t*>(arr(0x4792458));   /* TFGame.Characters */
    {   /* the console: what the menu shows (the scripted-input logger needs the Pro Controller hook, which Joy-Con don't use) */
        static uint32_t lastMask = 0xffffffffu;
        static int every = 0;
        uint32_t mask = pl ? (pl[0] | pl[1] << 1 | pl[2] << 2 | pl[3] << 3 | pl[4] << 4 | pl[5] << 5) : 0;   /* Players is bool[6] */
        if (pl && (mask != lastMask || ++every % 600 == 0)) {
            lastMask = mask;
            HWLOG("[vs] menu: players %d%d%d%d%d%d archers %d %d %d %d, match settings %s", pl[0], pl[1], pl[2], pl[3], pl[4], pl[5],
                  ch ? ch[0] : -1, ch ? ch[1] : -1, ch ? ch[2] : -1, ch ? ch[3] : -1, ms ? "set" : "none");
        }
    }
    if (g_lobbyStart) {   /* TF.EX's NetplayButton: CurrentMatchSettings = VersusMatchSettings; the lobby's players, handles 0..n-1 */
        if (!ms) {
            uintptr_t vf = *reinterpret_cast<uintptr_t*>(Base() + 0x4793398);
            uintptr_t vms = vf ? *reinterpret_cast<uintptr_t*>(vf) : 0;
            if (!vms || !f) { HWLOG("[vs] lobby start: no versus match settings yet"); return; }
            *reinterpret_cast<uintptr_t*>(f) = vms; ms = vms;
        }
        if (!pl || !ch) return;
        for (int i = 0; i < ArrLen(0x4793890); i++) pl[i] = i < g_lobbyPlayers;   /* TFGame.Players: 8 (megamod/eightplayer.hpp widens it) */
        /* (the archers: TF.EX's ArcherService.ApplyToGame in MapScenePatch.StartSession_Prefix) */
        if (g_lobbyTower >= 0) {   /* MapScene.StartSession: MatchSettings.LevelSystem = the selected button's Data.GetLevelSystem() */
            void* lsys = LobbyLevelSystem(g_lobbyTower, g_lobbyWide);
            if (lsys) *reinterpret_cast<void**>(ms + 0x28) = lsys;
            else HWLOG("[vs] lobby start: no level system for tower %d", g_lobbyTower);
        }
        pcgameplay::SetWideMatch(reinterpret_cast<void*>(ms), g_lobbyWide);   /* after the level system (its tower), before Session.StartGame */
        HWLOG("[vs] lobby start: players %d%d%d%d00, archers %d %d %d %d, goal %d, tower %d%s", pl[0], pl[1], pl[2], pl[3], ch[0], ch[1], ch[2], ch[3], g_goal, g_lobbyTower, g_lobbyWide ? " (wide)" : "");
    } else {
        int joined = 0;
        for (int i = 0; pl && i < 6; i++) joined += pl[i] != 0;
        if (!pl || !ms || joined != 2) { g_vsJoinedFrames = 0; return; }   /* any two players (Joy-Con can land in other slots) */
        if (++g_vsJoinedFrames < 90) return;
        if (!(pl[0] && pl[1])) { HWLOG("[vs] two players joined in slots %d%d%d%d%d%d: using slots 1 and 2", pl[0], pl[1], pl[2], pl[3], pl[4], pl[5]); pl[0] = pl[1] = 1; for (int i = 2; i < 6; i++) pl[i] = 0; }
        if (ch && (ch[0] != 0 || ch[1] != 1)) { HWLOG("[vs] archers %d %d -> 0 1 (the browser's: archer = seat)", ch[0], ch[1]); ch[0] = 0; ch[1] = 1; }
    }
    g_vsStarted = true;
    if (g_lobbyStart) {   /* TF.EX: the lobby screen fades to the map screen; the rest of the start is in its StartSession (LobbyStartSession) */
        pcgameplay::Set(true);   /* (online mode has it on already; a test host has no NETPLAY mode) */
        tfex::MapScenePatch::Enter(g_lobbyTower, g_lobbyWide, g_lobbyRandomMap);
        return;
    }
    FillControllers();
    NativeSessionSetup(ms);
    using Fn = void (*)(void*);
    reinterpret_cast<Fn>(Base() + 0xab54e0)(nullptr);
    HWLOG("[xplay] native versus start: StartSession returned");
    LogPlayersArray("after StartSession");
}

/* tfex/MapScenePatch.cpp: the lobby map screen's MapScene.StartSession, before the game's (lobbymatch.hpp). The map screen set
 * VersusMatchSettings.LevelSystem from the lobby tower's button (the one tfex::Tower::Select picked); the level system is set again from
 * that tower, as the native start did, so the level played doesn't depend on the screen. */
void LobbyStartSession() {
    uintptr_t f = *reinterpret_cast<uintptr_t*>(Base() + 0x4793818);   /* MainMenu.CurrentMatchSettings (the MapScene made it VersusMatchSettings) */
    uintptr_t ms = f ? *reinterpret_cast<uintptr_t*>(f) : 0;
    if (!ms || !g_lobbyStart) { HWLOG("[vs] lobby StartSession: no match settings (%d)", (int)g_lobbyStart); return; }
    if (g_lobbyTower >= 0 && !tfex::MapScenePatch::StartedWithGen()) {   /* a wide lobby / the test host: the hand pick. A standard netplay lobby:
        * TF.EX's map screen (Begin_Postfix, GetRandomVersusTower) selected the button, and vanilla StartSession takes its level system,
        * as on TF.EX (2026-10-18: the hand pick no longer overrides it) */
        void* lsys = LobbyLevelSystem(g_lobbyTower, g_lobbyWide);
        if (lsys) *reinterpret_cast<void**>(ms + 0x28) = lsys;
        else HWLOG("[vs] lobby StartSession: no level system for tower %d", g_lobbyTower);
    }
    FillControllers();
    NativeSessionSetup(ms);
}

static void NpStart() {
    g_remoteChanges = 0; g_rbLoadsAtStart = g_rbLoads; g_matchFirstTicks = 0; g_remotesGone = 0; std::memset(g_lastRemote, 0, sizeof g_lastRemote);
    g_npN = g_lobbyStart ? g_lobbyPlayers : 2;
    g_rbReal = true;   /* TF.EX's ggrs_ffi always predicts (no lockstep): a frame's tick-log line waits until it's final (LogCommit). The GC runs as
                        * on the PC: TF.State's LoadState is the rollback, nothing is restored in memory */
    /* the session: TF.EX's generated NetplayManager over ggrs_ffi (Init at the first round's load by TF.EX's NetplayRoundLogic; the input delay
     * and Fps 60 in its config JSON, glue Ffi.ConfigJson) */
    tfex::gen::statert::BeforeMatch();   /* TF.State's classes before the window, its context reset (tfexgen_state.hpp) */
    g_npSession = tfex::gen::svc::GenNM();
    HWLOG("[np] session: %s, %d players, seat %d local", g_npSession ? "TF.EX's NetplayManager over ggrs_ffi" : "FAILED (ERROR: TF.EX's NetplayManager doesn't exist)", g_npN, g_npSeat);
}
} // namespace xplay
