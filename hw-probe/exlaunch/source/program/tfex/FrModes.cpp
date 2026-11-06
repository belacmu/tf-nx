/* FrModes.cpp: the Switch's MatchSettings team helpers under FortRise's custom modes.
 *
 * TFEX-DIFF S-native: the PC's GetPlayerAllegiance, GetMaxTeamSize, IsFriendly and GetPlayerTeamSize ask the TeamMode property, which
 * FortRise patches (patch_MatchSettings.get_TeamMode, generated: a custom mode asks its IVersusGameMode.IsTeamMode, for TF.EX's Netplay
 * mode the lobby's Team Deathmatch). The Switch's build inlines TeamMode into these four as `Mode == 5` (docs/TEAM-MODES-RE.md section
 * 7), so under Mode 11 they would answer "no teams". Around each, in a custom mode that FortRise's TeamMode calls a team mode, the
 * Switch's own body runs with Mode 5 for the call (Mode put back right after): the same branch as the PC's.
 *
 * Also VERSUS's GAME MODE button under a custom mode (FortRise's patch_VersusModeButton, generated, scrolls to Teams+'s TEAMS+): the
 * Switch's VersusModeButton.Render (+0xbb1f90) inlines GetModeName as a table of modes 3-6 (+0x4576a00: entry = table[mode - 3], its
 * String = *entry; any other mode branches to GetModeName's throw, +0xbb2138 `cmp w8,#4; b.hs`). For a custom mode, Render runs with
 * Mode 5 and table[2]'s String lent FortRise's name for the mode (GetModeName, generated); its GetModeIcon call (+0xbb2014) answers
 * for the real mode (a shell on the generated GetModeIcon hook). Everything put back right after.
 *
 * And Session's score code: the Switch's build inlines Session.GetScoreIndex (`TeamMode ? MatchSettings.GetPlayerAllegiance(i) : i`)
 * into nine Session methods (EndRound, GetScoreLead, GetSpawnHatState, IsLosingNotAllTied, WasWinningAtStartOfRound,
 * IsWinningNotAllTied, IsWinning, HasWonMinusOne, IsLosing: every `bl GetPlayerAllegiance` in Session but GetScoreIndex's own, each
 * after the `cmn idx,#1` / `bl get_TeamMode` of the inlined body; the PC source uses that value only as a score index). A mod's
 * GetScoreIndex (Teams+'s RulesHooks prefix: score slots per group, a lone wolf's own) never runs there: a wolf's allegiance -1 indexed
 * Scores and threw (+0xb4591c, GetScoreLead). While one of them runs, its GetPlayerAllegiance call answers with the game's real
 * GetScoreIndex (+0xb45230: the generated hooks on it, then the game's body), as the PC's call would. */
#include "lib.hpp"
#include "FrModes.hpp"
#include "../hwlog.hpp"
#include "gen/tfexgen_binding.hpp"
#include "runtime/tfexgen_code_rt.hpp"
#include "gen/tfexgen_hooks.hpp"
#include "gen/tfexgen_Fr_VersusModeButton.hpp"
#include <lib/util/sys/modules.hpp>
#include <cstdint>

