/* stb_vorbis (public domain / MIT, Sean Barrett): FAudio's copy (stb_vorbis.h, from the browser repo's vendor/FAudio/src at 6839b88), the
 * decoder FortRise's OggAudioTrack calls (FAudio.stb_vorbis_*). Here it decodes a mod's OGG music for tfex/runtime/tfexgen_frmods.cpp
 * (OggToWav). FAudio's copy leaves the C runtime to FAudio_internal.h: the headers and its two FAudio_ macros are given here. The module
 * imports libc from the game (pow, exp, cos, sin, log are there); floor, ldexp and qsort aren't, so they are defined in this file. */
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <limits.h>

#define FAUDIOAPI
#define FAudio_alloca(size) malloc(size)   /* (no alloc_buffer is passed: every temp_alloc is this, freed by temp_free) */
#define FAudio_dealloca(p) free(p)
#define STB_VORBIS_NO_STDIO 1
#define STB_VORBIS_NO_PUSHDATA_API 1
#define assert(x) ((void)0)

static double stbv_floor(double x) { return __builtin_floor(x); }   /* (frintm) */
static double stbv_ldexp(double x, int e) {   /* x * 2^e, e within a double's exponent (the decoder's: exp - 788, a float's range) */
    while (e > 1000) { x *= 0x1p1000; e -= 1000; }
    while (e < -1000) { x *= 0x1p-1000; e += 1000; }
    union { double d; unsigned long long u; } p;
    p.u = (unsigned long long)(e + 1023) << 52;
    return x * p.d;
}
static void stbv_swap(char* a, char* b, size_t size) { char t; while (size--) { t = *a; *a++ = *b; *b++ = t; } }
static void stbv_sift(char* a, size_t root, size_t n, size_t size, int (*cmp)(const void*, const void*)) {
    for (size_t child; (child = 2 * root + 1) < n; root = child) {
        if (child + 1 < n && cmp(a + child * size, a + (child + 1) * size) < 0) child++;
        if (cmp(a + root * size, a + child * size) >= 0) return;
        stbv_swap(a + root * size, a + child * size, size);
    }
}
static void stbv_qsort(void* base, size_t n, size_t size, int (*cmp)(const void*, const void*)) {   /* heapsort: a codebook's entries */
    char* a = (char*)base;
    for (size_t i = n / 2; i-- > 0;) stbv_sift(a, i, n, size, cmp);
    for (size_t end = n; end-- > 1;) { stbv_swap(a, a + end * size, size); stbv_sift(a, 0, end, size, cmp); }
}
#define floor stbv_floor
#define ldexp stbv_ldexp
#define qsort stbv_qsort

#include "stb_vorbis.h"
