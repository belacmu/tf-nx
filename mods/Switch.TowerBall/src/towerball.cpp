/* TowerBall (mode 6), the catalog mod Switch.TowerBall (mods/Switch.TowerBall: mod.toml, NOTES.md): local play only; online is TF.EX's.
 * Imported from ../combined-mod/exlaunch-overlay/source/program/main.cpp on 2026-10-30 (its source of truth is here now): the hooks
 * unchanged but for the online gate (MM_ONLINE_PASS) and the log (MMLOG: the match log); its instruction patches go through
 * megamod::Patch. Its hooks go in from towerball::Boot (its folder loaded), never at boot. */

#include "lib.hpp"
#include "megamod/megamod.hpp"   /* MMLOG (the match log), the online gate, Patch */
#include "modcatalog.hpp"   /* tfex::mods::RegisterApi (its versus mode for other code) */
#if XPLAY_EXTRAS   /* (megamod.hpp: "TF.EX Switch" builds leave the megamod out) */
#include "towerball.hpp" /* reusable hook layer + validated offset table */
#include "towerball_levels.hpp" /* generated per-tower basket/chest marker tables */
#include "patch/code_patcher.hpp"  /* exl::patch::CodePatcher — instruction patching */
#include <lib/util/sys/modules.hpp> /* exl::util::GetMainModuleInfo (module base) */
#include "tfex/gen/tfexgen_hooks.hpp"
#include "tfex/gen/tfexgen_GlueFrMods.hpp"
#include "pools.hpp"

/* ---- TowerBall gameplay (mode 6) --------------------------------------------
 * ALL game logic lives in towerball_logic.hpp (tb::logic), exlaunch-free and
 * driven through the tb::env function table — the same logic compiles into the
 * macOS host test harness (tools/towerball/host_test.cpp) against a fake world,
 * so state-machine changes are unit-tested WITHOUT running the game.
 * The hooks below are thin adapters: game event -> logic entry point.
 */
#include "towerball_logic.hpp"

static void EnvLog(const char* fmt, long a, long b, long c, long d) {
    MMLOG(fmt, a, b, c, d);
}
static void* EnvGetPlayer(void* level, int idx) {
    return tb::Call<void* (*)(void*, int)>(tb::off::Level_GetPlayer)(level, idx);
}
static void* EnvArrowCreate(int type, void* owner, float x, float y, float dir) {
    return tb::Call<tb::ArrowCreateFn>(tb::off::Arrow_Create)(type, owner, 0, 0, x, y, dir);
}
static bool EnvSceneAdd(void* level, void* entity) { return tb::SceneAdd(level, entity); }
static void EnvRemoveSelf(void* entity) {
    tb::Call<void (*)(void*)>(tb::off::Entity_RemoveSelf)(entity);
}
static void EnvArrowDrop(void* arrow_, int side) {
    tb::CallVirtual<void (*)(void*, int)>(arrow_, tb::slot::Arrow_Drop, side);
}
/* Respawn primitive: new Player at (x,y), FFA-neutral, unfrozen, indicator on
 * (PC RespawnPlayer parity), then Level.Add. Ctor self-allocates on x0==0. */
static void* EnvNewPlayer(void* session, void* level, int idx, int allegiance, float x, float y) {
    tb::Inv2 inv = tb::Call<tb::GetInventoryFn>(tb::off::Session_GetPlayerInventory)(session, idx);
    int hat = tb::Call<tb::GetHatFn>(tb::off::Session_GetSpawnHatState)(session, idx);
    void* p = tb::Call<tb::PlayerCtorFn>(tb::off::Player_Ctor)(
        nullptr, idx, allegiance, allegiance, inv.a, inv.b, (uint64_t)(uint32_t)hat,
        /*frozen=*/0, /*flash=*/0, /*indicator=*/1, x, y);
    if (p && tb::SceneAdd(level, p)) return p;
    return nullptr;
}
static int EnvGoalScore(void* matchSettings) {
    return tb::Call<int (*)(void*)>(tb::off::MatchSettings_GoalScore)(matchSettings);
}
static void EnvDrawRect(float x, float y, float w, float h, uint32_t rgba) {
    tb::Call<tb::DrawRectFn>(tb::off::Draw_Rect)(rgba, x, y, w, h);
}
/* Endgame FX (PC IncreaseScore at goal score): slow-mo + stop the versus music
 * + the final-kill sting. All three lifted from RoundLogic.FinalKill's tail. */
static void EnvSlowMo(void* level) {
    void* orb = tb::RdPtr(level, tb::fld::Level_OrbLogic);
    if (orb) tb::Call<void (*)(void*)>(tb::off::OrbLogic_DoSlowMoKill)(orb);
    void* sess = tb::RdPtr(level, tb::fld::Level_Session);
    void* ms   = sess ? tb::RdPtr(sess, tb::fld::Session_MatchSettings) : nullptr;
    void* ls   = ms ? tb::RdPtr(ms, tb::fld::MatchSettings_LevelSystem) : nullptr;
    if (ls) tb::CallVirtual<void (*)(void*)>(ls, tb::slot::LS_StopVersusMusic);
    /* BRUTE statics are DOUBLY indirected — the data cell holds a pointer to
     * the static's storage, which holds the object (FinalKill tail 0xb3a84c:
     * ldr x8,[cell]; ldr x8,[x8]). Missing the second deref vtable-called the
     * storage holder → the score-5 endgame crash (2026-07-06 21:31). */
    uintptr_t base = exl::util::GetMainModuleInfo().m_Total.m_Start;
    void** store = *reinterpret_cast<void***>(base + tb::off::Sounds_sfx_finalKill);
    void* sfx = store ? *store : nullptr;
    if (sfx) tb::CallVirtual<void (*)(void*, float, float)>(sfx, tb::slot::SFX_Play, 160.0f, 1.0f);
}
static void EnvEndRound(void* session) {
    if (session) tb::Call<void (*)(void*)>(tb::off::Session_EndRound)(session);
}
static void EnvAddScore(void* rl, int scoreIndex, int amount) {
    tb::Call<void (*)(void*, int, int)>(tb::off::RoundLogic_AddScore)(rl, scoreIndex, amount);
}
static int EnvGetHighest(void* session) {
    return tb::Call<int (*)(void*)>(tb::off::Session_GetHighestScore)(session);
}
static void EnvSqueak(float x, float vol) {
    uintptr_t base = exl::util::GetMainModuleInfo().m_Total.m_Start;
    void** store = *reinterpret_cast<void***>(base + tb::off::Sounds_env_arrowToyChar);
    void* sfx = store ? *store : nullptr;   /* BRUTE statics: DOUBLE deref */
    if (sfx) tb::CallVirtual<void (*)(void*, float, float)>(sfx, tb::slot::SFX_Play, x, vol);
}
static void EnvGoalSound(float x) {
    uintptr_t base = exl::util::GetMainModuleInfo().m_Total.m_Start;
    void** store = *reinterpret_cast<void***>(base + tb::off::Sounds_sfx_devTimeFinalDummy);
    void* sfx = store ? *store : nullptr;   /* BRUTE statics: DOUBLE deref */
    if (sfx) tb::CallVirtual<void (*)(void*, float, float)>(sfx, tb::slot::SFX_Play, x, 1.0f);
}
/* ---- Runtime managed strings + subtexture cache -----------------------------
 * Static buffers laid out as Mono String objects; the String vtable is copied
 * lazily from the TOWERBALL mode-name String (ModeName_Table — materialized by
 * the time any mode-6 HUD draws). Strings are used ONLY for Atlas.get_Item
 * lookups (no GC retention), and live outside the GC heap (Boehm ignores them).
 */
struct StaticString {
    void*    vtable;
    void*    sync;
    int32_t  len;
    char16_t chars[48];
};

static void FillStaticString(StaticString& S, const char* txt, void* vt) {
    S.vtable = vt; S.sync = nullptr;
    int n = 0;
    for (const char* c = txt; *c && n < 47; c++) S.chars[n++] = (char16_t)*c;
    S.chars[n] = 0; S.len = n;
}
static StaticString g_texNames[tb::TexKeyCount] = {};
static void*        g_texCache[tb::TexKeyCount] = {};
static const char*  kTexName [tb::TexKeyCount] = {
    "Switch.TowerBall/rim", "Switch.TowerBall/net", "arrows/squeakyArrow", "teamA", "teamB",
    "Switch.TowerBall/rimR", "Switch.TowerBall/netR", "Switch.TowerBall/chest",
    "Switch.TowerBall/netS1", "Switch.TowerBall/netS2", "Switch.TowerBall/netS3",
    "Switch.TowerBall/netRS1", "Switch.TowerBall/netRS2", "Switch.TowerBall/netRS3" };   /* (its folder's: FortRise names a mod's
     * textures <mod>/<name>; the ball by the game's name: the catalog's replaces puts the folder's ball there) */
