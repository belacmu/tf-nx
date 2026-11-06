// Prints what tests/teamspawn-ref.txt holds (made by tests/refgen: TF.EX's own DeterministicRandom on .NET through verbatim copies
// of Calc.Shuffle, CalcExtensions.OwnShuffledIndexes and RoundLogicPatch.ApplyOrder; and SpikeballPatch's Choose(1, -1)).
//   c++ -std=c++17 -O2 -I../exlaunch/source/program teamspawn-test.cpp -o /tmp/ts && /tmp/ts | diff - teamspawn-ref.txt
#include <cstdio>
#include <string>
#include "teamspawn.hpp"
using namespace teamspawn;

// .NET's float "R" formatting for the values used here (all exact in a few decimals).
static std::string F(float f) { char b[32]; snprintf(b, sizeof b, "%.9g", (double)f); return b; }
static void L(const char* tag, const V2* v, int n) {
    printf("  %s ", tag);   // C#: $"  A {string.Join(" ", ...)}"
    for (int i = 0; i < n; i++) printf("%s(%s,%s)", i ? " " : "", F(v[i].x).c_str(), F(v[i].y).c_str());
    printf("\n");
}
int main() {
    int seeds[] = { 0, 1, 42, 12345, -1, 2147483647, 987654321 };
    int sizes[][2] = { { 1, 1 }, { 2, 2 }, { 3, 2 }, { 2, 3 }, { 4, 4 }, { 6, 6 }, { 1, 3 }, { 0, 2 }, { 5, 1 } };
    for (int seed : seeds) {
        for (auto& sz : sizes) {
            xoshiro::State r; xoshiro::Seed(&r, seed);
            V2 A[16], B[16], a2[16], b2[16];
            for (int i = 0; i < sz[0]; i++) A[i] = { 20.f + 13.5f * i, 40.f + 30.f * i };
            for (int i = 0; i < sz[1]; i++) B[i] = { 300.f - 13.5f * i, 40.f + 30.f * i };
            int n = sz[0] > sz[1] ? sz[0] : sz[1];
            int32_t order[16];
            int draws = ShuffledIndexes(&r, n, order);
            if (draws != (n > 0 ? n - 1 : 0)) printf("WRONG DRAW COUNT %d\n", draws);
            int na = ApplyOrder(A, sz[0], order, n, a2), nb = ApplyOrder(B, sz[1], order, n, b2);
            printf("teams seed %d A %d B %d: order", seed, sz[0], sz[1]);
            for (int i = 0; i < n; i++) printf(" %d", order[i]);
            printf("\n");
            L("A", a2, na);
            L("B", b2, nb);
            printf("  next %d\n", xoshiro::Next(&r));
        }
        xoshiro::State s; xoshiro::Seed(&s, seed);
        printf("spindir seed %d:", seed);
        for (int i = 0; i < 12; i++) printf(" %d", xoshiro::Next(&s, 2) == 0 ? 1 : -1);
        printf("\n");
    }
}
