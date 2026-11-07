/* TeamPlusOutlines.cpp: Teams+'s yellow team in the Switch's team outlines (TFEX-DIFF S-native: the PC has no outline layer. The
 * Switch's styling, the user 2026-11-07: a player on a team has an outline of the team's colour, made as blue and red are; a player on
 * no team (a lone wolf) none).
 *
 * The Switch game's TeamOutlinesLayer (a team match's layer, made by its Level ctor) knows two teams: the Player ctor tags each player
 * 0x27 (the red canvas) if its allegiance is 1, else 0x26 (the blue canvas) (+0xae698c..+0xae69a4); BeforeRender (+0xb68c40) draws the
 * tagged players into the two canvases, Render (+0xb69080) draws them tinted. In a Teams+ match yellow (2) and the lone wolves (3+)
 * fell to "else": every archer had a blue outline. Here, as the hand-written three-team mod did before Teams+ (git 72ff4750^,
 * megamod/threeteam.hpp: YellowRingFillHook / YellowRingRenderHook, EntityAddTagHook; the scene's tag table is full at 0x28, so yellow
 * can't have a tag of its own: its players are picked by team instead):
 *  - the Player ctor's outline tag only for blue (0) and red (1) in a Teams+ match (the generated Player.ctor hook's shell gives the
 *    allegiance, a hook on Entity.AddTag skips that one call site);
 *  - after the game's two fills, the yellow team drawn the same way (the layer's canvas recipe, its materials, each player's two
 *    outline passes, vtable +0xf8 / +0x100) into a third canvas; after its Render, that one drawn tinted Teams+'s yellow (its ColorB,
 *    as the game's blue and red).
 * BeforeRender runs once per drawn frame (xplay.cpp TeamOutlinesBeforeDraw, the 240 Hz fix), so do these fills. */
#include "lib.hpp"
#include <lib/util/sys/modules.hpp>
#include "../hwlog.hpp"
#include "gen/tfexgen_binding.hpp"
#include "gen/tfexgen_hooks.hpp"
#include "gen/tfexgen_Tp_Teams3.hpp"
#include "runtime/tfexgen_code_rt.hpp"
#include <cstdint>