static const bool   kTexMenu [tb::TexKeyCount] = { false, false, false, true, true, false, false,
                                                   false, false, false, false, false, false, false };

static void* StringVtable() {
    uintptr_t base = exl::util::GetMainModuleInfo().m_Total.m_Start;
    void** table = reinterpret_cast<void**>(base + tb::off::ModeName_Table);
    void* entry = table[6 - 3];
    void* s = entry ? *reinterpret_cast<void**>(entry) : nullptr;
    return s ? *reinterpret_cast<void**>(s) : nullptr;   /* String vtable */
}

static void* TexFor(int key) {
    if (key < 0 || key >= tb::TexKeyCount) return nullptr;
    if (g_texCache[key]) return g_texCache[key];
    void* vt = StringVtable();
    if (!vt) return nullptr;                /* mode UI not materialized yet */
    StaticString& S = g_texNames[key];
    S.vtable = vt; S.sync = nullptr;
    int n = 0;
    for (const char* c = kTexName[key]; *c && n < 47; c++) S.chars[n++] = (char16_t)*c;
    S.chars[n] = 0; S.len = n;
    void* atlas = kTexMenu[key]
        ? tb::Call<void* (*)()>(tb::off::TFGame_get_MenuAtlas)()
        : tb::Call<void* (*)()>(tb::off::TFGame_get_Atlas)();
    if (!atlas) return nullptr;
    void* sub = tb::Call<void* (*)(void*, void*)>(tb::off::Atlas_get_Item)(atlas, &S);
    g_texCache[key] = sub;                  /* atlas retains it; safe to cache */
    return sub;
}

static void EnvDrawTex(int key, float x, float y, uint32_t rgba) {
    void* sub = TexFor(key);
    if (!sub) return;
    tb::Call<void (*)(void*, uint32_t, float, float)>(tb::off::Draw_OutlineTextureCentered)(
        sub, rgba, x, y);
}

/* Text draws (SlamNotification "DUNK!"): same StaticString trick, drawn with
 * Draw.OutlineTextCentered(font, str, pos, color, scale) = 0x9245a0. */
static StaticString g_textStr[tb::TextKeyCount] = {};
static const char*  kTextName[tb::TextKeyCount] = { "DUNK!", "OVERTIME!" };

static void EnvDrawText(int key, float x, float y, uint32_t rgba, float scale) {
    if (key < 0 || key >= tb::TextKeyCount) return;
    void* vt = StringVtable();
    if (!vt) return;
    StaticString& S = g_textStr[key];
    if (S.vtable != vt) {
        S.vtable = vt; S.sync = nullptr;
        int n = 0;
        for (const char* c = kTextName[key]; *c && n < 23; c++) S.chars[n++] = (char16_t)*c;
        S.chars[n] = 0; S.len = n;
    }
    void* font = tb::Call<void* (*)()>(tb::off::TFGame_get_Font)();
    if (!font) return;
    /* caller sites zero-extend the packed Color into the full x2 — match that */
    tb::Call<void (*)(void*, void*, uint64_t, float, float, float)>(
        tb::off::Draw_OutlineTextCentered_Str)(font, &S, (uint64_t)rgba, x, y, scale);
}

static void EnvSpawnTreasure(void* rl) {
    tb::Call<void (*)(void*)>(tb::off::RL_SpawnTreasureChestsVersus)(rl);
}
static void EnvReleaseArrows(void* cushion, float vx, float vy) {
    tb::Call<void (*)(void*, float, float)>(tb::off::ArrowCushion_ReleaseArrows)(cushion, vx, vy);
}
static void EnvShootUpdate(void* arrow_) {
    tb::CallVirtual<void (*)(void*)>(arrow_, tb::slot::Arrow_ShootUpdate);
}
static void EnvFlash(void* entity, int frames) {
    if (entity) tb::Call<void (*)(void*, int, void*)>(tb::off::LevelEntity_Flash)(
        entity, frames, nullptr);
}
static void* EnvCollideAt(void* level, float x, float y, int tags) {
    return tb::Call<void* (*)(void*, int, float, float)>(
        tb::off::Level_CollideFirst_Point)(level, tags, x, y);
}
static void EnvExplode(void* level, float x, float y, int playerIndex) {
    tb::Call<tb::ExplosionSpawnFn>(tb::off::Explosion_Spawn)(level, playerIndex, 0, 0, 0, x, y);
}
static int EnvGetAllegiance(void* session, int idx) {
    void* ms = tb::RdPtr(session, tb::fld::Session_MatchSettings);
    void* teams = ms ? tb::RdPtr(ms, tb::fld::MatchSettings_Teams) : nullptr;
    if (!teams) return -1;
    return tb::Call<int (*)(void*, int)>(tb::off::Teams_GetAllegiance)(teams, idx);
}
static bool EnvArrowCheck(void* entity, void* arrow_) {
    return tb::Call<bool (*)(void*, void*)>(tb::off::LevelEntity_ArrowCheck)(entity, arrow_);
}
static void EnvSetPlayerState(void* player, int state) {
    tb::Call<void (*)(void*, int)>(tb::off::Player_set_State)(player, state);
}
static void EnvStartWiggle(void* arrow_) {
    tb::Call<void (*)(void*)>(tb::off::Arrow_StartWiggle)(arrow_);
}
static void EnvRemoveIndicator(void* player) {
    tb::Call<void (*)(void*)>(tb::off::Player_RemoveIndicator)(player);
}
/* v3 archer bow-quirk: 3 special skins keep the bow up while aiming even when
 * holding the ball. char 8 alt 0, char 6 alt 1, char 7 alt 1. */
static bool EnvAimingBowSkin(void* player) {
    int ci  = tb::Call<int (*)(void*)>(tb::off::Player_get_CharacterIndex)(player);
    int alt = tb::Call<int (*)(void*)>(tb::off::Player_get_AltSelect)(player);
    bool quirk = (ci == 8 && alt == 0) || (ci == 6 && alt == 1) || (ci == 7 && alt == 1);
    if (!quirk) return false;
    return tb::RdU8(player, tb::fld::Player_Aiming) != 0;
}
/* v3 ball bounce dust: Particles.PlayerDust[3] through the game's own
 * ParticleSystem.Emit (positions randomized by the game's Calc.Random). */
static void EnvEmitDust(void* level, int amount, float px, float py,
                        float rx, float ry, float dir, bool withDir) {
    if (!level) return;
    void* ps = tb::RdPtr(level, tb::fld::Level_Particles);
    if (!ps) return;
    uintptr_t base = exl::util::GetMainModuleInfo().m_Total.m_Start;
    void** cell = *reinterpret_cast<void***>(base + tb::off::Particles_PlayerDust);
    void* arr = cell ? *cell : nullptr;          /* ParticleType[] (BRUTE double deref) */
    if (!arr) return;
    int32_t* lenp = reinterpret_cast<int32_t*>(tb::RdPtr(arr, tb::fld::Array_LenPtr));
    void** data   = reinterpret_cast<void**>(tb::RdPtr(arr, tb::fld::Array_Data));
    if (!lenp || !data || *lenp <= 3) return;
    void* type = data[3];                        /* v3: PlayerDust[3] */
    if (!type) return;
    if (withDir)
        tb::Call<void (*)(void*, void*, int, float, float, float, float, float)>(
            tb::off::ParticleSystem_Emit5)(ps, type, amount, px, py, rx, ry, dir);
    else
        tb::Call<void (*)(void*, void*, int, float, float, float, float)>(
            tb::off::ParticleSystem_Emit4)(ps, type, amount, px, py, rx, ry);
}
/* v3 win flow: LightingLayer.SetSpotlight(winners). The setter just stores the
 * array ([layer+0xa8]) and the render loop foreach's it — served by the SAME
 * fake-array pattern as the variants row (header cloned from a real managed
 * array; data/len pointers redirected to statics). */
static uint8_t g_spotArr[0x30];
static void*   g_spotPtrs[8];   /* combined build: towerball scales to 4v4 */
static int32_t g_spotCount = 0;
static void EnvSpotlight(void* level, void* session, int team) {
    if (!level || !session) return;
    int n = 0;
    for (int i = 0; i < tb::logic::kMaxPlayers; i++) {
        if (EnvGetAllegiance(session, i) != team) continue;
        void* e = tb::Call<void* (*)(void*, int)>(tb::off::Level_GetPlayerOrCorpse)(level, i);
        if (e) g_spotPtrs[n++] = e;
    }
    if (n == 0) return;                     /* nobody to light: keep vanilla lighting */
    void* donor = tb::RdPtr(session, tb::fld::Session_Scores);   /* any real array header */
    void* lighting = tb::RdPtr(level, tb::fld::Level_LightingLayer);
    if (!donor || !lighting) return;
    memcpy(g_spotArr, donor, 0x30);
    *reinterpret_cast<void**>(g_spotArr + tb::fld::Array_Data)   = g_spotPtrs;
    *reinterpret_cast<void**>(g_spotArr + tb::fld::Array_LenPtr) = &g_spotCount;
    g_spotCount = n;
    tb::Call<void (*)(void*, void*)>(tb::off::LightingLayer_SetSpotlight)(lighting, g_spotArr);
    MMLOG("[towerball] spotlight on team %d (%d entities)", team, n);
}

