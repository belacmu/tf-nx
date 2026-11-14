/* TF.EX's input lock (TF.EX.Domain/Services/TF/InputService.cs DisableAllControllers / EnableAllControllers / IsInputLocked /
 * DisableAllControllerExceptLocal, and the gates that read it: TF.EX.Patchs/PlayerInput/XGamePadInput.cs + KeyboardInput.cs, whose
 * menu getters return false while IsForeignSeat (in a netplay lobby: IsInputLocked) or ResultsVoteLock.IsActive (a vote is pending,
 * PauseMenuPatch.IsVotePending)), on the Switch's controller slots.
 *
 * JoyConInput is every controller on the Switch, not only Joy-Cons: PlayerInput.AssignInputs (+0xb054b0) makes six, one per npad slot
 * (JoyConInput's constructor +0xa61690), whatever the pad's style (a single Joy-Con, a pair, handheld, a Pro Controller); the build has a
 * KeyboardInput class that the game never makes (no keyboard players; TF.EX's code makes one for a remote seat: svc::NewRemoteSeatInput).
 * TF.EX's XGamePadInputPatch (generated, gen/tfexgen_XGamePadInputPatch) runs as postfixes on the Switch's controller class, JoyConInput
 * (TowerFall.PlayerInput's virtual getters: MenuConfirm 0x38, MenuConfirmCheck 0x40, MenuBack 0x48, MenuStart 0x50, MenuStartCheck 0x58,
 * MenuAlt 0x60, MenuAlt2 0x68, MenuUp 0x70, MenuDown 0x78, MenuRight 0x80, MenuLeft 0x88, MenuAltCheck 0xb0, MenuAlt2Check 0xb8,
 * MenuBackCheck 0xc0; tools/re metaresolve; the Up/Down/Left/Right checks 0x90-0xa8 have no TF.EX patch). Its IsForeignSeat reads the
 * lock (IsInputLocked) and the local controller's seat (InputService.EnsureLocalControllerSeat); its intercepts start the lobby match
 * and open the lobby editor on the roll call. The bodies are taken from the live MenuInputs' vtables once the game has its inputs, one
 * trampoline per distinct body (AOT may fold identical getters: a shared body gets the first slot's patch, logged). Slot 0x38 first
 * carries xplay's rule (true on every call in a scripted native match without a TF.EX session).
 * The lock itself (Disable / EnableAllControllers, IsInputLocked) and the local controller's seat (EnsureLocalControllerSeat,
 * DisableAllControllerExceptLocal, ObserveLocalDevice, RebindLocalInput) are TF.EX's InputService, generated (unit InputService); its
 * GameContext store (the local controller, the lock flag) is the runtime's (tfexgen_services.cpp).
 * TFEX-DIFF (S-input), the one place the Switch's controllers differ: the game keeps TFGame.PlayerInputs in npad order (PlayerInputs[i] =
 * JoyConInput(i) for MInput.JoyCons[i]): MainMenu.Update's controller refresh (every 60 frames in states 2/3/5/6) calls
 * JoyConInput.UpdateStyle on PlayerInputs[i] for every attached npad i, and when one attaches or detaches rebuilds the array
 * (PlayerInput.AssignInputs). TF.EX's InputService rearranges PlayerInputs (the local controller at its lobby seat, the others emptied):
 * on the PC gamepads update through their own list, and TF.EX stops the vanilla refresh in a lobby (MainMenu.NoGamepadUpdates, its
 * MainMenu Update prefix). Here the refresh also waits while the InputService holds the input (Disable .. EnableAllControllers, e.g. a
 * loader on TF.EX's menus): its EnableAllControllers gives the npad order back (AssignInputs) when it unlocks outside a lobby. Without it
 * a refresh met an emptied slot (the menu crash) or rebuilt the array under TF.EX's seats. It waits for EnableAllControllers, not for
 * the lock's 5 s timeout (IsInputLocked): the slots stay emptied until then (2026-11-01 console crash: the lock ran out under a loader
 * and the next refresh read an empty slot).
 *  - Gameplay input (GetState) isn't patched: a lobby match reads GGRS's inputs. MenuSkipReplay / MenuSaveReplay / Name: no replays on
 *    the Switch; the name comes from NetplayPreferences. */
#include "lib.hpp"
#include "lib/hook/nx64/impl.hpp"
#include "../pools.hpp"
#include <lib/util/sys/modules.hpp>
#include <cstdint>
#include <cstring>
#include "Game.hpp"
#include "../hwlog.hpp"
#include "../rnd.hpp"
#include "gen/tfexgen_XGamePadInputPatch.hpp"
#include "gen/tfexgen_Auto_Lobby.hpp"
#include "gen/tfexgen_GlueRewrites.hpp"
#include "runtime/tfexgen_services.hpp"
#include "MainMenuPatch.hpp"
#include "MatchmakingService.hpp"

