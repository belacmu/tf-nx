// Prints what tests/dmath-ref.txt holds (TF.EX's DeterministicMath on the real .NET runtime), from the generated unit State_DeterministicMath.
//   P=../exlaunch/source/program; c++ -std=c++20 -O2 -w -ffp-contract=off -I$P -I$P/tfex/gen -I$P/tfex/runtime dmath-test.cpp \
//     $P/tfex/gen/tfexgen_State_DeterministicMath.cpp -o dm && ./dm | diff - dmath-ref.txt
#include <cstdio>
#include <cstring>
#include <cfloat>
#include <cmath>
#include "tfex/gen/tfexgen_State_DeterministicMath.hpp"
namespace dmath = tfex::gen::State_DeterministicMath;
static uint64_t bits(double d) { uint64_t b; memcpy(&b, &d, 8); return b; }
static uint64_t Fnv(uint64_t h, double d) {
    uint64_t b = bits(d);
    if (d != d) b = 0xFFF8000000000000ull;   // one NaN, as the C# side canonicalises
    for (int i = 0; i < 8; i++) { h ^= (b >> (8 * i)) & 0xFF; h *= 1099511628211ull; }
    return h;
}
static uint32_t state = 2463534242u;
static uint32_t Next() { state ^= state << 13; state ^= state >> 17; state ^= state << 5; return state; }
static double Rng(double lo, double hi) { return lo + ((double)(Next() >> 8) / 16777216.0) * (hi - lo); }
int main() {
    const double PI = 3.141592653589793;
    double special[] = { 0.0, -0.0, 1.0, -1.0, 0.5, 2.0, 3.0, 10.0, 0.98, 0.85, 0.9, 1e-300, 4.9e-324, 1e-9, 1e6, 1e6 + 1, -1e6, 1e7, 1e15, 1e300, PI, PI / 2, PI / 4, -PI, 2 * PI, 100 * PI, 3.725290298461914e-09, 0.4375, 0.6875, 1.1875, 2.4375, 7.3786976294838206e19, 512, 513, -3, 1024, -1075, 1075, (-__builtin_nan("")), INFINITY, -INFINITY, 4.9406564584124654e-324, DBL_MAX, -DBL_MAX };
    for (double a : special) for (double b : special) {
        double s = dmath::Sin(a), c = dmath::Cos(a), t = dmath::Atan2(a, b), p = dmath::Pow(a, b);
        printf("special %016llx %016llx %016llx %016llx %016llx %016llx\n", (unsigned long long)bits(a), (unsigned long long)bits(b), (unsigned long long)bits(s), (unsigned long long)bits(c), (unsigned long long)bits(t), (unsigned long long)bits(p));
    }
    uint64_t hs = 14695981039346656037ull, hc = hs, ha = hs, hp = hs;
    for (int i = 0; i < 200000; i++) {
        double angle = Rng(-10, 10); double y = Rng(-500, 500); double x = Rng(-500, 500); double bse = Rng(0, 2); double e = Rng(0, 3);
        hs = Fnv(hs, dmath::Sin(angle)); hc = Fnv(hc, dmath::Cos(angle));
        ha = Fnv(ha, dmath::Atan2(y, x)); hp = Fnv(hp, dmath::Pow(bse, e)); hp = Fnv(hp, dmath::Pow((double)0.98f, e)); hp = Fnv(hp, dmath::Pow((double)0.85f, e));
        double big = Rng(-3e6, 3e6); hs = Fnv(hs, dmath::Sin(big)); hc = Fnv(hc, dmath::Cos(big));
        double pb = Rng(-5, 5); double pe = std::floor(Rng(-8, 8)); hp = Fnv(hp, dmath::Pow(pb, pe));
        double qb = Rng(0.01, 100); double qe = Rng(-30, 30); hp = Fnv(hp, dmath::Pow(qb, qe));
        double ay = Rng(-1e-5, 1e-5); double ax = Rng(-1e-5, 1e-5); ha = Fnv(ha, dmath::Atan2(ay, ax));
    }
    printf("hashes sin %016llx cos %016llx atan2 %016llx pow %016llx\n", (unsigned long long)hs, (unsigned long long)hc, (unsigned long long)ha, (unsigned long long)hp);
}
