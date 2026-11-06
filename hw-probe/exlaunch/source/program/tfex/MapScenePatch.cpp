/* TF.EX v0.21.0's map screen for a lobby match (TF.EX.Patchs/Scene/MapScene.cs MapScenePatch; the MapScene branches of
 * XGamePadInput.cs / KeyboardInput.cs; RollCallElement.cs StartVersus_Postfix), on the Switch's vanilla MapScene (1.4.1.6, same code as
 * the PC's docs/reference/tf8-decompile/TowerFall/MapScene.cs). Before this the Switch called MapScene.StartSession directly and waited in
 * the level while the other side's map screen ran (docs/ws-map-select.md).
 *
 * The flow, as TF.EX: lobby screen -> MainMenu.State = Fade (20 frames, FinishTransition -> FadeAction = MainMenu.GotoVersusLevelSelect)
 * -> MapScene: Begin_Postfix selects the lobby's button (RANDOM for MapId -1) -> menu confirm is forced while the scene is the MapScene
 * (every other menu input false) -> the vanilla confirm: a tower zooms in (VersusMapButton.OnConfirm), RANDOM steps right to the tower
 * GetRandomVersusTower gives (VersusRandomSelect.OnConfirm, 6 frames a button) -> DoEnterAreaZoom (60 + 20 frames) -> StartSession,
 * whose prefix/postfix are xplay::LobbyStartSession (the GGRS session starts there, at the same point as TF.EX's). A rematch is TF.EX's
 * own RematchLobby handler (generated MatchmakingService: Engine.Scene = new MapScene(Versus), MatchmakingService.cs:763); the
 * Switch's native lobby start runs underneath from that MapScene's Begin (EndOfMatch::RematchMapScene), and Enter then selects on it.
 *
 * Switch bodies (tools/re: classmeta.py, the MapScene vtable from relocs, callers): MapScene.Begin +0xab1240 (vtable 0x20),
 * InitVersusButtons +0xab1fe0, GetRandomVersusTower +0xab2a50 (VersusRandomSelect.OnConfirm +0xbb5800 calls it at +0xbb5900),
 * StartSession +0xab54e0, ScrollToButton +0xab4730; MapButton.OnSelect +0xaa9330 / OnDeselect +0xaa9360 (not virtual), Data +0x70,
 * Title +0x78; MapScene.Selection +0x50, Buttons +0x58 (List: items +0x10, size +0x18); MainMenu.FinishTransition +0xa9fdf0,
 * GotoVersusLevelSelect +0xaa07a0 (new MapScene(SixPlayer ? 1 : 0), Engine.Scene = it). The map screen's input (Confirm true, Back /
 * Left / Right false in a netplay lobby) is TF.EX's own XGamePadInputPatch, generated and run on the Switch's JoyConInput getters
 * (tfex/InputLock.cpp). */
#include "gen/tfexgen_MapScenePatch.hpp"
#include "gen/tfexgen_hooks.hpp"
#include "MapScenePatch.hpp"
#include "MainMenuPatch.hpp"
#include "EndOfMatch.hpp"
#include "Game.hpp"
#include "runtime/tfexgen_services.hpp"
#include "../netws.hpp"
#include "lib.hpp"
#include <cstdint>

namespace tfex::Tower { bool ButtonIsTower(void* button, int index, bool wide); }
namespace xplay { void LobbyStartSession(); }

