/* TF.EX's netplay screens (TF.EX.Patchs.Scene/MainMenuPatch.cs, TF.EX.Domain.CustomComponent/*; TFEX-UI-SPEC.md 1-2): TF.EX's own,
 * generated (tfex/gen/tfexgen_NetplayMenus.cpp, CORE-FLOW rows 2-5). TF.EX extends MainMenu.MenuState past vanilla's values
 * (LobbyBrowser 59, LobbyBuilder 60, NetplaySelect 62, PrivateSelect 63, PrivateJoinCode 64, QuickPlaySearch 65) and drives them through
 * MainMenu.CallStateFunc(name, state), whose prefix handles "Create"/"Destroy" for those states (vanilla never sees them). The Switch's
 * CallStateFunc (+0xa9f100) is the same reflection call, so TF.EX's prefix (MainMenu_CallStateFunc) runs from this file's hook, and its
 * Update postfix (MainMenu_Update_Postfix: the browser / builder inputs, lobby creation) from Frame. ScreenTitle's state lookup: TF.EX's
 * ScreenTitlePatch, generated and on by default. LoaderPatch (vanilla Loader.TweenIn throws; TF.EX's screens add a Loader): on with them.
 *
 * TFEX-DIFFs (the Switch really differs; everything else is TF.EX's code):
 *  - S-keyboard: TF.EX's JoinCodeEntry "pastes" the code from the PC clipboard (MainMenuPatch.ReadClipboard); here that paste is the
 *    Switch's software keyboard (KeyboardCode: the [service] rule), or the harness's test code. The PC keyboard reads as nothing pressed.
 *  - S-keyboard: no clipboard: TF.EX's COPY CODE guide reads SHOW CODE and shows the code in its notification (glue Rewrites.ShowPrivateCode).
 * The hand screens this replaced (switch-over 2026-10-17): docs/ws-tfex-update.md "Menus track". */
#include "gen/tfexgen_NetplayMenus.hpp"
#include "gen/tfexgen_binding.hpp"
#include "gen/tfexgen_hooks.hpp"
#include "gen/tfexgen_LobbyInfos.hpp"
#include "gen/tfexgen_Auto_JoinCodeEntry.hpp"
#include "gen/tfexgen_LobbyVersusModeButton.hpp"
#include "gen/tfexgen_Auto_LobbyBuilderContext.hpp"
#include "runtime/tfexgen_services.hpp"
#include "gen/tfexgen_MatchmakingService.hpp"
#include "gen/tfexgen_Auto_TowerFallSceneExtensions.hpp"
#include "Game.hpp"
#include "MainMenuPatch.hpp"
#include "NetplayScreens.hpp"
#include "ModsMenu.hpp"
#include "gen/tfexgen_Auto_Lobby.hpp"
#include "runtime/tfexgen_code_rt.hpp"
#include "../hwlog.hpp"
#include "lib.hpp"
#include "nn/fs.hpp"
#include <cstdio>
#include <cstring>