/* v3 IncreaseScore rumble: PlayerInputs[scorer].Rumble(1f, 10). PlayerInputs
 * cell 0x4791fa8 (BRUTE static: cell -> storage -> array obj); Rumble is
 * vtable slot 0x28 (s0=strength, w1=frames — C ABI puts the float in s0). */
static void EnvRumble(int playerIndex, float strength, int frames) {
    uintptr_t base = exl::util::GetMainModuleInfo().m_Total.m_Start;
    void** storage = *reinterpret_cast<void***>(base + 0x4791fa8);
    if (!storage || !*storage) return;
    void** arr = reinterpret_cast<void**>(*storage);
    int32_t* lenp = *reinterpret_cast<int32_t**>(reinterpret_cast<uint8_t*>(arr) + tb::fld::Array_LenPtr);
    void** data = *reinterpret_cast<void***>(reinterpret_cast<uint8_t*>(arr) + tb::fld::Array_Data);
    if (!lenp || !data || playerIndex < 0 || playerIndex >= *lenp) return;
    void* input = data[playerIndex];
    if (!input) return;
    tb::CallVirtual<void (*)(void*, float, int)>(input, 0x28, strength, frames);
}

namespace tb { Env env = { &EnvGetPlayer, &EnvArrowCreate, &EnvSceneAdd,
                           &EnvRemoveSelf, &EnvArrowDrop, &EnvLog,
                           &EnvNewPlayer, &EnvGoalScore, &EnvGetAllegiance,
                           &EnvDrawRect, &EnvSlowMo, &EnvExplode, &EnvDrawTex,
                           &EnvEndRound, &EnvSqueak, &EnvAddScore, &EnvGetHighest,
                           &EnvDrawText, &EnvCollideAt, &EnvFlash, &EnvShootUpdate, &EnvReleaseArrows, &EnvSpawnTreasure,
                           &EnvArrowCheck, &EnvSetPlayerState, &EnvStartWiggle,
                           &EnvGoalSound, &EnvRemoveIndicator, &EnvAimingBowSkin,
                           &EnvEmitDust, &EnvSpotlight, &EnvRumble }; }

/* ---- Team mode: mode 6 IS a team mode (PC TowerBall: TeamMode = true) -------
 * get_TeamMode is hardcoded `Mode == 5`; GetPlayerAllegiance only consults the
 * lobby team assignment when Mode == 5 (else Neutral). Widening both to mode 6
 * turns on the whole team pipeline: team lobby, SpawnPlayersTeams (TeamSpawnA/B
 * markers — our levels carry them), team Session.Scores, team win logic. */
HOOK_DEFINE_TRAMPOLINE(GetTeamModeHook) {
    static bool Callback(void* ms) {
        MM_ONLINE_PASS(Orig(ms));
        if (ms && tb::RdI32(ms, tb::fld::MatchSettings_Mode) == tb::mode::Warlord) return true;
        return Orig(ms);
    }
};

HOOK_DEFINE_TRAMPOLINE(GetPlayerAllegianceHook) {
    static int Callback(void* ms, int idx) {
        MM_ONLINE_PASS(Orig(ms, idx));
        if (ms && tb::RdI32(ms, tb::fld::MatchSettings_Mode) == tb::mode::Warlord) {
            void* teams = tb::RdPtr(ms, tb::fld::MatchSettings_Teams);
            if (teams) return tb::Call<int (*)(void*, int)>(tb::off::Teams_GetAllegiance)(teams, idx);
        }
        return Orig(ms, idx);
    }
};

/* ---- Runtime BODY hooks for virtual methods -----------------------------
 * Bodies are resolved off a live instance's vtable (tb::ResolveVirtualBody)
 * and trampolined with InstallAtPtr. Shared class method tables are NEVER
 * written: BRUTE's delegate ctor reverse-resolves slot VALUES to method
 * records, so a foreign pointer in the table nulls the lookup and the next
 * instance construction crashes on [null+0x18] — this was the ball-respawn
 * crash (~90 frames after every round start). Body detours receive the
 * tag-ADJUSTED this; subtract the recorded adjust to get the object. Every
 * detour is guarded so foreign callers (other modes, non-ball arrows, and
 * possible LLVM identical-code-folded bodies shared across classes) fall
 * straight through to Orig. */

static int g_rlLogged = 0;
static void* g_ballImg = nullptr;          /* current ball's Image (rotation; set by FixBallSprite) */

/* Player.Update body (vtable slot 0x40): post-update bow hiding. */
static uintptr_t g_playerUpdateAdj    = 0;
static uintptr_t g_playerExplodeAdj   = 0;
static bool      g_playerUpdateHooked = false;

HOOK_DEFINE_TRAMPOLINE(PlayerOnExplodeHook) {
    static void Callback(void* self, void* explosion, float nx, float ny) {
        MM_ONLINE_PASS(Orig(self, explosion, nx, ny));
        tb::logic::OnPlayerExploded(
            reinterpret_cast<uint8_t*>(self) - g_playerExplodeAdj);
        Orig(self, explosion, nx, ny);
    }
};

HOOK_DEFINE_TRAMPOLINE(PlayerUpdateHook) {
    static void Callback(void* self) {
        MM_ONLINE_PASS(Orig(self));
        Orig(self);
        tb::logic::OnPlayerPostUpdate(
            reinterpret_cast<uint8_t*>(self) - g_playerUpdateAdj);
    }
};

static void TryHookPlayerUpdate(void* level) {
    if (g_playerUpdateHooked || !level) return;
    for (int i = 0; i < 4; i++) {
        void* p = EnvGetPlayer(level, i);
        if (!p) continue;
        uintptr_t body = tb::ResolveVirtualBody(p, tb::slot::Update, &g_playerUpdateAdj);
        if (!body) return;
        PlayerUpdateHook::InstallAtPtr(body);
        uintptr_t bodyE = tb::ResolveVirtualBody(p, tb::slot::Entity_OnExplode, &g_playerExplodeAdj);
        if (bodyE) PlayerOnExplodeHook::InstallAtPtr(bodyE);
        g_playerUpdateHooked = true;
        MMLOG("[towerball] Player.Update body hooked @%p adj=%d (bow-hide, explode-drop=%d)",
                    reinterpret_cast<void*>(body), static_cast<int>(g_playerUpdateAdj),
                    bodyE ? 1 : 0);
        return;
    }
}

/* WarlordRoundLogic bodies (slots 0x28/0x30/0x38), resolved from the live
 * RoundLogic at the first mode-6 session. In mode 6 our logic REPLACES
 * Warlord's elimination/final-kill handling entirely (PC TowerBall parity) —
 * dead players respawn on a timer and the round only ends on goals. */
static uintptr_t g_rlUpdateAdj = 0;
static bool      g_rlHooked    = false;

HOOK_DEFINE_TRAMPOLINE(RLUpdateHook) {
    static void Callback(void* self) {
        MM_ONLINE_PASS(Orig(self));
        if (tb::logic::InMode6()) {
            if (g_rlLogged < 3) {
                MMLOG("[towerball] RoundLogic.OnUpdate live (mode-6 brain)");
                g_rlLogged++;
            }
            tb::logic::OnRLUpdate(reinterpret_cast<uint8_t*>(self) - g_rlUpdateAdj);
            TryHookPlayerUpdate(tb::logic::g_level);
        }
        Orig(self);
    }
};

HOOK_DEFINE_TRAMPOLINE(RLRoundStartHook) {
    static void Callback(void* self) {
        MM_ONLINE_PASS(Orig(self));
        Orig(self);                 /* 3.1: base.OnRoundStart() FIRST */
        if (tb::logic::InMode6()) {
            tb::logic::OnRoundStart(self);
            MMLOG("[towerball] round start - ball state reset");
        }
    }
};

HOOK_DEFINE_TRAMPOLINE(RLPlayerDeathHook) {
    static void Callback(void* self, void* player, void* corpse, int playerIndex,
                         int cause, int killerIndex, float posX, float posY) {
        MM_ONLINE_PASS(Orig(self, player, corpse, playerIndex, cause, killerIndex, posX, posY));
        if (tb::logic::InMode6()) {
            tb::logic::OnCorpseSpawned(corpse);
            MMLOG("[towerball] RL.OnPlayerDeath enter (idx=%d cause=%d killer=%d)",
                        playerIndex, cause, killerIndex);
            tb::logic::OnRLPlayerDeath(player, playerIndex, cause, killerIndex);
        }
        /* ALWAYS run Orig: Warlord.OnPlayerDeath is exactly base
         * RoundLogic.OnPlayerDeath = the PURE STATS block (Kills, MatchStats,
         * SaveData, SessionStats) with NO round-end logic — the elimination
         * lives in the round-over checks we already suppress. Skipping it
         * (old code) was why the awards screen had no data. v3 replicates
         * this exact block by hand because IT extends TDM (whose base WOULD
         * run elimination); ours doesn't. */
        Orig(self, player, corpse, playerIndex, cause, killerIndex, posX, posY);
    }
};