namespace tfex::MapScenePatch {
using namespace game;

namespace off {
    constexpr uintptr_t ScrollToButton = 0xab4730, OnSelect = 0xaa9330, OnDeselect = 0xaa9360, FinishTransition = 0xa9fdf0,
                        GotoVersusLevelSelect = 0xaa07a0;
    constexpr uint32_t Selection = 0x50, Buttons = 0x58, ButtonData = 0x70;
}

static int g_tower = -1;
static bool g_wide = false, g_random = false;
static bool g_active = false;       /* from Enter to the map screen's StartSession */
static bool g_rematchBegin = false; /* TF.EX's RematchLobby MapScene's Begin: its buttons are the lobby's (before Enter) */
static bool g_fadePending = false;  /* the lobby screen fades out; its FinishTransition opens the map */
static int g_mapFrames = 0;         /* MapScene.Update calls in this map screen (the log's timing) */

bool Active() { return g_active; }
void Reset() { g_active = false; g_fadePending = false; }

static void GotoVersusLevelSelect() {
    g_mapFrames = 0;
    Fn<void (*)()>(off::GotoVersusLevelSelect)();
    netws::Log("[map] MainMenu.GotoVersusLevelSelect: the map screen (tower %d%s, %s)", g_tower, g_wide ? " wide" : "", g_random ? "RANDOM" : "fixed");
}

static void SelectLobbyButton(void* scene);
void Enter(int tower, bool wide, bool random) {
    g_tower = tower; g_wide = wide; g_random = random || tower < 0;
    g_active = true;
    void* scene = CurrentScene();
    if (Is(scene, "MainMenu")) {   /* RollCallElement.StartVersus_Postfix: FadeAction = GotoVersusLevelSelect; State = Fade */
        g_fadePending = true;
        Fn<void (*)(void*, int)>(MainMenu::set_State)(scene, MainMenu::Fade);
        netws::Log("[map] lobby screen: MainMenu.State = Fade, then the map screen");
    } else if (Is(scene, "MapScene")) {   /* TF.EX's RematchLobby made this MapScene: the selection its Begin would have made */
        g_fadePending = false;
        g_mapFrames = 0;
        SelectLobbyButton(scene);
    } else {
        g_active = false;
        netws::Log("[map] ERROR: Enter from neither the lobby screen nor TF.EX's rematch MapScene: no map screen");
    }
}

/* MainMenu.FinishTransition in State Fade calls FadeAction (MainMenu.cs FinishTransition); TF.EX's StartVersus_Postfix made it
 * GotoVersusLevelSelect. */
HOOK_DEFINE_TRAMPOLINE(FinishTransitionHook) {
    static void Callback(void* menu) {
        if (g_fadePending && At<int32_t>(menu, MainMenu::state) == MainMenu::Fade) {
            g_fadePending = false;
            GotoVersusLevelSelect();
            return;
        }
        Orig(menu);
    }
};

static int ButtonCount(void* scene, void*** items) {
    void* list = At<void*>(scene, off::Buttons);
    *items = list ? ArrayData<void*>(At<void*>(list, 0x10)) : nullptr;
    return list && *items ? At<int32_t>(list, 0x18) : 0;
}
static void* TowerButton(void* scene) {
    void** b; int n = ButtonCount(scene, &b);
    for (int i = 0; i < n; i++) if (Tower::ButtonIsTower(b[i], g_tower, g_wide)) return b[i];
    return nullptr;
}
static void* RandomButton(void* scene) {
    void** b; int n = ButtonCount(scene, &b);
    for (int i = 0; i < n; i++) if (Is(b[i], "VersusRandomSelect")) return b[i];
    return nullptr;
}

/* TF.EX's MapScenePatch runs as generated hooks (translate.toml hooks = true: InitVersusButtons, Begin, GetRandomVersusTower and
 * StartSession; each body checks for a netplay mode itself, as on the PC). These shells add the Switch's parts. TFEX-DIFF S-wide: a
 * WiderSet-wide lobby keeps the Switch's tower port (WiderSet's towers 16..31 are WiderSet's code, not TF.EX's: Tower::SelectWide over the
 * Switch's six-player list), so its map screen takes the original instead of TF.EX's Begin / GetRandomVersusTower. InitVersusButtons is
 * TF.EX's in both (its prefix unlocks the four towers while the buttons are made, wide or not, as on the PC with WiderSet). */
static bool UseGen() { return (g_active || g_rematchBegin) && !g_wide && IsNetplay(); }
static bool g_startedWithGen = false;
bool StartedWithGen() { return g_startedWithGen; }
namespace H = gen::hooks;

/* MapScenePatch.Begin_Postfix: Selection.OnDeselect(); the button whose Data.ID.X == MapId (RANDOM for -1, and when there is none);
 * Selection = it, OnSelect(), ScrollToButton(it). */
static void SelectLobbyButton(void* scene) {
    if (UseGen()) {
        gen::MapScenePatch::Begin_Postfix(scene);
        void* target = At<void*>(scene, off::Selection);
        char tn[48] = "";
        netws::Log("[map] MapScene.Begin (TF.EX's Begin_Postfix): selected %s", target ? TypeName(target, tn, sizeof tn) : "nothing");
        return;
    }
    void* sel = At<void*>(scene, off::Selection);
    if (sel) Fn<void (*)(void*)>(off::OnDeselect)(sel);
    void* target = g_random ? nullptr : TowerButton(scene);
    if (!target) target = RandomButton(scene);
    void** b; int n = ButtonCount(scene, &b);
    if (target) {
        At<void*>(scene, off::Selection) = target;
        Fn<void (*)(void*)>(off::OnSelect)(target);
        Fn<void (*)(void*, void*)>(off::ScrollToButton)(scene, target);
    }
    char tn[48] = "";
    netws::Log("[map] MapScene.Begin: %d buttons, selected %s (%s)", n, target ? TypeName(target, tn, sizeof tn) : "nothing",
               target && !Is(target, "VersusRandomSelect") ? "the lobby's tower" : "RANDOM");
}
/* A MapScene beginning while the lobby waits for the vote is TF.EX's RematchLobby's (its buttons made with the lobby's unlocks, the
 * same wide set as the match before): the Switch's native start runs from here, and its Enter selects on this scene. */
static void BeginShell(H::MapScene_Begin& c) {
    const bool rematch = !g_active && EndOfMatch::AwaitingVote();
    g_rematchBegin = rematch;
    if (g_active && g_wide) c.Orig(); else c();   /* TF.EX's Begin_Postfix: the lobby's tower selected */
    g_rematchBegin = false;
    if (rematch) { EndOfMatch::RematchMapScene(); return; }
    if (!g_active) return;
    if (g_wide) { SelectLobbyButton(c.self); return; }
    void* target = At<void*>(c.self, off::Selection);
    char tn[48] = "";
    netws::Log("[map] MapScene.Begin (TF.EX's Begin_Postfix): selected %s", target ? TypeName(target, tn, sizeof tn) : "nothing");
}

/* MapScenePatch.MapScene_GetRandomVersusTower: TF.EX shuffles the netplay-safe VersusMapButtons with new Random(lobby seed) and takes the
 * first (generated, over the vanilla pick). A wide lobby: tfex::Tower::SelectWide made the pick; its button, without the vanilla body
 * (its unseeded new Random() and lastRandomVersusTower only matter offline). */
static void GetRandomVersusTowerShell(H::MapScene_GetRandomVersusTower& c) {
    if (g_active && g_wide) {
        void* b = TowerButton(c.self);
        netws::Log("[map] GetRandomVersusTower: tower %d%s after %d map frames", g_tower, b ? "" : " has no button: the vanilla pick", g_mapFrames);
        if (b) { c.result_ = b; c.ran_ = false; } else c.Orig();
        return;
    }
    c();
    if (!g_active) return;
    char tn[48] = "";
    netws::Log("[map] GetRandomVersusTower (TF.EX's): %s after %d map frames", c.result_ ? TypeName(c.result_, tn, sizeof tn) : "nothing", g_mapFrames);
}

/* MapScene.StartSession: TF.EX's StartSession_Prefix (the archers: ArcherService.CompactSeatsToHandles, ApplyToGame; teams), then the
 * Switch's native session setup before the original (xplay::LobbyStartSession: players, archers, teams, mode, controllers, rng, the
 * GGRS session; TFEX-DIFF S-start), then TF.EX's StartSession_Postfix. */
static void StartSessionShell(H::MapScene_StartSession& c) {
    if (!g_active) { c(); return; }
    netws::Log("[map] MapScene.StartSession after %d map frames", g_mapFrames);
    g_startedWithGen = UseGen();   /* (xplay::LobbyStartSession: TF.EX's pick stands) */
    Reset();
    c.beforeOrig_ = [](H::MapScene_StartSession&) {
        uint8_t* pl = TFGame::Players_(); int32_t* ch = TFGame::Characters_();   /* (the archer store TF.EX filled, as ApplyToGame applied it) */
        netws::Log("[map] TF.EX's StartSession_Prefix: %d archers in the store; Players %d%d%d%d, Characters %d %d %d %d",
                   (int)gen::svc::ArcherPlayers().Count(), pl ? pl[0] : -1, pl ? pl[1] : -1, pl ? pl[2] : -1, pl ? pl[3] : -1,
                   ch ? ch[0] : -1, ch ? ch[1] : -1, ch ? ch[2] : -1, ch ? ch[3] : -1);
        xplay::LobbyStartSession();
    };
    c();
}

/* The map screen's frames (the log's timing). */
HOOK_DEFINE_TRAMPOLINE(MapUpdateHook) {
    static void Callback(void* scene) { if (g_active) g_mapFrames++; Orig(scene); }
};

void Install() {
    FinishTransitionHook::InstallAtOffset(off::FinishTransition);
    H::MapScene_Begin::Wrap(BeginShell);
    H::MapScene_GetRandomVersusTower::Wrap(GetRandomVersusTowerShell);
    H::MapScene_StartSession::Wrap(StartSessionShell);
    MapUpdateHook::InstallAtOffset(0xab3bb0);   /* MapScene.Update (vtable 0x30) */
}

} // namespace tfex::MapScenePatch
