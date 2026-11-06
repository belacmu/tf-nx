/* TF.EX's DeterministicMath on the Switch (VIABILITY-TESTS step 3).
 *
 * The math itself is TF.State's DeterministicMath, generated (tfex/gen/tfexgen_State_DeterministicMath.cpp, unit
 * State_DeterministicMath; built with -ffp-contract=off, config.mk: no fused multiply-add, as .NET).
 *
 * TFEX-DIFF (the Switch can't run IL transpilers): TF.State's DeterministicMathPatch rewrites the Math.Sin/Cos/Atan2/Pow call sites
 * inside the gameplay methods it patches (Calc.AngleToVector, Calc.Angle, SineWave, Wiggler, Particle.Update, Arrow.ArrowUpdate,
 * Player.DodgingUpdate, the Ease functions, the random-level generators). Every PC use of those four functions in gameplay is in that
 * list; the others are menus, drawing and exact integer powers (PC decompile: MapSeedSelect, MatchSettings, Draw, Mud,
 * DarkWorldTowerData). So the Switch redirects the four managed wrapper functions (Math.Sin etc., each a single `b` to the C library's
 * PLT stub) to DeterministicMath for every caller. FNA and library code that calls the PLT stubs directly is left alone.
 *
 *   Math.Sin +0xad50 -> PLT +0x2861dd0      Math.Cos +0xad60 -> +0x2861de0
 *   Math.Atan2 +0xade0 -> +0x2861e60        Math.Pow +0xae20 -> +0x2861ea0
 *
 * Self-test: the four hashes over 200,000 game-like inputs, called through the game's own wrapper
 * addresses, must equal the .NET reference (hw-probe/tests/dmath-ref.txt). That also checks that this toolchain
 * emitted no fused multiply-adds. */
#include "lib.hpp"
#include "loggers.hpp"
#include "patch/code_patcher.hpp"
#include <lib/util/sys/modules.hpp>
#include <common.hpp>

#include <cstdint>
#include <cstring>

#include "tfex/gen/tfexgen_State_DeterministicMath.hpp"
#include "hwlog.hpp"

extern "C" {
double dm_sin(double x) { return tfex::gen::State_DeterministicMath::Sin(x); }
double dm_cos(double x) { return tfex::gen::State_DeterministicMath::Cos(x); }
double dm_atan2(double y, double x) { return tfex::gen::State_DeterministicMath::Atan2(y, x); }
double dm_pow(double x, double y) { return tfex::gen::State_DeterministicMath::Pow(x, y); }
}

