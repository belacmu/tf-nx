// Host test of the generated InputCodec (tfexgen_Auto_InputCodec.*): TF.EX's InputCodecTests (tests/TF.EX.Utils/ReplayComparer.cs) on
// the C++ the Switch runs. AimAxesRoundTripBitExact: 10,000 floats in -1..1 survive the int bits and back (FromFloat is
// BitConverter.SingleToInt32Bits, not generated: the Switch doesn't call it, so its .NET definition is used here); SeatCountFollowsStride.
#include "tfexgen_Auto_InputCodec.hpp"
#include <cstdio>
#include <cstring>
#include <random>

namespace gen = tfex::gen;
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; std::fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); std::fprintf(stderr, __VA_ARGS__); std::fputc('\n', stderr); } } while (0)

int main() {
    std::mt19937 rng(1234);
    std::uniform_real_distribution<double> d(0.0, 1.0);
    for (int i = 0; i < 10000; i++) {
        float value = (float)(d(rng) * 2.0 - 1.0);
        int32_t bits; std::memcpy(&bits, &value, 4);
        float back = gen::Auto_InputCodec::ToFloat(bits);
        CHECK(std::memcmp(&back, &value, 4) == 0, "round trip of %.9g gave %.9g", value, back);
    }
    static int32_t flat[16 * 4];
    CHECK(gen::Auto_InputCodec::SeatCount(gen::Arr<int32_t>{flat, 16 * 4}) == 4, "SeatCount(new int[Stride * 4]) != 4");
    CHECK(gen::Auto_InputCodec::SeatCount(gen::Arr<int32_t>{nullptr, 0}) == 0, "SeatCount(null) != 0");
    CHECK(gen::Auto_InputCodec::Offset(3) == 48, "Offset(3) != 3 * Stride (16)");
    std::printf("codec_test: %s (%d failure(s))\n", fails ? "FAIL" : "PASS", fails);
    return fails ? 1 : 0;
}