namespace swkbd { bool ReadCode(char* out, int len, const char16_t* header, const char* alphabet); }
namespace tfex::NetplayScreens {
using namespace tfex::game;

bool IsName(void* str, const char* s) {   /* a managed string == an ASCII literal */
    if (!str) return false;
    int n = At<int32_t>(str, 0x10); const char16_t* c = reinterpret_cast<const char16_t*>(reinterpret_cast<uintptr_t>(str) + 0x14);
    int k = 0; for (; s[k]; k++) if (k >= n || c[k] != (char16_t)s[k]) return false;
    return k == n;
}

namespace {
constexpr uintptr_t CallStateFunc = 0xa9f100;
constexpr uintptr_t Entity_ctor = 0x92d620, Layer_Add = 0x938e40, Layers_get = 0x92d9f0;

/* the strings the code display shows: managed strings outside the GC heap (MString), made once from a live string's class word */
MString g_str[8]; const char* g_strText[8]; int g_nStr = 0;
void* Str(const char* s) {
    for (int i = 0; i < g_nStr; i++) if (!std::strcmp(g_strText[i], s)) return &g_str[i];
    const void* live = MainMenuPatch::LiveString();
    if (!live || g_nStr >= 8) return nullptr;
    MakeString(&g_str[g_nStr], live, s); g_strText[g_nStr] = s;
    return &g_str[g_nStr++];
}

/* ---- join by code (TFEX-DIFF S-keyboard): the code TF.EX's JoinCodeEntry pastes. 5 characters of A-Z without I and O, 2-9
 * (JoinCodeEntry.IsCodeChar) ---- */
const char kCodeAlphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
char g_testCode[8] = "";
/* the harness's test code (hw.txt joincode XXXXX, or "file": sd:/xplay/joincode.txt), else the Switch's software keyboard */
bool ReadJoinCode(char* code) {
    bool ok;
    if (!std::strcmp(g_testCode, "file")) {   /* tests: written by the harness once the page's lobby exists */
        ok = false;
        nn::fs::FileHandle h;
        if (nn::fs::OpenFile(&h, "sd:/xplay/joincode.txt", nn::fs::OpenMode_Read) == 0) {
            char buf[8] = {};
            if (nn::fs::ReadFile(h, 0, buf, 5) == 0 && std::strlen(buf) == 5) { std::memcpy(code, buf, 5); code[5] = 0; ok = true; }
            nn::fs::CloseFile(h);
        }
    } else if (g_testCode[0]) { std::snprintf(code, 8, "%s", g_testCode); ok = true; }
    else ok = swkbd::ReadCode(code, 5, u"ENTER CODE", kCodeAlphabet);
    return ok;
}

void* g_menu = nullptr;   /* the main menu of the last frame (BrowserRows) */

/* ---- test hook (TFEX-DIFF, tests only, like the join code's): hw.txt lobbymode / lobbymap / lobbylength preset TF.EX's LobbyBuilder
 * when it is made for a new lobby. The mode through TF.EX's own LobbyVersusModeButton.SelectMode (its Team Deathmatch player minimum,
 * its sides); the map and the length as their buttons write them (ownLobby.GameData.MapId / MatchLength). Unset: TF.EX's defaults. ---- */
constexpr int kUnset = -1000;
int g_presetMode = kUnset, g_presetMap = kUnset, g_presetLength = kUnset, g_presetMax = kUnset, g_presetBestOf = kUnset;
char g_presetVariants[160] = "";   /* hw.txt lobbyvariants TITLE|-TITLE...: the variant toggles a player would switch on (or off, -) in the builder */
/* the builder's variants (TF.EX's lobby takes the VariantToggles whose Variant.Value is on at START): VersusMatchSettings.Variants */
void ApplyVariantPresets() {
    void* ms = Static<void*>(MainMenu::VersusMatchSettings);
    void* mv = ms ? At<void*>(ms, gen::bind::f_MatchSettings_Variants) : nullptr;
    auto vs = gen::GameArr<void*>(mv ? At<void*>(mv, gen::bind::f_MatchVariants_Variants) : nullptr, gen::bind::m_Array_get_Length);
    /* every title, in log lines of ~200 characters (lobby variants travel by title: one only one side has is dropped on the other) */
    char all[256] = ""; size_t at = 0; int on = 0, part = 0;
    for (int i = 0; i < vs.len; i++) {
        void* title = vs[i] ? At<void*>(vs[i], gen::bind::f_Variant_Title) : nullptr;
        if (!title) continue;
        char t[64] = ""; int n = At<int32_t>(title, 0x10); const char16_t* c = reinterpret_cast<const char16_t*>(reinterpret_cast<uintptr_t>(title) + 0x14);
        for (int k = 0; k < n && k < 63; k++) t[k] = (char)c[k];
        if (at + std::strlen(t) + 2 > 200) { hw::Log("[screens] variant titles (%d), part %d: %s", vs.len, ++part, all); at = 0; all[0] = 0; }
        at += std::snprintf(all + at, sizeof all - at, "%s%s", at ? "|" : "", t);
        const char* p = g_presetVariants;
        while (*p) {
            const char* e = std::strchr(p, '|'); size_t len = e ? (size_t)(e - p) : std::strlen(p);
            bool off = len > 0 && *p == '-';   /* "-TITLE": switched off (TF.EX's tournament rules turn some on entering NETPLAY) */
            const char* q = off ? p + 1 : p; size_t ql = off ? len - 1 : len;
            if (ql == std::strlen(t) && !std::strncmp(q, t, ql)) { Fn<void (*)(void*, bool)>(gen::bind::m_Variant_set_Value_953d)(vs[i], !off); on += off ? 0 : 1; }
            if (!e) break; p = e + 1;
        }
    }
    if (at) hw::Log("[screens] variant titles (%d), part %d: %s", vs.len, ++part, all);
    hw::Log("[screens] test preset: %d variant(s) on (%s) of %d", on, g_presetVariants, vs.len);
}
void ApplyBuilderPresets(void* menu) {
    if (g_presetVariants[0] && !gen::Auto_LobbyBuilderContext::get_IsEditing()) ApplyVariantPresets();
    if (g_presetMode == kUnset && g_presetMap == kUnset && g_presetLength == kUnset && g_presetMax == kUnset && g_presetBestOf == kUnset) return;
    if (gen::Auto_LobbyBuilderContext::get_IsEditing()) return;   /* editing an existing lobby: its own settings */
    gen::ws::Lobby* l = gen::svc::GetOwnLobby();
    gen::ws::GameData* d = l ? gen::RefOf(l->GameData) : nullptr;
    if (!d) { hw::Log("[screens] test preset: no own lobby to preset"); return; }
    if (g_presetMode != kUnset && d->Mode != g_presetMode) {   /* the GAME MODE button's option */
        void* t = gen::LobbyVersusModeButton::RuntimeType();
        void* b = t ? gen::Auto_TowerFallSceneExtensions::Get(menu, t) : nullptr;
        if (b) gen::LobbyVersusModeButton::SelectMode(b, g_presetMode); else d->Mode = g_presetMode;
    }
    if (g_presetMap != kUnset) d->MapId = g_presetMap;
    if (g_presetLength != kUnset) d->MatchLength = g_presetLength;
    if (g_presetMax != kUnset) l->MaxPlayers = g_presetMax;   /* LOBBY SIZE (LobbyVersusPlayerCountButton reads it) */
    if (g_presetBestOf != kUnset) d->BestOf = g_presetBestOf;   /* SERIES (LobbyVersusSeriesButton.Select writes it; START's EnforceSeriesRules applies the rest) */
    hw::Log("[screens] test preset: LobbyBuilder mode %d, map %d, length %d, max players %d, best of %d", d->Mode, d->MapId, d->MatchLength, l->MaxPlayers, d->BestOf);
}
}

void SetBuilderVariants(const char* titles) {   /* hw.txt lobbyvariants (tests) */
    std::snprintf(g_presetVariants, sizeof g_presetVariants, "%s", titles);
    for (char* q = g_presetVariants; *q; q++) if (*q == '\r' || *q == '\n') *q = 0;
}
void SetBuilderPreset(char what, int value) {   /* hw.txt lobbymode ('m') / lobbymap ('p') / lobbylength ('l') / lobbymax ('x') / lobbyseries ('s') */
    (what == 'm' ? g_presetMode : what == 'p' ? g_presetMap : what == 'x' ? g_presetMax : what == 's' ? g_presetBestOf : g_presetLength) = value;
}

HOOK_DEFINE_TRAMPOLINE(CallStateFuncHook) {
    static void Callback(void* menu, void* name, int32_t state) {   /* TF.EX's prefix: false skips the original (its own states never reach vanilla) */
        if (ModsMenu::CallStateFunc(menu, name, state)) return;   /* FortRise's registered state (MODS): its CustomMenuState */
        if (gen::NetplayMenus::MainMenu_CallStateFunc(menu, name, state)) Orig(menu, name, state);
        gen::NetplayMenus::MainMenu_CallStateFunc_Postfix(menu, name, state);   /* TF.EX's postfix (Harmony runs it either way): the EDIT LOBBY guide */
        if (state == MainMenu::Rollcall && IsName(name, "Create")) {   /* (the first few: the guide is made when the host may edit the lobby) */
            static int n = 0;
            if (n++ < 4) hw::Log("[screens] roll call: TF.EX's CallStateFunc postfix ran (EDIT LOBBY guide %s)",
                                 gen::MatchmakingService::CanEditLobbySettings(gen::svc::GenMM()) ? "made: the host can edit the lobby" : "not made: not the lobby's host");
        }
        if (state == LobbyBuilder && IsName(name, "Create")) ApplyBuilderPresets(menu);   /* tests only */
    }
};

void* KeyboardCode() {
    char code[8] = {};
    if (!ReadJoinCode(code)) return gen::ManagedUtf8("", 0);
    hw::Log("[screens] join code from the keyboard: %s", code);
    return gen::ManagedUtf8(code, (int)std::strlen(code));
}
void CodeEntryConfirm(void* entry) {
    if (gen::Len(gen::Auto_JoinCodeEntry::S(entry)->code) == 0) gen::Auto_JoinCodeEntry::Paste(entry);
    gen::Auto_JoinCodeEntry::Submit(entry);
}
bool IsCustom(int s) { return s == LobbyBrowser || s == LobbyBuilder || s == NetplaySelect || s == PrivateSelect || s == PrivateJoinCode || s == QuickPlaySearch; }
/* the auto-UI's "lobbies listed" (tests): TF.EX's browser rows are LobbyInfos entities in the menu */
int BrowserRows() {
    void* t = gen::LobbyInfos::RuntimeType();
    return g_menu && t && gen::Auto_TowerFallSceneExtensions::Get(g_menu, t) ? 1 : 0;
}
void Install() {
    CallStateFuncHook::InstallAtOffset(CallStateFunc);
}
void SetTestJoinCode(const char* code) { std::snprintf(g_testCode, sizeof g_testCode, "%s", code); }

/* each frame on the main menu: TF.EX's Update postfix, and the private lobby's code (TFEX-DIFF) */
void Frame(void* menu) {
    if (!menu) return;
    g_menu = menu;
    gen::NetplayMenus::MainMenu_Update_Postfix(menu);
}
}  // namespace tfex::NetplayScreens