namespace dm {

static uintptr_t Base() { return exl::util::GetMainModuleInfo().m_Total.m_Start; }

struct Wrapper { const char* name; uintptr_t offset, stub; const void* ours; };

static bool Redirect(const Wrapper& w) {
    uint32_t insn = *reinterpret_cast<uint32_t*>(Base() + w.offset);
    int64_t imm = insn & 0x3FFFFFF;
    if (imm & (1 << 25)) imm -= (int64_t)1 << 26;
    bool isB = (insn & 0xFC000000) == 0x14000000;
    if (!isB || w.offset + imm * 4 != w.stub) {
        hw::Log("[dm] Math.%s at +%#lx isn't `b +%#lx` (%08x): NOT redirected", w.name, (unsigned long)w.offset, (unsigned long)w.stub, insn);
        return false;
    }
    int64_t delta = ((int64_t)reinterpret_cast<uintptr_t>(w.ours) - (int64_t)(Base() + w.offset)) / 4;
    if (delta >= ((int64_t)1 << 25) || delta < -((int64_t)1 << 25)) {
        hw::Log("[dm] Math.%s: the replacement is out of branch range", w.name);
        return false;
    }
    exl::patch::CodePatcher p(w.offset);
    p.Write<uint32_t>(0x14000000u | ((uint32_t)delta & 0x3FFFFFF));
    return true;
}

static uint64_t Fnv(uint64_t h, double d) {
    uint64_t b; std::memcpy(&b, &d, 8);
    if (d != d) b = 0xFFF8000000000000ull;
    for (int i = 0; i < 8; i++) { h ^= (b >> (8 * i)) & 0xFF; h *= 1099511628211ull; }
    return h;
}
static uint32_t g_state;
static uint32_t NextU() { g_state ^= g_state << 13; g_state ^= g_state >> 17; g_state ^= g_state << 5; return g_state; }
static double Rng(double lo, double hi) { return lo + ((double)(NextU() >> 8) / 16777216.0) * (hi - lo); }

/* The same 200,000-input hashes as tests/dmath-test.cpp, but through the game's wrapper addresses. */
static void SelfTest() {
    using F1 = double (*)(double);
    using F2 = double (*)(double, double);
    auto sin_ = reinterpret_cast<F1>(Base() + 0xad50);
    auto cos_ = reinterpret_cast<F1>(Base() + 0xad60);
    auto atan2_ = reinterpret_cast<F2>(Base() + 0xade0);
    auto pow_ = reinterpret_cast<F2>(Base() + 0xae20);
    g_state = 2463534242u;
    uint64_t hs = 14695981039346656037ull, hc = hs, ha = hs, hp = hs;
    uint64_t t0 = svcGetSystemTick();
    for (int i = 0; i < 200000; i++) {
        double angle = Rng(-10, 10); double y = Rng(-500, 500); double x = Rng(-500, 500); double bse = Rng(0, 2); double e = Rng(0, 3);
        hs = Fnv(hs, sin_(angle)); hc = Fnv(hc, cos_(angle));
        ha = Fnv(ha, atan2_(y, x)); hp = Fnv(hp, pow_(bse, e)); hp = Fnv(hp, pow_((double)0.98f, e)); hp = Fnv(hp, pow_((double)0.85f, e));
        double big = Rng(-3e6, 3e6); hs = Fnv(hs, sin_(big)); hc = Fnv(hc, cos_(big));
        double pb = Rng(-5, 5); double pe = __builtin_floor(Rng(-8, 8)); hp = Fnv(hp, pow_(pb, pe));
        double qb = Rng(0.01, 100); double qe = Rng(-30, 30); hp = Fnv(hp, pow_(qb, qe));
        double ay = Rng(-1e-5, 1e-5); double ax = Rng(-1e-5, 1e-5); ha = Fnv(ha, atan2_(ay, ax));
    }
    double ms = (svcGetSystemTick() - t0) / 19200.0;
    bool ok = hs == 0xce7dcc6f8e1d2c87ull && hc == 0x9c9e3438fa8667caull && ha == 0xeaace4c67e471e6bull && hp == 0xc046da3aeaff708dull;
    hw::Log("[dm] self-test through the game's Math wrappers, 200,000 inputs: sin %016llx cos %016llx atan2 %016llx pow %016llx: %s (%.0f ms)",
            (unsigned long long)hs, (unsigned long long)hc, (unsigned long long)ha, (unsigned long long)hp, ok ? "matches TF.EX's DeterministicMath on .NET" : "DOES NOT MATCH", ms);
}

void Install() {
    Wrapper ws[] = {
        { "Sin", 0xad50, 0x2861dd0, reinterpret_cast<const void*>(&dm_sin) },
        { "Cos", 0xad60, 0x2861de0, reinterpret_cast<const void*>(&dm_cos) },
        { "Atan2", 0xade0, 0x2861e60, reinterpret_cast<const void*>(&dm_atan2) },
        { "Pow", 0xae20, 0x2861ea0, reinterpret_cast<const void*>(&dm_pow) },
    };
    bool ok = true;
    for (auto& w : ws) ok &= Redirect(w);
    if (!ok) return;
    hw::Log("[dm] Math.Sin/Cos/Atan2/Pow redirected to TF.EX's DeterministicMath");
    SelfTest();
}

} // namespace dm

void dm_main() { dm::Install(); }
