/* FrModsBoot.hpp: FortRise content mods from sd:/xplay/mods, loaded once by the translated driver (glue SwitchMods.LoadAll). */
#include "FrModsBoot.hpp"
#include "lib.hpp"
#include <lib/util/sys/modules.hpp>
#include "../hwlog.hpp"
#include "gen/tfexgen_GlueFrMods.hpp"
#include "gen/tfexgen_hooks.hpp"
#include "gen/tfexgen_TfexModule.hpp"
#include "gen/tfexgen_codemods.hpp"
#include "runtime/tfexgen_mods.hpp"
#include "modcatalog.gen.hpp"
#include <strings.h>
#include "lib/hook/nx64/impl.hpp"   /* TrampolinesUsed: each mod's hooks */
#include "runtime/tfexgen_code_rt.hpp"
#include "../pools.hpp"
#include "nn/fs.hpp"
#include "gen/tfexgen_binding.hpp"
#include "runtime/tfexgen_frmods.hpp"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <strings.h>

namespace pcgameplay { bool On(); }
namespace hangwatch { void Watch(const char* name); void Unwatch(); }   /* hangwatch.cpp */


namespace tfex::frmods {
namespace {
/* TowerFall.GameData.Load (tools/re sigs): TFGame.Load's step right after ArcherData.Initialize (+0xb7847c in the load function). FortRise
 * loads content mods there (its TFGame.Load: GameData.Load, then RiseCore.Initialize, then Arrow / ... / Particles.Initialize, whose
 * per-archer tables, Particles.Dash, are sized by ArcherData.Archers): the same place here, so they see the mod archers too */
constexpr uintptr_t kGameDataLoad = 0xa46740;
constexpr uint32_t kGameDataLoadWord = 0xd10203ff;   /* its first instruction (sub sp, sp, #0x80): the build this was read from */
bool g_loaded = false;

/* TF.EX's module, constructed as FortRise does once the mods are loaded (TFEXModModule's constructor: RegisterAndLoad, then
 * OnVariantsRegister, before any menu makes a MatchVariants); its OnInitialize runs once its settings exist (MatchmakingService.cpp) */
void TfexBoot() {
    void* content = tfex::gen::GlueFrMods::RegistryMod(tfex::gen::GlueFrMods::TfexRegistry());
    tfex::gen::TfexModule::RegisterAndLoad(content, nullptr, content, nullptr);
    tfex::gen::TfexModule::OnVariantsRegister(content, nullptr);
    hw::Log("[frmods] TF.EX's module constructed: icon %s, right-stick variant %s", tfex::gen::TfexModule::get_InternetIcon() ? "registered" : "NOT registered",
            tfex::gen::TfexModule::get_RightStickVariant() ? "registered" : "NOT registered");
}

/* The code mods compiled into the module, by their meta.json name, booted in FortRise's order (ModuleManager: ModOrder by meta.json
 * priority, Lowest first, as seen on the PC: Teams+ (Lowest) before TF.EX (Low); a mod waits for its dependencies and optional
 * dependencies that are there). A mod not in the registry (its SD folder absent, or kept out by the catalog check) isn't booted.
 * TF.EX here; every catalog mod (mods/<name>/mod.toml, its `boot`) from the generated kCatalog. */
struct CodeMod { const char* name; void (*boot)(); };
constexpr CodeMod kHandMods[] = { {"TF.EX", TfexBoot} };
constexpr int kHandCount = (int)(sizeof kHandMods / sizeof *kHandMods);
CodeMod kCodeMods[kHandCount + tfex::mods::kCatalogCount];
int kCodeModCount = 0;
void CodeModTable() {
    if (kCodeModCount) return;
    for (const CodeMod& m : kHandMods) kCodeMods[kCodeModCount++] = m;
    for (int i = 0; i < tfex::mods::kCatalogCount; i++)
        if (tfex::mods::kCatalog[i].boot) kCodeMods[kCodeModCount++] = { tfex::mods::kCatalog[i].name, tfex::mods::kCatalog[i].boot };
}

int IndexOf(const char* name) {
    for (int i = 0; i < kCodeModCount; i++) if (std::strcmp(kCodeMods[i].name, name) == 0) return i;
    return -1;
}
/* FortRise's load order over every loaded mod (ModuleManager.LoadMods: ModOrder by priority, Lowest first; a mod waits until its
 * dependencies and the optional ones that are there have loaded), booting the code mods as their turn comes. The mods without code
 * here (TF.State, FortRise.Content, the content folders) take their turn too: on the PC TF.EX (Low) waits for TF.State (High), so the
 * ordinary mods (Normal) load before it, and their game modes come before TF.EX's Netplay mode in FortRise's list */
void BootCodeMods() {
    CodeModTable();
    const int n = tfex::mods::Count();
    bool done[tfex::mods::kMaxMods] = {};
    int left = 0;
    for (int i = 0; i < n; i++) if (tfex::mods::At(i)->loaded) left++; else done[i] = true;
    auto indexOf = [&](const char* name) { for (int i = 0; i < n; i++) if (tfex::mods::At(i)->loaded && !strcasecmp(tfex::mods::At(i)->name, name)) return i; return -1; };
    char order[256] = ""; int w = 0;
    while (left > 0) {
        int pick = -1;
        for (int i = 0; i < n; i++) {
            if (done[i]) continue;
            const tfex::mods::Mod* m = tfex::mods::At(i);
            bool ready = true;
            for (int k = 0; k < m->depCount + m->optionalCount && ready; k++) {
                int d = indexOf(k < m->depCount ? m->deps[k].name : m->optional[k - m->depCount].name);
                ready = d < 0 || done[d];   /* (a dependency not on the Switch, TF.Replay: not waited for) */
            }
            if (ready && (pick < 0 || m->priority < tfex::mods::At(pick)->priority)) pick = i;
        }
        if (pick < 0) {   /* a dependency cycle: the rest in registry order */
            hw::Log("[frmods] ERROR: the mods' dependencies make a cycle: the rest load in registry order");
            for (int i = 0; i < n; i++) if (!done[i]) { pick = i; break; }
        }
        done[pick] = true; left--;
        int c = IndexOf(tfex::mods::At(pick)->name);
        if (c < 0) continue;   /* no code to boot */
        w += std::snprintf(order + w, sizeof order - w, "%s%s", w ? ", " : "", kCodeMods[c].name);
        int before = exl::hook::nx64::TrampolinesUsed();
        kCodeMods[c].boot();
        int used = exl::hook::nx64::TrampolinesUsed();
        hw::Log("[mods] %s booted: %d hook trampolines (pool %d of %d)", kCodeMods[c].name, used - before, used,
                exl::hook::nx64::TrampolinesMax());
    }
    hw::Log("[frmods] code mods booted in FortRise's order: %s", order);
}

/* The catalog's `replaces` (mods/<name>/mod.toml): a loaded catalog mod's own texture (<mod>/<with>, from its folder's atlas)
 * in place of the game's `id`, before the mods boot */
void CatalogReplaces() {
    for (int c = 0; c < tfex::mods::kCatalogCount; c++) {
        const tfex::mods::CatalogMod& k = tfex::mods::kCatalog[c];
        if (!k.replaceCount || !tfex::mods::Loaded(k.name)) continue;
        int ok = 0;
        for (int i = 0; i < k.replaceCount; i++) {
            const tfex::mods::CatalogReplace& r = k.replaces[i];
            char mod[128]; int n = std::snprintf(mod, sizeof mod, "%s/%s", k.name, r.with);
            if (tfex::gen::GlueFrMods::ReplaceSubtexture(tfex::gen::ManagedUtf8(r.id, (int)std::strlen(r.id)), tfex::gen::ManagedUtf8(mod, n), r.atlas)) ok++;
            else hw::Log("[mods] ERROR: %s: its folder's atlas has no %s: the game's %s stays", k.displayName, r.with, r.id);
        }
        hw::Log("[mods] %s: %d of %d game textures replaced by its own", k.displayName, ok, k.replaceCount);
    }
}

/* Mod files the game's XML loader (Calc.LoadXML +0x90c570: levels, tower data) reads from a mod's folder on the SD card:
 *  - FortRise's mod paths, mod:<name>/<path in the mod> (a FortRise mod's RootPath; WiderSet's wide levels): any loaded mod's folder;
 *  - the catalog's `overlay`: while a catalog mod is loaded, the game files its patterns match ('*' = one path segment).
 * '\\' and '/' are the same separator. */
bool Matches(const char* pat, const char* path) {
    for (;;) {
        if (*pat == '*') { pat++; while (*path && *path != '/') path++; continue; }
        if (!*pat || !*path) return !*pat && !*path;
        if (*pat != *path) return false;
        pat++; path++;
    }
}
HOOK_DEFINE_TRAMPOLINE(OverlayXmlHook) {
    static void* Callback(void* path) {
        if (path && g_loaded) {
            char p[224]; int n = tfex::gen::Utf8Of(path, p, sizeof p);
            for (int i = 0; i < n; i++) if (p[i] == '\\') p[i] = '/';
            if (std::strncmp(p, "mod:", 4) == 0) {   /* FortRise's mod path */
                char name[64]; int k = 0;
                for (const char* c = p + 4; *c && *c != '/' && k < 63; c++) name[k++] = *c;
                name[k] = 0;
                const tfex::mods::Mod* m = p[4 + k] == '/' ? tfex::mods::Loaded(name) : nullptr;
                if (!m || m->builtin) {
                    static int said = 0;
                    if (said++ < 5) hw::Log("[mods] %s: no loaded mod folder %s (the game's loader gets the path as it is)", p, name);
                } else if (void* doc = OverlayDoc(*m, m->displayName, p + 5 + k)) return doc;
            }
            for (int c = 0; c < tfex::mods::kCatalogCount; c++) {
                const tfex::mods::CatalogMod& k = tfex::mods::kCatalog[c];
                bool hit = false;
                for (int o = 0; o < k.overlayCount && !hit; o++) hit = Matches(k.overlay[o], p);
                const tfex::mods::Mod* m = hit ? tfex::mods::Loaded(k.name) : nullptr;
                if (!m) continue;
                if (void* doc = OverlayDoc(*m, k.displayName, p)) return doc;
            }
        }
        return Orig(path);
    }
    /* the folder's copy as an XmlDocument (as TF.EX's in-memory scenario maps), or null (logged: the game's own file then) */
    static void* OverlayDoc(const tfex::mods::Mod& m, const char* display, const char* rel) {
        static char text[kOverlayMax];
        static int served = 0, missing = 0;
        char file[256]; std::snprintf(file, sizeof file, "sd:/xplay/mods/%s/%s", m.folder, rel);
        nn::fs::FileHandle h;
        if (nn::fs::OpenFile(&h, file, nn::fs::OpenMode_Read) != 0) {
            if (missing++ < 5) hw::Log("[mods] %s: its folder has no %s: the game's own file", display, rel);
            return nullptr;
        }
        long sz = 0; nn::fs::GetFileSize(&sz, h);
        if (sz <= 0 || sz >= (long)sizeof text) {
            nn::fs::CloseFile(h);
            pools::Exhausted("overlay file buffer", (int)sizeof text, file);
            return nullptr;
        }
        bool ok = nn::fs::ReadFile(h, 0, text, (size_t)sz) == 0;
        nn::fs::CloseFile(h);
        if (!ok) { hw::Log("[mods] ERROR: %s: can't read %s", display, file); return nullptr; }
        /* (a UTF-8 byte-order mark: the game's file loader skips it, XmlDocument.LoadXml takes it for text before the root and throws) */
        int bom = sz >= 3 && (unsigned char)text[0] == 0xef && (unsigned char)text[1] == 0xbb && (unsigned char)text[2] == 0xbf ? 3 : 0;
        void* doc = tfex::gen::Fn<void* (*)(void*)>(tfex::gen::bind::m_XmlDocument__ctor_0)(nullptr);
        if (doc) tfex::gen::Virt<void>(doc, tfex::gen::bind::v_XmlDocument_LoadXml_6538, tfex::gen::ManagedUtf8(text + bom, (int)sz - bom));
        if (served++ < 5 || served % 50 == 0) hw::Log("[mods] %s: %s from its folder (%ld bytes; %d served)", display, rel, sz, served);
        return doc;
    }
    static constexpr int kOverlayMax = 256 * 1024;
};

HOOK_DEFINE_TRAMPOLINE(GameDataLoadHook) {
    static void Callback() {
        Orig();
        if (g_loaded) return;   /* (the game's other caller, a reload: the mods are in already) */
        g_loaded = true;
        hw::Log("[frmods] loading FortRise content mods (after GameData.Load, as FortRise)");
        hangwatch::Watch("the mod loader's thread");   /* (a freeze while loading shows its stack too) */
        tfex::gen::GlueFrMods::LoadAll();
        hangwatch::Unwatch();
        CatalogReplaces();
        BootCodeMods();
    }
};
/* FortRise's PlayerHair drawing (gen Fr_PlayerHair: Render, RenderBlack, RenderOutline) for a hair with FortRise hair data (an archer
 * mod's <Hair>, a streamed skin's: its ArcherData's ExtraHairData); the game's own drawing for the rest. TFEX-DIFF: the Switch's game
 * draws Madeline's hair itself (TENACIOUS TRAVELER, a Switch-only archer: her colour and dash states), which FortRise's body (the PC's
 * hair, white by default) would replace. Same for the PC's archers with hair (ANCIENT EXILE): the game's body, as before. */
bool FortRiseHair(void* hair) {
    using namespace tfex::gen;
    void* follow = hair ? gen::Fld<void*>(hair, bind::f_PlayerHair_Follow) : nullptr;
    void* player = follow ? gen::As(follow, gen::TypeOf(bind::t_Player)) : nullptr;
    void* archer = player ? gen::Fn<void* (*)(void*)>(bind::m_Player_get_ArcherData)(player) : nullptr;
    if (archer) return gen::frmods::ExtraHairData(archer).HasValue;
    /* a corpse's hair: FortRise's data on it. A player's corpse takes the player's own hair (the game's PlayerCorpse(Player, int): Hair =
     * player.Hair, Follow = the corpse; FortRise leaves it as is), whose Color patch_Player.Added_Postfix set; a skeleton's corpse gets
     * its own (CorpseHook) */
    if (!follow || !gen::As(follow, gen::TypeOf(bind::t_PlayerCorpse)) || !gen::SideGetOpt<gen::Color>("Color", hair).has) return false;
    static int said = 0;
    if (said++ < 1) hw::Log("[frmods] hair: FortRise's drawing for a mod archer's corpse");
    return true;
}
/* FortRise's patch_PlayerCorpse(Vector2, ArcherData, ...): the game's constructor (+0xafa140: the corpse, its hair as the Switch makes it),
 * then FortRise's hair data onto that hair (glue SwitchMods.CorpseHair, TFEX-DIFF S-native there). this = null allocates: the constructor
 * returns the object. Position (a Vector2) comes in s0 / s1. The game makes these for Dark World's skeletons (Skeleton.Die). */
HOOK_DEFINE_TRAMPOLINE(CorpseHook) {
    static void* Callback(void* self, float x, float y, void* archerData, int32_t team, int32_t facing, int32_t player, int32_t killer) {
        void* corpse = Orig(self, x, y, archerData, team, facing, player, killer);
        if (corpse && archerData && tfex::gen::frmods::ExtraHairData(archerData).HasValue) {
            tfex::gen::GlueFrMods::CorpseHair(corpse, archerData);
            static int said = 0;
            if (said++ < 3) hw::Log("[frmods] hair: a corpse made from a mod archer's data has FortRise's hair (%d)", said);
        }
        return corpse;
    }
};
/* MEXICAN PUPPET (glue SwitchMods.NativeReskin, TFEX-DIFF S-native): local play shows it in the game's Prancing Puppet alt slot, online it
 * is its own archer. The roll call skips an archer the save data says is locked (RollcallElement.ChangeSelectionLeft / Right:
 * CharacterTaken || !GetArcherUnlocked), so its own archer is "locked" in local play; the alt has no per-archer check (Alt flips any
 * archer), so online an archer 6 element left on its alt goes back to the normal costume after the roll call's update. */
constexpr uintptr_t kGetArcherUnlocked = 0xb9a620;   /* UnlockData.GetArcherUnlocked(int, bool): the Switch's (a second argument) */
HOOK_DEFINE_TRAMPOLINE(ArcherUnlockedHook) {
    static bool Callback(void* self, int32_t index, bool flag) {
        int32_t own = tfex::gen::GlueFrMods::ReskinOwnArcher();
        if (own >= 0 && index == own && !pcgameplay::On()) return false;   /* local play: the game's alt slot is MEXICAN PUPPET */
        return Orig(self, index, flag);
    }
};
void RollcallShell(tfex::gen::hooks::RollcallElement_Update& c) {
    c();
    int32_t game = tfex::gen::GlueFrMods::ReskinGameArcher();
    if (game < 0 || !pcgameplay::On() || !c.self) return;
    using namespace tfex::gen;
    if (Fn<int32_t (*)(void*)>(bind::m_RollcallElement_get_CharacterIndex)(c.self) != game || Fld<int32_t>(c.self, bind::f_RollcallElement_archerType) != 1) return;
    Fld<int32_t>(c.self, bind::f_RollcallElement_archerType) = 0;   /* online: not the game's alt (MEXICAN PUPPET is its own archer) */
    if (void* portrait = Fld<void*>(c.self, bind::f_RollcallElement_portrait))
        Fn<void (*)(void*, int32_t, int32_t, int32_t)>(bind::m_ArcherPortrait_SetCharacter_021b)(portrait, game, 0, -1);
    static int said = 0;
    if (said++ < 3) hw::Log("[frmods] online: archer %d's alt can't be picked (MEXICAN PUPPET is its own archer online)", (int)game);
}
/* A mod's own music (glue SwitchMods' music part, TFEX-DIFF S-native: FortRise replaces the game's music engine; here a mod track plays
 * beside it as a SoundEffectInstance): the game's Music.Play / PlayImmediate / Stop end a playing mod track (FortRise's replacements stop
 * theirs), and Music.Update plays the track's PlayNext queue once it ends (FortRise's Update). Monocle.Music's statics (tools/re classmeta). */
constexpr uintptr_t kMusicUpdate = 0x941090, kMusicPlay = 0x9410d0, kMusicPlayImmediate = 0x941180, kMusicStop = 0x941240;
HOOK_DEFINE_TRAMPOLINE(MusicPlayHook) { static void Callback(void* name) { tfex::gen::GlueFrMods::GameMusic(); Orig(name); } };
HOOK_DEFINE_TRAMPOLINE(MusicPlayImmediateHook) { static void Callback(void* name) { tfex::gen::GlueFrMods::GameMusic(); Orig(name); } };
HOOK_DEFINE_TRAMPOLINE(MusicStopHook) { static void Callback() { tfex::gen::GlueFrMods::GameMusic(); Orig(); } };
HOOK_DEFINE_TRAMPOLINE(MusicUpdateHook) { static void Callback() { Orig(); tfex::gen::GlueFrMods::MusicUpdate(); } };
template <class C> void HairShell(C& c) {
    static int logged = 0;
    bool fr = FortRiseHair(c.self);
    if (fr && logged < 3) { logged++; hw::Log("[frmods] hair: FortRise's drawing for a mod archer's hair (%d)", logged); }
    if (fr) c(); else c.Orig();
}
}  // namespace

/* The catalog mods' `early` (mod.toml): at module start, after every built-in hook (main.cpp), folder or not: only what must precede the
 * game's own setup (WiderSet's 8 seats: the player arrays), inert without the folder. Each one's hooks logged as at boot. */
void EarlyMods() {
    for (int c = 0; c < tfex::mods::kCatalogCount; c++) {
        const tfex::mods::CatalogMod& k = tfex::mods::kCatalog[c];
        if (!k.early) continue;
        int before = exl::hook::nx64::TrampolinesUsed();
        k.early();
        hw::Log("[mods] %s early: %d hook trampolines at module start (folder or not)", k.displayName, exl::hook::nx64::TrampolinesUsed() - before);
    }
}

void Install() {
    uint32_t w = *reinterpret_cast<uint32_t*>(exl::util::GetMainModuleInfo().m_Total.m_Start + kGameDataLoad);
    if (w != kGameDataLoadWord) { hw::Log("[frmods] GameData.Load isn't as expected (%08x): content mods not loaded", w); return; }
    GameDataLoadHook::InstallAtOffset(kGameDataLoad);
    OverlayXmlHook::InstallAtOffset(0x90c570);   /* Calc.LoadXML: the catalog's overlay (outermost: pcgameplay's LoadXmlHook is in already) */
    ArcherUnlockedHook::InstallAtOffset(kGetArcherUnlocked);
    CorpseHook::InstallAtOffset(tfex::gen::bind::m_PlayerCorpse__ctor_ca85);
    MusicPlayHook::InstallAtOffset(kMusicPlay);
    MusicPlayImmediateHook::InstallAtOffset(kMusicPlayImmediate);
    MusicStopHook::InstallAtOffset(kMusicStop);
    MusicUpdateHook::InstallAtOffset(kMusicUpdate);
    tfex::gen::hooks::RollcallElement_Update::Wrap(RollcallShell);
    tfex::gen::hooks::PlayerHair_Render::Wrap(HairShell<tfex::gen::hooks::PlayerHair_Render>);
    tfex::gen::hooks::PlayerHair_RenderBlack::Wrap(HairShell<tfex::gen::hooks::PlayerHair_RenderBlack>);
    tfex::gen::hooks::PlayerHair_RenderOutline::Wrap(HairShell<tfex::gen::hooks::PlayerHair_RenderOutline>);
    /* TF.EX's custom-skin patches (ArcherDataPatch, PlayerPatch's and VersusPlayerMatchResultsPatch's SkinSlot scopes) are on with every
     * TF.EX patch (tfex::gen::Install: hooks::EnableAll) */
}
}  // namespace tfex::frmods