/* Warlord.OnLevelLoadFinish spawns players FFA (allegiance -1 → both teams get
 * neutral/blue outlines and random spawn points). In mode 6, run TDM's
 * OnLevelLoadFinish body on this RL instead — base load + VersusStart +
 * SpawnPlayersTeams (TeamSpawnA/B, real allegiances) + Players count. It only
 * touches base RoundLogic fields (PC-verified), so the Warlord instance is a
 * valid `this` for it. */
static uintptr_t g_rlLevelLoadAdj = 0;

HOOK_DEFINE_TRAMPOLINE(RLLevelLoadHook) {
    static void Callback(void* self) {
        MM_ONLINE_PASS(Orig(self));
        if (tb::logic::InMode6()) {
            void* rl = reinterpret_cast<uint8_t*>(self) - g_rlLevelLoadAdj;
            tb::Call<void (*)(void*)>(tb::off::TDM_OnLevelLoadFinish)(rl);
            MMLOG("[towerball] level load: TDM team spawn used");
            return;
        }
        Orig(self);
    }
};

static void TryHookRoundLogic(void* rl) {
    if (g_rlHooked || !rl) return;
    uintptr_t bodyU = tb::ResolveVirtualBody(rl, tb::slot::RL_OnUpdate, &g_rlUpdateAdj);
    uintptr_t bodyS = tb::ResolveVirtualBody(rl, tb::slot::RL_OnRoundStart, nullptr);
    uintptr_t bodyD = tb::ResolveVirtualBody(rl, tb::slot::RL_OnPlayerDeath, nullptr);
    uintptr_t bodyL = tb::ResolveVirtualBody(rl, tb::slot::RL_OnLevelLoadFinish, &g_rlLevelLoadAdj);
    if (!bodyU || !bodyS || !bodyD || !bodyL) {
        MMLOG("[towerball] RL body resolve FAILED (u=%p s=%p d=%p l=%p)",
                    reinterpret_cast<void*>(bodyU), reinterpret_cast<void*>(bodyS),
                    reinterpret_cast<void*>(bodyD), reinterpret_cast<void*>(bodyL));
        return;
    }
    RLUpdateHook::InstallAtPtr(bodyU);
    RLRoundStartHook::InstallAtPtr(bodyS);
    RLPlayerDeathHook::InstallAtPtr(bodyD);
    RLLevelLoadHook::InstallAtPtr(bodyL);
    g_rlHooked = true;
    MMLOG("[towerball] RL bodies hooked upd=%p start=%p death=%p load=%p",
                reinterpret_cast<void*>(bodyU), reinterpret_cast<void*>(bodyS),
                reinterpret_cast<void*>(bodyD), reinterpret_cast<void*>(bodyL));
}

/* 3.1 custom variants on Switch: no custom-variant UI exists, so two vanilla
 * toggles carry them (SUDDEN DEATH -> TimedRounds, BOTTOMLESS TREASURE ->
 * HoopTreasure), and v3's forced-off variants (ReturnAsGhosts/TriggerCorpses/
 * TeamRevive) are zeroed. Variant fields aren't declaration-ordered, so they
 * are identified at RUNTIME by their Title strings; the Title field offset is
 * itself discovered from the known NoSeekingArrows slot [variants+0x128]. */
static int g_variantTitleOff = -1;

/* TOWERBALL variants-row state (see InjectVariantRow below) */
static StaticString g_vrTitle[2], g_vrHeader, g_vrDesc[2];
alignas(16) static uint8_t g_variantClone[2][0x100];
alignas(16) static uint8_t g_fakeVarArr[0x30];
static void*   g_variantPtrs[160];
static int32_t g_variantPtrCount = 0;
static int     g_vHeaderOff = -1, g_vDescOff = -1, g_vLinksOff = -1, g_vArrOff = -1;


static bool VariantTitleMatches(void* variant, const char* want) {
    if (!variant || g_variantTitleOff < 0) return false;
    void* str = tb::RdPtr(variant, (uint32_t)g_variantTitleOff);
    uintptr_t ps = reinterpret_cast<uintptr_t>(str);
    if (ps < 0x2000000000ULL || ps >= 0x8000000000ULL || (ps & 3)) return false;
    int32_t len = tb::RdI32(str, tb::fld::String_Length);
    if (len <= 0 || len > 40) return false;
    const char16_t* ch = reinterpret_cast<const char16_t*>(
        reinterpret_cast<uint8_t*>(str) + tb::fld::String_Chars);
    int i = 0;
    for (const char* w = want; *w; w++) {
        char16_t c = 0;
        do { if (i >= len) return false; c = ch[i++]; } while (c == u' ');
        if (c >= u'a' && c <= u'z') c -= 32;
        if (c != (char16_t)*w) return false;
    }
    while (i < len) if (ch[i++] != u' ') return false;
    return true;
}

static void ScanVariants(void* session) {
    void* ms = tb::RdPtr(session, tb::fld::Session_MatchSettings);
    void* variants = ms ? tb::RdPtr(ms, tb::fld::MatchSettings_Variants) : nullptr;
    if (!variants) return;
    if (g_variantTitleOff < 0) {
        void* nsa = tb::RdPtr(variants, 0x128);      /* NoSeekingArrows anchor */
        for (uint32_t off = 0x8; off <= 0x50; off += 8) {
            g_variantTitleOff = (int)off;
            if (VariantTitleMatches(nsa, "NOSEEKINGARROWS")) break;
            g_variantTitleOff = -1;
        }
        if (g_variantTitleOff >= 0)
            MMLOG("[towerball] Variant.Title found at +0x%x", g_variantTitleOff);
        else {
            MMLOG("[towerball] Variant.Title NOT found - variant features off");
            return;
        }
    }
    bool haveRow = (g_vArrOff >= 0 && tb::RdPtr(variants, (uint32_t)g_vArrOff) == g_fakeVarArr);
    if (haveRow) {                          /* real TOWERBALL row toggles */
        tb::logic::g_timedRounds  = g_variantClone[0][tb::fld::Variant_Value] != 0;
        tb::logic::g_hoopTreasure = g_variantClone[1][tb::fld::Variant_Value] != 0;
    }
    for (uint32_t off = 0x18; off <= 0x340; off += 8) {
        void* v = tb::RdPtr(variants, off);
        uintptr_t pv = reinterpret_cast<uintptr_t>(v);
        if (pv < 0x2000000000ULL || pv >= 0x8000000000ULL || (pv & 7)) continue;
        if      (VariantTitleMatches(v, "SUDDENDEATH")) {
            if (haveRow)                    /* persistence: clone -> carrier (saved) */
                tb::WrU8(v, tb::fld::Variant_Value, g_variantClone[0][tb::fld::Variant_Value]);
            else
                tb::logic::g_timedRounds  = tb::RdU8(v, tb::fld::Variant_Value) != 0;
        }
        else if (VariantTitleMatches(v, "BOTTOMLESSTREASURE")) {
            if (haveRow)
                tb::WrU8(v, tb::fld::Variant_Value, g_variantClone[1][tb::fld::Variant_Value]);
            else
                tb::logic::g_hoopTreasure = tb::RdU8(v, tb::fld::Variant_Value) != 0;
        }
        else if (VariantTitleMatches(v, "RETURNASGHOSTS"))  tb::WrU8(v, tb::fld::Variant_Value, 0);
        else if (VariantTitleMatches(v, "TRIGGERCORPSES"))  tb::WrU8(v, tb::fld::Variant_Value, 0);
        else if (VariantTitleMatches(v, "TEAMREVIVE"))      tb::WrU8(v, tb::fld::Variant_Value, 0);
    }
    MMLOG("[towerball] variants: timed=%d hoopTreasure=%d (+forced-off trio)",
                tb::logic::g_timedRounds ? 1 : 0, tb::logic::g_hoopTreasure ? 1 : 0);
}

/* ---- TOWERBALL variants row (real grid entries) --------------------------
 * The variants screen iterates MatchVariants.Variants (a Variant[]) and any
 * entry with a non-empty Header string starts a new section. We append two
 * STATIC Variant clones (donors: SUDDEN DEATH -> TIMED ROUNDS, BOTTOMLESS
 * TREASURE -> HOOP TREASURE, icons inherited) through a STATIC fake array
 * (BRUTE arrays hold data/len POINTERS, so no managed allocation is needed).
 * All field offsets are discovered at runtime from known anchors; Links is
 * nulled (the Value setter null-checks it). */

