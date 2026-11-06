// Prints what tests/miasma-ref.txt holds (TF.EX's real MiasmaSequenceController + Miasma model + TowerFall's Monocle.Ease +
// FNA's MathHelper, run on the .NET runtime; generator: scratchpad miasma-golden/Program.cs, see MIASMA-PORT.md).
//   c++ -std=c++17 -O2 -ffp-contract=off -I../exlaunch/source/program miasma-test.cpp -o /tmp/ms && /tmp/ms | diff - miasma-ref.txt
// (also passes with -O0, -O3, and with clang's default fp-contract: the header pins contraction off itself.)
// `--dump` additionally prints every evaluated value (the lines starting "d "), for diffing a mismatch against the C# side.
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <vector>
#include "miasma_seq.hpp"
using namespace miasma_seq;

static bool dump = false;
static uint32_t B(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
static float Fl(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }

struct H {
    uint64_t h = 14695981039346656037ull, n = 0;
    void U(uint32_t v) { for (int i = 0; i < 4; i++) { h ^= (v >> (8 * i)) & 0xFF; h *= 1099511628211ull; } n++; }
};
static uint32_t xs = 2463534242u;
static uint32_t Next() { xs ^= xs << 13; xs ^= xs >> 17; xs ^= xs << 5; return xs; }

static std::vector<float> SetInt() { std::vector<float> l; for (int i = 0; i <= 4000; i++) l.push_back((float)i); return l; }
static std::vector<float> SetAcc() { std::vector<float> l; float a = 0.0f; for (int i = 0; i < 4000; i++) { a += 1.0000019f; l.push_back(a); } return l; }
static std::vector<float> SetRand() { std::vector<float> l; xs = 2463534242u; for (int i = 0; i < 20000; i++) { float v = (float)(Next() >> 8) / 16777216.0f * 4100.0f; l.push_back(v); } return l; }
static std::vector<float> SetEdge() {
    const float bps[] = { 0, 20, 60, 100, 120, 180, 240, 420, 480, 540, 720, 900, 960, 1140, 1380, 1620, 2100, 2220, 3060, 3180, 4000 };
    std::vector<float> l;
    for (float b : bps) for (int k = -4; k <= 4; k++) { if (b == 0 && k < 0) continue; l.push_back(Fl((uint32_t)((int64_t)B(b) + k))); }
    return l;
}
static void EvLine(int mode, int dir, float t) {
    Result r = Evaluate(mode, t, dir);
    printf("eval %d %d %08x %08x %08x %d%d\n", mode, dir, B(t), B(r.Percent), B(r.SideWeight), r.Collidable ? 1 : 0, r.NervesOfSteelCheck ? 1 : 0);
}

int main(int argc, char** argv) {
    dump = argc > 1 && !strcmp(argv[1], "--dump");
    { State d = State::Default(); uint64_t db; double dd = d.ActualDepth; memcpy(&db, &dd, 8);
      printf("default-counter %08x mode %d depth %016llx\n", B(d.Counter), d.Mode, (unsigned long long)db); }
    struct NS { const char* name; std::vector<float> set; };
    std::vector<NS> sets = { { "int", SetInt() }, { "acc", SetAcc() }, { "rand", SetRand() }, { "edge", SetEdge() } };
    const int dirs[3] = { 0, 1, -1 };

    for (int mode = 0; mode <= 3; mode++) for (int dir : dirs) for (auto& s : sets) {
        H h;
        for (float t : s.set) {
            Result r = Evaluate(mode, t, dir);
            h.U(B(t)); h.U(B(r.Percent)); h.U(B(r.SideWeight)); h.U((r.Collidable ? 1u : 0u) | (r.NervesOfSteelCheck ? 2u : 0u));
            if (dump) printf("d eval %d %d %08x %08x %08x %d%d\n", mode, dir, B(t), B(r.Percent), B(r.SideWeight), r.Collidable ? 1 : 0, r.NervesOfSteelCheck ? 1 : 0);
        }
        printf("hash eval mode %d dir %d set %s n %llu %016llx\n", mode, dir, s.name, (unsigned long long)(h.n / 4), (unsigned long long)h.h);
    }
    for (int mode = 0; mode <= 2; mode++) for (int dir : dirs) {
        for (float t : SetEdge()) EvLine(mode, dir, t);
        for (int i = 0; i <= 4000; i += 20) EvLine(mode, dir, (float)i);
    }

    for (int mode = 0; mode <= 3; mode++) for (int dir : dirs) for (auto& s : sets) {
        H h; uint32_t trues = 0;
        for (float t : s.set) { bool b = ShouldRollAmaranthDir(mode, t, dir); h.U(B(t)); h.U(b ? 1u : 0u); if (b) trues++; }
        printf("hash roll mode %d dir %d set %s n %llu trues %u %016llx\n", mode, dir, s.name, (unsigned long long)(h.n / 2), trues, (unsigned long long)h.h);
    }
    { std::vector<float> a = SetInt(), c = SetAcc();
      for (float t : a) if (ShouldRollAmaranthDir(1, t, 0)) { printf("first-roll int %08x\n", B(t)); break; }
      for (float t : c) if (ShouldRollAmaranthDir(1, t, 0)) { printf("first-roll acc %08x\n", B(t)); break; } }

    const float pSpecial[] = { -0.1f, 0.0f, 0.05f, 0.1f, 0.25f, 0.33f, 0.15f, 1.05f, 0.2f, 0.3333333f, 0.016666668f, 0.3f, 0.34f, 0.3333f, 0.9999f, 0.31666666f };
    const float sSpecial[] = { 0.0f, 0.25f, -0.25f, 0.1234f };
    const float tSpecial[] = { 0.0f, 1.0f, 5.0f, 10.0f, 19.0f, 19.999998f, 20.0f, 21.0f, 50.0f, 99.0f, 99.99999f, 100.0f, 100.00001f, 101.0f, 1.0000019f, 2.0000038f, 59.99999f, 60.0f, 1000.0f, -1.0f };
    for (float p : pSpecial) for (float s : sSpecial) for (float t : tSpecial) {
        float pc, sw; bool rm;
        EvaluateDissipate(t, p, s, &pc, &sw, &rm);
        printf("diss %08x %08x %08x %08x %08x %d\n", B(t), B(p), B(s), B(pc), B(sw), rm ? 1 : 0);
    }
    {
        std::vector<float> pr; for (int i = 0; i <= 1700; i += 11) pr.push_back(Evaluate(0, (float)i, 0).Percent);
        std::vector<float> sr(sSpecial, sSpecial + 4);
        for (int i = 180; i <= 4000; i += 122) { sr.push_back(Evaluate(1, (float)i, 1).SideWeight); sr.push_back(Evaluate(1, (float)i, -1).SideWeight); }
        std::vector<float> tr; for (int i = 0; i <= 110; i++) tr.push_back((float)i);
        float a = 0.0f; for (int i = 0; i < 110; i++) { a += 1.0000019f; tr.push_back(a); }
        xs = 7; for (int i = 0; i < 200; i++) tr.push_back((float)(Next() >> 8) / 16777216.0f * 110.0f);
        H h;
        for (float p : pr) for (float s : sr) for (float t : tr) {
            float pc, sw; bool rm;
            EvaluateDissipate(t, p, s, &pc, &sw, &rm);
            h.U(B(pc)); h.U(B(sw)); h.U(rm ? 1u : 0u);
            if (dump) printf("d diss %08x %08x %08x %08x %08x %d\n", B(t), B(p), B(s), B(pc), B(sw), rm ? 1 : 0);
        }
        printf("hash diss p %zu s %zu t %zu %016llx\n", pr.size(), sr.size(), tr.size(), (unsigned long long)h.h);
    }

    // Per-tick state machine through Step()/BeginDissipate(): the same scenario as the C# side.
    const float tms[3] = { 1.0000019f, 1.0f, 0.5f };
    for (float tm : tms) for (int mode : { 0, 1, 2 }) {
        H h; int sims = 0; uint64_t rolls = 0;
        int stepT = mode == 0 ? 1 : 7;
        for (int T = 1; T <= 4000; T += stepT) {
            sims++;
            State m = State::Default(); m.Mode = mode;
            float ePercent = -0.1f, eSide = 0.0f; bool eCol = false, eNerves = false, removed = false;
            uint32_t rngState = 12345u + (uint32_t)T;
            int maxSteps = T + 120;
            auto rngNext2 = [&]() -> int { rngState ^= rngState << 13; rngState ^= rngState >> 17; rngState ^= rngState << 5; return (int)(rngState % 2u); };
            for (int s = 1; s <= maxSteps && !removed; s++) {
                if (s == T + 1 && !m.IsDissipating) { BeginDissipate(m, ePercent, eSide); h.U(0xD1550000u); }
                StepOut o = Step(m, tm, rngNext2);
                ePercent = o.Percent; eSide = o.SideWeight; eCol = o.Collidable;
                if (o.SetNerves) eNerves = o.Nerves;
                if (o.RemoveSelf) removed = true;
                if (o.RolledDir) rolls++;
                h.U(B(ePercent)); h.U(B(eSide)); h.U((eCol ? 1u : 0u) | (eNerves ? 2u : 0u) | (removed ? 4u : 0u) | (m.IsDissipating ? 8u : 0u)); h.U((uint32_t)(m.Dir + 1));
                h.U(B(m.Ticks)); h.U(B(m.DissipateTicks));
            }
        }
        printf("hash sim tm %08x mode %d sims %d rolls %llu words %llu %016llx\n", B(tm), mode, sims, (unsigned long long)rolls, (unsigned long long)h.n, (unsigned long long)h.h);
    }
    return 0;
}
