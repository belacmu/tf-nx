// Prints what tests/digest-ref.txt holds (tests/refgen `digest`: the C# twin of digest.hpp, GameplayDigest in refgen/Digest.cs).
//   c++ -std=c++17 -O2 -I../exlaunch/source/program digest-test.cpp -o /tmp/dg && /tmp/dg | diff - digest-ref.txt
#include <cstdio>
#include "digest.hpp"

static uint64_t x = 0x2545F4914F6CDD1Dull;
static uint64_t R() { x ^= x << 13; x ^= x >> 7; x ^= x << 17; return x; }
static float F() { return (float)((int64_t)(R() % 640000) - 160000) / 1000.f; }
int main() {
    static digest::Input d;
    for (int c = 0; c < 40; c++) {
        d = digest::Input();
        for (int i = 0; i < 4; i++) d.rng[i] = R();
        for (int s = 0; s < 4; s++) {   // C# object initializer order: Alive, X, Y, SX, SY
            bool a = R() % 3 != 0;
            float px = F(), py = F(), sx = F(), sy = F();
            d.seat[s] = { a, px, py, sx, sy };
        }
        int n = (int)(R() % (c < 20 ? 6u : 40u));
        for (int k = 0; k < n; k++) { float ax = F(), ay = F(); d.arrow[k] = { ax, ay }; }
        d.nArrows = n;
        if (c == 7) { if (n > 0) d.arrow[n] = d.arrow[0]; else { d.arrow[n].x = 1.f; d.arrow[n].y = 2.f; } d.nArrows = n + 1; }
        if (c == 9 && n > 0) d.arrow[0].x = -0.f;
        printf("digest %d %016llx\n", c, (unsigned long long)digest::Compute(d));
    }
}
