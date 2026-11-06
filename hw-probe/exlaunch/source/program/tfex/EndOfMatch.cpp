/* The end of a lobby match and what follows it (TF.EX's HUDPatch match end, PauseMenuPatch's vote menu, MatchmakingService's
 * RematchLobby / ArcherSelectLobby handling, QuitToNetplayEntry): moved here from MainMenuPatch.cpp (2026-10-17, no behaviour change).
 * Owned by Integration B ("match & after"). */
#include "EndOfMatch.hpp"
#include "MainMenuPatch.hpp"
#include "NetplayScreens.hpp"
#include "Game.hpp"
#include "MatchmakingService.hpp"
#include "Notification.hpp"
#include "InputLock.hpp"
#include "gen/tfexgen_InputService.hpp"
#include "gen/tfexgen_GameContext.hpp"
#include "gen/tfexgen_Auto_GGRSConfig.hpp"
#include "SwitchHud.hpp"
#include "gen/tfexgen_PauseMenuPatch.hpp"
#include "gen/tfexgen_hooks.hpp"
#include "gen/tfexgen_Auto_MatchEndChoices.hpp"
#include "runtime/tfexgen_services.hpp"
#include "gen/tfexgen_Auto_Lobby.hpp"
#include "gen/tfexgen_LightPauseMenu.hpp"
#include "gen/tfexgen_NetplayRoundLogic.hpp"
#include "gen/tfexgen_NetplayVersusMode.hpp"
#include "gen/tfexgen_NetplayManager.hpp"
#include "gen/tfexgen_GameContext.hpp"
#include "gen/tfexgen_MatchReport.hpp"
#include "gen/tfexgen_MatchmakingService.hpp"
#include "gen/tfexgen_Auto_LevelExtensions.hpp"
#include "gen/tfexgen_Notification.hpp"
#include "gen/tfexgen_ResultsVoteLock.hpp"
#include "gen/tfexgen_Auto_TowerFallSceneExtensions.hpp"
#include "gen/tfexgen_binding.hpp"
#include "runtime/tfexgen_code_rt.hpp"
#include "../netws.hpp"
#include "../lobbymatch.hpp"
#include "lib.hpp"
#include <cstdio>
#include <cstring>
#include <cstdarg>
namespace xplay { extern int g_npDelay, g_lobbyMatchNo; int SessionHz(); }   /* the session's input delay (session ticks), the match's number and rate (xplay.cpp) */

