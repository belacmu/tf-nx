/* TeamsPlusNative.cpp: Teams+'s IL edits (its transpilers, PC: Harmony) as the Switch's native patches (TFEX-DIFF S-native).
 *
 * Teams+ (vendor/teamplus) rewrites the game's IL in three places; the Switch's game is compiled code, so each is a patch of the
 * instructions the same C# compiled to (docs/PLAN-THREETEAM-FORTRISE-2026-10-31.md step 2's decision), applied when Teams+ is
 * constructed (its generated boot, tfex::gen::codemods::Boot_TeamsPlus: translate.toml [code_mods] `native`), as FortRise applies a mod's patches:
 *  - ResultsHooks.LabelArray_Transpiler: VersusRoundResults' coroutine counts the "P#" labels per score row in `new int[2]` and
 *    indexes it by GetScoreIndex: a third group (YELLOW, slot 2) threw IndexOutOfRange (emulator, 2026-10-31: +0xbba37c `b.ls` to the
 *    throw). Teams+ makes it `new int[Math.Max(2, Teams3.GroupCount)]`; here the count is 8 (+0xbba264 `mov w8,#2` -> `mov w8,#8`):
 *    a match has at most 8 groups (one per player), and never less than the game's 2 (TF.EX's 4-seat and other modes unchanged).
 *  - RulesHooks.OnPlayerDeath_Transpiler: RoundLogic.OnPlayerDeath's two GetPlayerAllegiance calls (its team-kill test, `allegiance of
 *    the victim == allegiance of the killer`: two lone wolves, both Neutral, were a team kill) go to RulesHooks.AllegianceForKillType
 *    (generated: a wolf answers a value of its own). The calls at +0xb38734 / +0xb38748 (every GetPlayerAllegiance call in
 *    OnPlayerDeath: tools/re), answered at their return addresses by FrModes.cpp's GetPlayerAllegiance hook.
 *  - SafetyHooks.TeamLookup_Transpiler: SensorBlock's Sequence coroutine colours its gems with ArcherData.Teams[(int)Teams[squisher]]
 *    (+0xb42fa4 `bl MatchTeams.get_Item`, then the bounds check on ArcherData.Teams: a wolf's -1 threw); Teams+ makes that call
 *    Teams3.TeamDataAllegiance (generated: a wolf's own white entry). A hook on MatchTeams.get_Item answers that one call site
 *    (return address +0xb42fa8) with it; every other call is the game's. */
#include "lib.hpp"
#include "patch/code_patcher.hpp"
#include <lib/util/sys/modules.hpp>
#include "../hwlog.hpp"
#include "FrModes.hpp"
#include "gen/tfexgen_Tp_RulesHooks.hpp"
#include "gen/tfexgen_Tp_Teams3.hpp"
#include <cstdint>

namespace tfex::teamplus {
namespace {
struct Word { uintptr_t off; uint32_t expect, repl; const char* what; };
constexpr Word kWords[] = {
    { 0xbba264, 0x321f03e8 /* mov w8,#2 */, 0x52800108 /* mov w8,#8 */, "VersusRoundResults' P-label rows int[2] -> int[8] (ResultsHooks.LabelArray_Transpiler)" },
};
constexpr uintptr_t kMatchTeamsGetItem = 0xabc420, kSensorTeamLookupRet = 0xb42fa8;
HOOK_DEFINE_TRAMPOLINE(MatchTeamsGetItemHook) {
    static int32_t Callback(void* teams, int32_t playerIndex) {
        uintptr_t lr = reinterpret_cast<uintptr_t>(__builtin_return_address(0)) - exl::util::GetMainModuleInfo().m_Total.m_Start;
        if (lr != kSensorTeamLookupRet) return Orig(teams, playerIndex);
        static int logged = 0;
        int32_t r = tfex::gen::Tp_Teams3::TeamDataAllegiance(teams, playerIndex);
        if (logged++ < 2) hw::Log("[teamplus] SensorBlock's team colour for player %d: Teams3.TeamDataAllegiance %d", (int)playerIndex, (int)r);
        return r;
    }
};
}  // namespace

void InstallOutlines();
void InstallNative() {
    uintptr_t base = exl::util::GetMainModuleInfo().m_Total.m_Start;
    int done = 0;
    for (const Word& w : kWords) {
        uint32_t cur = *reinterpret_cast<const uint32_t*>(base + w.off);
        if (cur == w.repl) { done++; continue; }   /* (already: a second boot of the mod's code) */
        if (cur != w.expect) { hw::Log("[teamplus] ERROR: native patch %s @+%#lx: found %08x, expected %08x: left alone", w.what, (unsigned long)w.off, cur, w.expect); continue; }
        exl::patch::CodePatcher(w.off).Write(w.repl);
        done++;
    }
    int sites = 0;
    sites += tfex::frmodes::RedirectAllegianceCall(0xb38738, tfex::gen::Tp_RulesHooks::AllegianceForKillType, "Teams+'s AllegianceForKillType (OnPlayerDeath, victim)") ? 1 : 0;
    sites += tfex::frmodes::RedirectAllegianceCall(0xb3874c, tfex::gen::Tp_RulesHooks::AllegianceForKillType, "Teams+'s AllegianceForKillType (OnPlayerDeath, killer)") ? 1 : 0;
    static bool hooked = false;
    if (!hooked) { MatchTeamsGetItemHook::InstallAtOffset(kMatchTeamsGetItem); hooked = true; }
    InstallOutlines();   /* Teams+'s groups in the Switch's team outlines (TeamPlusOutlines.cpp) */
    hw::Log("[teamplus] native patches (Teams+'s IL edits): %d of %d words, %d of 2 OnPlayerDeath call sites, SensorBlock's team lookup hooked",
            done, (int)(sizeof kWords / sizeof *kWords), sites);
}
}  // namespace tfex::teamplus
