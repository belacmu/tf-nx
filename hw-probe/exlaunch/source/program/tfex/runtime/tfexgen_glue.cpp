/* tfexgen_glue.hpp: hw.txt switches and hooks for generated TF.EX code.
 * MainMenu.Render: TF.EX's MainMenu_Render postfix (generated, NetplayMenus).
 * "genpatch <Class>": the generated hooks of TF.EX patch class <Class> (translate.toml units with hooks = true).
 * "gennotiftest <frame>": the generated Notification (tfexgen_Notification) in the main menu: the class made 30 frames in, a toast
 * at <frame>, a full GC 40 frames later, its state every 30 frames, and another toast (the other text) every 420 frames
 * (tools/re/notif-test.sh runs it).
 * "tfextest <args>": TF.EX's own test mode, as its dev-console command `test <args>` (TFCommands.LaunchTestMode, generated: mode, startLevel,
 * map, seed, checkDistance[, variants, players]; e.g. `tfextest LMS 0 1 42 2`): a match in a GGRS SyncTestSession, a rollback every
 * frame. Launched once, from the main menu's 120th frame (TEST ONLY). */
#include "tfexgen_glue.hpp"
#include "tfexgen_code_rt.hpp"
#include "tfexgen_binding.hpp"
#include "../../hwlog.hpp"
#include "lib.hpp"
#include "../MainMenuPatch.hpp"
#include "tfexgen_GlueFrMods.hpp"   /* SwitchMods.LoadSounds (a mod archer's own sounds) */
#include <cstdio>
#include <cstring>
#if __has_include("tfexgen_Notification.hpp")
#include "tfexgen_Notification.hpp"   /* TF.EX's Notification.cs, generated (stage 3) */
#define TFEXGEN_HAVE_NOTIF 1
#endif
#include "tfexgen_ScenarioLaunch.hpp"
#include "tfexgen_NetplayManager.hpp"
#include "tfexgen_services.hpp"   /* TFCommands' launch of TF.EX's test mode (TEST ONLY) */
#if __has_include("tfexgen_hooks.hpp")
#include "tfexgen_hooks.hpp"         /* TF.EX's Harmony patches as hooks (HookGen) */
#include "tfexgen_NetplayMenus.hpp"   /* TF.EX's MainMenuPatch (NetplayMenus unit): MainMenu_Render */
#define TFEXGEN_HAVE_HOOKS 1
#endif