namespace xplay { bool ForcesMenuConfirm(); }   /* xplay.cpp: a scripted native match without a TF.EX session (MenuConfirm true on every call) */

namespace tfex::InputLock {
namespace {
uintptr_t Base() { return exl::util::GetMainModuleInfo().m_Total.m_Start; }
bool g_forced = false, g_before = false;   /* MainMenu.NoGamepadUpdates held for the lock, and its value before */
}

/* MainMenu.Update's caller, before the original (tfex/MainMenuPatch.cpp): the TFEX-DIFF above */
void BeforeMenuUpdate() {
    bool& noUpdates = game::Static<bool>(game::MainMenu::NoGamepadUpdates);   /* (a GOT slot: the static's address) */
    if (gen::svc::InputHeld()) {
        static bool expiryLogged = false;
        if (!g_forced) {
            g_forced = true; g_before = noUpdates; expiryLogged = false;
            static int logged = 0;
            if (logged < 4) { logged++; hw::Log("[input] the Switch's controller refresh waits: TF.EX's InputService holds the input (S-input)"); }
        }
        static int expiries = 0;
        if (!expiryLogged && !gen::svc::InputLocked()) {   /* once per hold, the first few holds */
            expiryLogged = true;
            if (expiries++ < 4) hw::Log("[input] TF.EX's input lock ran out (5 s) before EnableAllControllers: the refresh keeps waiting (S-input)");
        }
        noUpdates = true;
    } else if (g_forced) {
        g_forced = false; noUpdates = g_before;
    }
}

/* MainMenu.Update's caller, before the original (as the game's own refresh), in TF.EX's screens (59-65): the Switch's controller refresh there too (TFEX-DIFF S-input,
 * glue Rewrites.RefreshControllers: why). Every 60 frames, as the game's own, and only while TF.EX has controller updates on (no lobby:
 * NoGamepadUpdates false) and nothing holds the input. A Joy-Con pair split into two single Joy-Cons in the online menu then shows as
 * two controllers with their own icons, as it does on the game's screens. */
void RefreshInTfexScreens(bool tfexScreen, int state) {
    static int frames = 0, runs = 0, changes = 0;
    if (!tfexScreen || ++frames % 60 != 0) return;
    if (game::Static<bool>(game::MainMenu::NoGamepadUpdates) || game::Static<bool>(game::MainMenu::NoGamepads) || gen::svc::InputHeld()) return;
    bool changed = gen::GlueRewrites::RefreshControllers();
    if (runs++ < 1) hw::Log("[input] controller refresh in TF.EX's screen %d (S-input: the game's runs only in its own screens)", state);
    if (changed) {
        if (changes < 8) hw::Log("[input] controllers changed in TF.EX's screen %d: PlayerInputs rebuilt (AssignInputs), TF.EX's local player and guests rebound", state);
        else if (changes % 50 == 0) hw::Log("[input] controllers changed in TF.EX's screens: %d times", changes + 1);
        changes++;
    }
}

namespace {
namespace XP = gen::XGamePadInputPatch;
using Patch = void (*)(bool&, void*);
Patch g_patch[13] = {};   /* the TF.EX postfix of each installed gate */
#define TFEX_GATE_HOOK(N)                                                                 \
    HOOK_DEFINE_TRAMPOLINE(Gate##N) {                                                     \
        static bool Callback(void* self) {                                                \
            bool r = Orig(self);                                                          \
            g_patch[N](r, self);                                                          \
            return r;                                                                     \
        }                                                                                 \
    };
TFEX_GATE_HOOK(0) TFEX_GATE_HOOK(1) TFEX_GATE_HOOK(2) TFEX_GATE_HOOK(3) TFEX_GATE_HOOK(4) TFEX_GATE_HOOK(5) TFEX_GATE_HOOK(6)
TFEX_GATE_HOOK(7) TFEX_GATE_HOOK(8) TFEX_GATE_HOOK(9) TFEX_GATE_HOOK(10) TFEX_GATE_HOOK(11) TFEX_GATE_HOOK(12)
#undef TFEX_GATE_HOOK
HOOK_DEFINE_TRAMPOLINE(GateConfirm) {   /* slot 0x38: xplay's scripted-match rule first (no TF.EX session), then TF.EX's MenuConfirm_patch */
    static bool Callback(void* self) {
        if (xplay::ForcesMenuConfirm()) return true;
        bool r = Orig(self);
        XP::MenuConfirm_patch(r, self);
        return r;
    }
};
using RawFn = bool (*)(void*);   /* a gate's original getter: the controller's own value, before TF.EX's postfix */
const RawFn kRaw[] = { [](void* s) { return Gate0::Orig(s); }, [](void* s) { return Gate1::Orig(s); }, [](void* s) { return Gate2::Orig(s); },
                       [](void* s) { return Gate3::Orig(s); }, [](void* s) { return Gate4::Orig(s); }, [](void* s) { return Gate5::Orig(s); },
                       [](void* s) { return Gate6::Orig(s); }, [](void* s) { return Gate7::Orig(s); }, [](void* s) { return Gate8::Orig(s); },
                       [](void* s) { return Gate9::Orig(s); }, [](void* s) { return Gate10::Orig(s); }, [](void* s) { return Gate11::Orig(s); },
                       [](void* s) { return Gate12::Orig(s); } };
int g_gateOf[0xc8 / 8] = {};   /* vtable slot / 8 -> its gate + 1 (0: no gate of its own) */
/* JoyConInput.GetState (slot 0x20, InputState returned through x8): TF.EX's XGamepadInput.GetState postfix. In a netplay Level it gives
 * the seat's GGRS input (GetCurrentInput: the frame being simulated), as every other seat's FakeController does, so each GetState caller
 * (Player.Update, PlayerCorpse's dodge-too-late check...) reads the match's input; elsewhere RightStickShot (inert without its variant). */
HOOK_DEFINE_TRAMPOLINE(GateGetState) {
    static gen::GameVal<28> Callback(void* self) {
        gen::GameVal<28> r = Orig(self);
        XP::XGamepadInput_GetState(r, self);
        return r;
    }
};
bool g_getStateHooked = false;
using Installer = void (*)(uintptr_t);
template <class H> void Install(uintptr_t off) { H::InstallAtOffset(off); }
const Installer kGates[] = { Install<Gate0>, Install<Gate1>, Install<Gate2>, Install<Gate3>, Install<Gate4>, Install<Gate5>, Install<Gate6>,
                             Install<Gate7>, Install<Gate8>, Install<Gate9>, Install<Gate10>, Install<Gate11>, Install<Gate12> };
struct SlotPatch { uint32_t slot; Patch patch; const char* name; };
const SlotPatch kSlots[] = {
    { 0x40, XP::MenuConfirmCheck_patch, "MenuConfirmCheck" }, { 0x48, XP::MenuBack_patch, "MenuBack" },
    { 0x50, XP::MenuStart_patch, "MenuStart" }, { 0x58, XP::MenuStartCheck_patch, "MenuStartCheck" },
    { 0x60, XP::MenuAlt_patch, "MenuAlt" }, { 0x68, XP::MenuAlt2_patch, "MenuAlt2" },
    { 0x70, XP::MenuUp_patch, "MenuUp" }, { 0x78, XP::MenuDown_patch, "MenuDown" },
    { 0x80, XP::MenuRight_patch, "MenuRight" }, { 0x88, XP::MenuLeft_patch, "MenuLeft" },
    { 0xb0, XP::MenuAltCheck_patch, "MenuAltCheck" }, { 0xb8, XP::MenuAlt2Check_patch, "MenuAlt2Check" },
    { 0xc0, XP::MenuBackCheck_patch, "MenuBackCheck" } };
constexpr uintptr_t MenuInput_MenuInputs = 0x47951b0;   /* MenuInput.MenuInputs (static field slot; xplay.cpp) */
bool g_installed = false;
}

/* LightPauseMenu.Pressed(MenuButton) on the Switch (TF.EX reads XGamepadInput / KeyboardInput's raw buttons): the local JoyConInputs'
 * menu getters in TFGame.PlayerInputs (Confirm 0 = MenuConfirm, Back 1 = MenuBack, Up 2, Down 3, Start 4 = MenuStart, Alt2 5 = MenuAlt2:
 * TF.EX v0.30.0's LB / left trigger, the lobby's host role button), each through its
 * original getter: the controller's own value, without TF.EX's intercepts (and xplay's MenuConfirm rule) on top. */
bool MenuPressed(int button) {
    static const uint32_t kSlot[6] = { 0x38, 0x48, 0x70, 0x78, 0x50, 0x68 };
    if (button < 0 || button > 5 || !g_installed) return false;
    uintptr_t f = *reinterpret_cast<uintptr_t*>(Base() + game::TFGame::PlayerInputs);
    uintptr_t arr = f ? *reinterpret_cast<uintptr_t*>(f) : 0;
    uintptr_t* data = arr ? *reinterpret_cast<uintptr_t**>(arr + 0x10) : nullptr;
    int n = arr ? **reinterpret_cast<int32_t**>(arr + 0x20) : 0;   /* (managed arrays: data at +0x10, a pointer to the length at +0x20) */
    for (int i = 0; data && i < n && i < 8; i++) {
        void* in = reinterpret_cast<void*>(data[i]);
        if (!in) continue;
        bool dup = false;
        for (int j = 0; j < i; j++) dup |= data[j] == data[i];
        if (dup) continue;
        char tn[48] = "";
        rnd::TypeName(in, tn, sizeof tn);
        if (std::strcmp(tn, "JoyConInput")) continue;
        /* the raw button, as TF.EX's LightPauseMenu.Pressed reads the pad itself: the original getter, not TF.EX's intercept (InterceptBack
         * returns true while netplay is initialised: read through the patched getter, Back closed the overlay the frame after it opened) */
        int g = button == 0 ? 0 : g_gateOf[kSlot[button] / 8];
        bool down = button == 0 ? GateConfirm::Orig(in) : g ? kRaw[g - 1](in) : game::Virt<bool>(in, kSlot[button]);
        if (down) return true;
    }
    return false;
}


/* Every frame until done (xplay's FrameShell): the first live JoyConInput in MenuInput.MenuInputs gives the getter bodies. */
void InstallLate() {
    if (g_installed) return;
    uintptr_t f = *reinterpret_cast<uintptr_t*>(Base() + MenuInput_MenuInputs);
    uintptr_t arr = f ? *reinterpret_cast<uintptr_t*>(f) : 0;
    uintptr_t* data = arr ? *reinterpret_cast<uintptr_t**>(arr + 0x10) : nullptr;
    void* input = nullptr;
    for (int i = 0; data && i < 4 && !input; i++) {
        char tn[48] = "";
        if (data[i]) rnd::TypeName(reinterpret_cast<void*>(data[i]), tn, sizeof tn);
        if (!std::strcmp(tn, "JoyConInput")) input = reinterpret_cast<void*>(data[i]);
    }
    if (!input) return;
    g_installed = true;
    if (exl::hook::nx64::TrampolinesUsed() + 15 > exl::hook::nx64::TrampolinesMax()) {   /* up to 15 trampolines, installed at run time */
        pools::Exhausted("exlaunch trampolines", exl::hook::nx64::TrampolinesMax(), "TF.EX's input-lock gates not installed (the lock has no effect)");
        return;
    }
    const uintptr_t lo = Base(), hi = Base() + 0x2863000;
    uintptr_t confirm = rnd::VtableBody(input, 0x38);
    uintptr_t done[14] = {}; int nd = 0;
    if (confirm > lo && confirm < hi) { GateConfirm::InstallAtOffset(confirm - lo); done[nd++] = confirm; }
    if (uintptr_t gs = rnd::VtableBody(input, 0x20); gs > lo && gs < hi) { GateGetState::InstallAtOffset(gs - lo); g_getStateHooked = true; }
    else hw::Log("[input] ERROR: JoyConInput.GetState body outside the game (%#lx): TF.EX's GetState postfix not installed", (unsigned long)gs);
    int k = 0, shared = 0;
    for (const SlotPatch& sp : kSlots) {
        uintptr_t body = rnd::VtableBody(input, sp.slot);
        bool dup = false;
        for (int j = 0; j < nd; j++) dup |= done[j] == body;
        if (dup) { shared++; hw::Log("[input] JoyConInput %s shares a body with an earlier getter: its TF.EX postfix is not applied", sp.name); continue; }
        if (body <= lo || body >= hi) { hw::Log("[input] JoyConInput %s: body outside the game (%#lx), no TF.EX postfix", sp.name, (unsigned long)body); continue; }
        g_patch[k] = sp.patch;
        g_gateOf[sp.slot / 8] = k + 1;
        kGates[k++](body - lo);
        done[nd++] = body;
    }
    hw::Log("[input] TF.EX's XGamePadInputPatch on %d JoyConInput menu getters (MenuConfirm +%#lx; %d slots share a body); trampolines %d of %d", nd,
            (unsigned long)(confirm - lo), shared, exl::hook::nx64::TrampolinesUsed(), exl::hook::nx64::TrampolinesMax());
}
} // namespace tfex::InputLock

/* xplay.cpp's FrameShell (inside namespace xplay) declares this at block scope. */
namespace xplay { void tfex_InputLockInstallLate() { tfex::InputLock::InstallLate(); } }
