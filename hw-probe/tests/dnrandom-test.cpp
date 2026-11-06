// Prints what tests/dnrandom-ref.txt holds (from the real .NET runtime), from the C++ port.
//   c++ -std=c++17 -O1 -I../exlaunch/source/program dnrandom-test.cpp -o /tmp/dnr && /tmp/dnr | diff - dnrandom-ref.txt
#include <cstdio>
#include <cstring>
#include "dnrandom.hpp"
using namespace dnrandom;
int main() {
    int32_t seeds[] = { 0, 1, 42, -1, 123456789, INT32_MIN, INT32_MAX, 161803398, -2147483647, 1000000007 };
    for (int32_t seed : seeds) {
        State s; Init(&s, seed);
        printf("seed %d\n", seed);
        printf("next:"); for (int i = 0; i < 12; i++) printf(" %d", Next(&s)); printf("\n");
        printf("max100:"); for (int i = 0; i < 12; i++) printf(" %d", Next(&s, 100)); printf("\n");
        printf("range5_10:"); for (int i = 0; i < 12; i++) printf(" %d", Next(&s, 5, 10)); printf("\n");
        printf("range-50_50:"); for (int i = 0; i < 12; i++) printf(" %d", Next(&s, -50, 50)); printf("\n");
        printf("large:"); for (int i = 0; i < 6; i++) printf(" %d", Next(&s, INT32_MIN, INT32_MAX)); printf("\n");
        printf("max1:"); for (int i = 0; i < 4; i++) printf(" %d", Next(&s, 1)); printf("\n");
        printf("max0:"); for (int i = 0; i < 2; i++) printf(" %d", Next(&s, 0)); printf("\n");
        printf("eq3_3: %d\n", Next(&s, 3, 3));
        printf("double:"); for (int i = 0; i < 6; i++) { double d = Sample(&s); uint64_t b; memcpy(&b, &d, 8); printf(" %016llx", (unsigned long long)b); } printf("\n");
        uint8_t buf[19]; NextBytes(&s, buf, 19);
        printf("bytes:"); for (uint8_t x : buf) printf(" %d", x); printf("\n");
        int32_t last = 0; for (int i = 0; i < 2000; i++) last = Next(&s);
        printf("after2000: %d\n", last);
    }
}
