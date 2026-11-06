/* TF.EX's Notification on the Switch: the class itself is GENERATED from TF.EX's Notification.cs (tfex/gen/tfexgen_Notification.*, by
 * tools/tfex/translator); this file is the Switch side around it (Notification.hpp; the pattern: docs/ws-tfex-subclass.md
 * "Integrating a generated class"):
 *   - the API our call sites use (ASCII texts -> managed strings), behind hw.txt "notif 1";
 *   - TFEX-DIFF: S-rollback: every static entry point that changes game memory goes through BetweenTicks::Run, so in a Level a call
 *     from between ticks is carried out inside the next simulated tick and replayed by resimulations;
 *   - SharedCanvas (the generated ctor's `new Canvas`, once the translator's ledger rule routes it here). */
#include "Notification.hpp"
#include "BetweenTicks.hpp"
#include "Game.hpp"
#include "gen/tfexgen_Notification.hpp"
#include "runtime/tfexgen_code_rt.hpp"
#include "../hwlog.hpp"
#include <cstdio>
#include <cstring>

namespace tfex::Notification {
using namespace tfex::game;
using namespace tfex::game::brute;

namespace {
bool g_enabled = true;    /* TF.EX always shows its notifications (and its runtime-made classes come with them); hw.txt "notif 0" = off */
bool g_ready = false, g_tried = false;
/* GC memory (uncollectable: scanned, restored by a rollback): the shared canvases */
constexpr int kCanvasCache = 48;
struct Cells { void* canvases[kCanvasCache]; };
Cells* g_cells = nullptr;
struct TextArgs { char text[96]; int appear, staying; bool sticky, noAnim; };
void* g_lastMade = nullptr;

void* Managed(const char* ascii) { return gen::FromAscii(ascii ? ascii : ""); }   /* a GC string (the queue may keep it) */
void CreateThunk(void* scene, const void* p) {
    auto* a = static_cast<const TextArgs*>(p);
    g_lastMade = gen::Notification::Create(scene, Managed(a->text), a->appear, a->staying, a->sticky, a->noAnim);
}
void CreateOrDeferThunk(void* scene, const void* p) { gen::Notification::CreateOrDefer(scene, Managed(static_cast<const TextArgs*>(p)->text)); }
void FlushThunk(void* scene, const void*) { gen::Notification::FlushDeferred(scene); }
void ClearDeferredThunk(void*, const void*) { gen::Notification::ClearDeferred(); }
void ClearThunk(void* scene, const void* p) { gen::Notification::Clear(scene, *static_cast<const int*>(p)); }
TextArgs Args(const char* text, int appear, int staying, bool sticky, bool noAnim) {
    TextArgs a{ {}, appear, staying, sticky, noAnim };
    std::snprintf(a.text, sizeof a.text, "%s", text ? text : "");
    return a;
}
} // namespace

void SetEnabled(bool on) { g_enabled = on; }
bool Enabled() { return g_enabled; }

bool EnsureClass() {
    if (g_ready) return true;
    if (!g_enabled) { static bool told = false; if (!told) { told = true; hw::Log("[notif] off (hw.txt \"notif 1\" turns TF.EX's Notification on): no toast"); } return false; }
    if (g_tried) return false;
    /* only in a menu: the class's type record, statics, BetweenTicks' cell and the canvases are GC allocations held by native
     * pointers; made inside a match's rollback window, a rollback past them frees that memory under those pointers and our later
     * writes land in gameplay objects (the desync of 2026-10-17: made at the first "Synchronized!" toast). MainMenuPatch::Update makes
     * it on the first menu frames when notif is on; a toast asked for before that, outside a menu, is dropped. */
    if (!SceneIs("MainMenu")) {
        static bool told = false;
        if (!told) { told = true; hw::Log("[notif] the class is made in a menu only: a toast outside one before that is dropped"); }
        return false;
    }
    g_tried = true;
    if (!gen::Notification::EnsureClass()) { hw::Log("[notif] the generated class could not be made"); return false; }
    g_cells = static_cast<Cells*>(Fn<void* (*)(uint64_t)>(MallocUncollectable)(sizeof(Cells)));
    if (!g_cells || !BetweenTicks::Enable()) return false;
    std::memset(g_cells, 0, sizeof *g_cells);
    for (int k = 1; k < kCanvasCache; k++) SharedCanvas(k * 10, 14);   /* all of them now: outside any rollback window */
    g_ready = true;
    hw::Log("[notif] TF.EX.Domain.CustomComponent.Notification (generated, tfex/gen) : Monocle.Entity made: type %p", gen::Notification::RuntimeType());
    return true;
}

/* TFEX-DIFF: S-rollback. `new Canvas(w, h)` makes a GPU render target (native, outside the undo log); in a Level the toast is made inside
 * a simulated tick, so each resimulation would make another. TF.EX only reads canvas.Width: canvases are made once per size (in the
 * menu, EnsureClass) and shared. */
void* SharedCanvas(int32_t width, int32_t height) {
    auto make = [&] { return Fn<void* (*)(void*, int, int)>(Canvas::ctor)(nullptr, width, height); };
    int k = width / 10;
    if (!g_cells || height != 14 || width % 10 || k <= 0 || k >= kCanvasCache) return make();
    if (!g_cells->canvases[k]) g_cells->canvases[k] = make();
    return g_cells->canvases[k];
}

/* the API (TF.EX's static members); in a Level between ticks: a request for the next tick (null returned: TF.EX's callers don't use it there) */
static void* CreateArgs(void* scene, TextArgs a);
void* Create(void* scene, const char* text, int appearDuration, int stayingDuration, bool isSticky, bool withoutAnimation) {
    if (!scene || !EnsureClass()) return nullptr;
    return CreateArgs(scene, Args(text, appearDuration, stayingDuration, isSticky, withoutAnimation));
}
static void* CreateArgs(void* scene, TextArgs a) {
    g_lastMade = nullptr;
    return BetweenTicks::Run(scene, CreateThunk, &a, sizeof a) ? g_lastMade : nullptr;
}
void CreateOrDefer(void* scene, const char* text) {
    if (!scene || !EnsureClass()) return;
    TextArgs a = Args(text, 20, 250, false, false);
    BetweenTicks::Run(scene, CreateOrDeferThunk, &a, sizeof a);
}
void FlushDeferred(void* scene) { if (scene && EnsureClass()) BetweenTicks::Run(scene, FlushThunk, nullptr, 0); }
void ClearDeferred() { if (EnsureClass()) BetweenTicks::Run(CurrentScene(), ClearDeferredThunk, nullptr, 0); }
void Clear(void* scene, int layerIndex) { if (scene && EnsureClass()) BetweenTicks::Run(scene, ClearThunk, &layerIndex, sizeof layerIndex); }
void* RuntimeType() { return g_ready ? gen::Notification::RuntimeType() : nullptr; }

} // namespace tfex::Notification