namespace xplay { void PrepareTestSession(); void BeginTestSession(void* nm, int players); }   /* Integration B2: the match's capture and TF.State's context before the launch; the gate drives the session after it */
namespace pcgameplay { void Set(bool on); }
namespace tfex::gen {
namespace {
HOOK_DEFINE_TRAMPOLINE(MainMenuRenderHook) {
    static void Callback(void* self) {
        Orig(self);
        NetplayMenus::MainMenu_Render(self);   /* TF.EX's MainMenu.Render postfix: the roll call's pings / connection icons, waiting for the host,
                                                * checking connections, quick play starting / searching (Integration A's switch-over) */
    }
};
/* the menu's state (MainMenu.state, tfex/Game.hpp), logged as it changes */
int g_lastState = -1;
void MenuStateFrame(void* menu) {
    int st = gen::Fld<int32_t>(menu, game::MainMenu::state);
    if (st != g_lastState) { hw::Log("[tfexgen] menu state %d", st); g_lastState = st; }
}
#ifdef TFEXGEN_HAVE_NOTIF
int g_notifFrame = -1, g_frames = 0, g_made = 0;
void* g_menu = nullptr;
void** g_toastRoot = nullptr;   /* the test toast: in scanned memory (gen::RootStatic), as every reference we keep */
#define g_toast gen::RootStatic(g_toastRoot)
void NotifTestFrame(void* menu) {
    if (menu != g_menu) { g_menu = menu; g_frames = 0; }
    g_frames++;
    if (g_frames == 30)
        hw::Log("[notif] generated class TF.EX.Domain.CustomComponent.Notification : Monocle.Entity: %s, type %p",
                Notification::EnsureClass() ? "made" : "FAILED", Notification::RuntimeType());
    if (g_frames == g_notifFrame) {
        g_toast = Notification::Create(menu, FromAscii(g_made++ % 2 ? "Failed to create lobby" : "TF.EX notification test"), 20, 250, false, false);
        hw::Log("[notif] test: created %p at MainMenu frame %d (generated; is Entity %d, is MainMenu %d)", g_toast, g_frames,
                As(g_toast, TypeOf(bind::t_Entity)) == g_toast, As(g_toast, TypeOf(bind::t_MainMenu)) != nullptr);
    }
    if (g_toast && g_frames == g_notifFrame + 40) {
        Fn<void (*)()>(0x2857ea0)();   /* GC_gcollect (tfex/Notification.cpp's test): our fields must be scanned */
        hw::Log("[notif] test: GC_gcollect done; toast %p still at %.1f,%.1f", g_toast, Fld<Vector2>(g_toast, bind::f_Entity_Position).X,
                Fld<Vector2>(g_toast, bind::f_Entity_Position).Y);
    }
    if (g_toast && g_frames >= g_notifFrame && g_frames <= g_notifFrame + 400 && (g_frames - g_notifFrame) % 30 == 0)
        hw::Log("[notif] test MainMenu frame %d: toast %p pos %.1f,%.1f scene %p", g_frames, g_toast, Fld<Vector2>(g_toast, bind::f_Entity_Position).X,
                Fld<Vector2>(g_toast, bind::f_Entity_Position).Y, Fn<void* (*)(void*)>(bind::m_Entity_get_Scene)(g_toast));
    if (g_frames == g_notifFrame + 400) g_notifFrame += 420;   /* again (the other text) */
}
#endif
/* hw.txt "tfextest <args>": TF.EX's test command's arguments, launched once from the main menu, as the dev console's `test` would be:
 * only once TFGame.Loaded (MainMenuLoadWait's end: the lighting effect TF.EX's levels render with is made just before it). A frame
 * count was not enough: the console's slower load reached frame 120 first, and Level.Render's TFGame.LightingAlpha was still null. */
char g_testArgs[128];
int g_testFrames = 0;
bool g_testLaunched = false;
void TestModeFrame() {
    if (!g_testArgs[0] || g_testLaunched) return;
    if (!(Cctor(bind::c_TFGame), Fn<bool (*)()>(bind::m_TFGame_get_Loaded)())) { g_testFrames = 0; return; }
    if (++g_testFrames < 30) return;   /* a moment on the loaded main menu, as a player typing the command */
    g_testLaunched = true;
    char buf[128]; std::snprintf(buf, sizeof buf, "%s", g_testArgs);
    const char* toks[16]; int n = 0;
    for (char* t = std::strtok(buf, " \t"); t && n < 16; t = std::strtok(nullptr, " \t")) toks[n++] = t;
    gen::Arr<void*> args = gen::NewArr<void*>(bind::t_String_85cb, n);   /* string[]: the console command's args */
    for (int i = 0; i < n && args.data; i++) args.data[i] = gen::FromAscii(toks[i]);
    hw::Log("[test] tfextest: TF.EX's test mode (TFCommands.LaunchTestMode) with `%s` (%d args)", g_testArgs, n);
    pcgameplay::Set(true);   /* the PC gameplay TF.EX plays (PC levels, version gaps), as a lobby match's start turns it on (S-native) */
    xplay::PrepareTestSession();   /* capture on and TF.State's context (statert::BeforeMatch) before the level is built, as a lobby match before StartSession */
    ScenarioLaunch::LaunchTestMode(args);
    void* nm = svc::GenNM();
    int players = nm ? NetplayManager::GetNumPlayers(nm) : 0;
    hw::Log("[test] tfextest: launched (TF.EX's StartTestMode returned): test mode %d, %d players, session rate %d Hz", nm ? (int)NetplayManager::IsTestMode(nm) : -1,
            players, nm ? NetplayManager::GetSessionFps(nm) : 0);
    if (nm && NetplayManager::IsTestMode(nm)) {
        xplay::BeginTestSession(nm, players);   /* Integration B2's netplay gate drives the sync-test session */
    }
}
HOOK_DEFINE_TRAMPOLINE(MainMenuUpdateHook) {
    static void Callback(void* self) {
        Orig(self);
        PrepareRuntime();   /* the runtime's tables, made on the first menu frame: the game's GC is up (not in Install: exl_main runs
                             * before it, and MallocUncollectable aborts there, the 171f0de boot crash) and no match has started */
        MenuStateFrame(self);
        GlueFrMods::LoadSounds();   /* a mod archer's own sounds, once the game's are loaded (glue SwitchMods.LoadSounds: TFGame.MainMenuLoadWait's) */
        TestModeFrame();
#ifdef TFEXGEN_HAVE_NOTIF
        if (g_notifFrame >= 0) NotifTestFrame(self);
#endif
    }
};
} // namespace

bool HwLine(const char* line) {
    int a;
    if (!std::strncmp(line, "tfextest ", 9)) {
        std::snprintf(g_testArgs, sizeof g_testArgs, "%s", line + 9);
        for (char* e = g_testArgs + std::strlen(g_testArgs); e > g_testArgs && (e[-1] == '\r' || e[-1] == '\n' || e[-1] == ' '); ) *--e = 0;
        hw::Log("[test] hw.txt tfextest `%s`: TF.EX's test mode from the main menu (TEST ONLY)", g_testArgs);
        return true;
    }
#ifdef TFEXGEN_HAVE_NOTIF
    if (std::sscanf(line, "gennotiftest %d", &a) == 1) { g_notifFrame = a; return true; }
#endif
#ifdef TFEXGEN_HAVE_HOOKS
    char cls[64];
    if (std::sscanf(line, "genpatch %63s", cls) == 1) {   /* a TF.EX patch class's generated hooks (installed now: hw.txt is read at boot) */
        bool ok = hooks::Enable(cls);
        hw::Log("[tfexgen] genpatch %s: %s", cls, ok ? "hooks installed" : "no generated hooks for that class");
        return true;
    }
#endif
    return false;
}

/* TF.State's patches, at load like TF.EX's Harmony patches on PC. Their order against the module's own hooks doesn't matter: none
 * shares an address with them (Player.DropArrow's hand copy, rnd's DropArrowHook, is gone). What looked like an order effect was
 * rnd::Install (exl_main, after this) checking the first instructions of AddArrow / LoseHat / LavaControl.Extend /
 * Lava.BubbleComplete, which these hooks had already replaced: one failed check turned rnd's whole whitelist off (no
 * Session.StartGame reseed), and the gameplay RNG differed from frame 120. rnd no longer checks the methods it doesn't hook. */
void Install() {
#ifdef TFEXGEN_HAVE_HOOKS
    /* every TF.EX patch class the translator made, as TF.EX loads all its Harmony patches at start-up; the Switch's own steps around some
     * of them are wrappers (gen::hooks::<method>::Wrap), not hooks of their own */
    int n = hooks::EnableAll();
    hw::Log("[tfexgen] %d of %d TF.EX patch classes hooked (TF.EX's own code)", n, hooks::ClassCount());
#endif
    if (bind::rt_MainMenu_Render) MainMenuRenderHook::InstallAtOffset(bind::rt_MainMenu_Render);
    if (bind::rt_MainMenu_Update) MainMenuUpdateHook::InstallAtOffset(bind::rt_MainMenu_Update);
}

} // namespace tfex::gen
