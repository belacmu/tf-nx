/* TFEX-DIFF S-hud: TF.EX's HUD entities in a Level (MatchEndChoices, LightPauseMenu) draw in the 320-wide game's coordinates
 * (WiderSet's UIXOffset is the only shift TF.EX knows). The Switch's Level HUD layer (layer 4, a CameraLayer) draws in the console's
 * 420-wide canvas instead: the 320-wide level sits at x 50..370 between the side art, which is drawn over the HUD. So TF.EX's vote panel
 * at (2, 32) was under the left side art (2026-10-17: a probe string drawn at (120, 120) from the panel's Render showed at the level's
 * x 70). SwitchHud::Shift around such an entity's Render ends the layer's SpriteBatch, begins it again with the same state and the
 * transform moved kCanvasX right, and restores it afterwards: TF.EX's coordinates land where TF.EX means them.
 *
 * FNA's SpriteBatch on the Switch (Begin 0xc49d0, End 0xc4e70, read from Begin's body): sortMode +0x48, blend +0x50, sampler +0x58,
 * depthStencil +0x60, rasterizer +0x68, effect +0x70, beginCalled +0x78 (byte), the transform as Begin's Matrix? argument at +0x98
 * (64-byte Matrix, then hasValue at +0x40). CameraLayer.Render (0x90ff10) begins it with Camera.Matrix, renders the entities, ends it. */
#pragma once
#include "gen/tfexgen_binding.hpp"
#include "runtime/tfexgen_code_rt.hpp"
#include <cstdint>
#include <cstring>

namespace tfex::SwitchHud {

constexpr float kCanvasX = 50.0f;   /* (420 - 320) / 2: the Switch canvas' left side art */

struct Shift {
    struct NullableMatrix { float m[16]; uint8_t hasValue; uint8_t pad_[3]; };
    static_assert(sizeof(NullableMatrix) == 0x44);
    uint8_t* sb = nullptr;
    NullableMatrix saved{};
    static constexpr uintptr_t kBegin = 0xc49d0, kEnd = 0xc4e70;
    using BeginFn = void (*)(void*, int32_t, void*, void*, void*, void*, void*, NullableMatrix*);
    using EndFn = void (*)(void*);

    void Begin(NullableMatrix* m) {
        gen::Fn<BeginFn>(kBegin)(sb, *reinterpret_cast<int32_t*>(sb + 0x48), *reinterpret_cast<void**>(sb + 0x50),
                                 *reinterpret_cast<void**>(sb + 0x58), *reinterpret_cast<void**>(sb + 0x60),
                                 *reinterpret_cast<void**>(sb + 0x68), *reinterpret_cast<void**>(sb + 0x70), m);
    }
    explicit Shift(float dx = kCanvasX) {
        uint8_t* b = static_cast<uint8_t*>(gen::Fn<void* (*)()>(gen::bind::m_Draw_get_SpriteBatch)());
        if (!b || !b[0x78] || *reinterpret_cast<int32_t*>(b + 0x48) == 1) return;   /* not begun, or Immediate (draws at once: left alone) */
        sb = b;
        std::memcpy(&saved, sb + 0x98, sizeof saved);
        NullableMatrix m = saved;
        if (!m.hasValue) { std::memset(m.m, 0, sizeof m.m); m.m[0] = m.m[5] = m.m[10] = m.m[15] = 1.0f; m.hasValue = 1; }
        for (int c = 0; c < 4; c++) m.m[12 + c] += dx * m.m[c];   /* (v + (dx, 0)) * M: row 4 += dx * row 1 */
        gen::Fn<EndFn>(kEnd)(sb);   /* what the layer drew so far, with its own transform */
        Begin(&m);
    }
    ~Shift() {
        if (!sb) return;
        gen::Fn<EndFn>(kEnd)(sb);
        Begin(&saved);
    }
    Shift(const Shift&) = delete;
    Shift& operator=(const Shift&) = delete;
};

} // namespace tfex::SwitchHud