static bool VariantStringIs(void* str, const char* want) {   /* space/case-blind */
    uintptr_t ps = reinterpret_cast<uintptr_t>(str);
    if (ps < 0x2000000000ULL || ps >= 0x8000000000ULL || (ps & 3)) return false;
    int32_t len = tb::RdI32(str, tb::fld::String_Length);
    if (len <= 0 || len > 64) return false;
    const char16_t* ch = reinterpret_cast<const char16_t*>(
        reinterpret_cast<uint8_t*>(str) + tb::fld::String_Chars);
    int i = 0;
    for (const char* w = want; *w; w++) {
        char16_t c = 0;
        do { if (i >= len) return false; c = ch[i++]; } while (c == u' ');
        if (c >= u'a' && c <= u'z') c -= 32;
        if (c != (char16_t)*w) return false;
    }
    while (i < len) if (ch[i++] != u' ') return false;
    return true;
}

static void* FindVariantByTitle(void* variants, const char* want) {
    if (g_variantTitleOff < 0) return nullptr;
    for (uint32_t off = 0x18; off <= 0x340; off += 8) {
        void* v = tb::RdPtr(variants, off);
        uintptr_t pv = reinterpret_cast<uintptr_t>(v);
        if (pv < 0x2000000000ULL || pv >= 0x8000000000ULL || (pv & 7)) continue;
        if (VariantTitleMatches(v, want)) return v;
    }
    return nullptr;
}

static void InjectVariantRow() {
    uintptr_t base = exl::util::GetMainModuleInfo().m_Total.m_Start;
    void** cell = *reinterpret_cast<void***>(base + tb::off::MainMenu_VersusMatchSettings);
    void* ms = cell ? *cell : nullptr;
    if (!ms) return;
    void* variants = tb::RdPtr(ms, tb::fld::MatchSettings_Variants);
    if (!variants) return;
    /* already injected into THIS MatchVariants? */
    if (g_vArrOff >= 0 && tb::RdPtr(variants, (uint32_t)g_vArrOff) == g_fakeVarArr) return;
    void* vt = StringVtable();
    if (!vt) return;                        /* String vtable not materialized yet */
    /* title-offset discovery (shared with ScanVariants) */
    if (g_variantTitleOff < 0) {
        void* nsa = tb::RdPtr(variants, 0x128);
        for (uint32_t off = 0x8; off <= 0x50; off += 8) {
            g_variantTitleOff = (int)off;
            if (VariantTitleMatches(nsa, "NOSEEKINGARROWS")) break;
            g_variantTitleOff = -1;
        }
        if (g_variantTitleOff < 0) return;
    }
    void* sudden     = FindVariantByTitle(variants, "SUDDENDEATH");
    void* bottomless = FindVariantByTitle(variants, "BOTTOMLESSTREASURE");
    void* noTreasure = FindVariantByTitle(variants, "NOTREASURE");
    void* noTimeLim  = FindVariantByTitle(variants, "NOTIMELIMIT");
    void* teamRevive = FindVariantByTitle(variants, "TEAMREVIVE");
    if (!sudden || !bottomless || !noTreasure) {
        MMLOG("[towerball] variant row: donors not found - skipping");
        return;
    }
    /* Header offset: NoTreasure carries the "TREASURE" section header */
    if (g_vHeaderOff < 0)
        for (uint32_t off = 0x8; off <= 0x50; off += 8)
            if (VariantStringIs(tb::RdPtr(noTreasure, off), "TREASURE")) { g_vHeaderOff = (int)off; break; }
    /* Description offset (optional): TeamRevive's attr text */
    if (g_vDescOff < 0 && teamRevive)
        for (uint32_t off = 0x8; off <= 0x50; off += 8)
            if (VariantStringIs(tb::RdPtr(teamRevive, off),
                "INTEAMDEATHMATCHMODE,RESURRECTYOURDEADFRIENDS")) { g_vDescOff = (int)off; break; }
    /* Links offset (optional): SuddenDeath's list contains NoTimeLimit */
    if (g_vLinksOff < 0 && noTimeLim)
        for (uint32_t off = 0x8; off <= 0x60; off += 8) {
            void* lst = tb::RdPtr(sudden, off);
            uintptr_t pl = reinterpret_cast<uintptr_t>(lst);
            if (pl < 0x2000000000ULL || pl >= 0x8000000000ULL || (pl & 7)) continue;
            int32_t cnt = tb::RdI32(lst, tb::fld::List_Count);
            if (cnt < 1 || cnt > 8) continue;
            void* arr = tb::RdPtr(lst, tb::fld::List_Items);
            uintptr_t pa = reinterpret_cast<uintptr_t>(arr);
            if (pa < 0x2000000000ULL || pa >= 0x8000000000ULL) continue;
            void** data = reinterpret_cast<void**>(tb::RdPtr(arr, tb::fld::Array_Data));
            if (!data) continue;
            bool hit = false;
            for (int i = 0; i < cnt; i++) if (data[i] == noTimeLim) hit = true;
            if (hit) { g_vLinksOff = (int)off; break; }
        }
    /* Variants array field on MatchVariants: element check against a donor */
    if (g_vArrOff < 0)
        for (uint32_t off = 0x18; off <= 0x400; off += 8) {
            void* p = tb::RdPtr(variants, off);
            uintptr_t pp = reinterpret_cast<uintptr_t>(p);
            if (pp < 0x2000000000ULL || pp >= 0x8000000000ULL || (pp & 7)) continue;
            int32_t* lenp = reinterpret_cast<int32_t*>(tb::RdPtr(p, tb::fld::Array_LenPtr));
            uintptr_t plen = reinterpret_cast<uintptr_t>(lenp);
            if (plen < 0x2000000000ULL || plen >= 0x8000000000ULL || (plen & 3)) continue;
            int32_t n = *lenp;
            if (n < 40 || n > 150) continue;
            void** data = reinterpret_cast<void**>(tb::RdPtr(p, tb::fld::Array_Data));
            if (!data) continue;
            bool hit = false;
            for (int i = 0; i < n; i++) if (data[i] == sudden) { hit = true; break; }
            if (hit) { g_vArrOff = (int)off; break; }
        }
    if (g_vHeaderOff < 0 || g_vArrOff < 0) {
        MMLOG("[towerball] variant row: offsets missing (hdr=%d arr=%d) - skipping",
                    g_vHeaderOff, g_vArrOff);
        return;
    }
    /* build the clones (shallow copy inherits icon + flags; donors are simple
     * non-per-player variants) */
    FillStaticString(g_vrTitle[0], "TIMED ROUNDS", vt);
    FillStaticString(g_vrTitle[1], "HOOP TREASURE", vt);
    FillStaticString(g_vrHeader,   "TOWERBALL", vt);
    FillStaticString(g_vrDesc[0],  "TOWERBALL: TIMED MATCH + OVERTIME", vt);
    FillStaticString(g_vrDesc[1],  "TOWERBALL: TREASURE ON EVERY GOAL", vt);
    void* donors[2] = { sudden, bottomless };
    for (int i = 0; i < 2; i++) {
        memcpy(g_variantClone[i], donors[i], 0xC0);
        memset(g_variantClone[i] + 0xC0, 0, 0x40);
        *reinterpret_cast<void**>(g_variantClone[i] + g_variantTitleOff) = &g_vrTitle[i];
        *reinterpret_cast<void**>(g_variantClone[i] + g_vHeaderOff) = (i == 0) ? &g_vrHeader : nullptr;
        if (g_vDescOff >= 0)
            *reinterpret_cast<void**>(g_variantClone[i] + g_vDescOff) = &g_vrDesc[i];
        if (g_vLinksOff >= 0)
            *reinterpret_cast<void**>(g_variantClone[i] + g_vLinksOff) = nullptr;
        /* PERSISTENCE: seed the clone from its carrier. The clones live in
         * .bss and reset every boot; the carriers (SUDDEN DEATH / BOTTOMLESS
         * TREASURE) ride the vanilla variant save. ScanVariants mirrors clone
         * changes back into the carriers at session start, so TIMED ROUNDS /
         * HOOP TREASURE survive a reboot. Documented tradeoff: the carrier
         * shows toggled in the RULES section too and applies in vanilla modes. */
        g_variantClone[i][tb::fld::Variant_Value] =
            tb::RdU8(donors[i], tb::fld::Variant_Value);
    }
    /* grow the array via a static fake (data/len are POINTER fields) */
    void* realArr = tb::RdPtr(variants, (uint32_t)g_vArrOff);
    memcpy(g_fakeVarArr, realArr, 0x30);
    int32_t n = *reinterpret_cast<int32_t*>(tb::RdPtr(realArr, tb::fld::Array_LenPtr));
    void** data = reinterpret_cast<void**>(tb::RdPtr(realArr, tb::fld::Array_Data));
    if (n > 150) n = 150;
    /* TOWERBALL section FIRST (user pick): the grid builder walks the array
     * in order and opens a section at each Header — clones up front put the
     * TOWERBALL row at the top, RULES etc. follow unchanged. */
    g_variantPtrs[0] = g_variantClone[0];
    g_variantPtrs[1] = g_variantClone[1];
    for (int i = 0; i < n; i++) g_variantPtrs[i + 2] = data[i];
    g_variantPtrCount = n + 2;
    *reinterpret_cast<void**>(g_fakeVarArr + tb::fld::Array_Data)   = g_variantPtrs;
    *reinterpret_cast<void**>(g_fakeVarArr + tb::fld::Array_LenPtr) = &g_variantPtrCount;
    *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(variants) + g_vArrOff) = g_fakeVarArr;
    MMLOG("[towerball] TOWERBALL variants row injected (n=%d, hdr=+0x%x arr=+0x%x links=%s desc=%s)",
                n + 2, g_vHeaderOff, g_vArrOff,
                g_vLinksOff >= 0 ? "ok" : "kept", g_vDescOff >= 0 ? "ok" : "kept");
}

