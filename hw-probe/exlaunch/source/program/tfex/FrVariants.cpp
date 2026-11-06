/* FrVariants.hpp: FortRise's patch_MatchVariants.ctor in place of the game's MatchVariants constructor. */
#include "FrVariants.hpp"
#include "lib.hpp"
#include "../hwlog.hpp"
#include "gen/tfexgen_binding.hpp"
#include "gen/tfexgen_Fr_MatchVariants.hpp"
#include <cstdint>

namespace tfex::frvariants {
namespace {
HOOK_DEFINE_TRAMPOLINE(CtorHook) {
    /* the game's constructors here take this = null to allocate and return the object: the game's constructor first (MonoMod's
     * orig_ctor), then FortRise's part on the object it returns */
    static void* Callback(void* self, bool noPerPlayer) {
        void* obj = Orig(self, noPerPlayer);
        static int n = 0;
        if (n++ == 0) hw::Log("[frmods] the first MatchVariants (%s): FortRise's constructor (its custom variants appended)", obj ? "made" : "NOT made");
        if (obj) tfex::gen::Fr_MatchVariants::ctor(obj, noPerPlayer);
        return obj;
    }
};
}  // namespace

void Install() { CtorHook::InstallAtOffset(tfex::gen::bind::m_MatchVariants__ctor_953d); }
void AddSwitchOnlyVariants(void* list) {
    static bool done = false;
    if (done || !list) return;
    done = true;
    static const char* const kSwitchOnly[] = { "PASS THROUGH TEAM", "KEEP YOUR ARROWS", "NO HYPERS", "REAPER CHALICE" };
    for (const char* t : kSwitchOnly)
        tfex::gen::Fn<void (*)(void*, void*)>(tfex::gen::bind::m_List_1_eb65_Add_0b43)(list, tfex::gen::FromAscii(t));
    hw::Log("[frmods] the Switch-only variants (%d) hidden and off in netplay: TF.EX's UnauthorizedVariant list", (int)(sizeof kSwitchOnly / sizeof *kSwitchOnly));
}
}  // namespace tfex::frvariants
