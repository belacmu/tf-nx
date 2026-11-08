/* FortRise's MODS menu on the Switch: the hand part (OUR OWN CODE). The screen is FortRise's own, translated (gen/tfexgen_Fr_UIModMenu,
 * UIModPanel, ModItem, UIModListPanel, UISearchPanel) over the Switch's mod registry (runtime/tfexgen_mods.cpp; overrides.toml's
 * FortRise MODS rules); its button and the OPTIONS filter are glue (translator/glue/ModsButton.cs). What is left for here, as the Switch
 * has no MonoMod:
 *  - FortRise's patch_MainMenu.CallStateFunc: a registered state's CustomMenuState gets Create / Destroy (CustomMenuStateRegistry); MODS
 *    is the one registered state, kState (NetplayScreens.cpp's CallStateFunc hook asks here first);
 *  - its CreateOptions: a mod picked in MODS shows only its settings (glue CreateFilteredOptions), else the game's.
 * TOGGLE MODS is FortRise's UIModToggler, translated (gen/tfexgen_Fr_UIModToggler: an ON / OFF per mod folder, for the next start), its
 * own registered state kToggleState (overrides.toml FortRiseModule.UIModTogglerEntry); its Mods folder, blacklist.txt and restart are the
 * Switch's (glue SwitchMods: sd:/xplay/mods, the registry, a restart notice). */
#include "ModsMenu.hpp"
#include "NetplayScreens.hpp"
#include "gen/tfexgen_Fr_UIModMenu.hpp"
#include "gen/tfexgen_Fr_UIModToggler.hpp"
#include "gen/tfexgen_GlueFrMods.hpp"
#include "gen/tfexgen_GlueModsButton.hpp"
#include "runtime/tfexgen_code_rt.hpp"
#include "runtime/tfexgen_frmods.hpp"
#include "../hwlog.hpp"
#include "lib.hpp"

namespace tfex::ModsMenu {
namespace {
constexpr uintptr_t CreateOptions = 0xaa26b0;   /* MainMenu.CreateOptions (classmeta) */
void** g_stateRoot = nullptr;                    /* the MODS screen's UIModMenu while it is open (a GC root) */
void** g_toggleRoot = nullptr;                   /* TOGGLE MODS' UIModToggler while it is open (a GC root) */

HOOK_DEFINE_TRAMPOLINE(CreateOptionsHook) {
    static void Callback(void* menu) {
        if (gen::GlueModsButton::CreateFilteredOptions(menu)) {
            static int logged = 0;
            if (logged++ < 3) hw::Log("[mods] OPTIONS: only the settings of the mod picked in MODS (FortRise's FilterMod)");
            return;
        }
        Orig(menu);
    }
};
}  // namespace

void AfterCreateMain(void* menu) {
    gen::GlueModsButton::AddToMain(menu);
    static int logged = 0;
    if (logged++ < 3) hw::Log("[mods] main menu: FortRise's MODS button added");
}

bool CallStateFunc(void* menu, void* name, int32_t state) {
    if (state == kToggleState) {
        void*& screen = gen::RootStatic(g_toggleRoot);
        if (NetplayScreens::IsName(name, "Create")) {
            screen = gen::Fr_UIModToggler::New(menu);
            if (screen) gen::Fr_UIModToggler::Create(screen);
            hw::Log("[mods] TOGGLE MODS screen (FortRise's UIModToggler): %d folder(s) off at start", tfex::gen::frmods::BlackCount());
        } else {
            if (screen) gen::Fr_UIModToggler::Destroy(screen);   /* (blacklist.txt written; a change: the restart notice) */
            screen = nullptr;
        }
        return true;
    }
    if (state != kState) return false;
    void*& screen = gen::RootStatic(g_stateRoot);
    if (NetplayScreens::IsName(name, "Create")) {
        screen = gen::GlueFrMods::NewModsMenu(menu);
        if (screen) gen::Fr_UIModMenu::Create(screen);
        hw::Log("[mods] MODS screen: %d mods listed (FortRise's UIModMenu)", screen ? gen::GlueFrMods::LoadedMods().Length() : -1);
    } else {
        if (screen) gen::Fr_UIModMenu::Destroy(screen);
        screen = nullptr;
    }
    return true;
}

void Install() {
    CreateOptionsHook::InstallAtOffset(CreateOptions);
}
}  // namespace tfex::ModsMenu