/* Session bootstrap: fires for every session; the first mode-6 session
 * installs the RoundLogic body hooks. Non-mode-6 sessions clear the tracked
 * session so stale pointers can never satisfy InMode6. */
HOOK_DEFINE_TRAMPOLINE(GetRoundLogicHook) {
    static void* Callback(void* session) {
        MM_ONLINE_PASS(Orig(session));
        tb::logic::UnmaskMode();            /* rematch safety: real mode before Orig */
        void* rl = Orig(session);
        bool mode6 = (tb::ModeFromSession(session) == tb::mode::Warlord) && rl;
        tb::logic::OnSessionStart(session, mode6);
        if (mode6) {
            g_rlLogged = 0;
            TryHookRoundLogic(rl);
            /* PC TowerBallRoundLogic: CanMiasma = false — no closing purple
             * curtain (Warlord's base ctor enabled it; per-instance field). */
            tb::WrU8(rl, tb::fld::RoundLogic_CanMiasma, 0);
            ScanVariants(session);
            MMLOG("[towerball] mode-6 session live (rl=%p, miasma off)", rl);
        }
        return rl;
    }
};

/* The ball must never home on players (PC forces Variants.NoSeekingArrows
 * around every BasketBall update — the "erratic roll" was toy-arrow seeking). */
HOOK_DEFINE_TRAMPOLINE(UpdateSeekingHook) {
    static void Callback(void* self) {
        MM_ONLINE_PASS(Orig(self));
        if (self == tb::logic::g_ball) return;
        Orig(self);
    }
};

/* PC BasketBall's image origin is its CENTER (5,5 on a 10x10); the vanilla toy
 * arrow draws with Origin(11,3) — 2px low on our ball art, i.e. "sunk into the
 * floor". Find the ball's Image through the known arrow-class image-field
 * offsets (each CreateGraphics stores it at one of these) by matching the
 * exact (11,3) origin signature, then lift Origin.Y to the art's center. */
static bool g_ballSpriteLogged = false;
static bool g_ballImgScaleOk  = false;

static void FixBallSprite(void* arrow_) {
    static constexpr uint32_t kImageFieldCandidates[] = { 0x1c8, 0x1d0, 0x1e0, 0x1e8 };
    for (uint32_t off : kImageFieldCandidates) {
        void* img = tb::RdPtr(arrow_, off);
        uintptr_t p = reinterpret_cast<uintptr_t>(img);
        if (p < 0x2000000000ULL || p >= 0x8000000000ULL || (p & 7)) continue; /* heap ptrs only */
        if (tb::RdF32(img, tb::fld::Img_OriginX) == 11.0f &&
            tb::RdF32(img, tb::fld::Img_OriginY) == 3.0f) {
            tb::WrF32(img, tb::fld::Img_OriginY, 5.0f);
            g_ballImg = img;
            /* scale-pulse gate: only pulse if +0x34/+0x38 really are Scale
             * (fresh Image => both exactly 1.0f) */
            g_ballImgScaleOk = tb::RdF32(img, tb::fld::Img_ScaleX) == 1.0f &&
                               tb::RdF32(img, tb::fld::Img_ScaleY) == 1.0f;
            if (!g_ballSpriteLogged) {
                g_ballSpriteLogged = true;
                MMLOG("[towerball] ball image found at arrow+0x%x - origin centered", off);
            }
            return;
        }
    }
}

/* BasketBall bounce: ToyArrow's OnCollideH/V BODIES, resolved lazily from the
 * first mode-6 toy arrow. These bodies may be inherited from Arrow (shared by
 * every arrow type) — the g_ball identity check in logic guards that. Also
 * catches delegate-bound invocations: Arrow's ctor binds its collision
 * delegates to these same concrete bodies. */
static uintptr_t g_collideHAdj = 0, g_collideVAdj = 0, g_collectibleAdj = 0;
static uintptr_t g_explodeAdj = 0, g_squishAdj = 0, g_lavaAdj = 0;
static uintptr_t g_arrowUpdAdj = 0, g_fallModeAdj = 0;
static bool      g_ballPhysicsHooked = false;

/* v3 MyToyArrow.Update, exact (body likely shared Arrow.Update — ball-only). */
HOOK_DEFINE_TRAMPOLINE(BallArrowUpdateHook) {
    static void Callback(void* self) {
        MM_ONLINE_PASS(Orig(self));
        void* arrow_ = reinterpret_cast<uint8_t*>(self) - g_arrowUpdAdj;
        if (!tb::logic::InMode6() || arrow_ != tb::logic::g_ball) { Orig(self); return; }
        tb::logic::PreBallUpdate(arrow_);
        Orig(self);
        tb::logic::PostBallUpdate(arrow_);
        if (g_ballImg) {                    /* v3: rotation += Speed.X * 0.002 */
            float r = tb::RdF32(g_ballImg, tb::fld::Img_Rotation);
            r += tb::RdF32(arrow_, tb::fld::Arrow_SpeedX) * 0.002f;
            tb::WrF32(g_ballImg, tb::fld::Img_Rotation, r);
            /* v3 scaleWiggler pulse (baked 20f envelope; see kBallWiggle) */
            int& step = tb::logic::g_scaleWigStep;
            if (g_ballImgScaleOk && step >= 0) {
                float s = 1.0f + 0.2f * tb::logic::kBallWiggle[step];
                tb::WrF32(g_ballImg, tb::fld::Img_ScaleX, s);
                tb::WrF32(g_ballImg, tb::fld::Img_ScaleY, s);
                if (++step > 20) step = -1;   /* table end == exact scale 1.0 */
            }
        }
    }
};

/* v3 EnterFallMode override: mark + base + Speed.X*2 (min 1 toward center). */
HOOK_DEFINE_TRAMPOLINE(BallEnterFallModeHook) {
    static void Callback(void* self, int bounce, int zeroX, int sound) {
        MM_ONLINE_PASS(Orig(self, bounce, zeroX, sound));
        void* arrow_ = reinterpret_cast<uint8_t*>(self) - g_fallModeAdj;
        if (tb::logic::InMode6() && arrow_ == tb::logic::g_ball) {
            Orig(self, bounce, zeroX, sound);
            tb::logic::PostBallEnterFallMode(arrow_);
            return;
        }
        Orig(self, bounce, zeroX, sound);
    }
};

/* v3 MyToyArrow: the ball ignores explosions, squishes, and lava. Bodies may
 * be Arrow-generic (shared with every arrow type) — the g_ball guard scopes
 * the immunity to THE ball only. */
HOOK_DEFINE_TRAMPOLINE(BallOnExplodeHook) {
    static void Callback(void* self, void* explosion, float nx, float ny) {
        MM_ONLINE_PASS(Orig(self, explosion, nx, ny));
        if (reinterpret_cast<uint8_t*>(self) - g_explodeAdj == tb::logic::g_ball) return;
        Orig(self, explosion, nx, ny);
    }
};
HOOK_DEFINE_TRAMPOLINE(BallDoSquishHook) {
    static void Callback(void* self) {
        MM_ONLINE_PASS(Orig(self));
        if (reinterpret_cast<uint8_t*>(self) - g_squishAdj == tb::logic::g_ball) return;
        Orig(self);
    }
};
HOOK_DEFINE_TRAMPOLINE(BallHitLavaHook) {
    static void Callback(void* self) {
        MM_ONLINE_PASS(Orig(self));
        if (reinterpret_cast<uint8_t*>(self) - g_lavaAdj == tb::logic::g_ball) return;
        Orig(self);
    }
};