namespace tfex::teamplus {
namespace {
uintptr_t Base() { return exl::util::GetMainModuleInfo().m_Total.m_Start; }
template <class F> F Call(uintptr_t off) { return reinterpret_cast<F>(Base() + off); }
template <class T> T& At(void* o, uint32_t off) { return *reinterpret_cast<T*>(static_cast<uint8_t*>(o) + off); }
void* GotObj(uintptr_t slot) { return **reinterpret_cast<void***>(Base() + slot); }

/* game functions and fields (v1.4.1.6; the three-team mod's resolve_ring / resolve_plan notes) */
constexpr uintptr_t kTagCallRet = 0xae69a8;        /* Player ctor: the return address of its outline-tag AddTag call */
constexpr uintptr_t kEntityAddTag = 0x92fef0;      /* Entity.AddTag(entity, tag) */
constexpr uintptr_t kLayerGetScene = 0x936ac0;     /* (layer) -> scene */
constexpr uintptr_t kLevelGetPlayers = 0xa7b8b0;   /* (level) -> List<Player> */
constexpr uintptr_t kListEnumMoveNext = 0x912ef0;  /* (&{list, index, count, current}) -> bool */
constexpr uintptr_t kPlayerGetDead = 0xaec3a0;     /* (player) -> bool */
constexpr uint32_t kPlayerTeamColor = 0x10c, kListCount = 0x1c;
constexpr uintptr_t kCanvasNew = 0x9100b0;         /* (null, w, h) -> canvas (the layer ctor's recipe) */
constexpr uintptr_t kScreenWidthGot = 0x4792130;   /* the canvases' width (the layer ctor's) */
constexpr uintptr_t kSetTarget = 0x922ed0;         /* (canvas): the fill's render target */
constexpr uintptr_t kClear = 0x923160;             /* (packed colour) */
constexpr uintptr_t kDrawCtx = 0x9228e0;           /* () -> the draw context */
constexpr uintptr_t kBatchBegin = 0xc49d0;         /* (ctx, 0, material, m3, m4, m5, 0, &buf48) */
constexpr uintptr_t kBatchEnd = 0xc4e70;           /* (ctx) */
constexpr uintptr_t kCanvasGraphic = 0x9563c0;     /* (canvas) -> its drawable */
constexpr uintptr_t kVecZero = 0x105720;           /* () -> the draw position */
constexpr uintptr_t kDrawTinted = 0xc5f50;         /* (ctx, graphic, packed colour, x, y) */
constexpr uintptr_t kM3 = 0x4727cf0, kM4 = 0x4727048, kM5 = 0x4727700, kRenderMaterial = 0x4726f70;   /* GOT slots */

int g_ctorAllegiance = -1;   /* the Player being constructed (its allegiance), while its ctor runs */
void Ctor(tfex::gen::hooks::Player_ctor& c) {
    const int prev = g_ctorAllegiance;
    g_ctorAllegiance = c.allegiance;
    c();
    g_ctorAllegiance = prev;
}
int g_skipped = 0;
HOOK_DEFINE_TRAMPOLINE(AddTagHook) {
    static void Callback(void* entity, int32_t tag) {
        if (g_ctorAllegiance > 1 && (tag == 0x26 || tag == 0x27) && tfex::gen::Tp_Teams3::get_Live()
            && reinterpret_cast<uintptr_t>(__builtin_return_address(0)) - Base() == kTagCallRet) {
            if (g_skipped++ < 4) hw::Log("[teamplus] outline: a player of group %d not in the blue canvas (%s)", g_ctorAllegiance, g_ctorAllegiance == tfex::gen::Tp_Teams3::Yellow ? "yellow: its own outline" : "a lone wolf: none");
            return;
        }
        Orig(entity, tag);
    }
};

struct ListEnum { void* list; int32_t idx; int32_t count; void* current; };
void** g_canvas = nullptr;   /* [0]: the yellow canvas, in uncollectable memory (the module's .bss is no GC root) */
bool g_filled = false;
int CollectYellow(void* level, void** out, int cap) {
    void* list = Call<void* (*)(void*)>(kLevelGetPlayers)(level);
    if (!list) return 0;
    ListEnum e{ list, 0, At<int32_t>(list, kListCount), nullptr };
    int n = 0;
    while (n < cap && (Call<int (*)(void*)>(kListEnumMoveNext)(&e) & 1)) {
        void* p = e.current;
        if (!p || (Call<int (*)(void*)>(kPlayerGetDead)(p) & 1)) continue;
        if (At<int32_t>(p, kPlayerTeamColor) == tfex::gen::Tp_Teams3::Yellow) out[n++] = p;
    }
    return n;
}
}  // namespace

/* after the game's BeforeRender (xplay.cpp's TeamOutlinesHook): the yellow team into the third canvas, as the game fills blue and red */
void OutlinesFill(void* layer) {
    g_filled = false;
    if (!layer || !tfex::gen::Tp_Teams3::get_Live()) return;
    void* level = Call<void* (*)(void*)>(kLayerGetScene)(layer);
    if (!level) return;
    void* players[8];
    const int n = CollectYellow(level, players, 8);
    if (!n) return;
    if (!g_canvas) g_canvas = static_cast<void**>(tfex::gen::RuntimeAlloc(sizeof(void*)));
    if (!g_canvas) return;
    if (!g_canvas[0]) {
        g_canvas[0] = Call<void* (*)(void*, int, int)>(kCanvasNew)(nullptr, **reinterpret_cast<int**>(Base() + kScreenWidthGot), 0xf0);
        if (!g_canvas[0]) { hw::Log("[teamplus] ERROR: the yellow outline canvas couldn't be made: no yellow outlines"); return; }
        hw::Log("[teamplus] outline canvas for the yellow team made");
    }
    Call<void (*)(void*)>(kSetTarget)(g_canvas[0]);
    Call<void (*)(uint32_t)>(kClear)(At<uint32_t>(layer, 0x90));
    for (int pass = 0; pass < 2; pass++) {   /* the game's two passes: the outline, then the hollow */
        uint8_t buf[0x48] = {};
        void* ctx = Call<void* (*)()>(kDrawCtx)();
        Call<void (*)(void*, void*, void*, void*, void*, void*, void*, void*)>(kBatchBegin)(
            ctx, nullptr, At<void*>(layer, pass == 0 ? 0x80 : 0x88), GotObj(kM3), GotObj(kM4), GotObj(kM5), nullptr, buf);
        for (int i = 0; i < n; i++) tfex::gen::Virt<void>(players[i], pass == 0 ? 0xf8 : 0x100);
        Call<void (*)(void*)>(kBatchEnd)(Call<void* (*)()>(kDrawCtx)());
    }
    g_filled = true;
}

namespace {
HOOK_DEFINE_TRAMPOLINE(RenderHook) {
    static void Callback(void* layer) {
        Orig(layer);
        if (!g_filled || !g_canvas || !g_canvas[0] || !tfex::gen::Tp_Teams3::get_Live()) return;
        struct V2 { float x, y; };
        void* ctx = Call<void* (*)()>(kDrawCtx)();
        uint8_t buf[0x48] = {};
        Call<void (*)(void*, void*, void*, void*, void*, void*, void*, void*)>(kBatchBegin)(
            ctx, nullptr, GotObj(kRenderMaterial), GotObj(kM3), GotObj(kM4), GotObj(kM5), nullptr, buf);
        void* gfx = Call<void* (*)(void*)>(kCanvasGraphic)(g_canvas[0]);
        V2 v = Call<V2 (*)()>(kVecZero)();
        Call<void (*)(void*, void*, uint32_t, float, float)>(kDrawTinted)(ctx, gfx, tfex::gen::Tp_Teams3::YellowColorB().packed, v.x, v.y);
        Call<void (*)(void*)>(kBatchEnd)(Call<void* (*)()>(kDrawCtx)());
        static bool said = false;
        if (!said) { said = true; hw::Log("[teamplus] outline: the yellow team's outlines drawn"); }
    }
};
}  // namespace

void InstallOutlines() {
    static bool done = false;
    if (done) return;
    done = true;
    tfex::gen::hooks::Player_ctor::Wrap(&Ctor);
    AddTagHook::InstallAtOffset(kEntityAddTag);
    RenderHook::InstallAtOffset(0xb69080);   /* TeamOutlinesLayer.Render */
    hw::Log("[teamplus] team outlines: the yellow team's beside the game's blue and red, none for lone wolves");
}
}  // namespace tfex::teamplus
