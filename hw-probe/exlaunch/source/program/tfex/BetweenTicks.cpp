/* BetweenTicks.hpp. */
#include "BetweenTicks.hpp"
#include "Game.hpp"
#include "lib.hpp"
#include <cstring>

namespace tfex::BetweenTicks {
using namespace tfex::game;
using namespace tfex::game::brute;

namespace {
struct Request { void* scene; int frame; uint32_t seq; Action fn; uint8_t payload[kPayload]; };
constexpr int kRequests = 64;
Request g_req[kRequests];
uint32_t g_nextSeq = 1;
uint32_t* g_applied = nullptr;   /* in GC memory (uncollectable): restored by a rollback with the changes the requests made */
bool g_inTick = false;
int FrameOf(void* scene) { return (int)Fn<float (*)(void*)>(0x943c60)(scene); }   /* Scene.FrameCounter: rolled back with the Level */
bool IsLevel(void* scene) { return As(scene, Type(Type_Level)) != nullptr; }

/* this Level's requests not carried out in the memory as it is now (seq above *g_applied), oldest first */
void Apply(void* scene) {
    int frame = FrameOf(scene);
    for (;;) {
        Request* best = nullptr;
        for (auto& r : g_req) if (r.seq && r.scene == scene && r.seq > *g_applied && r.frame <= frame && (!best || r.seq < best->seq)) best = &r;
        if (!best) break;
        *g_applied = best->seq;
        best->fn(scene, best->payload);
    }
    for (auto& r : g_req) if (r.seq && (r.scene != scene || r.frame < frame - 2 * 48) && r.seq <= *g_applied) r.seq = 0;   /* past any rollback */
}
HOOK_DEFINE_TRAMPOLINE(SceneUpdateHook) {   /* Monocle.Scene.Update: Level.Update calls it in every tick, resimulated ones too */
    static void Callback(void* scene) {
        bool level = g_applied && IsLevel(scene);
        if (level) { g_inTick = true; Apply(scene); }
        Orig(scene);
        if (level) g_inTick = false;
    }
};
} // namespace

bool Enable() {
    if (g_applied) return true;
    g_applied = static_cast<uint32_t*>(Fn<void* (*)(uint64_t)>(MallocUncollectable)(16));
    if (!g_applied) return false;
    *g_applied = 0;
    SceneUpdateHook::InstallAtOffset(0x944130);
    return true;
}
bool InTick() { return g_inTick; }
bool Run(void* scene, Action fn, const void* payload, size_t size) {
    if (!g_applied || g_inTick || !scene || !IsLevel(scene) || size > kPayload) { fn(scene, payload); return true; }
    Request* slot = &g_req[0];
    for (auto& r : g_req) if (!r.seq) { slot = &r; break; } else if (r.seq < slot->seq) slot = &r;   /* a free one, else the oldest */
    slot->scene = scene; slot->frame = FrameOf(scene); slot->seq = g_nextSeq++; slot->fn = fn;
    std::memset(slot->payload, 0, kPayload); std::memcpy(slot->payload, payload, size);
    return false;
}

} // namespace tfex::BetweenTicks