/* PC BasketBall.IsCollectible (effective): Speed.Length() < 2 — the ball is
 * takeable purely by speed, never by landing state (it never lands; PC pins
 * it in Shooting). Everything else keeps the vanilla state-based rule. */
HOOK_DEFINE_TRAMPOLINE(BallIsCollectibleHook) {
    static bool Callback(void* self) {
        MM_ONLINE_PASS(Orig(self));
        void* arrow_ = reinterpret_cast<uint8_t*>(self) - g_collectibleAdj;
        if (tb::logic::InMode6() && arrow_ == tb::logic::g_ball)
            return tb::logic::BallCollectible(arrow_);
        return Orig(self);
    }
};

HOOK_DEFINE_TRAMPOLINE(BallCollideHHook) {
    static void Callback(void* self, void* platform) {
        MM_ONLINE_PASS(Orig(self, platform));
        if (tb::logic::OnBallCollideH(reinterpret_cast<uint8_t*>(self) - g_collideHAdj))
            return;
        Orig(self, platform);
    }
};
HOOK_DEFINE_TRAMPOLINE(BallCollideVHook) {
    static void Callback(void* self, void* platform) {
        MM_ONLINE_PASS(Orig(self, platform));
        if (tb::logic::OnBallCollideV(reinterpret_cast<uint8_t*>(self) - g_collideVAdj))
            return;
        Orig(self, platform);
    }
};

static uintptr_t g_checkTargetsAdj = 0;
/* v3 CheckForTargetCollisions replacement (see logic::OnBallCheckTargets).
 * Non-ball toy arrows (TOY ARROWS variant) fall through to the vanilla body. */
HOOK_DEFINE_TRAMPOLINE(BallCheckTargetsHook) {
    static bool Callback(void* self) {
        MM_ONLINE_PASS(Orig(self));
        int v = tb::logic::OnBallCheckTargets(
            reinterpret_cast<uint8_t*>(self) - g_checkTargetsAdj);
        if (v >= 0) return v == 1;
        return Orig(self);
    }
};

static void TryHookBallPhysics(void* toyArrow) {
    if (g_ballPhysicsHooked || !toyArrow) return;
    uintptr_t bodyH = tb::ResolveVirtualBody(toyArrow, tb::slot::OnCollideH, &g_collideHAdj);
    uintptr_t bodyV = tb::ResolveVirtualBody(toyArrow, tb::slot::OnCollideV, &g_collideVAdj);
    uintptr_t bodyC = tb::ResolveVirtualBody(toyArrow, tb::slot::Arrow_IsCollectible, &g_collectibleAdj);
    uintptr_t bodyE = tb::ResolveVirtualBody(toyArrow, tb::slot::Entity_OnExplode, &g_explodeAdj);
    uintptr_t bodyS = tb::ResolveVirtualBody(toyArrow, tb::slot::Arrow_DoSquish, &g_squishAdj);
    uintptr_t bodyL = tb::ResolveVirtualBody(toyArrow, tb::slot::Arrow_HitLava, &g_lavaAdj);
    if (!bodyH || !bodyV || !bodyC) return;
    BallCollideHHook::InstallAtPtr(bodyH);
    BallCollideVHook::InstallAtPtr(bodyV);
    BallIsCollectibleHook::InstallAtPtr(bodyC);
    if (bodyE) BallOnExplodeHook::InstallAtPtr(bodyE);
    if (bodyS) BallDoSquishHook::InstallAtPtr(bodyS);
    if (bodyL) BallHitLavaHook::InstallAtPtr(bodyL);
    uintptr_t bodyU = tb::ResolveVirtualBody(toyArrow, tb::slot::Update, &g_arrowUpdAdj);
    if (bodyU) BallArrowUpdateHook::InstallAtPtr(bodyU);
    uintptr_t bodyF = tb::ResolveVirtualBody(toyArrow, tb::slot::Arrow_EnterFallMode, &g_fallModeAdj);
    if (bodyF) BallEnterFallModeHook::InstallAtPtr(bodyF);
    uintptr_t bodyT = tb::ResolveVirtualBody(toyArrow, tb::slot::Arrow_CheckForTargets, &g_checkTargetsAdj);
    if (bodyT) BallCheckTargetsHook::InstallAtPtr(bodyT);
    g_ballPhysicsHooked = true;
    MMLOG("[towerball] ball bodies hooked H=%p V=%p collect=%p targets=%p imm=%d%d%d",
                reinterpret_cast<void*>(bodyH), reinterpret_cast<void*>(bodyV),
                reinterpret_cast<void*>(bodyC), reinterpret_cast<void*>(bodyT),
                bodyE ? 1 : 0, bodyS ? 1 : 0, bodyL ? 1 : 0);
}

HOOK_DEFINE_TRAMPOLINE(ArrowCreateHook) {
    static void* Callback(int type, void* owner, uint64_t nChar, uint64_t nPlayer,
                          float x, float y, float dir) {
        MM_ONLINE_PASS(Orig(type, owner, nChar, nPlayer, x, y, dir));
        void* arrow_ = Orig(type, owner, nChar, nPlayer, x, y, dir);
        tb::logic::OnArrowCreated(arrow_, type, owner);
        if (arrow_ && type == tb::arrow::Toy && tb::logic::InMode6()) {
            TryHookBallPhysics(arrow_);
            FixBallSprite(arrow_);
        }
        return arrow_;
    }
};

HOOK_DEFINE_TRAMPOLINE(CollectArrowsHook) {
    static bool Callback(void* self, void* arr) {
        MM_ONLINE_PASS(Orig(self, arr));
        if (arr) {
            int32_t* lenp = reinterpret_cast<int32_t*>(tb::RdPtr(arr, tb::fld::Array_LenPtr));
            int32_t* data = reinterpret_cast<int32_t*>(tb::RdPtr(arr, tb::fld::Array_Data));
            if (lenp && data) {
                int verdict = tb::logic::OnCollect(self, data, *lenp);
                if (verdict >= 0) return verdict == 1;
            }
        }
        return Orig(self, arr);
    }
};

HOOK_DEFINE_TRAMPOLINE(ShootArrowHook) {
    static void Callback(void* self) {
        MM_ONLINE_PASS(Orig(self));
        if (tb::logic::OnShoot(self)) return;   /* ball throw replaces the shot */
        Orig(self);
    }
};

/* Scoreboard: piggyback on Player.HUDRender (HUD sprite batch is active).
 * logic::RenderHUD draws once per frame regardless of player count. */
HOOK_DEFINE_TRAMPOLINE(HUDRenderHook) {
    static void Callback(void* self, bool wrapped) {
        MM_ONLINE_PASS(Orig(self, wrapped));
        Orig(self, wrapped);
        if (tb::logic::InMode6()) tb::logic::RenderHUD();
    }
};

/* Dunk: Player.DuckingUpdate runs each frame of the Ducking state — carrier
 * ducking on a basket platform scores (see logic::OnDucking). */
HOOK_DEFINE_TRAMPOLINE(DuckingUpdateHook) {
    static int Callback(void* self) {
        MM_ONLINE_PASS(Orig(self));
        tb::logic::OnDucking(self);
        return Orig(self);
    }
};

/* (PlayerDieHook REMOVED 2026-07-06: 0xaf3450 is the Die(Arrow) overload — x1 is
 * the killing Arrow*, and the old (int cause) signature truncated that pointer
 * before forwarding to Orig => InvalidMemoryRegion on every arrow kill. The
 * ball-drop now lives in the RL.OnPlayerDeath detour, which covers ALL death
 * causes with a verified ABI.) */

/* (Retired spikes — all proven on hardware, machinery lives in towerball.hpp:
 * ShootArrow-driven RoundLogic patch → replaced by the GetRoundLogic bootstrap;
 * ScanSession/DumpScores diagnostics → offsets baked into tb::fld;
 * SpawnSpike (extra arrow per shot) → confirmed entity spawning 2026-07-06.) */

/* ---- Selectable-mode milestone: make Modes.Warlord (6) reachable -----------
 * The versus mode-select (VersusModeButton) caps the Right-scroll at mode 5
 * (`cmp w9,#5; b.eq`). Mode 6 (Warlord) is fully wired but UI-unreachable — the
 * ideal slot for TowerBall. Rewrite the four `cmp w9,#5` gates to `cmp w9,#6`
 * so the user can scroll to a new 4th versus mode. (Displays/plays as Warlord
 * until we hook GetModeName/GetModeIcon + GetRoundLogic to make it TowerBall.)
 */
static void PatchModeSelectCap() {
    for (uintptr_t site : tb::modecap::sites)
        megamod::Patch(site, tb::modecap::CMP_W9_5, tb::modecap::CMP_W9_6, "mode select cap 5 -> 6");
    MMLOG("[towerball-mode] lifted versus mode cap 5->6 (%d sites) - mode 6 selectable",
                (int)(sizeof(tb::modecap::sites) / sizeof(tb::modecap::sites[0])));
}

