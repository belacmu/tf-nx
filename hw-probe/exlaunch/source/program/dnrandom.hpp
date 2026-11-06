/* .NET's seeded System.Random (the "Net5CompatSeedImpl" / CompatPrng: Knuth's subtractive generator),
 * bit for bit, as plain C++ with no platform dependencies. Used to give the Switch's System.Random the
 * results the browser's .NET gives for a seed (the Switch's own is JKISS32; see VIABILITY-TESTS test 4).
 * Tested against the real runtime: hw-probe/tests/dnrandom-test.cpp vs tests/dnrandom-ref.txt. */
#pragma once
#include <cstdint>

namespace dnrandom {

/* C# int arithmetic wraps around silently; signed overflow in C++ is undefined, so every step
 * that can overflow (seeds near int.MinValue/int.MaxValue do) goes through these. */
inline int32_t Add(int32_t a, int32_t b) { return (int32_t)((uint32_t)a + (uint32_t)b); }
inline int32_t Sub(int32_t a, int32_t b) { return (int32_t)((uint32_t)a - (uint32_t)b); }

struct State {
    int32_t seedArray[56];
    int32_t inext, inextp;
};

inline void Init(State* s, int32_t seed) {
    int32_t subtraction = (seed == INT32_MIN) ? INT32_MAX : (seed < 0 ? -seed : seed);
    int32_t mj = Sub(161803398, subtraction);
    s->seedArray[55] = mj;
    s->seedArray[0] = 0;
    int32_t mk = 1;
    int ii = 0;
    for (int i = 1; i < 55; i++) {
        if ((ii += 21) >= 55) ii -= 55;
        s->seedArray[ii] = mk;
        mk = Sub(mj, mk);
        if (mk < 0) mk = Add(mk, INT32_MAX);
        mj = s->seedArray[ii];
    }
    for (int k = 1; k < 5; k++) {
        for (int i = 1; i < 56; i++) {
            int n = i + 30;
            if (n >= 55) n -= 55;
            s->seedArray[i] = Sub(s->seedArray[i], s->seedArray[1 + n]);
            if (s->seedArray[i] < 0) s->seedArray[i] = Add(s->seedArray[i], INT32_MAX);
        }
    }
    s->inext = 0;
    s->inextp = 21;
}

/* [0, int.MaxValue) */
inline int32_t InternalSample(State* s) {
    int32_t locINext = s->inext, locINextp = s->inextp;
    if (++locINext >= 56) locINext = 1;
    if (++locINextp >= 56) locINextp = 1;
    int32_t retVal = Sub(s->seedArray[locINext], s->seedArray[locINextp]);
    if (retVal == INT32_MAX) retVal--;
    if (retVal < 0) retVal = Add(retVal, INT32_MAX);
    s->seedArray[locINext] = retVal;
    s->inext = locINext;
    s->inextp = locINextp;
    return retVal;
}

inline double Sample(State* s) { return InternalSample(s) * (1.0 / INT32_MAX); }

inline double GetSampleForLargeRange(State* s) {
    int32_t result = InternalSample(s);
    bool negative = (InternalSample(s) % 2 == 0);
    if (negative) result = Sub(0, result);
    double d = result;
    d += (INT32_MAX - 1);
    d /= 2 * (uint32_t)INT32_MAX - 1;
    return d;
}

inline int32_t Next(State* s) { return InternalSample(s); }

/* maxValue >= 0 (the caller handles the exception case) */
inline int32_t Next(State* s, int32_t maxValue) { return (int32_t)(Sample(s) * maxValue); }

/* minValue <= maxValue (the caller handles the exception case) */
inline int32_t Next(State* s, int32_t minValue, int32_t maxValue) {
    int64_t range = (int64_t)maxValue - minValue;
    if (range <= INT32_MAX) return (int32_t)(Sample(s) * (double)range) + minValue;
    return (int32_t)((int64_t)(GetSampleForLargeRange(s) * (double)range) + minValue);
}

inline void NextBytes(State* s, uint8_t* buffer, int32_t n) {
    for (int32_t i = 0; i < n; i++) buffer[i] = (uint8_t)InternalSample(s);
}

} // namespace dnrandom
