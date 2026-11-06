/* TF.EX's online menu on the Switch (MainMenuPatch.hpp). TF.EX adds managed subclasses of the vanilla menu items; the Switch build is AOT,
 * so each TF.EX component is a vanilla object of the closest class, set up the way TF.EX's constructor would, with the methods TF.EX
 * overrides hooked and dispatched on the instance:
 *   NetplayButton      = an ArchivesButton (a MainModeButton drawing one Image, like TF.EX's) titled NETPLAY / ONLINE MATCHS;
 *   CompactCoOpButton  = the vanilla CoOpButton moved to (290, 180) without its players text (TF.EX removes it and adds its own).
 * The lobby screen is the vanilla roll call (MenuState 5), steered as RollCallElementPatch / ReadyBannerPatch do. */
#include "MainMenuPatch.hpp"
#include "ModsMenu.hpp"
#include "InputLock.hpp"
#include "NetplayOptions.hpp"
#include "EndOfMatch.hpp"
#include "gen/tfexgen_Auto_WiderSetMenu.hpp"
#include "gen/tfexgen_RollcallLayout.hpp"
#include "gen/tfexgen_binding.hpp"
#include "gen/tfexgen_NetplayButton.hpp"
#include "gen/tfexgen_InputService.hpp"
#include "gen/tfexgen_CompactCoOpButton.hpp"
#include "gen/tfexgen_NetplayEntry.hpp"
#include "gen/tfexgen_RollCallElementPatch.hpp"
#include "gen/tfexgen_MatchmakingService.hpp"
#include "gen/tfexgen_Auto_Lobby.hpp"
#include "gen/tfexgen_Auto_Series.hpp"
#include "gen/tfexgen_MainMenuCreate.hpp"
#include "gen/tfexgen_hooks.hpp"
#include "runtime/tfexgen_services.hpp"
#include "gen/tfexgen_Auto_NetplayPreferences.hpp"
#include "SwitchHud.hpp"
#include "runtime/tfexgen_code_rt.hpp"
#include "NetplayScreens.hpp"
#include "Game.hpp"
#include "MatchmakingService.hpp"
#include "Notification.hpp"
#include "../netws.hpp"
#include "../lobbymatch.hpp"
#include "lib.hpp"
#include <cstdio>
#include <new>
#include <cstdarg>
#include <initializer_list>
#include <cstring>