/* ---- TOWERBALL on FortRise's GAME MODE button ------------------------------
 * The button is FortRise's (generated Fr_VersusModeButton: its own list, the three built-in modes then the registered custom ones,
 * Teams+'s TEAMS+, TF.EX's Netplay). TOWERBALL (mode 6) goes in as the fourth built-in mode, right after them (index 3): TF.EX's
 * VersusModeButtonPatch stops the scroll at its Netplay mode (it restores the previous mode and index), so nothing after Netplay is
 * reachable. tfexgen_services.cpp BuiltInVersusMode / BuiltInVersusModeCount (4 with it) keep FortRise's index arithmetic for the
 * registered modes (currentIndex - BuiltInModeCount). Local play only; a shell on the generated constructor hook (one trampoline). */
static void ModeButtonCtorShell(tfex::gen::hooks::VersusModeButton_ctor& c) {
    c();
    if (::megamod::Online()) return;
    tfex::gen::VList<int32_t>* modes = tfex::gen::GlueFrMods::get_VersusModeSelect();
    if (!modes) return;
    modes->Insert(3, 6);
    static int logged = 0;
    if (logged++ < 1) MMLOG("[towerball-mode] TOWERBALL inserted as FortRise's fourth built-in GAME MODE (%d modes)", modes->Count());
}

/* ---- Mode-6 crash fix: supply a valid icon --------------------------------
 * VersusModeButton.Render calls GetModeIcon(mode) → MenuAtlas["gameModes/warlord"]
 * for mode 6. Warlord was disabled in this build, so that atlas key was stripped
 * → KeyNotFoundException → guest abort the instant mode 6 renders. GetModeIcon is
 * a static returning a Subtexture* in x0; for mode 6 return the Team Deathmatch
 * icon (mode 5 — a present asset) instead. (GetModeName(6) safely returns the
 * baked "WARLORD" string, so only the icon needs redirecting.) A real TowerBall
 * icon replaces this at the rebrand step (ship it via LayeredFS atlas).
 */
/* ---- Rebrand: mode 6 name "WARLORD" -> "TOWERBALL" -------------------------
 * The name is drawn from a baked "WARLORD" System.String. GetModeName is INLINED
 * into VersusModeButton.Render (its only "caller" is a reflection thunk), so we
 * can't hook it — instead we patch the String object in place. Both the inlined
 * Render lookup and the standalone GetModeName read the same pointer table
 * (tb::off::ModeName_Table): entry = table[mode-3], String = *entry.
 *
 * No mono_string_new on AOT, but overwriting the existing String works. Mono
 * String layout: [vtable @0x00][sync @0x08][int32 length @0x10][UTF-16 @0x14].
 * "WARLORD" (7) -> alloc 0x14+8*2=0x24, 8-aligned to 0x28. "TOWERBALL" (9) needs
 * 0x14+10*2=0x28 — fits exactly. Guard on length==7 so we only touch the genuine
 * WARLORD string and it's self-limiting (length becomes 9 afterwards).
 *
 * Driven from GetModeIconHook, which DOES fire during Render(mode 6) — by then
 * the String is materialized. Idempotent + null-guarded, so a first-frame race
 * just retries next frame.
 */
static bool g_nameLogged = false;
static void PatchTowerBallName() {
    uintptr_t base = exl::util::GetMainModuleInfo().m_Total.m_Start;
    void** table = reinterpret_cast<void**>(base + tb::off::ModeName_Table);
    void* entry = table[6 - 3];                     /* mode 6 → index 3 */
    if (!entry) return;
    void* s = *reinterpret_cast<void**>(entry);     /* the String object */
    if (!s) return;
    uint8_t* str = reinterpret_cast<uint8_t*>(s);
    int32_t& len = *reinterpret_cast<int32_t*>(str + 0x10);
    if (len != 7) return;                           /* not the unpatched WARLORD */
    static const uint16_t NEW[10] = { 'T','O','W','E','R','B','A','L','L', 0 };
    uint16_t* chars = reinterpret_cast<uint16_t*>(str + 0x14);
    for (int i = 0; i < 10; i++) chars[i] = NEW[i];
    len = 9;
    if (!g_nameLogged) {
        MMLOG("[towerball-mode] renamed mode 6 -> TOWERBALL (in-place)");
        g_nameLogged = true;
    }
}

HOOK_DEFINE_TRAMPOLINE(GetModeIconHook) {
    static void* Callback(int mode) {
        MM_ONLINE_PASS(Orig(mode));
        tb::logic::UnmaskMode();            /* menu is rendering again — drop the mask */
        InjectVariantRow();                 /* idempotent; needs the String vtable */
        if (mode == 6) {
            PatchTowerBallName();            /* rebrand the name while we're here */
            /* GetModeIcon(6) looks up gameModes/warlord: TowerBall's folder's banner, put there by the
             * catalog's replaces (translate.toml [mods.catalog."Switch.TowerBall"]). */
            return Orig(6);
        }
        return Orig(mode);
    }
};

/* ---- TowerBall levels: swap the round's arena in mode 6 ---------------------
 * towerball-mod (LayeredFS) ADDS Content/Levels/{Versus,6P-Versus}/<tower>/tb.oel.
 * VersusLevelSystem.GenLevels fills this.levels with the tower's level paths
 * ("....../NN.oel"); in mode 6 we rewrite each path's filename to "tb.oel" —
 * same UTF-16 length, patched in place (the WARLORD-rename trick).
 *
 * The list holds REFERENCES to the tower's canonical path strings, so a naive
 * patch would leak into later non-TowerBall sessions. Every GenLevels call
 * therefore FIRST restores all outstanding patches (original digit chars are
 * recorded), then re-patches only when the session is mode 6. Also harvested
 * here: the tower index + wide/standard, for the baked marker tables
 * (towerball_levels.hpp) that drive baskets and ball spawns.
 */
HOOK_DEFINE_TRAMPOLINE(GenLevelsHook) {
    static void Callback(void* self, void* matchSettings) {
        MM_ONLINE_PASS(Orig(self, matchSettings));
        tb::logic::PreGenLevels();          /* restore canonical strings, any mode */
        Orig(self, matchSettings);
        tb::logic::PostGenLevels(self, matchSettings);
    }
};

namespace towerball {
/* TowerBall's start (mod.toml boot, the generated catalog's, tfex/FrModsBoot.cpp): its folder sd:/xplay/mods/Switch.TowerBall loaded, its art
 * registered (Switch.TowerBall/<name>; the ball as arrows/squeakyArrow: the catalog's replaces), its tb.oel levels served from the
 * folder (the catalog's overlay). Its hooks go in now, outermost as they were at boot. */
void Boot() {
    /* TowerBall bootstrap + gameplay hooks (all DIRECT bodies). */
    GetRoundLogicHook::InstallAtOffset(tb::off::Session_GetRoundLogic);
    UpdateSeekingHook::InstallAtOffset(tb::off::Arrow_UpdateSeeking);
    ArrowCreateHook::InstallAtOffset(tb::off::Arrow_Create);
    CollectArrowsHook::InstallAtOffset(tb::off::Player_CollectArrows);
    ShootArrowHook::InstallAtOffset(tb::off::Player_ShootArrow);
    GenLevelsHook::InstallAtOffset(tb::off::VLS_GenLevels);
    GetTeamModeHook::InstallAtOffset(tb::off::MatchSettings_GetTeamMode);
    GetPlayerAllegianceHook::InstallAtOffset(tb::off::MatchSettings_GetPlayerAllegiance);
    DuckingUpdateHook::InstallAtOffset(tb::off::Player_DuckingUpdate);
    HUDRenderHook::InstallAtOffset(tb::off::Player_HUDRender);

    /* Selectable-mode milestone: make mode 6 reachable in the versus UI. */
    PatchModeSelectCap();   /* (the constructor's inlined UpdateSides; FortRise's Update / UpdateSides replace the rest) */
    tfex::gen::hooks::VersusModeButton_ctor::Wrap(ModeButtonCtorShell);
    /* Mode 6: TowerBall's banner from its folder (Switch.TowerBall/banner) + the name rebranded to "TOWERBALL" (patched in place from
     * inside this hook, during Render). */
    GetModeIconHook::InstallAtOffset(tb::off::VersusModeButton_GetModeIcon);
    static const tfex::mods::VersusModeApi kMode = { 6, true };   /* mode 6 (the Warlord slot), its own tb.oel levels */
    tfex::mods::RegisterApi("Switch.TowerBall", tfex::mods::kVersusModeApi, &kMode);
    MMLOG("[megamod] TowerBall started (its folder Switch.TowerBall): mode 6 on the GAME MODE button");
}

}  // namespace towerball
#else
namespace towerball { void Boot() { ::hw::Log("[towerball] not in this build (TF.EX Switch, no extras)"); } }
#endif   // XPLAY_EXTRAS