namespace tfex::frmodes {
namespace {
constexpr uintptr_t kGetPlayerAllegiance = 0xabb5c0, kGetMaxTeamSize = 0xabb5f0, kIsFriendly = 0xabb880, kGetPlayerTeamSize = 0xabb910;
constexpr int32_t kTeamDeathmatch = 5;

/* Mode 5 while the original runs, when FortRise's TeamMode (the hooked getter) says a custom mode plays in teams */
struct TeamModeAs5 {
    int32_t* mode = nullptr; int32_t was = 0;
    explicit TeamModeAs5(void* ms, const char* what) {
        if (!ms || !gen::SideGet<bool>("IsCustom", ms)) return;
        if (!gen::Fn<bool (*)(void*)>(gen::bind::m_MatchSettings_get_TeamMode)(ms)) return;
        mode = &gen::Fld<int32_t>(ms, gen::bind::f_MatchSettings_Mode);
        was = *mode; *mode = kTeamDeathmatch;
        static int logged = 0;
        if (logged++ < 4) hw::Log("[frmodes] %s: custom mode %d plays in teams (FortRise's TeamMode): the Switch's body with Mode 5", what, (int)was);
    }
    ~TeamModeAs5() { if (mode) *mode = was; }
};

void* g_scoreSession = nullptr;   /* the Session whose inlined GetScoreIndex is running (ScoreIndexScope) */
constexpr uintptr_t kGetScoreIndex = 0xb45230;

struct AllegianceSite { uintptr_t ret; AllegianceAt fn; };
AllegianceSite g_allegianceSites[4];
int g_nAllegianceSites = 0;

HOOK_DEFINE_TRAMPOLINE(GetPlayerAllegianceHook) {
    static int32_t Callback(void* self, int32_t playerIndex) {
        if (g_nAllegianceSites) {   /* a mod's redirected call site (RedirectAllegianceCall), by the caller's return address */
            uintptr_t lr = reinterpret_cast<uintptr_t>(__builtin_return_address(0)) - exl::util::GetMainModuleInfo().m_Total.m_Start;
            for (int i = 0; i < g_nAllegianceSites; i++)
                if (g_allegianceSites[i].ret == lr) {
                    int32_t r = g_allegianceSites[i].fn(self, playerIndex);
                    static int logged = 0;
                    if (logged++ < 4) hw::Log("[frmodes] GetPlayerAllegiance at +%#lx (player %d): the mod's answer %d", (unsigned long)lr, (int)playerIndex, (int)r);
                    return r;
                }
        }
        if (void* session = g_scoreSession) {   /* an inlined GetScoreIndex: the real one (its own allegiance call answered normally) */
            g_scoreSession = nullptr;
            int32_t r = gen::Fn<int32_t (*)(void*, int32_t)>(kGetScoreIndex)(session, playerIndex);
            g_scoreSession = session;
            static int logged = 0;
            if (logged++ < 2) hw::Log("[frmodes] Session's inlined GetScoreIndex (player %d): the real one, %d", (int)playerIndex, (int)r);
            return r;
        }
        TeamModeAs5 t(self, "GetPlayerAllegiance"); return Orig(self, playerIndex);
    }
};
/* the eight Session methods with the inlined GetScoreIndex and no hook of their own (EndRound: xplay.cpp's shell) */
#define TFEX_SCORE_HOOK(Name, Ret)                                                                                     \
    HOOK_DEFINE_TRAMPOLINE(Name##Hook) {                                                                                \
        static Ret Callback(void* self, int32_t playerIndex) { ScoreIndexScope s(self); return Orig(self, playerIndex); } \
    };
TFEX_SCORE_HOOK(GetScoreLead, int32_t) TFEX_SCORE_HOOK(GetSpawnHatState, int32_t) TFEX_SCORE_HOOK(IsLosingNotAllTied, bool)
TFEX_SCORE_HOOK(WasWinningAtStartOfRound, bool) TFEX_SCORE_HOOK(IsWinningNotAllTied, bool) TFEX_SCORE_HOOK(IsWinning, bool)
TFEX_SCORE_HOOK(HasWonMinusOne, bool) TFEX_SCORE_HOOK(IsLosing, bool)
#undef TFEX_SCORE_HOOK
HOOK_DEFINE_TRAMPOLINE(GetMaxTeamSizeHook) {
    static int32_t Callback(void* self) { TeamModeAs5 t(self, "GetMaxTeamSize"); return Orig(self); }
};
HOOK_DEFINE_TRAMPOLINE(IsFriendlyHook) {
    static bool Callback(void* self, int32_t a, int32_t b) { TeamModeAs5 t(self, "IsFriendly"); return Orig(self, a, b); }
};
HOOK_DEFINE_TRAMPOLINE(GetPlayerTeamSizeHook) {
    static int32_t Callback(void* self, int32_t playerIndex) { TeamModeAs5 t(self, "GetPlayerTeamSize"); return Orig(self, playerIndex); }
};
constexpr uintptr_t kModeButtonRender = 0xbb1f90, kModeNameTable = 0x4576a00;
int32_t g_iconMode = -1;   /* while Render draws a custom mode: that mode, for its icon */

HOOK_DEFINE_TRAMPOLINE(ModeButtonRenderHook) {
    static void Callback(void* self) {
        void* vs = (gen::Cctor(gen::bind::c_MainMenu), gen::Static<void*>(gen::bind::f_MainMenu_VersusMatchSettings));
        int32_t* mode = vs ? &gen::Fld<int32_t>(vs, gen::bind::f_MatchSettings_Mode) : nullptr;
        if (!mode || (*mode >= 3 && *mode <= 6)) { Orig(self); return; }
        void* name = gen::Fr_VersusModeButton::GetModeName(*mode);
        uintptr_t base = exl::util::GetMainModuleInfo().m_Total.m_Start;
        void** slot = *reinterpret_cast<void***>(base + kModeNameTable + 2 * sizeof(void*));   /* mode 5's entry */
        if (!name || !slot) { Orig(self); return; }
        static int logged = 0;
        if (logged++ < 2) {
            char n[48] = ""; gen::Utf8Of(name, n, sizeof n);
            hw::Log("[frmodes] GAME MODE button draws custom mode %d as %s (the Switch's inlined name table covers modes 3-6)", (int)*mode, n);
        }
        void* saved = *slot; int32_t was = *mode;
        *slot = name; *mode = kTeamDeathmatch; g_iconMode = was;
        Orig(self);
        *slot = saved; *mode = was; g_iconMode = -1;
    }
};
bool g_modeButtonSelected = false;   /* the GAME MODE button selected at its last update (the local-play test driver steers to it) */
void ModeUpdateShell(gen::hooks::VersusModeButton_Update& c) {
    g_modeButtonSelected = gen::Fn<bool (*)(void*)>(gen::bind::m_MenuItem_get_Selected)(c.self);
    c();
}
void ModeIconShell(gen::hooks::VersusModeButton_GetModeIcon& c) {
    if (g_iconMode >= 0 && c.mode == kTeamDeathmatch) c.mode = g_iconMode;
    c();
}
}  // namespace

bool ModeButtonSelected() { return g_modeButtonSelected; }
bool RedirectAllegianceCall(uintptr_t returnOffset, AllegianceAt fn, const char* what) {
    constexpr int kCap = (int)(sizeof g_allegianceSites / sizeof *g_allegianceSites);
    for (int i = 0; i < g_nAllegianceSites; i++) if (g_allegianceSites[i].ret == returnOffset) return true;
    if (g_nAllegianceSites == kCap) { hw::Log("[frmodes] ERROR: GetPlayerAllegiance call-site table full (%d): %s not redirected", kCap, what); return false; }
    g_allegianceSites[g_nAllegianceSites++] = AllegianceSite{returnOffset, fn};
    hw::Log("[frmodes] GetPlayerAllegiance's call at +%#lx answered by %s (%d of %d sites)", (unsigned long)returnOffset, what, g_nAllegianceSites, kCap);
    return true;
}
ScoreIndexScope::ScoreIndexScope(void* session) : prevSession(g_scoreSession) { g_scoreSession = session; }
ScoreIndexScope::~ScoreIndexScope() { g_scoreSession = prevSession; }

void Install() {
    GetPlayerAllegianceHook::InstallAtOffset(kGetPlayerAllegiance);
    GetMaxTeamSizeHook::InstallAtOffset(kGetMaxTeamSize);
    IsFriendlyHook::InstallAtOffset(kIsFriendly);
    GetPlayerTeamSizeHook::InstallAtOffset(kGetPlayerTeamSize);
    ModeButtonRenderHook::InstallAtOffset(kModeButtonRender);
    GetScoreLeadHook::InstallAtOffset(0xb457d0); GetSpawnHatStateHook::InstallAtOffset(0xb46260);
    IsLosingNotAllTiedHook::InstallAtOffset(0xb463b0); WasWinningAtStartOfRoundHook::InstallAtOffset(0xb469c0);
    IsWinningNotAllTiedHook::InstallAtOffset(0xb46bb0); IsWinningHook::InstallAtOffset(0xb46ca0);
    HasWonMinusOneHook::InstallAtOffset(0xb46d60); IsLosingHook::InstallAtOffset(0xb46e20);
    gen::hooks::VersusModeButton_GetModeIcon::Wrap(ModeIconShell);
    gen::hooks::VersusModeButton_Update::Wrap(ModeUpdateShell);
    hw::Log("[frmodes] MatchSettings' inlined team tests (GetPlayerAllegiance, GetMaxTeamSize, IsFriendly, GetPlayerTeamSize) follow FortRise's TeamMode");
}
}  // namespace tfex::frmodes