namespace tfex {
void NetplayManagerStartLobbyMatch();
using namespace models;
using namespace game;

static void Log(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
static void Log(const char* fmt, ...) {
    char b[300];
    va_list a; va_start(a, fmt); vsnprintf(b, sizeof b, fmt, a); va_end(a);
    netws::Log("%s", b);
}

namespace MainMenuPatch {

static bool g_matchStarted = false;     /* NetplayManager started the lobby's match (StartLobby) */
static bool g_awaitingVote = false;     /* the match ended; waiting for the server's ArcherSelectLobby / RematchLobby */
static bool g_endgame = false;          /* the end-of-match menu belongs to the lobby (TF.EX PauseMenuPatch.IsNetplayEndgame) */
static bool g_voted = false;            /* this console voted (TF.EX votedMenu: one vote, then the menu is locked) */
static int g_autoVote = 0, g_endgameFrames = 0;   /* hw.txt "lobbyvote 1|2|3": the test console votes REMATCH! / ARCHER SELECT, or QUITs, 3 s after the results */
void SetAutoVote(int v) { g_autoVote = v; }
bool InEndgame() { return g_endgame; }
static int g_matches = 0;


} // namespace MainMenuPatch

/* xplay: the lobby match has ended (results shown and the remote closed the relay). TF.EX's HUD.Update postfix: NotifyMatchEnded, then
 * the vote; the Switch v1 always votes ARCHER SELECT (a rematch needs a new session mid-level). */
/* TF.EX's HUD.Update postfix once VersusMatchResults is finished (xplay has just done NetplayManager.Reset): NotifyMatchEnded, then the
 * vote happens in the vanilla VersusMatchEnd pause menu (Confirm on the results, as offline): PauseMenuPatch below. */
/* TF.EX's HUDPatch.HUD_Update (generated, genpatch HUDPatch on by default in xplay.cpp) sends NotifyMatchEnded and makes MatchEndChoices
 * (the vote panel) on the next HUD.Update of the finished VersusMatchResults, once the session is reset (IsInit false). It keys on the
 * netplay mode (always set for a lobby match since the scripted test host was retired, 2026-10-18). */
void OnLobbyResultsFinished(int winnerSeat, const int* scores, int nScores) {
    auto& mm = MatchmakingService::Instance();
    const bool fromHud = IsNetplay();
    if (!fromHud) Log("[lobby] WARNING: the lobby match ended outside TF.EX's netplay mode: no MatchEnded sent");
    (void)mm; (void)scores;
    MainMenuPatch::g_awaitingVote = true;
    MainMenuPatch::g_endgame = true;
    MainMenuPatch::g_voted = false;
    MainMenuPatch::g_matches++;
    Log("[lobby] match results finished: winner seat %d, %d scores; MatchEnded %s, the vote is in the end-of-match menu", winnerSeat, nScores, fromHud ? "from TF.EX's HUDPatch" : "sent");
}


namespace MainMenuPatch {

/* ---- The vanilla VersusMatchEnd menu (REMATCH! / ARCHER SELECT / QUIT) is TF.EX's PauseMenuPatch, generated
 * (tfex/gen/tfexgen_PauseMenuPatch.cpp): Update (trims the menu for an empty lobby / a spectator), Quit and VersusMatchSettingsAndSave
 * (QuitToNetplayEntry), VersusArcherSelect / VersusRematch (the vote, votedMenu, "Waiting for other players..."), AddItem (no MATCH
 * SETTINGS; CONTINUE in a series). Its hooks are the generated ones (tfexgen_hooks, on with every TF.EX patch); the Switch wraps two of
 * them (gen::hooks::<method>::Wrap) for the steps of its own around TF.EX's:
 *  - PauseMenu's constructor inlines its AddItem calls (tools/tfex/inlined.py; the only BL to AddItem +0xae2550 is a wrapper at
 *    +0x27c1e4c), so TF.EX's AddItem prefix never sees the menu's own items: on the menu's first Update, TF.EX's prefix (generated)
 *    runs over each item the constructor added, which it drops or renames as AddItem would have (no MATCH SETTINGS in netplay; a series
 *    lobby's CONTINUE, no ARCHER SELECT, no QUIT when ranked; an empty lobby's menu);
 *  - TF.EX's QUIT has no Switch teardown: QuitTeardownFrame below. ---- */
static void* g_votedMenu = nullptr;     /* (the generated unit keeps TF.EX's votedMenu; this one only marks the test's vote) */
static void* g_trimmedMenu = nullptr;   /* the last menu TF.EX's AddItem rule ran over */
static void* g_lastMenu = nullptr; static int g_menuOpenedAt = 0;   /* the test vote: g_endgameFrames when the end-of-match menu first updated */
static gen::LitN<3> kSelPrefix{u"> "};
/* TF.EX's AddItem prefix over the items PauseMenu's constructor added with AddItem inlined: each one dropped (the prefix returns false)
 * or renamed (its ref name), in optionNames, selectedOptionNames ("> " + name, as the game's AddItem) and optionActions */
static void AddItemRule(void* menu) {
    if (menu == g_trimmedMenu) return;
    g_trimmedMenu = menu;
    void* names = At<void*>(menu, gen::bind::f_PauseMenu_optionNames);
    void* sel = At<void*>(menu, gen::bind::f_PauseMenu_selectedOptionNames);
    void* actions = At<void*>(menu, gen::bind::f_PauseMenu_optionActions);
    if (!names || !actions) return;
    auto count = gen::Fn<int32_t (*)(void*)>(gen::bind::m_List_1_eb65_get_Count);
    auto item = gen::Fn<void* (*)(void*, int32_t)>(gen::bind::m_List_1_eb65_get_Item_e2bf);
    auto removeAt = gen::Fn<void (*)(void*, int32_t)>(gen::bind::m_List_1_eb65_RemoveAt_e2bf);
    auto setItem = gen::Fn<void (*)(void*, int32_t, void*)>(gen::bind::m_List_1_eb65_set_Item_b707);
    char out[160] = ""; int w = 0;
    for (int i = 0; i < count(names); i++) {
        void* was = item(names, i);
        void* name = was;
        bool keep = gen::PauseMenuPatch::PauseMenu_AddItem(menu, name);
        char nb[32]; gen::Utf8Of(was, nb, sizeof nb);
        if (!keep) {
            removeAt(names, i);
            if (sel && count(sel) > i) removeAt(sel, i);
            if (gen::Fn<int32_t (*)(void*)>(gen::bind::m_List_1_b3d8_get_Count)(actions) > i) gen::Fn<void (*)(void*, int32_t, int32_t)>(gen::bind::m_List_1_b3d8_RemoveRange_c11b)(actions, i, 1);
            w += std::snprintf(out + w, sizeof out - w > 0 ? sizeof out - w : 0, " -%s", nb);
            i--;
            continue;
        }
        if (name != was && !gen::StrEq(name, was)) {
            setItem(names, i, name);
            if (sel && count(sel) > i) setItem(sel, i, gen::Concat(kSelPrefix.P(), name));
            char rb[32]; gen::Utf8Of(name, rb, sizeof rb);
            w += std::snprintf(out + w, sizeof out - w > 0 ? sizeof out - w : 0, " %s->%s", nb, rb);
        }
    }
    if (out[0]) Log("[lobby] pause menu %p: TF.EX's AddItem over the constructor's inlined items:%s", menu, out);
}
/* PauseMenu.Update: TF.EX's AddItem rule over its items (above), the test's menu bookkeeping, then TF.EX's patches (its postfix) and the original */
static void PauseUpdateShell(gen::hooks::PauseMenu_Update& c) {
    AddItemRule(c.self);
    {   /* a pause menu over a level: whether the level says it is paused (TF.EX's XGamePadInputPatch passes Back through only then) */
        static int logged = 0;
        void* lv = game::CurrentScene();
        if (logged < 4 && !g_endgame && game::SceneIs("Level")) {
            logged++;
            auto mi = [](uintptr_t f) { return (int)gen::Fn<bool (*)()>(f)(); };
            Log("[pause] PauseMenu.Update %p: Level.Paused %d; MenuInput Back %d Start %d Confirm %d Down %d", c.self,
                (int)gen::Fn<bool (*)(void*)>(gen::bind::m_Level_get_Paused)(lv), mi(gen::bind::m_MenuInput_get_Back), mi(gen::bind::m_MenuInput_get_Start),
                mi(gen::bind::m_MenuInput_get_Confirm), mi(gen::bind::m_MenuInput_get_Down));
        }
    }
    if (c.self != g_lastMenu) { g_lastMenu = c.self; if (g_endgame) g_menuOpenedAt = g_endgameFrames > 0 ? g_endgameFrames : 1; }
    c();
}
/* the votes (TF.EX's prefixes vote, lock the menu and disable the controllers): the test's vote mark and a log line */
static void VoteMark(void* menu, const char* what) {
    if (g_votedMenu == menu || !g_endgame) return;
    g_votedMenu = menu; g_voted = true;
    Log("[lobby] voted %s (TF.EX's PauseMenu_Versus%s)", what, what[0] == 'R' ? "Rematch" : "ArcherSelect");
}
static void PauseArcherSelectShell(gen::hooks::PauseMenu_VersusArcherSelect& c) { c(); if (!c.ran_) VoteMark(c.self, "ARCHER SELECT"); }
static void PauseRematchShell(gen::hooks::PauseMenu_VersusRematch& c) { c(); if (!c.ran_) VoteMark(c.self, "REMATCH!"); }
static void PauseQuitShell(gen::hooks::PauseMenu_Quit& c) { c(); if (!c.ran_) Log("[lobby] QUIT (TF.EX's QuitToNetplayEntry)"); }
static void PauseSettingsAndSaveShell(gen::hooks::PauseMenu_VersusMatchSettingsAndSave& c) { c(); if (!c.ran_) Log("[lobby] QUIT from a netplay level (TF.EX's QuitToNetplayEntry)"); }
/* PlayerInput.AssignInputs in a lobby: TF.EX's PlayerInputPatch prefix (generated) decides, as on the PC (its HUDPatch calls it, and
 * MenuInput.UpdateInputs, when the match results finish). The first calls in a netplay lobby are logged (the generated hook's own log
 * stops at 8 calls, all at boot). A skip of both at the results (S-input) was removed 2026-10-28: with caller logging it never fired
 * (HUDPatch calls them before MatchEnded, which set its flag), and rematch's match-2 desync comes and goes with or without it
 * (STATUS.md: ShiftBlocks[0].Counter off by 3 quarter-ticks at frame 0). */
static void AssignInputsShell(gen::hooks::PlayerInput_AssignInputs& c) {
    c();
    if (!IsNetplay()) return;
    static int n = 0;
    if (n++ < 6) Log("[lobby] PlayerInput.AssignInputs in a lobby (after %d matches, in %s): %s", g_matches, SceneIs("Level") ? "the Level" : "a menu",
                     c.ran_ ? "the game's ran (TF.EX's prefix let it)" : "TF.EX's prefix skipped it");
}
/* xplay: the lobby match didn't synchronize within TF.EX's CONNECTION_TIMEOUT_MS (NetplayManager.AbortIfSynchronizationFailed ->
 * AbortToVersusOptions). TF.EX's TFGame.Update then (TFGame.cs:223-241): ui_invalid, DisconnectFromServer, ResetPeer, an empty own lobby,
 * Level.GoToNetplayEntryMenu, the input service back, Notification(menu, GetFailedConnectionMessage, 10, 450). Sounds.ui_invalid isn't
 * bound (silent); the input service has nothing to restore on the Switch. */


/* hw.txt "lobbyvote 1|2|3": the test console votes through the end-of-match menu as a player does, with controller 1's buttons
 * (xplay's controller hook, TestButtons): 3 s after the results Confirm (the vanilla VersusMatchResults opens the VersusMatchEnd
 * PauseMenu), then Down to the item (REMATCH! 0, ARCHER SELECT 1, QUIT 2: MATCH SETTINGS isn't added in a lobby), then Confirm. So the
 * menu's own hooks (hand or TF.EX's generated PauseMenuPatch) take the vote. */
static unsigned long g_testBits = 0;
static int g_testHold = 0;
constexpr unsigned long kConfirm = 1u << 0, kDown = 1u << 15;
static void TestPress(unsigned long bits) { g_testBits = bits; g_testHold = 8; }
static int g_mecRenders = 0;   /* the vote panel's Render calls (logged with the test vote) */
/* TF.EX's HUD entities in the Level draw through SwitchHud::Shift (TFEX-DIFF S-hud: the Switch's HUD layer is the 420-wide canvas) */
static void MatchEndChoicesRender(void* self) { g_mecRenders++; SwitchHud::Shift hud; gen::Auto_MatchEndChoices::Render(self); }
static void LightPauseMenuRender(void* self) { SwitchHud::Shift hud; gen::LightPauseMenu::Render(self); }
/* what TF.EX's InterceptConfirm asks before it lets Confirm open the end-of-match menu: the level's Session.GetWinner() != -1 and the
 * VersusMatchResults finished, and no ResultsVoteLock (with GoalScore and the scores behind GetWinner) */
static void LogConfirmGate(int press) {
    static int lastFrames = 0; static int64_t lastMs = 0;
    const int64_t nowMs = (int64_t)(svcGetSystemTick() / 19200);   /* (19.2 MHz) */
    void* level = CurrentScene();
    if (!level || !SceneIs("Level")) { Log("[lobby] test: Confirm %d outside the level", press); return; }
    void* session = gen::Fn<void* (*)(void*)>(gen::bind::m_Level_get_Session)(level);
    void* ms = session ? gen::Fld<void*>(session, gen::bind::f_Session_MatchSettings) : nullptr;
    void* scores = session ? gen::Fld<void*>(session, gen::bind::f_Session_Scores) : nullptr;
    void* results = tfex::gen::Auto_TowerFallSceneExtensions::Get(level, gen::TypeOf(gen::bind::t_VersusMatchResults));
    const int32_t* sc = scores ? *reinterpret_cast<int32_t* const*>(reinterpret_cast<uintptr_t>(scores) + 0x10) : nullptr;   /* (the game's int[]: data at +0x10, netplay_gate.cpp SeatScores) */
    Log("[lobby] test: Confirm %d: GetWinner %d, GoalScore %d (Mode %d, MatchLength %d), scores %d %d, results %s finished %d, vote lock %d; TimeMult %.3f DeltaTicks %ld, %d frames in %ld ms", press,
        session ? gen::Fn<int32_t (*)(void*)>(gen::bind::m_Session_GetWinner)(session) : -9,
        ms ? gen::Fn<int32_t (*)(void*)>(gen::bind::m_MatchSettings_get_GoalScore)(ms) : -9,
        ms ? gen::Fld<int32_t>(ms, gen::bind::f_MatchSettings_Mode) : -9, ms ? gen::Fld<int32_t>(ms, gen::bind::f_MatchSettings_MatchLength) : -9,
        sc ? sc[0] : -9, sc ? sc[1] : -9, results ? "yes" : "no", results ? (int)gen::Fld<uint8_t>(results, gen::bind::f_VersusMatchResults_finished) : -1,
        (int)tfex::gen::ResultsVoteLock::IsActive(), (double)gen::Fn<float (*)()>(gen::bind::m_Engine_get_TimeMult)(),
        (long)gen::Fn<int64_t (*)()>(gen::bind::m_Engine_get_DeltaTicks)(), g_endgameFrames - lastFrames, (long)(nowMs - lastMs));
    lastFrames = g_endgameFrames; lastMs = nowMs;
}
static void TestVoteFrame() {
    if (g_testHold > 0) g_testHold--;   /* held for 8 frames (counted per frame: the game polls the controller several times a frame) */
    if (!g_endgame || !g_autoVote) { g_endgameFrames = 0; g_menuOpenedAt = 0; return; }
    int f = ++g_endgameFrames;
    const int downs = g_autoVote == 1 ? 0 : g_autoVote == 2 ? 1 : 2;
    static const char* const kNames[] = { "REMATCH!", "ARCHER SELECT", "QUIT" };
    if (f == 170) {   /* diagnostic: TF.EX's vote panel (HUDPatch made it when the results finished) */
        void* type = gen::Auto_MatchEndChoices::RuntimeType();
        int found = -1, count = -1;
        void* level = CurrentScene();
        if (type && level && SceneIs("Level")) {
            void* layers = gen::Fn<void* (*)(void*)>(gen::bind::m_Scene_get_Layers)(level);
            void* layer = layers ? gen::Fn<void* (*)(void*, int32_t)>(gen::bind::m_Dictionary_2_d0c1_get_Item_109a)(layers, 4) : nullptr;
            void* ents = layer ? gen::Fn<void* (*)(void*)>(gen::bind::m_Layer_get_Entities)(layer) : nullptr;
            count = ents ? gen::Fn<int32_t (*)(void*)>(gen::bind::m_List_1_a639_get_Count)(ents) : -1;
            found = 0;
            void* inst = nullptr;
            for (int i = 0; i < count; i++) { void* e = gen::Fn<void* (*)(void*, int32_t)>(gen::bind::m_List_1_a639_get_Item_e2bf)(ents, i); if (gen::As(e, type)) { found++; inst = e; } }
            if (inst) {   /* the instance as the layer will render it: Visible, its Scene, and the Render its vtable dispatches to */
                uintptr_t desc = *reinterpret_cast<uintptr_t*>(inst);
                uintptr_t slot = (*reinterpret_cast<uintptr_t**>(desc + 8))[gen::bind::v_Entity_Render / 8] & 0x7fffffffffffffffull;
                Log("[lobby] test: MatchEndChoices %p: Visible %d Active %d, Scene %p (level %p), Render slot %s", inst,
                    At<uint8_t>(inst, Entity::Visible), At<uint8_t>(inst, Entity::Active), At<void*>(inst, Entity::Scene), level,
                    slot == reinterpret_cast<uintptr_t>(&MatchEndChoicesRender) ? "= the counting wrapper" : "NOT the wrapper");
                auto* st = gen::MatchmakingService::GetEndGameStatus(gen::svc::GenMM());
                Log("[lobby] test: MatchEndChoices rendered %d times so far; GetEndGameStatus %d entries (the panel draws nothing when 0)", g_mecRenders, st ? st->Count() : -1);
            }
        }
        Log("[lobby] test: MatchEndChoices class %s, %d of %d HUD-layer entities", type ? "made" : "NOT made", found, count);
    }
    if (f == 180) {
        if (g_autoVote == 3) Log("[lobby] test: QUIT (hw.txt lobbyvote 3): Confirm, Down x2, Confirm in the end-of-match menu");
        else Log("[lobby] test: voting %s (hw.txt lobbyvote): Confirm, Down x%d, Confirm in the end-of-match menu", kNames[downs], downs);
        TestPress(kConfirm);   /* the results: open the VersusMatchEnd menu */
        LogConfirmGate(1);
    }
    /* VersusMatchResults: Confirm while not focused only tweens the results in (the first one after the match), the next one opens the
     * menu: Confirm once a second until the menu updates, then the items */
    if (g_menuOpenedAt <= 0) {
        if (f > 180 && (f - 180) % 60 == 0 && f < 180 + 600) { TestPress(kConfirm); if (f <= 180 + 120) LogConfirmGate((f - 180) / 60 + 1); }
        if (f == 180 + 600) Log("[lobby] test: the end-of-match menu didn't open after 10 Confirms");
        return;
    }
    int m = f - g_menuOpenedAt;
    for (int k = 0; k < downs; k++) if (m == 30 + 30 * k) TestPress(kDown);
    if (m == 30 + 30 * downs) TestPress(kConfirm);
    if (m == 30 + 30 * downs + 600 && g_endgame) Log("[lobby] test: still in the end-of-match menu 10 s after the vote (voted %d)", (int)g_voted);
}
/* TF.EX's QUIT (PauseMenuPatch.QuitToNetplayEntry: LeaveLobby, Level.GoToNetplayEntryMenu = new MainMenu(NetplaySelect)) and its
 * ARCHER SELECT (the generated MatchmakingService's ArcherSelectLobby handler: ReturnToMenu(Rollcall) = new MainMenu(Rollcall)) have no
 * Switch teardown: once the lobby's match Level gave way to a MainMenu that none of our own paths made (they clear these flags first),
 * finish it here (TFEX-DIFF S-start: xplay owns the match's native state): xplay's EndLobbyMatch (already done at the results; again
 * after a mid-match QUIT), TFGame.Players empty, versus roll call. */
static bool g_levelSeen = false;
static void QuitTeardownFrame(void* menu) {
    if (g_matchStarted && SceneIs("Level")) g_levelSeen = true;
    if (!g_matchStarted && !g_endgame) g_levelSeen = false;
    if (!menu || !(g_endgame || (g_matchStarted && g_levelSeen))) return;
    /* TF.EX's MatchmakingService.ReturnToMenu: Rollcall (ArcherSelectLobby) or SeriesLobby (61, between series games): the players stay */
    constexpr int32_t kSeriesLobby = 61;
    const int32_t state = At<int32_t>(menu, MainMenu::state);
    const bool archerSelect = state == MainMenu::Rollcall, toLobby = archerSelect || state == kSeriesLobby;
    Log("[lobby] the match's Level was left for the menu (TF.EX's %s): the Switch's teardown",
        archerSelect ? "ArcherSelectLobby: ReturnToMenu(Rollcall)" : toLobby ? "SeriesLobby: ReturnToMenu(SeriesLobby)" : "QUIT / GoToNetplayEntryMenu");
    g_endgame = false; g_awaitingVote = false; g_matchStarted = false; g_levelSeen = false; g_votedMenu = nullptr;
    gen::InputService::EnableAllControllers(gen::svc::Service());   /* (TF.EX's QuitToNetplayEntry / ReturnToMenu: EnableAllControllers) */
    xplay::EndLobbyMatch();
    uint8_t* players = TFGame::Players_();   /* (ARCHER SELECT keeps them, as TF.EX: its roll call was made with the match's players joined) */
    for (int i = 0; players && !toLobby && i < 6; i++) players[i] = 0;
    /* the versus roll call in the lobby's layout: wide stays wide (WiderSet's IsWide holds until the main menu, as on the PC); was always
     * Versus, so ARCHER SELECT after a wide match got the side art back (2026-10-29 console) */
    Static<int32_t>(MainMenu::RollcallMode) = MainMenuPatch::IsWide() ? MainMenu::SixPlayer : MainMenu::Versus;
    Static<void*>(MainMenu::CurrentMatchSettings) = Static<void*>(MainMenu::VersusMatchSettings);
    Log("[lobby] back to the lobby screen (MainMenu %d): the %s layout", At<int32_t>(menu, MainMenu::state), MainMenuPatch::IsWide() ? "wide" : "standard");
}
/* ---- TF.EX's generated NetplayManager: the match start (EstablishSessionIfSynchronized, the sync abort) and disconnect handling ---- */
static void** g_nmRoot = nullptr;   /* the generated NetplayManager, in uncollectable (GC-scanned) memory; made on a menu */
static void MakeGenNetplayManager() {
    static bool tried = false;
    if (tried) return;
    tried = true;
    if (!gen::NetplayManager::EnsureClass()) { Log("[lobby] ERROR: TF.EX's NetplayManager class FAILED: lobby matches have no TF.EX start / disconnect handling"); return; }
    g_nmRoot = static_cast<void**>(game::Fn<void* (*)(uint64_t)>(game::brute::MallocUncollectable)(sizeof(void*)));
    if (!g_nmRoot) return;
    void* s = gen::svc::Service();
    /* TF.EX's GameContext first (ServiceCollections' IGameContext singleton): the NetplayManager holds it, the hosted services read it */
    static void** gcRoot = nullptr;
    if (!gcRoot && gen::GameContext::EnsureClass()) {
        gcRoot = static_cast<void**>(game::Fn<void* (*)(uint64_t)>(game::brute::MallocUncollectable)(sizeof(void*)));
        if (gcRoot) { *gcRoot = gen::GameContext::New(); gen::svc::SetGameContextObj(gcRoot); }
    }
    Log("[lobby] TF.EX's GameContext (generated) %s", gcRoot && *gcRoot ? "made" : "FAILED (ERROR: no local controller, input lock or local seat)");
    *g_nmRoot = gen::NetplayManager::New(s, gcRoot ? *gcRoot : s, s, s, s);
    if (void* nm = *g_nmRoot) {
        /* TFEX-DIFF (the ggrs-ffi switch-over): the session's request list and per-seat stats live in this module's memory, which the GC
         * never collects and rollback.cpp never tracks or restores (they hold no managed reference). In the GC heap the request list was
         * overwritten mid-match before any rollback (console 2026-10-18 20:55: "a VList says it holds -1435254400 elements", the low half
         * of a heap pointer, from frame ~205), so TF.EX's list and ggrs_ffi's drifted apart. TF.EX's code fills them in place (KeepVal). */
        static gen::VList<int32_t> requests;
        static gen::ValMap<int32_t, gen::st::NetworkStats> perSeat;
        if (auto* r = gen::NetplayManager::S(nm)->_netplayRequests) requests = *r;
        if (auto* p = gen::NetplayManager::S(nm)->_networkStatsPerSeat) perSeat = *p;
        gen::NetplayManager::S(nm)->_netplayRequests = &requests;
        gen::NetplayManager::S(nm)->_networkStatsPerSeat = &perSeat;
    }
    Log("[lobby] TF.EX's NetplayManager (generated) %s", *g_nmRoot ? "made" : "FAILED (ERROR: lobby matches have no TF.EX start / disconnect handling)");
}
static void* NmAny() { return g_nmRoot ? *g_nmRoot : nullptr; }
}   // namespace MainMenuPatch
namespace EndOfMatch {
bool GenStart() { return MainMenuPatch::NmAny() != nullptr; }
/* xplay's copy of the session's input delay: TF.EX's GGRSConfig.InputDelay (the series' delay, else GetEffectiveInputDelay: the advisor's
 * choice, else the preference) that TF.EX's NetplayManager.Init set (NetplayRoundLogic.OnLevelLoadFinish). Once per match, before the first advance. */
void SyncInputDelay() {
    void* nm = MainMenuPatch::NmAny();
    if (!nm) return;
    static int doneFor = -1;
    if (!gen::NetplayManager::S(nm)->_pendingSessionRoundLogic || xplay::g_lobbyMatchNo == doneFor) return;
    doneFor = xplay::g_lobbyMatchNo;
    int32_t d240 = gen::Auto_GGRSConfig::get_InputDelay(gen::NetplayManager::get_GGRSConfig(nm));
    xplay::g_npDelay = d240;
    Log("[np] input delay %d ticks at %d Hz (TF.EX's GGRSConfig.InputDelay)", xplay::g_npDelay, xplay::SessionHz());
}
}   // namespace EndOfMatch
namespace MainMenuPatch {
/* TF.EX's runtime-made classes of the match side (the vote panel, the pause overlay) are made on the main menu, outside any rollback window
 * (as Notification's): made inside a tick, a rollback would take the class records back. */
static void EnsureMatchClasses(void* menu) {
    static bool done = false;
    if (done || !menu || !Notification::Enabled()) return;
    done = true;
    MakeGenNetplayManager();
    bool a = gen::Auto_MatchEndChoices::EnsureClass(), b = gen::LightPauseMenu::EnsureClass();
    bool r = gen::NetplayRoundLogic::EnsureClass() && gen::NetplayVersusMode::EnsureClass();   /* a lobby match's round logic (xplay.cpp GetRoundLogicHook) */
    if (a) gen::Override(gen::Auto_MatchEndChoices::ClassOf(), gen::bind::v_Entity_Render, reinterpret_cast<void*>(&MatchEndChoicesRender));
    if (b) gen::Override(gen::LightPauseMenu::ClassOf(), gen::bind::v_Entity_Render, reinterpret_cast<void*>(&LightPauseMenuRender));
    Log("[lobby] TF.EX's match classes made: MatchEndChoices %s, LightPauseMenu %s, NetplayRoundLogic %s", a ? "ok" : "FAILED", b ? "ok" : "FAILED",
        r ? "ok" : "FAILED (ERROR: lobby matches play the game's own round logic)");
}
static void EndgameFrame(MatchmakingService& mm, void* menu) {   /* the end of a lobby match (also for a test host, which has no NETPLAY mode) */
    EnsureMatchClasses(menu);
    QuitTeardownFrame(menu);
    TestVoteFrame();
    (void)mm;
}

} // namespace MainMenuPatch

namespace EndOfMatch {
namespace {
/* TFEX-DIFF S-hud: TF.EX's VersusPlayerMatchResults postfix adds each player's name as an OutlineText component (tagged IsPlayerName)
 * just under the gem, over the portrait's top; on the PC the entity draws its components in order, the name last. The Switch's
 * VersusPlayerMatchResults.Render (+0xbb2f10) draws the components, then its border (+0xc0) and portrait (+0xb8) again and the gem:
 * the portrait over the name (console 2026-11-01). After the game's Render the names are drawn once more, on top, as on the PC.
 * Its components: Entity.Components (+0x28) is a List<Component> (items +0x10, count +0x18); Component.Visible +0x22, Render vtable 0x48. */
HOOK_DEFINE_TRAMPOLINE(PlayerResultsRenderHook) {
    static void Callback(void* self) {
        Orig(self);
        void* list = At<void*>(self, 0x28);
        void* items = list ? At<void*>(list, 0x10) : nullptr;
        int32_t n = list ? At<int32_t>(list, 0x18) : 0;
        for (int32_t i = 0; items && i < n; i++) {
            void* c = game::ArrayData<void*>(items)[i];
            if (c && At<bool>(c, 0x22) && gen::SideGet<bool>("IsPlayerName", c)) gen::Virt<void>(c, gen::bind::v_Component_Render);
        }
    }
};
}  // namespace
void Install() {
    PlayerResultsRenderHook::InstallAtOffset(0xbb2f10);   /* VersusPlayerMatchResults.Render: TF.EX's player names over the portrait */
    using namespace MainMenuPatch;
    gen::hooks::PauseMenu_Update::Wrap(PauseUpdateShell);
    gen::hooks::PauseMenu_Quit::Wrap(PauseQuitShell);
    gen::hooks::PauseMenu_VersusMatchSettingsAndSave::Wrap(PauseSettingsAndSaveShell);
    gen::hooks::PauseMenu_VersusArcherSelect::Wrap(PauseArcherSelectShell);
    gen::hooks::PauseMenu_VersusRematch::Wrap(PauseRematchShell);
    gen::hooks::PlayerInput_AssignInputs::Wrap(AssignInputsShell);
}
void Frame(void* menu) { MainMenuPatch::EndgameFrame(MatchmakingService::Instance(), menu); }
/* MapScenePatch: TF.EX's RematchLobby handler (generated MatchmakingService: EnableAllControllers, Engine.Scene = new MapScene(Versus))
 * made the MapScene now beginning. TFEX-DIFF S-start: the Switch's native lobby start runs underneath (players, archers, the GGRS
 * session: NetplayManagerStartLobbyMatch -> xplay::StartLobbyMatch -> MapScenePatch::Enter on this scene). */
void RematchMapScene() {
    using namespace MainMenuPatch;
    g_awaitingVote = false; g_endgame = false; g_votedMenu = nullptr;
    xplay::EndLobbyMatch();
    Static<int32_t>(MainMenu::RollcallMode) = MainMenu::Versus;
    Log("[lobby] rematch (TF.EX's RematchLobby: the MapScene): the next match starts (seed %d)", (gen::svc::GetOwnLobby()->GameData.has ? gen::svc::GetOwnLobby()->GameData.value.Seed : 0));
    NetplayManagerStartLobbyMatch();
}
void* GenNetplayManager() { return MainMenuPatch::NmAny(); }
void MakeNetplayManager() { MainMenuPatch::MakeGenNetplayManager(); }
/* TEST ONLY (hw.txt lobbypause): the last PauseMenu that updated, if still in a scene: its items and the selected one */
void DescribePauseMenu(char* out, int cap) {
    void* m = MainMenuPatch::g_lastMenu;
    if (!m || !At<void*>(m, Entity::Scene)) { std::snprintf(out, (size_t)cap, "no PauseMenu in a scene"); return; }
    void* names = At<void*>(m, gen::bind::f_PauseMenu_optionNames);
    int n = names ? gen::Fn<int32_t (*)(void*)>(gen::bind::m_List_1_eb65_get_Count)(names) : 0, w = 0;
    w += std::snprintf(out + w, (size_t)(cap - w), "PauseMenu %p index %d of %d:", m, At<int32_t>(m, gen::bind::f_PauseMenu_optionIndex), n);
    for (int i = 0; i < n && w < cap - 24; i++) {
        void* str = gen::Fn<void* (*)(void*, int32_t)>(gen::bind::m_List_1_eb65_get_Item_e2bf)(names, i);
        int len = str ? At<int32_t>(str, 0x10) : 0;
        out[w++] = ' ';
        for (int k = 0; k < len && w < cap - 2; k++) { char16_t c = gen::CharAt(str, k); out[w++] = c < 0x80 ? (char)c : '?'; }
        out[w] = 0;
    }
}
bool AwaitingVote() { return MainMenuPatch::g_awaitingVote; }
bool MatchStarted() { return MainMenuPatch::g_matchStarted; }
void SetMatchStarted() { MainMenuPatch::g_matchStarted = true; }
int Matches() { return MainMenuPatch::g_matches; }
unsigned long TestButtons() {
    using namespace MainMenuPatch;
    return g_testHold > 0 ? g_testBits : 0;
}
} // namespace EndOfMatch
} // namespace tfex
