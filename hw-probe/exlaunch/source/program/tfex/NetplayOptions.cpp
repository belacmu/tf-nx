/* TF.EX's netplay settings (its NetplaySettings: input delay, auto adjust, accepted rollback, custom skins, the name, the server, auto
 * update) on the OPTIONS screen, as on the PC: FortRise's MainMenu.InitOptions (generated, Fr_MainMenuOptions) lists each mod's settings
 * under its header (its InitModOptions, glue SwitchMods; FortRise's MODS menu filters them to one mod), and for TF.EX that is
 * NetplaySettings.Create (generated) through glue SettingsUi (FortRise's ISettingsCreate as the game's OptionsButtons). Each change runs TF.EX's Apply; leaving the screen
 * (MainMenu.DestroyOptions) stores TF.EX.settings.json, as FortRise saves a mod's settings when its page closes. TFEX-DIFF S-keyboard:
 * text entries take the Switch's software keyboard (ReadKeyboard). */
#include "lib.hpp"
#include "NetplayOptions.hpp"
#include "MatchmakingService.hpp"
#include "Game.hpp"
#include "gen/tfexgen_NetplaySettings.hpp"
#include "gen/tfexgen_GlueSettingsUi.hpp"
#include "gen/tfexgen_Fr_MainMenuOptions.hpp"
#include "runtime/tfexgen_code_rt.hpp"
#include "../hwlog.hpp"
#include <cstring>
#include <cstdio>
#include "gen/tfexgen_binding.hpp"

namespace swkbd { bool ReadText(char16_t* out, int maxLen, const char16_t* header, const char16_t* initial); }

namespace tfex::NetplayOptions {
using namespace tfex::game;
namespace {
constexpr uintptr_t InitOptions = 0xaa38d0, DestroyOptions = 0xaa3ad0;   /* MainMenu.InitOptions(List<OptionsButton>), DestroyOptions() */
constexpr int32_t kOptionsState = 12;                                     /* MainMenu.MenuState.Options */
bool g_added = false;
void** g_buttonsRoot = nullptr;   /* the OPTIONS screen's List<OptionsButton> (GC-scanned slot) */
int g_first = -1;
char g_testText[4][128]; int g_testCount = 0, g_testNext = 0;
int ListCount(void* list) { return list ? At<int32_t>(list, 0x18) : 0; }
void* ListItem(void* list, int i) {   /* List<T>: _items (a managed array: its data pointer at +0x10) at +0x10 */
    void* items = At<void*>(list, 0x10);
    void** data = items ? At<void**>(items, 0x10) : nullptr;
    return data ? data[i] : nullptr;
}

/* MainMenu.InitOptions: FortRise's (patch_MainMenu.InitOptions, a MonoMod replacement, generated: Fr_MainMenuOptions), as on the PC
 * where TF.EX runs on FortRise: the mods' settings under a header each (its InitModOptions: AddEntries below), then the layout */
HOOK_DEFINE_TRAMPOLINE(InitOptionsHook) {
    static void Callback(void* menu, void* buttons) {
        if (!menu || !buttons) { Orig(menu, buttons); return; }
        gen::Fr_MainMenuOptions::InitOptions(menu, buttons);
    }
};
HOOK_DEFINE_TRAMPOLINE(DestroyOptionsHook) {
    static void Callback(void* menu) {
        Orig(menu);
        if (!g_added) return;
        g_added = false; g_first = -1; gen::RootStatic(g_buttonsRoot) = nullptr;
        NetplaySettingsFile::StoreObject();   /* the edited NetplaySettings into TF.EX.settings.json (FortRise: saved when the page closes) */
    }
};
}

void AddEntries(void* buttons) {   /* FortRise's InitModOptions for TF.EX (glue SwitchMods.InitModOptions), after its header */
    void* settings = NetplaySettingsFile::Object();
    if (!settings) return;
    g_first = ListCount(buttons);
    gen::RootStatic(g_buttonsRoot) = buttons;
    gen::NetplaySettings::Create(settings, buttons);   /* TF.EX's own entries (FortRise's ISettingsCreate: glue SettingsUi) */
    g_added = true;
    static int logged = 0;
    if (logged++ < 3) hw::Log("[tfex] OPTIONS: TF.EX's netplay settings added (NetplaySettings.Create) under FortRise's TF.EX header");
}

void AddTestText(const char* text) {
    if (g_testCount >= 4) return;
    char* t = g_testText[g_testCount++];
    std::snprintf(t, sizeof g_testText[0], "%s", text);
    for (int n = (int)std::strlen(t); n > 0 && (t[n - 1] == '\n' || t[n - 1] == '\r' || t[n - 1] == ' '); n--) t[n - 1] = 0;
}
int FirstEntry() { return g_first; }
int SelectedIndex() {
    void* list = g_buttonsRoot ? *g_buttonsRoot : nullptr;
    for (int i = 0; list && i < ListCount(list); i++)
        if (void* b = ListItem(list, i); b && Fn<bool (*)(void*)>(gen::bind::m_MenuItem_get_Selected)(b)) return i;
    return -1;
}
void* ReadKeyboard(void* initialValue) {
    if (g_testNext < g_testCount) {   /* tests: the queued text (hw.txt optionstext) */
        const char* t = g_testText[g_testNext++];
        hw::Log("[tfex] OPTIONS: typed \"%s\" (test text instead of the keyboard)", t);
        return gen::ManagedUtf8(t, (int)std::strlen(t));
    }
    char16_t initial[129] = {};
    int n = initialValue ? gen::Len(initialValue) : 0;
    for (int i = 0; i < n && i < 128; i++) initial[i] = gen::CharAt(initialValue, i);
    char16_t text[129] = {};
    if (!swkbd::ReadText(text, 128, u"TF.EX", initial)) return nullptr;
    char utf8[129]; int k = 0;
    for (; text[k] && k < 128; k++) utf8[k] = text[k] < 0x80 ? (char)text[k] : '?';
    utf8[k] = 0;
    hw::Log("[tfex] OPTIONS: typed \"%s\"", utf8);
    return gen::ManagedUtf8(utf8, k);
}

void Install() {
    InitOptionsHook::InstallAtOffset(InitOptions);
    DestroyOptionsHook::InstallAtOffset(DestroyOptions);
}
}