namespace pcgameplay { void Set(bool on); bool Verify(const char* when); }
namespace tfex {
void NetplayManagerStartLobbyMatch();
using namespace models;

namespace game {
uintptr_t Base() { return exl::util::GetMainModuleInfo().m_Total.m_Start; }
const char* TypeName(void* obj, char* out, int cap) {
    out[0] = 0;
    uintptr_t desc = obj ? *reinterpret_cast<uintptr_t*>(obj) : 0;
    uintptr_t klass = desc ? *reinterpret_cast<uintptr_t*>(desc) : 0;
    uintptr_t name = klass ? reinterpret_cast<uintptr_t*>(klass)[4] : 0;
    if (name >= Base() + 0x2863000 && name < Base() + 0x450d168) {
        const uint16_t* u = reinterpret_cast<const uint16_t*>(name);
        int k = 0;
        while (k < cap - 1 && u[k] >= 0x20 && u[k] < 0x7f) { out[k] = (char)u[k]; k++; }
        out[k] = 0;
    }
    return out;
}
bool Is(void* obj, const char* typeName) { char tn[48]; return obj && !std::strcmp(TypeName(obj, tn, sizeof tn), typeName); }
void MakeString(MString* m, const void* liveString, const char* text) {
    m->klass = *reinterpret_cast<const uintptr_t*>(liveString); m->zero = 0; m->len = 0;
    while (text[m->len] && m->len < 43) { m->ch[m->len] = (char16_t)text[m->len]; m->len++; }
    m->ch[m->len] = 0;
}
void* CurrentScene() {
    void* eng = Fn<void* (*)()>(Engine::get_Instance)();
    return eng ? At<void*>(eng, Engine::scene) : nullptr;
}
bool SceneIs(const char* typeName) { return Is(CurrentScene(), typeName); }
} // namespace game
using namespace game;

static void Log(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
static void Log(const char* fmt, ...) {
    char b[300];
    va_list a; va_start(a, fmt); vsnprintf(b, sizeof b, fmt, a); va_end(a);
    netws::Log("%s", b);
}

/* ---- VersusModeExtensions ---- */
static bool g_netplayMode = false;
bool IsNetplay() { return g_netplayMode; }
void ApplyNetplayMode() { g_netplayMode = true; pcgameplay::Set(true); }     /* online mode: the PC gameplay TF.EX plays (pcgameplay.cpp) */
void ClearNetplayMode() { g_netplayMode = false; pcgameplay::Set(false); }   /* back to the Switch's own */

namespace MainMenuPatch {

/* ---- the menu's buttons as MainMenu.CreateMain builds them (MainModeButton's constructor runs once per button, in table order) ---- */

/* ---- TF.EX's custom components ---- */
/* TF.EX's RollCallElementPatch, generated (genpatch RollCallElementPatch, on with the runtime-made classes: notif 1): its prefixes hold the
 * remote seats (the generated RollcallReconcile drives them), EnterJoined / LeaveJoined publish the local player, Update selects the team and
 * refreshes a remote portrait, Render draws the team label. The hand roll-call hooks below then keep only the Switch's own parts. */
/* TF.EX's own lobby (the generated MatchmakingService's, through the services) */
static gen::ws::Lobby* Own() { return gen::svc::GetOwnLobby(); }
static bool OwnEmpty() { gen::ws::Lobby* l = Own(); return !l || gen::Auto_Lobby::get_IsEmpty(l); }
static gen::ws::Player* PlayerAtSeat(int seat) { for (gen::ws::Player& p : Own()->Players) if (p.Seat == seat) return &p; return nullptr; }
static gen::ws::Player* OwnPlayer() {
    const char* id = MatchmakingService::Instance().GetRoomPeerId();
    for (gen::ws::Player& p : Own()->Players) if (!p.RoomPeerId.null && id && !std::strcmp(p.RoomPeerId.s, id)) return &p;
    return nullptr;
}
static MString g_sNetplay;
static bool g_wide = false;      /* WiderSet's wide set (docs/ws-wide.md): the Switch acts as a TF.EX player with WiderSet 0.9.0 installed,
                                  * IsWide = this. Set by the STANDARD / WIDE screen after NETPLAY (as WiderSet's picker on the PC), or
                                  * hw.txt "lobbywide 1" (tests that host without the menus) */
/* WiderSetMenu.IsNetplayRequested (NETPLAY opened the STANDARD / WIDE screen): the generated unit's static, so TF.EX's generated
 * FightButtonPatch (genpatch, on by default) clears the same flag */
static bool& g_netplayRequested = tfex::gen::Auto_WiderSetMenu::IsNetplayRequested;
static bool g_testWide = false;           /* hw.txt "lobbywide": what the debug driver picks on that screen */





HOOK_DEFINE_TRAMPOLINE(CreateMainHook) {   /* TF.EX's MainMenu_CreateMain postfix (generated): the NETPLAY button, the netplay mode cleared */
    static void Callback(void* menu) {
        Orig(menu);
        ModsMenu::AfterCreateMain(menu);   /* FortRise's CreateMain (the MODS button), before TF.EX's postfix as on the PC */
        gen::MainMenuCreate::MainMenu_CreateMain(menu);
    }
};

/* ---- the lobby: TF.EX's LobbyBuilder START (creating a public lobby with the builder's defaults) ---- */
static int g_matchLength = 2;   /* TF.EX's default (Standard); hw.txt "lobbylength <0..3>" */
static int g_uiStartPlayers = 2;   /* lobbyui 1: the host starts once this many players are in (hw.txt lobbymax) */
static int g_maxPlayers = 2;    /* MaxPlayers of the hosted lobby: the LobbyBuilder's LOBBY SIZE (hw.txt "lobbymax <n>": its default; TF.EX's LobbyVersusPlayerCountButton keeps it within its LOBBY SIZE) */
static int g_mapId = -1;        /* TF.EX's default (GameData.MapId = -1: RANDOM); hw.txt "lobbymap <-1..15>" */
static int g_lobbyMode = 3;     /* the hosted lobby's GameData.Mode (LobbyBuilder's mode: 3 Last Man Standing, 4 Head Hunters, 5 Team
                                 * Deathmatch); hw.txt "lobbymode" until LobbyBuilder is ported */
static int g_lobbyTeam = -1;    /* this player's first team in a team lobby (TF.EX starts at -1 = "UP/DOWN"); hw.txt "lobbyteam 0|1" */
/* ---- joining (TF.EX's LobbyBrowser: GetLobbies -> OnRetrieveSuccess -> A on a LobbyInfos -> JoinLobby -> OnJoinSuccess): the list is
 * NetplayScreens' LobbyBrowser; hw.txt "lobbyjoin 1" makes the auto-UI (lobbyui 1/2) join the first lobby there. ---- */
static int g_joinMode = 0;
void SetJoinMode(int mode) { g_joinMode = mode; }
/* StandardModeButton / WideModeButton.MenuAction (+0xb63890 / +0xbce530; vanilla: CurrentMatchSettings = VersusMatchSettings,
 * SixPlayer = 0 / 1, RollcallMode = 0 / 1, state 5). Opened from NETPLAY: only the choice (WiderSet's IsWide) and NetplaySelect, as TF.EX's
 * SelectionButton_MenuAction; the local versus settings are left alone. */
/* The Switch's own wide screen layout (its 6-player mode: no side art, the 420-wide menus), as its StandardModeButton / WideModeButton
 * set it (VersusMatchSettings.SixPlayer +0x20 and MainMenu.RollcallMode = 0 / 1; their CurrentMatchSettings store is left to the menu
 * flow, which sets it on the way to the map). TFEX-DIFF S-wide: it follows WiderSet's IsWide (SetWide), which on the PC widens every
 * menu from WiderSet's picker until STANDARD, or until TF.EX's CreateMain postfix sets it false back on the main menu. */
static bool g_wideLayout = false;
static void SetSwitchWideLayout(bool wide) {
    if (!*reinterpret_cast<void**>(Base() + MainMenu::RollcallMode) || !*reinterpret_cast<void**>(Base() + MainMenu::VersusMatchSettings)) return;   /* (statics not made yet: hw.txt at boot) */
    void* vs = Static<void*>(MainMenu::VersusMatchSettings);
    if (vs) At<uint8_t>(vs, 0x20) = wide ? 1 : 0;   /* MatchSettings.SixPlayer */
    Static<int32_t>(MainMenu::RollcallMode) = wide ? 1 : 0;
    if (wide != g_wideLayout) Log("[menu] the Switch's %s screen layout (SixPlayer %d, RollcallMode %d)", wide ? "wide" : "standard", wide ? 1 : 0, wide ? 1 : 0);
    g_wideLayout = wide;
}
static bool ModeButtonAction(void* button, bool wide) {
    if (!g_netplayRequested) return false;
    SetWide(wide ? 1 : 0);   /* WiderSet's IsWide, and with it the Switch's screen layout */
    Log("[menu] %s: NetplaySelect (WiderSet wide %s)", wide ? "WIDE" : "STANDARD", wide ? "on" : "off");
    void* menu = At<void*>(button, MenuItem::MainMenu);
    if (menu) Fn<void (*)(void*, int)>(MainMenu::set_State)(menu, NetplayScreens::NetplaySelect);
    return true;
}
/* The Switch's wide screen in TF.EX's menus. MainMenu.Update (+0xa9f4d0) moves Screen.PadOffset toward -50 (the side art slid out of
 * the 420-wide screen) only while RollcallMode is SixPlayer and the state is the roll call (5) or VERSUS's STANDARD / WIDE screen (7),
 * keeps it in states 4 and 8-10, and moves it back toward 0 (the side art in) in any other state: TF.EX's netplay screens (59-65) got the
 * side art back right after the WIDE choice. TFEX-DIFF S-wide: WiderSet widens every netplay menu while IsWide, so with the wide layout
 * on, TF.EX's states get the roll call's rule (the same step: Calc.Approach by 3 * TimeMult, from where the offset was before this frame's
 * Update). (2026-10-20 console: the first version had the sign the wrong way round and slid the side art back in.) */
constexpr float kWidePad = -50.0f;   /* the six-player menus' PadOffset (MainMenu.Update's constant, rodata +0x38e27fc) */
static bool IsTfexMenuState(int32_t st) { return st >= NetplayScreens::LobbyBrowser && st <= NetplayScreens::QuickPlaySearch; }   /* 59-65 (61: the series lobby) */
static void* EngineScreen() { void* e = Fn<void* (*)()>(Engine::get_Instance)(); return e ? Fn<void* (*)(void*)>(Engine::get_Screen)(e) : nullptr; }
/* the wide screen's trace (the console log), in the picker (7) and TF.EX's states: PadOffset at the frame's start (what other code left),
 * after the game's Update, after ours; frames 1-3 and every 10th up to 60 of each state, and any change made outside MainMenu.Update;
 * 150 lines at most */
static void TraceWide(void* self, void* screen, float start, float afterGame, float final) {
    static int state = -1, frames = 0, lines = 0; static float lastFinal = 1e9f;
    int st = At<int32_t>(self, MainMenu::state);
    if (st != state) { state = st; frames = 0; }
    frames++;
    bool changedOutside = lastFinal < 1e8f && start != lastFinal;
    lastFinal = final;
    if (st != 3 && st != 5 && st != 7 && !IsTfexMenuState(st)) return;
    if (lines >= 250 || !(frames <= 3 || (frames <= 60 && frames % 10 == 0) || changedOutside)) return;
    lines++;
    if (st == 5 && frames == 60) {   /* where the roll call's UI lands: the UI layer camera's translation, the screen's offset and width */
        void* ui = gen::Fn<void* (*)(void*)>(gen::bind::m_MainMenu_get_UILayer)(self);
        void* cam = ui ? gen::Fld<void*>(ui, gen::bind::f_CameraLayer_Camera) : nullptr;
        gen::Matrix m = cam ? gen::Fn<gen::Matrix (*)(void*)>(gen::bind::m_Camera_get_Matrix)(cam) : gen::Matrix{};
        Log("[menu] roll call UI: UILayer camera translation %.1f,%.1f | Screen.Offset %.1f,%.1f, Width %d, PadOffset %.1f", m.M41, m.M42,
            At<float>(screen, gen::bind::f_Screen_Offset), At<float>(screen, gen::bind::f_Screen_Offset + 4),
            gen::Fn<int32_t (*)(void*)>(gen::bind::m_Screen_get_Width)(screen), At<float>(screen, Screen::PadOffset));
        void* scam = gen::Fn<void* (*)(void*)>(gen::bind::m_Scene_get_Camera)(self);
        gen::Matrix sm = scam ? gen::Fn<gen::Matrix (*)(void*)>(gen::bind::m_Camera_get_Matrix)(scam) : gen::Matrix{};
        const uintptr_t getters[] = { gen::bind::m_MainMenu_get_ButtonGuideA, gen::bind::m_MainMenu_get_ButtonGuideB, gen::bind::m_MainMenu_get_ButtonGuideC, gen::bind::m_MainMenu_get_ButtonGuideD };
        char g[200] = ""; int n = 0;
        for (uintptr_t get : getters) {
            void* guide = gen::Fn<void* (*)(void*)>(get)(self);
            void* ent = guide ? gen::Fld<void*>(guide, 0x10) : nullptr;   /* Component.Entity */
            n += snprintf(g + n, sizeof g - n, " [x %.0f, entity x %.0f, layer %d]", guide ? At<float>(guide, gen::bind::f_MenuButtonGuide_Position) : -1.0f,
                          ent ? At<float>(ent, gen::bind::f_Entity_Position) : -1.0f, ent ? gen::Fn<int32_t (*)(void*)>(gen::bind::m_Entity_get_LayerIndex)(ent) : -99);
        }
        Log("[menu] roll call UI: scene camera translation %.1f | button guides A-D:%s", sm.M41, g);
    }
    void* vs = Static<void*>(MainMenu::VersusMatchSettings);
    void* cur = Static<void*>(MainMenu::CurrentMatchSettings);
    Log("[menu] wide trace: state %d frame %d PadOffset start %.1f%s, after the game's Update %.1f, after ours %.1f | RollcallMode %d, "
        "VersusMatchSettings.SixPlayer %d, CurrentMatchSettings %s (SixPlayer %d)", st, frames, start,
        changedOutside ? " (written outside MainMenu.Update)" : "", afterGame, final, Static<int32_t>(MainMenu::RollcallMode),
        vs ? (int)At<uint8_t>(vs, 0x20) : -1, !cur ? "null" : cur == vs ? "= Versus" : "other", cur ? (int)At<uint8_t>(cur, 0x20) : -1);
}
HOOK_DEFINE_TRAMPOLINE(MainMenuUpdateHook) {
    static void Callback(void* self) {
        void* screen = EngineScreen();
        bool ours = screen && g_wideLayout && IsTfexMenuState(At<int32_t>(self, MainMenu::state));
        float before = screen ? At<float>(screen, Screen::PadOffset) : 0.0f;
        InputLock::BeforeMenuUpdate();   /* the Switch's controller refresh waits while TF.EX's InputService holds the input (InputLock.cpp) */
        int32_t st0 = At<int32_t>(self, MainMenu::state);
        InputLock::RefreshInTfexScreens(IsTfexMenuState(st0), st0);   /* ... and runs in TF.EX's screens too, before the update as the game's (InputLock.cpp) */
        Orig(self);
        if (!screen || EngineScreen() != screen) return;
        float afterGame = At<float>(screen, Screen::PadOffset);
        if (ours) {
            float step = Fn<float (*)()>(Engine::get_TimeMult)() * 3.0f;
            At<float>(screen, Screen::PadOffset) = Fn<float (*)(float, float, float)>(Calc::Approach)(before, kWidePad, step);
        }
        TraceWide(self, screen, before, afterGame, At<float>(screen, Screen::PadOffset));
    }
};
HOOK_DEFINE_TRAMPOLINE(StandardModeActionHook) { static void Callback(void* self) { if (!ModeButtonAction(self, false)) Orig(self); } };
HOOK_DEFINE_TRAMPOLINE(WideModeActionHook) { static void Callback(void* self) { if (!ModeButtonAction(self, true)) Orig(self); } };

/* ---- the lobby screen: RollCallElementPatch ---- */
static bool InLobbyScreen() { return IsNetplay(); }

/* RollcallElement.NotJoinedUpdate / JoinedUpdate / Render: TF.EX's RollCallElementPatch, its generated hooks (Install). */
/* TFEX-DIFF S-input: TF.EX's open-seat orb (RollCallElementPatch.UpdateOpenSeat, generated) goes at the PC's controlIcon, an Image at
 * RollcallElement.ControlIconPos (0, 90) that the Switch's RollcallElement lacks (its own control icon, subtexture +0xa0, is drawn at
 * the same place). translate.toml keeps that member in the DynamicData side table ([fortrise] side = true): each roll-call element
 * gets an Image there, never added to the element (not drawn), whose Position TF.EX reads; TF.EX's ChaosOrb shows on open seats as on
 * the PC. (Was our own "WAITING FOR PLAYERS" text on the first open seat, over the seat's HOLD L + R prompt.) */
static void ControlIconStandIns(void* menu) {
    gen::Arr<void*> els = gen::MatchmakingService::GetRollcallElements(menu);
    static int made = 0;
    for (int i = 0; i < els.len; i++) {
        void* el = els[i];
        if (!el || gen::SideGet<void*>("controlIcon", el)) continue;
        void* atlas = Fn<void* (*)()>(gen::bind::m_TFGame_get_MenuAtlas)();
        void* sub = atlas ? Fn<void* (*)(void*, void*)>(gen::bind::m_Atlas_get_Item_6538)(atlas, gen::FromAscii("map/towerLock")) : nullptr;
        struct NullableRect { int32_t x, y, w, h; uint8_t hasValue; uint8_t pad_[3]; } noClip{};
        void* icon = sub ? Fn<void* (*)(void*, void*, NullableRect*)>(gen::bind::m_Image__ctor_b568)(nullptr, sub, &noClip) : nullptr;
        if (!icon) { if (made++ < 3) Log("[lobby] ERROR: the roll call's controlIcon stand-in wasn't made (no open-seat orb)"); continue; }
        At<float>(icon, GraphicsComponent::PositionX) = 0.0f;   /* RollcallElement.ControlIconPos */
        At<float>(icon, GraphicsComponent::PositionY) = 90.0f;
        gen::SideSet<void*>("controlIcon", el, icon);
        if (made++ < 6) Log("[lobby] roll call seat %d: the controlIcon stand-in at (0, 90) for TF.EX's open-seat orb", At<int32_t>(el, RollcallElement::playerIndex));
    }
}
/* RollcallElement.StartVersus in a lobby: TF.EX's StartVersus prefix / postfix are replaced by the Switch's native match start (S-start,
 * overrides.toml: on the server's StartLobby, tfex/MapScenePatch.cpp Enter sets State = Fade and opens the map screen). TF.EX's MenuStart
 * intercept (tfex/InputLock.cpp) returns true once IsLobbyReady, which on the PC runs this vanilla StartVersus for its postfix; here
 * its own menu change (the next menu state) would undo Enter's Fade, so in a lobby it doesn't run. */
HOOK_DEFINE_TRAMPOLINE(StartVersusHook) {
    static void Callback(void* self) {
        if (InLobbyScreen() && !OwnEmpty()) {
            static int logged = 0;
            if (logged < 3) { logged++; Log("[lobby] RollcallElement.StartVersus in a lobby: the native match start has it (S-start)"); }
            return;
        }
        Orig(self);
    }
};
/* ReadyBannerPatch: TF.EX's own, generated (tfex/gen, genpatch ReadyBannerPatch, on by default in xplay.cpp). */

/* ---- per frame ---- */

/* ---- the debug driver (hw.txt "lobbyui 1") ---- */
static int g_autoUi = 0, g_uiPhase = 0, g_uiWait = 0, g_pressFrames = 0;
static int g_editUi = 0;   /* hw.txt "lobbyedit 1": the auto-UI edits the lobby once before readying */
static int g_wantArcher = -1, g_archerPresses = 0;   /* hw.txt "lobbyarcher N": the auto-UI steps Left to archer N before readying (tests) */
void SetLobbyArcher(int archer) { g_wantArcher = archer; }
void SetEditUi(int on) { g_editUi = on ? 1 : 0; }
static bool g_quickUi = false;   /* hw.txt "lobbyquick 1": the auto-UI picks QUICK PLAY */
void SetQuickUi(int on) { g_quickUi = on != 0; }
static int g_quickWait = 0;   /* hw.txt "lobbyquickwait S": the auto-UI waits S s on NetplaySelect before QUICK PLAY (the other side queues first: seat 0) */
void SetQuickWait(int s) { g_quickWait = s; }
static int g_privateUi = 0;   /* hw.txt "lobbyprivate 1": the auto-UI hosts a private lobby (PRIVATE -> CREATE); 2: joins one (JOIN, the code) */
void SetPrivateUi(int on) { g_privateUi = on; }
static unsigned long g_pressBits = 0;
static int g_lastState = -1, g_stateFrames = 0;
void SetAutoUi(int mode) { g_autoUi = mode; }
unsigned long AutoUiButtons() { return g_pressFrames > 0 ? g_pressBits : 0; }

static void Press(unsigned long bits, const char* what) { g_pressBits = bits; g_pressFrames = 8; g_uiWait = 0; Log("[ui] press %s (phase %d)", what, g_uiPhase); }
static int g_optionsDrive = 0, g_optionsStep = 0;   /* hw.txt "optionsdrive 1": OPTIONS -> NETPLAY NAME (A) -> SERVER (A) -> back (tests) */
void SetOptionsDrive(int on) { g_optionsDrive = on; }
static bool g_triedSpectate = false;   /* the join walk's one SPECTATE press */
static int g_delaySteps = 0;   /* hw.txt "lobbydelaysteps N": on the roll call, N taps of Right (Left if < 0) with Alt2 held (TF.EX's
                               * InputDelayAdvisor gesture: one 240 Hz frame each), before readying (tests) */
void SetDelaySteps(int n) { g_delaySteps = n; }
static unsigned long g_delayButton = 0;   /* hw.txt "lobbydelaybutton L|ZL": hold only that one for the gesture (tests; default both) */
void SetDelayButton(const char* b) { g_delayButton = !std::strncmp(b, "ZL", 2) ? (1u << 8) : !std::strncmp(b, "L", 1) ? (1u << 6) : 0; }
constexpr unsigned long kA = 1u << 0, kB = 1u << 1, kLeft = 1u << 12, kUp = 1u << 13, kRight = 1u << 14, kDown = 1u << 15, kPlus = 1u << 10, kAlt = (1u << 7) | (1u << 9), kAlt2 = (1u << 6) | (1u << 8);   /* Plus = MenuInput.Start; R/ZR = Alt (RB/RT) */
/* hw.txt "lobbylocal <n>" (tests: several local players, TF.EX v0.28.0; "lobbylocal2 1" = 2): controllers 2..n (npad ids 1..n-1, each
 * connected by the GetFullKey hook while the test runs) do what more people would: one after another, START on the netplay menu (TF.EX's
 * LocalPlayersPanel: a guest of this device), then, in the lobby once this device has that many seats (the server seats the guests from
 * local_players), each its team (a team lobby) and A on its seat (ready). Their buttons, per poll of each id. kMaxLocal: the ids the hook
 * answers (megamod's 8 controllers). */
constexpr int kMaxLocal = 8;
static int g_localN = 1;   /* this device's players in the test (1: controller 1 alone) */
static int g_localPhase[kMaxLocal], g_localWait[kMaxLocal], g_localPress[kMaxLocal];
static unsigned long g_localBtn[kMaxLocal];   /* the button g_localPress holds (START, A, or Up for a team) */
void SetLocal2(int on) { g_localN = on ? 2 : 1; }
void SetLocalPlayers(int n) {
    if (n > kMaxLocal) Log("[ui] ERROR: lobbylocal %d: the test drives %d controllers at most (kMaxLocal)", n, kMaxLocal);
    g_localN = n < 1 ? 1 : n > kMaxLocal ? kMaxLocal : n;
}
int TestLocalPlayers() { return g_localN; }
/* hw.txt lobbyback 1 (tests): once the lobby is ready with a remote player in, the host backs out instead of starting: B (un-ready), B
 * (leave the lobby), B on each screen to the main menu. The remote seat's roll call element still runs its JoinedUpdate in the frame the
 * lobby empties (2026-10-25 console crash: its input was null, TF.EX's EnsureRemoteControllers was a no-op on the Switch) */
static int g_backOut = 0;
void SetBackOut(int on) { g_backOut = on; }
unsigned long AutoUiLocalButtons(int id) {
    if (id < 1 || id >= g_localN || !IsNetplay()) return 0;
    int n = id + 1;   /* this controller's player number */
    if (g_localPress[id] > 0) { g_localPress[id]--; return g_localPress[id] > 2 ? g_localBtn[id] : 0; }
    if (OwnEmpty()) {
        int local = gen::InputService::GetLocalPlayerCount(gen::svc::Service());
        /* its turn once the ones before it joined: TF.EX takes the first device pressing START */
        if (g_lastState == NetplayScreens::NetplaySelect && local == id && ++g_localWait[id] > 120) {
            g_localWait[id] = 0; g_localPress[id] = 8; g_localBtn[id] = kPlus; Log("[ui] controller %d: START on the netplay menu (join as P%d)", n, n);
        } else if (local > id && g_localPhase[id] == 0 && g_lastState == NetplayScreens::NetplaySelect) {
            static bool said[kMaxLocal]; if (!said[id]) { said[id] = true; Log("[ui] controller %d: joined as P%d (%d local players)", n, n, local); }
        }
        return 0;
    }
    const char* peer = MatchmakingService::Instance().GetRoomPeerId();
    int mine = 0;
    gen::ws::Player* seat = nullptr;
    bool before = true;   /* this device's seats before this controller's (controller 1's included) are ready */
    for (gen::ws::Player& p : Own()->Players) if (!p.RoomPeerId.null && peer && !std::strcmp(p.RoomPeerId.s, peer)) {
        if (++mine == n) seat = &p;
        else if (mine < n && !p.Ready) before = false;
    }
    /* one A at a time, as people press: TF.EX's RollcallElement.EnterJoined holds every controller (DisableAllControllers) until the
     * server has the ready, so A presses in the same frame ready only one seat */
    bool turn = before && !gen::svc::InputHeld();
    g_localWait[id]++;
    switch (g_localPhase[id]) {
    case 0: if (seat) { g_localPhase[id] = 1; g_localWait[id] = 0; Log("[ui] controller %d: this device has seat %d of its own (%d own players)", n, n, mine); }
            break;
    case 1: if (g_localWait[id] <= 60 || !turn) break;
            /* a team lobby: its team first (TF.EX's roll call: Up = BLUE), as lobbyteam does for controller 1 */
            if (seat && seat->Team < 0 && gen::Auto_Lobby::get_IsTeamMode(Own())) { g_localWait[id] = 0; g_localPress[id] = 8; g_localBtn[id] = kUp; Log("[ui] controller %d: Up (BLUE)", n); break; }
            g_localPress[id] = 8; g_localBtn[id] = kA; g_localPhase[id] = 2; g_localWait[id] = 0; Log("[ui] controller %d: A (ready)", n); break;
    /* again every 2 s while its seat isn't ready: an A before its roll call seat was up does nothing, and TF.EX un-readies this device's
     * seats when the lobby changes (UnreadyOwnRollcall: someone joins) */
    case 2: if (seat && !seat->Ready && turn && g_localWait[id] > 120) {
                g_localWait[id] = 0; g_localPress[id] = 8; g_localBtn[id] = kA;
                static int again[kMaxLocal];
                ++again[id];
                if (again[id] <= 3) Log("[ui] controller %d: A again (its seat isn't ready)", n);
                else if (again[id] % 30 == 0) Log("[ui] controller %d: A again, %d times so far", n, again[id]);
            }
            break;
    }
    return 0;
}
static int g_afterPicker = 0;   /* the driver's phase after the STANDARD / WIDE screen */
/* hw.txt "lobbyui 3": walks TF.EX's screens: NETPLAY -> NetplaySelect -> Right (RANKED) + A -> RankedSelect -> Right (LEADERBOARD) + A
 * -> Leaderboard (4 s: TF.EX's LeaderboardPanel and the server's answer) -> Alt (own profile: PlayerProfilePanel, 3 s) -> B -> B ->
 * RankedSelect -> Left (QUEUE) + A -> RankedSearch (3 s, or TF.EX sends it back) -> B -> NetplaySelect -> Right (LOBBIES) + A ->
 * LobbyBrowser -> B -> NetplaySelect -> Up (SUPPORT) + A (no browser: TF.EX's notification) -> Right (CHANGELOG) + A -> ChangelogHistory
 * -> B -> NetplaySelect -> B -> main menu, then checks the PC gameplay is off again. */
static void AutoScreens(void* menu, int state) {
    switch (g_uiPhase) {
    case 0: if (menu && state == MainMenu::PressStart && g_stateFrames > 120 && g_uiWait > 60) { Press(kA, "A (press start)"); break; }
            if (menu && state == MainMenu::Main && g_stateFrames > 150) { Press(kRight, "Right (NETPLAY)"); g_uiPhase = 1; } break;
    case 1: if (g_uiWait > 30) { Press(kA, "A (NETPLAY)"); g_uiPhase = 40; g_afterPicker = 2; } break;
    /* the STANDARD / WIDE screen after NETPLAY: Left (STANDARD), Right if hw.txt lobbywide (WIDE), A */
    case 40: if (state == MainMenu::VersusSub && g_stateFrames > 60) { Press(kLeft, "Left (STANDARD)"); g_uiPhase = 41; } break;
    case 41: if (g_uiWait > 30) { if (g_testWide) { Press(kRight, "Right (WIDE)"); g_uiPhase = 42; } else { Press(kA, "A (STANDARD)"); g_uiPhase = g_afterPicker; } } break;
    case 42: if (g_uiWait > 30) { Press(kA, "A (WIDE)"); g_uiPhase = g_afterPicker; } break;
    /* TF.EX's NetplaySelect (v0.27): QUICK PLAY, RANKED, LOBBIES, PRIVATE */
    case 2: if (state == NetplayScreens::NetplaySelect && g_stateFrames > 90) { Press(kRight, "Right (RANKED)"); g_uiPhase = 50; } break;
    case 50: if (g_uiWait > 40) { Press(kA, "A (RANKED)"); g_uiPhase = 51; } break;
    case 51: if (state == gen::en::MenuState::RankedSelect && g_stateFrames > 90) { Press(kRight, "Right (LEADERBOARD)"); g_uiPhase = 52; } break;
    case 52: if (g_uiWait > 40) { Press(kA, "A (LEADERBOARD)"); g_uiPhase = 53; } break;
    case 53: if (state == gen::en::MenuState::Leaderboard && g_stateFrames > 240) { Log("[ui] screens walk: on TF.EX's leaderboard 4 s"); Press(kAlt, "Alt (own profile)"); g_uiPhase = 56; }
             else if (state == gen::en::MenuState::RankedSelect && g_uiWait > 120) { Log("[ui] screens walk: the leaderboard closed by itself (TF.EX's OnLeaderboardFailed)"); g_uiPhase = 54; }
             break;
    case 56: if (state == gen::en::MenuState::PlayerProfile && g_stateFrames > 180) { Log("[ui] screens walk: on TF.EX's player profile 3 s"); Press(kB, "B (back to the leaderboard)"); g_uiPhase = 57; }
             else if (state == gen::en::MenuState::Leaderboard && g_uiWait > 150) { Log("[ui] screens walk: no profile screen (%s)", g_stateFrames < g_uiWait ? "TF.EX's OnPlayerProfileFailed" : "Alt didn't open it"); g_uiPhase = 57; }
             break;
    case 57: if (state == gen::en::MenuState::Leaderboard && g_stateFrames > 60 && g_uiWait > 30) { Press(kB, "B (back to RankedSelect)"); g_uiPhase = 54; } break;
    case 54: if (state == gen::en::MenuState::RankedSelect && g_stateFrames > 60) { Press(kLeft, "Left (QUEUE)"); g_uiPhase = 58; } break;
    case 58: if (g_uiWait > 40) { Press(kA, "A (QUEUE)"); g_uiPhase = 59; } break;
    case 59: if (state == gen::en::MenuState::RankedSearch && g_stateFrames > 180) { Log("[ui] screens walk: on TF.EX's ranked search 3 s"); Press(kB, "B (leave the queue)"); g_uiPhase = 60; }
             else if (state == gen::en::MenuState::RankedSelect && g_uiWait > 150) { Log("[ui] screens walk: the ranked search %s", g_stateFrames < g_uiWait ? "closed by itself (TF.EX's answer to the server's RankedStatus)" : "didn't open"); g_uiPhase = 60; }
             break;
    case 60: if (state == gen::en::MenuState::RankedSelect && g_stateFrames > 60 && g_uiWait > 30) { Press(kB, "B (back to NetplaySelect)"); g_uiPhase = 55; } break;
    case 55: if (state == NetplayScreens::NetplaySelect && g_stateFrames > 90) { Press(kRight, "Right (LOBBIES)"); g_uiPhase = 3; } break;
    case 3: if (g_uiWait > 40) { Press(kA, "A (LOBBIES)"); g_uiPhase = 4; } break;
    case 4: if (state == NetplayScreens::LobbyBrowser && g_stateFrames > 90) { Press(kB, "B (back to NetplaySelect)"); g_uiPhase = 5; }
            else if (state == NetplayScreens::NetplaySelect && g_uiWait > 150) { Log("[ui] screens walk: the lobby browser closed by itself (no server)"); g_uiPhase = 5; }
            break;
    case 5: if (state == NetplayScreens::NetplaySelect && g_stateFrames > 90) { Press(kUp, "Up (SUPPORT)"); g_uiPhase = 70; } break;
    case 70: if (g_uiWait > 40) { Press(kA, "A (SUPPORT)"); g_uiPhase = 71; } break;
    case 71: if (g_uiWait > 120) { Log("[ui] screens walk: SUPPORT pressed, %s", state == NetplayScreens::NetplaySelect ? "still on NetplaySelect" : "LEFT NetplaySelect"); Press(kRight, "Right (CHANGELOG)"); g_uiPhase = 72; } break;
    case 72: if (g_uiWait > 40) { Press(kA, "A (CHANGELOG)"); g_uiPhase = 73; } break;
    case 73: if (state == gen::en::MenuState::ChangelogHistory && g_stateFrames > 180) { Log("[ui] screens walk: on TF.EX's changelog history 3 s"); Press(kB, "B (back to NetplaySelect)"); g_uiPhase = 74; }
             else if (state == NetplayScreens::NetplaySelect && g_uiWait > 150) { Log("[ui] screens walk: the changelog history didn't open"); g_uiPhase = 74; }
             break;
    case 74: if (state == NetplayScreens::NetplaySelect && g_stateFrames > 90 && g_uiWait > 30) { Press(kB, "B (back to the STANDARD / WIDE screen)"); g_uiPhase = 43; } break;
    case 43: if (state == MainMenu::VersusSub && g_stateFrames > 60) { Press(kB, "B (back to the main menu)"); g_uiPhase = 6; }
             else if (menu && state == MainMenu::Main && g_stateFrames > 60) g_uiPhase = 6;   /* TF.EX's NetplaySelect BackState: MenuReturn.NetplayEntry, else Main */
             break;
    case 6: if (menu && state == MainMenu::Main && g_stateFrames > 60) { pcgameplay::Verify("screens: back on the main menu"); Log("[ui] screens walk: done"); g_uiPhase = 7; } break;
    default: break;
    }
}
/* hw.txt "lobbyui 5": walks FortRise's MODS menu: Down (MODS, under FIGHT) + A -> the MODS list (TOGGLE MODS, SEARCH, then the mods in
 * the registry's order: FortRise, TF.EX, ...) -> A on FortRise (no settings page: FortRise stays on the list) -> Down (TF.EX) + A ->
 * OPTIONS with only TF.EX's entries -> B -> MODS (TF.EX selected again) -> B -> main menu */
static void AutoMods(void* menu, int state) {
    switch (g_uiPhase) {
    case 0: if (menu && state == MainMenu::PressStart && g_stateFrames > 120 && g_uiWait > 60) { Press(kA, "A (press start)"); break; }
            if (menu && state == MainMenu::Main && g_stateFrames > 150) { Press(kDown, "Down (MODS)"); g_uiPhase = 1; } break;
    case 1: if (g_uiWait > 30) { Press(kA, "A (MODS)"); g_uiPhase = 2; } break;
    case 2: if (state == ModsMenu::kState && g_stateFrames > 90) { Log("[ui] mods walk: on FortRise's MODS screen"); Press(kDown, "Down (SEARCH)"); g_uiPhase = 3; }
            else if (menu && state == MainMenu::Main && g_uiWait > 120) { Log("[ui] mods walk: MODS didn't open (still on the main menu)"); g_uiPhase = 9; }
            break;
    case 3: if (g_uiWait > 30) { Press(kDown, "Down (FortRise)"); g_uiPhase = 10; } break;
    case 10: if (g_uiWait > 30) { Press(kA, "A (FortRise: no settings page)"); g_uiPhase = 11; } break;
    case 11: if (g_uiWait > 60) { Log("[ui] mods walk: %s after A on FortRise", state == ModsMenu::kState ? "still on MODS" : "LEFT MODS"); Press(kDown, "Down (TF.EX)"); g_uiPhase = 4; } break;
    case 4: if (g_uiWait > 30) { Press(kA, "A (TF.EX's settings)"); g_uiPhase = 5; } break;
    case 5: if (state == 12 && g_stateFrames > 90) { Log("[ui] mods walk: on OPTIONS (TF.EX's entries from index %d: 1 = right under its header)", NetplayOptions::FirstEntry()); Press(kB, "B (back to MODS)"); g_uiPhase = 6; }
            else if (state == ModsMenu::kState && g_uiWait > 150) { Log("[ui] mods walk: TF.EX's settings didn't open"); Press(kB, "B (back to the main menu)"); g_uiPhase = 7; }
            break;
    case 6: if (state == ModsMenu::kState && g_stateFrames > 90) { Log("[ui] mods walk: back on MODS"); Press(kB, "B (back to the main menu)"); g_uiPhase = 7; } break;
    case 7: if (menu && state == MainMenu::Main && g_stateFrames > 60) { Log("[ui] mods walk: done"); g_uiPhase = 8; } break;
    case 9: Log("[ui] mods walk: done (failed)"); g_uiPhase = 8; break;
    default: break;
    }
}
static void AutoUi(void* menu, int state) {
    if (g_pressFrames > 0) { g_pressFrames--; return; }
    g_uiWait++;
    if (state == 72) {   /* TF.EX's tutorial (MenuState.Tutorial) after PRESS START for a new player: A = NEXT on each of its slides (every walk) */
        static int presses = 0;
        if (presses < 4 && g_stateFrames > 90 && g_uiWait > 60) { presses++; Press(kA, "A (tutorial: NEXT)"); }
        return;
    }
    if (state == 66) {   /* TF.EX's changelog panel (MenuState.Changelog) after PRESS START: A = CONTINUE, once (every walk) */
        static bool pressed = false;
        if (!pressed && g_stateFrames > 120 && g_uiWait > 60) { pressed = true; Press(kA, "A (changelog: CONTINUE)"); }
        return;
    }
    if (g_autoUi == 3) { AutoScreens(menu, state); return; }
    if (g_autoUi == 5) { AutoMods(menu, state); return; }
    if (g_autoUi == 4) {   /* tests: the game's own six-player path (VERSUS -> WIDE -> the local roll call), for the wide trace */
        if (state == MainMenu::PressStart && g_stateFrames > 120 && g_uiWait > 60) Press(kA, "A (press start)");
        else if (state == MainMenu::Main && g_stateFrames > 150 && g_uiWait > 60) Press(kA, "A (VERSUS)");
        else if (state == MainMenu::VersusSub && g_stateFrames > 60 && g_uiPhase == 0) { Press(kRight, "Right (WIDE)"); g_uiPhase = 1; }
        else if (state == MainMenu::VersusSub && g_uiPhase == 1 && g_uiWait > 60) { Press(kA, "A (WIDE)"); g_uiPhase = 2; }
        return;
    }
    if (g_optionsDrive && g_optionsStep == 99 && menu && state == MainMenu::Main && g_stateFrames > 60) {   /* back from OPTIONS: the main menu's */
        Log("[ui] PRESS START again (test: the main menu's default selection for NETPLAY)");                  /* cursor is on OPTIONS; the usual */
        Fn<void (*)(void*, int)>(MainMenu::set_State)(menu, MainMenu::PressStart); g_optionsStep = 100; g_uiWait = 0; return;   /* NETPLAY flow */
    }                                                                                                          /* starts from PRESS START */
    if (g_optionsDrive && g_optionsStep < 99) {   /* TF.EX's netplay settings on the OPTIONS screen (NetplayOptions.cpp), before NETPLAY */
        if (menu && state == MainMenu::Main && g_optionsStep == 0 && g_stateFrames > 150) {
            Log("[ui] OPTIONS (test: MainMenu.State = Options)");
            Fn<void (*)(void*, int)>(MainMenu::set_State)(menu, 12); g_optionsStep = 1; g_uiWait = 0; return;
        }
        if (state == 12) {
            int first = NetplayOptions::FirstEntry(), sel = NetplayOptions::SelectedIndex();
            int target = first < 0 ? -1 : g_optionsStep == 1 ? first + 4 : g_optionsStep == 2 ? first + 5 : -2;   /* NETPLAY NAME, SERVER (Create's order) */
            if (g_stateFrames < 90 || g_uiWait < 40 || target == -1) return;
            if (target == -2) { Press(kB, "B (leave OPTIONS: TF.EX's settings stored)"); g_optionsStep = 99; return; }
            if (sel != target) Press(sel < target ? kDown : kUp, sel < target ? "Down (OPTIONS)" : "Up (OPTIONS)");
            else { Press(kA, g_optionsStep == 1 ? "A (NETPLAY NAME)" : "A (SERVER)"); g_optionsStep++; }
            return;
        }
        if (g_optionsStep > 0) return;
    }
    /* TF.EX's series screen between a best-of's games (61): A every 1.5 s, a few times (the tower when it is our pick, then READY;
     * A on a READY seat does nothing) */
    static int seriesPresses = 0;
    if (state != NetplayScreens::SeriesLobby) seriesPresses = 0;
    else { if (g_stateFrames > 120 && g_uiWait > 90 && seriesPresses < 6) { seriesPresses++; Press(kA, "A (series screen: tower / READY)"); } return; }
    if (state == MainMenu::Rollcall && g_delaySteps != 0) {   /* the input-delay advisor first (tests) */
        if (g_stateFrames > 90 && g_uiWait > 30) {
            unsigned long hold = g_delayButton ? g_delayButton : kAlt2;
            const char* hn = hold == (1u << 8) ? "ZL" : hold == (1u << 6) ? "L" : "L/ZL";
            static char what[48]; std::snprintf(what, sizeof what, "%s + %s (input delay %s1)", hn, g_delaySteps > 0 ? "Right" : "Left", g_delaySteps > 0 ? "+" : "-");
            Press(hold | (g_delaySteps > 0 ? kRight : kLeft), what);
            g_delaySteps += g_delaySteps > 0 ? -1 : 1;
        }
        return;
    }
    auto& mm = MatchmakingService::Instance();
    switch (g_uiPhase) {
    case 0: if (menu && state == MainMenu::PressStart && g_stateFrames > 120 && g_uiWait > 60) { Press(kA, "A (press start)"); break; }
            if (menu && state == MainMenu::Main && g_stateFrames > 150) { Press(kRight, "Right (NETPLAY)"); g_uiPhase = 1; } break;
    case 1: if (g_uiWait > 30) { Press(kA, "A (NETPLAY)"); g_uiPhase = 40; g_afterPicker = 10; } break;
    /* the STANDARD / WIDE screen after NETPLAY: Left (STANDARD), Right if hw.txt lobbywide (WIDE), A */
    case 40: if (state == MainMenu::VersusSub && g_stateFrames > 60) { Press(kLeft, "Left (STANDARD)"); g_uiPhase = 41; } break;
    case 41: if (g_uiWait > 30) { if (g_testWide) { Press(kRight, "Right (WIDE)"); g_uiPhase = 42; } else { Press(kA, "A (STANDARD)"); g_uiPhase = g_afterPicker; } } break;
    case 42: if (g_uiWait > 30) { Press(kA, "A (WIDE)"); g_uiPhase = g_afterPicker; } break;
    /* TF.EX's screens: NetplaySelect (v0.27: QUICK PLAY, RANKED, LOBBIES, PRIVATE) -> LOBBIES -> LobbyBrowser; host: START -> LobbyBuilder
     * -> START; join (lobbyjoin 1): A on the first lobby (Alt refreshes while there is none) */
    case 10: if (state == NetplayScreens::NetplaySelect && g_stateFrames > 90) {
                 /* lobbylocal: controllers 2..n join here first (TF.EX v0.28: START on another controller on the netplay menu; AutoUiLocalButtons) */
                 if (gen::InputService::GetLocalPlayerCount(gen::svc::Service()) < g_localN) break;   /* (lobbylocal: every local controller joined) */
                 if (g_quickUi) { if (g_stateFrames > 90 + g_quickWait * 60) { Press(kA, "A (QUICK PLAY)"); g_uiPhase = 2; } break; }   /* lobbyquick 1: matched -> the lobby screen -> ready */
                 Press(kRight, "Right (RANKED)"); g_uiPhase = 14;
             } break;
    case 14: if (g_uiWait > 40) { Press(kRight, "Right (LOBBIES)"); g_uiPhase = g_privateUi ? 20 : 11; } break;
    /* lobbyprivate 1: PRIVATE -> CREATE -> LobbyBuilder (private) -> START */
    case 20: if (g_uiWait > 40) { Press(kRight, "Right (PRIVATE)"); g_uiPhase = 21; } break;
    case 21: if (g_uiWait > 40) { Press(kA, "A (PRIVATE)"); g_uiPhase = 22; } break;
    case 22: if (state == NetplayScreens::PrivateSelect && g_stateFrames > 90) {
                 if (g_privateUi == 2) { Press(kRight, "Right (JOIN)"); g_uiPhase = 23; } else { Press(kA, "A (CREATE)"); g_uiPhase = 13; }
             } break;
    /* lobbyprivate 2: PRIVATE -> JOIN -> PrivateJoinCode -> A (the code; hw.txt joincode), again every 4 s until the lobby screen */
    case 23: if (g_uiWait > 40) { Press(kA, "A (JOIN)"); g_uiPhase = 24; } break;
    case 24: if (state == MainMenu::Rollcall) { g_uiPhase = 2; break; }
             if (state == NetplayScreens::PrivateJoinCode && g_stateFrames > 60 && g_uiWait > 240) Press(kA, "A (submit the code)");
             break;
    case 11: if (g_uiWait > 40) { Press(kA, "A (LOBBIES)"); g_uiPhase = 12; } break;
    case 12: if (state != NetplayScreens::LobbyBrowser || g_stateFrames < 60) break;
             if (!g_joinMode) { Press(kPlus, "Start (LobbyBuilder)"); g_uiPhase = 13; break; }
             /* SPECTATE (Alt2) on it once first: refused on the Switch (glue Rewrites.JoinLobbyNoSpectate), the list stays */
             if (NetplayScreens::BrowserRows() > 0 && g_uiWait > 30 && !g_triedSpectate) { g_triedSpectate = true; Press(kAlt2, "Alt2 (SPECTATE: refused)"); break; }
             if (NetplayScreens::BrowserRows() > 0 && g_uiWait > 120) { Press(kA, "A (join the first lobby)"); g_uiPhase = 2; break; }
             if (g_uiWait > 200) Press(kAlt, "Alt (refresh)");
             break;
    case 13: if (state == NetplayScreens::LobbyBuilder && g_stateFrames > 60) { Press(kPlus, "Start (create the lobby)"); g_uiPhase = 2; } break;
    case 2: {
        gen::ws::Player* me = OwnEmpty() ? nullptr : OwnPlayer();
        /* lobbyedit 1: before readying, the host's START on its seat -> LobbyBuilder (editing) -> Right (game mode) -> START -> the lobby */
        if (g_editUi == 1 && me && me->IsHost && state == MainMenu::Rollcall && g_stateFrames > 120 && g_uiWait > 60) { Press(kPlus, "Start (edit the lobby)"); g_editUi = 2; break; }
        if (g_editUi == 2) { if (state == NetplayScreens::LobbyBuilder && g_stateFrames > 60) { Press(kRight, "Right (game mode)"); g_editUi = 3; } break; }
        if (g_editUi == 3) { if (g_uiWait > 40) { Press(kPlus, "Start (update the lobby)"); g_editUi = 4; } break; }
        if (me && me->Ready) { g_uiPhase = 3; g_uiWait = 0; break; }
        /* lobbyteam 0|1 in a team lobby: TF.EX's roll call team pick (HandleTeamSelection: Up = BLUE, Down = RED) before readying */
        if (g_lobbyTeam >= 0 && menu && state == MainMenu::Rollcall && me && gen::Auto_Lobby::get_IsTeamMode(Own()) && me->Team != g_lobbyTeam) {
            if (g_stateFrames > 90 && g_uiWait > 60) Press(g_lobbyTeam == 0 ? kUp : kDown, g_lobbyTeam == 0 ? "Up (BLUE)" : "Down (RED)");
            break;
        }
        if (g_wantArcher >= 0 && menu && state == MainMenu::Rollcall && me && me->Seat >= 0 && me->Seat < TFGame::Players_Len() && g_archerPresses < 64
            && TFGame::Characters_()[me->Seat] != g_wantArcher) {
            if (g_stateFrames > 90 && g_uiWait > 40) { g_archerPresses++; Press(kLeft, "Left (archer)"); }
            break;
        }
        if (menu && state == MainMenu::Rollcall && me && g_stateFrames > 90 && g_uiWait > 120) Press(kA, "A (ready)");
        break;
    }
    case 3: if (g_backOut && mm.CanHostStart() && Own()->Players.count >= 2) { if (g_uiWait > 120) { Press(kB, "B (un-ready: back out of the lobby)"); g_uiPhase = 30; } break; }
            if (mm.IsLobbyReady()) { g_uiPhase = 4; break; }
            { gen::ws::Player* me3 = OwnEmpty() ? nullptr : OwnPlayer();
              if (me3 && !me3->Ready) { Log("[ui] un-readied (lobby edited): ready again"); g_uiPhase = 2; g_uiWait = 0; break; } }
            if (g_autoUi == 1 && mm.CanHostStart() && Own()->Players.count >= g_uiStartPlayers && g_uiWait > 120) Press(kPlus, "+ (start)");   /* the Switch roll call starts with + (MenuStart: TF.EX's intercept, InputLock.cpp); once hw.txt lobbymax players are in (the lobby's size can be larger: wide, team modes); lobbyui 2 stops at archer select */
            break;
    case 4: if (menu && state == MainMenu::Rollcall && EndOfMatch::Matches() > 0 && g_stateFrames > 180) { Press(kB, "B (leave the lobby)"); g_uiPhase = 5; } break;
    case 30: { gen::ws::Player* me30 = OwnEmpty() ? nullptr : OwnPlayer();
               if (me30 && !me30->Ready && g_uiWait > 90) { Press(kB, "B (leave the lobby)"); g_uiPhase = 5; } } break;
    /* TF.EX: the lobby -> LobbyBrowser -> NetplaySelect -> the main menu: B on each */
    case 5: if (menu && state == MainMenu::Main && g_stateFrames > 60) { pcgameplay::Verify("back on the main menu"); Log("[ui] back on the main menu: done"); g_uiPhase = 6; break; }
            if (menu && NetplayScreens::IsCustom(state) && g_stateFrames > 90 && g_uiWait > 60) Press(kB, "B (back)");
            break;
    default: break;
    }
}


void Update() {
    /* TF.EX's runtime-made classes (hw.txt "notif 1") are made here, on the main menu, outside any rollback window (Notification.cpp
     * EnsureClass; the menu-only ones make theirs when their screen is built, also in a menu) */
    if (Notification::Enabled() && SceneIs("MainMenu")) Notification::EnsureClass();
    if (SceneIs("MainMenu")) NetplaySettingsFile::Apply();   /* TF.EX's settings into its NetplayPreferences (once), as its OnVerify at load */
    auto& mm = MatchmakingService::Instance();
    void* scene = CurrentScene();
    void* menu = Is(scene, "MainMenu") ? scene : nullptr;
    int state = menu ? At<int32_t>(menu, MainMenu::state) : -1;
    if (state != g_lastState) { g_lastState = state; g_stateFrames = 0; if (menu) Log("[menu] state %d", state); }
    g_stateFrames++;
    if (menu && state == MainMenu::Rollcall && IsNetplay() && !OwnEmpty()) ControlIconStandIns(menu);
    if (menu && state == MainMenu::Rollcall) {   /* evidence for the team lobbies (CORE-FLOW row 8): each seat's team as the lobby has it */
        static int8_t lastTeam[xplay::kNpSeats]; static bool teamInit = false; if (!teamInit) { teamInit = true; for (auto& t : lastTeam) t = -2; }
        bool team = !OwnEmpty() && gen::Auto_Lobby::get_IsTeamMode(Own());
        for (int seat = 0; seat < xplay::kNpSeats && seat < TFGame::Players_Len(); seat++) {
            gen::ws::Player* p = team ? PlayerAtSeat(seat) : nullptr;
            int8_t t = p ? (int8_t)p->Team : -2;
            if (t != lastTeam[seat]) { lastTeam[seat] = t; if (p) Log("[lobby] seat %d: team %s", seat, t == 0 ? "BLUE" : t == 1 ? "RED" : "none"); }
        }
        /* evidence for the roll call (row 8): what each joined seat's portrait draws (its ArcherData's name), on change */
        static void* lastDrawn[xplay::kNpSeats] = {};
        gen::Arr<void*> els = gen::MatchmakingService::GetRollcallElements(menu);
        for (int i = 0; i < els.len; i++) {
            void* el = els[i];
            int seat = el ? At<int32_t>(el, RollcallElement::playerIndex) : -1;
            if (seat < 0 || seat >= xplay::kNpSeats || seat >= TFGame::Players_Len()) continue;
            bool joined = gen::Fn<int32_t (*)(void*)>(gen::bind::m_StateMachine_get_State)(gen::Fld<void*>(el, gen::bind::f_RollcallElement_state)) == 1;
            void* portrait = gen::Fld<void*>(el, gen::bind::f_RollcallElement_portrait);
            void* data = joined && portrait ? gen::Fn<void* (*)(void*)>(gen::bind::m_ArcherPortrait_get_ArcherData)(portrait) : nullptr;
            if (data == lastDrawn[seat]) continue;
            lastDrawn[seat] = data;
            char name[32] = "";
            if (void* n = data ? gen::Fld<void*>(data, gen::bind::f_ArcherData_Name0) : nullptr) gen::Utf8Of(n, name, sizeof name);
            if (data) Log("[lobby] seat %d draws %s (Characters %d, AltSelect %d)", seat, name, TFGame::Characters_()[seat], TFGame::AltSelect_()[seat]);
        }
    }
    if (g_autoUi) AutoUi(menu, state);
    if (menu) NetplayScreens::Frame(menu);   /* TF.EX's MainMenu Update postfix: NoGamepadUpdates, the roll-call reconcile, the screens */
    EndOfMatch::Frame(menu);
    if (!IsNetplay()) return;
    if (menu && state == MainMenu::Rollcall && g_stateFrames % 300 == 0) {
        const uint8_t* pl = TFGame::Players_();
        gen::ws::Lobby* l = Own();
        char r[96] = ""; int m = 0;   /* the lobby's players: seat, ready (R) or not (-), team (up to 8 in a wide lobby) */
        for (gen::ws::Player& p : l->Players) if (m < (int)sizeof r - 8) m += snprintf(r + m, sizeof r - m, " %d%c%d", p.Seat, p.Ready ? 'R' : '-', p.Team);
        /* logged when it changes (was every 5 s without end: an idle lobby filled the log), with how many 5 s checks it stayed */
        char line[160];
        char seats[xplay::kNpSeats + 1] = ""; for (int i = 0; i < xplay::kNpSeats && i < TFGame::Players_Len(); i++) seats[i] = pl[i] ? '1' : '0';
        snprintf(line, sizeof line, "players %s | lobby %d/%d:%s | can start %d", seats, l->Players.count, l->MaxPlayers, r, (int)mm.CanHostStart());
        static char last[160] = ""; static int same = 0;
        if (std::strcmp(line, last) != 0) {
            if (same > 0) Log("[lobby] seats: the last line stayed for %d more checks (5 s each)", same);
            Log("[lobby] seats: %s", line);
            std::snprintf(last, sizeof last, "%s", line); same = 0;
        } else same++;
    }
    if (menu && state == MainMenu::Rollcall) {   /* the input-delay advice's button (console check): what the local controller's L, ZL and
                                                  * MenuAlt2Check read when one of them changes (TF.EX's gesture is Alt2: PC LB or LT; the
                                                  * Switch's JoyConInput reads LeftShoulder +0x100 or the left trigger), 20 lines at most */
        static int lines = 0, last = -1;
        void* in = gen::svc::LocalInput();
        void* jc = in ? At<void*>(in, 0x18) : nullptr;   /* JoyConInput.JoyCon (MInput.JoyConData) */
        if (jc && lines < 20) {
            bool l = Fn<bool (*)(void*, uint32_t)>(0x93eab0)(jc, 0x100);   /* JoyConData.Check(Buttons.LeftShoulder) */
            bool zl = Fn<bool (*)(void*, float)>(0x9400c0)(jc, *reinterpret_cast<float*>(Base() + 0x38e25a4));   /* LeftTriggerCheck(the game's threshold) */
            bool alt2 = Virt<bool>(in, 0xb8);   /* PlayerInput.MenuAlt2Check, as TF.EX's advisor reads it */
            /* and MenuLeftCheck / MenuRightCheck (the step): a single Joy-Con has no L / ZL, the game maps its SL to LeftShoulder (the
             * native npad reader +0x30e44 / +0x30fc0: npad bit 24 / 26 -> 0x100) and the stick to MenuLeft / Right */
            bool left = Virt<bool>(in, 0xa0), right = Virt<bool>(in, 0xa8);
            int sig = l | zl << 1 | alt2 << 2 | (alt2 && left) << 3 | (alt2 && right) << 4;
            if (sig != last) { last = sig; lines++; Log("[menu] input-delay button: L (SL on a single Joy-Con) %d, ZL %d -> MenuAlt2Check %d; with it Left %d Right %d (JoyCon %d)",
                                                         l, zl, alt2, alt2 && left, alt2 && right, At<int32_t>(in, 0x20)); }
        }
    }
    if (menu && state == NetplayScreens::SeriesLobby) {   /* TF.EX's series screen: the series' state as the server sends it (logged when it changes) */
        static int last = -1;
        gen::ws::Series* sr = gen::RefOf(Own()->Series);
        int sig = sr ? (gen::Auto_Series::Wins(sr, 0) * 100 + gen::Auto_Series::Wins(sr, 1) * 10 + (gen::Auto_Series::get_IsFinished(sr) ? 1 : 0)) : -2;
        if (sig != last) {
            last = sig;
            if (sr) Log("[lobby] series: wins %d-%d of %d needed, finished %d", gen::Auto_Series::Wins(sr, 0), gen::Auto_Series::Wins(sr, 1),
                        gen::Auto_Series::get_WinsNeeded(sr), (int)gen::Auto_Series::get_IsFinished(sr));
        }
    }
    /* StartLobby: the vanilla start would follow (Confirm passes, StartVersus -> Fade -> MapScene); the Switch starts natively. Also from
     * TF.EX's series screen between a best-of's games (61): its StartNextGame is the same vanilla Fade -> GotoVersusLevelSelect path
     * (S-start, as the roll call's) */
    constexpr int kSeriesLobby = 61;
    if (!EndOfMatch::MatchStarted() && !EndOfMatch::AwaitingVote() && mm.IsLobbyReady() && menu && (state == MainMenu::Rollcall || state == kSeriesLobby)) {
        EndOfMatch::SetMatchStarted();
        gen::ws::Lobby* l = Own();
        const gen::ws::GameData& g = l->GameData.value;
        Log("[tfex] StartLobby: seed %d, map %d, mode %d, length %d, %d players, my seat %d", g.Seed, g.MapId, g.Mode, g.MatchLength, l->Players.count, mm.GetLocalSeat());
        NetplayManagerStartLobbyMatch();
    }
}

/* A test host (hw.txt lobbyhost) never builds the main menu's NETPLAY button, whose text gives the screens their string class: QUIT's
 * NetplaySelect then had null strings (Atlas.get_Item(null) threw). Any managed string carries the class word: Int32.ToString's. */
constexpr uintptr_t Int32_ToString = 0x21b160;   /* (as levelitems.cpp) */
const void* LiveString() {
    if (!g_sNetplay.klass) { int32_t v = 0; if (const void* s = Fn<void* (*)(int32_t*)>(Int32_ToString)(&v)) MakeString(&g_sNetplay, s, "NETPLAY"); }
    return g_sNetplay.klass ? &g_sNetplay : nullptr;
}
void SetLobbyLength(int matchLength) { g_matchLength = matchLength; }
void SetLobbyMap(int mapId) { g_mapId = mapId; }
void SetLobbyMode(int mode) { g_lobbyMode = mode; }
void SetLobbyTeam(int team) { g_lobbyTeam = team; }
void SetLobbyMax(int n) { g_maxPlayers = n < 2 ? 2 : n; g_uiStartPlayers = g_maxPlayers; NetplayScreens::SetBuilderPreset('x', g_maxPlayers); }   /* the LobbyBuilder's LOBBY SIZE */
void SetWide(int on) { g_wide = g_testWide = on != 0; SetSwitchWideLayout(g_wide); Log("[tfex] WiderSet wide %s", g_wide ? "on" : "off"); }
bool IsWide() { return g_wide; }
alignas(SwitchHud::Shift) static unsigned char g_advShift[sizeof(SwitchHud::Shift)];
static bool g_advShifted = false;
bool AdvisorShiftBegin() {
    void* scene = Fn<void* (*)(void*)>(gen::bind::m_Engine_get_Scene)(Fn<void* (*)()>(Engine::get_Instance)());
    if (!g_wide || g_advShifted || !scene || At<int32_t>(scene, MainMenu::state) != MainMenu::Rollcall) return false;
    new (g_advShift) SwitchHud::Shift(SwitchHud::kCanvasX);
    return g_advShifted = true;
}
/* GuideRightMargin's rule: TF.EX's left layout puts its guides at this margin; while shifted, back onto the wide screen's left edge */
float AdvisorLeftMargin() { return g_advShifted ? 2.0f - 2.0f * SwitchHud::kCanvasX : 2.0f; }
void AdvisorShiftEnd() {
    if (!g_advShifted) return;
    reinterpret_cast<SwitchHud::Shift*>(g_advShift)->~Shift();
    g_advShifted = false;
}

void Install() {
    EndOfMatch::Install();
    CreateMainHook::InstallAtOffset(MainMenu::CreateMain);
    StandardModeActionHook::InstallAtOffset(0xb63890); WideModeActionHook::InstallAtOffset(0xbce530);
    StartVersusHook::InstallAtOffset(RollcallElement::StartVersus);
    MainMenuUpdateHook::InstallAtOffset(MainMenu::Update);

    NetplayScreens::Install();
}
void InstallLate() {}   /* (TF.EX's generated CompactCoOpButton carries its own look) */

} // namespace MainMenuPatch
} // namespace tfex
