/* Inlined.cpp: patches run where the Switch's build inlined the method they patch (tools/tfex/inlined.py finds them; tfex/FrModes.cpp
 * holds the team-mode ones, tfexgen_frmods.cpp VanillaGoalScore the GoalScore one, EndOfMatch.cpp the PauseMenu one).
 *
 * Monocle.Layer.Remove(Entity): TF.State's LayerPatch postfix (generated, tfexgen_State_Layer) resets its HUD and session state when a
 * VersusStart, a VersusRoundResults or a Miasma leaves a layer. On the PC, Layer.Clear(), Remove(Predicate<Entity>) and Remove(GameTags)
 * call Remove(entity) for each entity they take out, so the postfix runs for each; the Switch's build inlined those calls (its Remove
 * marks the entity: MarkedForRemoval, then toRemove). Around each of the three: the layer's entities not yet marked before the original,
 * and after it TF.State's postfix for each one the original marked, in the list's order. */
#include "lib.hpp"
#include "Inlined.hpp"
#include "../hwlog.hpp"
#include "gen/tfexgen_binding.hpp"
#include "gen/tfexgen_State_Layer.hpp"
#include "runtime/tfexgen_code_rt.hpp"
#include <cstdint>

namespace tfex::inlined {
namespace {
namespace bind = tfex::gen::bind;
/* Monocle.Layer's bodies on 1.4.1.6 (tools/re/callmap.idx), each starting `sub sp, sp, #0x50` */
constexpr uintptr_t kRemovePredicateOrTag1 = 0x9391c0, kRemovePredicateOrTag2 = 0x9392a0, kClear = 0x939380;
constexpr uint32_t kFirstWord = 0xd10143ff;

constexpr int kMax = 2048;   /* entities in one layer; more: the rest get no postfix (logged as a full pool) */
struct Snapshot { void* e[kMax]; int n; };

bool Marked(void* e) { return gen::Fn<bool (*)(void*)>(bind::m_Entity_get_MarkedForRemoval)(e); }

void Before(void* layer, Snapshot& s) {
    s.n = 0;
    void* list = layer ? gen::Fn<void* (*)(void*)>(bind::m_Layer_get_Entities)(layer) : nullptr;
    if (!list) return;
    int c = gen::Fn<int32_t (*)(void*)>(bind::m_List_1_a639_get_Count)(list);
    for (int i = 0; i < c; i++) {
        void* e = gen::Fn<void* (*)(void*, int32_t)>(bind::m_List_1_a639_get_Item_e2bf)(list, i);
        if (!e || Marked(e)) continue;
        if (s.n == kMax) {
            static bool told = false;
            if (!told) { told = true; hw::Log("[pools] EXHAUSTED inlined Layer.Remove snapshot (capacity %d): a layer of %d entities, the rest without TF.State's postfix", kMax, c); }
            break;
        }
        s.e[s.n++] = e;
    }
}

void After(void* layer, Snapshot& s, const char* what) {
    int n = 0;
    for (int i = 0; i < s.n; i++)
        if (Marked(s.e[i])) { tfex::gen::State_Layer::Layer_Remove_Entity(layer, s.e[i]); n++; }
    static int logged = 0;
    if (n && logged < 3) { logged++; hw::Log("[inlined] Layer.%s: TF.State's Layer.Remove postfix for %d entities (the Switch inlined Remove(entity))", what, n); }
}

HOOK_DEFINE_TRAMPOLINE(ClearHook) {
    static void Callback(void* self) { static Snapshot s; Before(self, s); Orig(self); After(self, s, "Clear"); }
};
HOOK_DEFINE_TRAMPOLINE(Remove1Hook) {
    static void Callback(void* self, uint64_t arg) { static Snapshot s; Before(self, s); Orig(self, arg); After(self, s, "Remove"); }
};
HOOK_DEFINE_TRAMPOLINE(Remove2Hook) {
    static void Callback(void* self, uint64_t arg) { static Snapshot s; Before(self, s); Orig(self, arg); After(self, s, "Remove"); }
};

bool Is(uintptr_t at) { return *reinterpret_cast<const uint32_t*>(gen::Base() + at) == kFirstWord; }
}  // namespace

void Install() {
    if (!Is(kClear) || !Is(kRemovePredicateOrTag1) || !Is(kRemovePredicateOrTag2)) {
        hw::Log("[inlined] ERROR: Monocle.Layer's Clear / Remove bodies aren't the 1.4.1.6 ones: TF.State's Layer.Remove postfix stays off there");
        return;
    }
    ClearHook::InstallAtOffset(kClear);
    Remove1Hook::InstallAtOffset(kRemovePredicateOrTag1);
    Remove2Hook::InstallAtOffset(kRemovePredicateOrTag2);
    hw::Log("[inlined] Layer.Clear / Remove(Predicate) / Remove(GameTags): TF.State's Layer.Remove postfix at their inlined calls");
}
}  // namespace tfex::inlined
