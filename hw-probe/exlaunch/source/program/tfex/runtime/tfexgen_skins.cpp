/* TF.EX's custom skins, the library half (OUR OWN CODE): see tfexgen_skins.hpp. */
#include "tfexgen_skins.hpp"
#include "../gen/tfexgen_binding.hpp"
#include "../Game.hpp"
#include "../../hwlog.hpp"
#include <cstring>

extern "C" {
void* tfx_deflate_begin(int32_t level);
int32_t tfx_deflate_feed(void* d, const uint8_t* src, size_t len, bool finish, uint8_t* out, size_t cap, size_t* used, size_t* wrote);
void tfx_deflate_end(void* d);
long tfx_inflate(const uint8_t* src, size_t len, uint8_t* out, size_t cap);
}

namespace tfex::gen::skins {
namespace {
uintptr_t BytesType() { return bind::t_Byte_ef68; }   /* System.Byte[] */
gen::Arr<uint8_t> NewBytes(int32_t n) { return gen::NewArr<uint8_t>(BytesType(), n); }

/* ---- SHA-256 (FIPS 180-4) */
constexpr uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
inline uint32_t Rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
void Block(uint32_t h[8], const uint8_t* p) {
    uint32_t w[64];
    for (int i = 0; i < 16; i++) w[i] = (uint32_t)p[i * 4] << 24 | (uint32_t)p[i * 4 + 1] << 16 | (uint32_t)p[i * 4 + 2] << 8 | p[i * 4 + 3];
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = Rotr(w[i - 15], 7) ^ Rotr(w[i - 15], 18) ^ (w[i - 15] >> 3), s1 = Rotr(w[i - 2], 17) ^ Rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
    for (int i = 0; i < 64; i++) {
        uint32_t t1 = hh + (Rotr(e, 6) ^ Rotr(e, 11) ^ Rotr(e, 25)) + ((e & f) ^ (~e & g)) + K[i] + w[i];
        uint32_t t2 = (Rotr(a, 2) ^ Rotr(a, 13) ^ Rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
}
}  // namespace

gen::Arr<uint8_t> Sha256(const gen::Arr<uint8_t>& data) {
    uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    const uint8_t* p = data.data;
    uint64_t n = data.data ? (uint64_t)data.len : 0, i = 0;
    for (; i + 64 <= n; i += 64) Block(h, p + i);
    uint8_t tail[128] = {};
    uint64_t rest = n - i;
    if (rest) std::memcpy(tail, p + i, rest);
    tail[rest] = 0x80;
    int blocks = rest + 9 > 64 ? 2 : 1;
    uint64_t bits = n * 8;
    for (int k = 0; k < 8; k++) tail[blocks * 64 - 1 - k] = (uint8_t)(bits >> (8 * k));
    for (int b = 0; b < blocks; b++) Block(h, tail + b * 64);
    auto out = NewBytes(32);
    for (int k = 0; out.data && k < 32; k++) out.data[k] = (uint8_t)(h[k / 4] >> (24 - 8 * (k % 4)));
    return out;
}

void* HexUpper(const gen::Arr<uint8_t>& data) {
    static const char kHex[] = "0123456789ABCDEF";
    int32_t n = data.data ? data.len : 0;
    void* s = gen::NewString(n * 2);
    if (!s) return nullptr;
    char16_t* c = const_cast<char16_t*>(gen::Chars(s));
    for (int32_t i = 0; i < n; i++) { c[i * 2] = (char16_t)kHex[data.data[i] >> 4]; c[i * 2 + 1] = (char16_t)kHex[data.data[i] & 15]; }
    return s;
}

gen::Arr<uint8_t> FromBase64(const void* s) {
    int32_t n = s ? gen::Len(s) : 0;
    const char16_t* c = s ? gen::Chars(s) : nullptr;
    auto val = [](char16_t ch) -> int {
        if (ch >= 'A' && ch <= 'Z') return ch - 'A'; if (ch >= 'a' && ch <= 'z') return ch - 'a' + 26;
        if (ch >= '0' && ch <= '9') return ch - '0' + 52; if (ch == '+') return 62; if (ch == '/') return 63; return -1;
    };
    int32_t chars = 0, pad = 0;
    for (int32_t i = 0; i < n; i++) {
        char16_t ch = c[i];
        if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') continue;
        if (ch == '=') { pad++; chars++; continue; }
        if (pad || val(ch) < 0) return NewBytes(0);   /* a character after padding, or not base64 */
        chars++;
    }
    if (chars % 4 != 0 || pad > 2) return NewBytes(0);
    auto out = NewBytes(chars / 4 * 3 - pad);
    if (!out.data) return out;
    uint32_t acc = 0; int bits = 0; int32_t k = 0;
    for (int32_t i = 0; i < n; i++) {
        int v = val(c[i]);
        if (v < 0) continue;
        acc = acc << 6 | (uint32_t)v; bits += 6;
        if (bits >= 8) { bits -= 8; if (k < out.len) out.data[k++] = (uint8_t)(acc >> bits); }
    }
    return out;
}

gen::Arr<uint8_t> Deflate(const gen::Arr<uint8_t>& data) {
    int32_t n = data.data ? data.len : 0;
    /* stored blocks at worst: 5 bytes per 64 KB block, plus a final empty one */
    auto buf = NewBytes(n + (n / 65535 + 2) * 5 + 64);
    if (!buf.data) return buf;
    void* d = tfx_deflate_begin(9);   /* CompressionLevel.Optimal */
    if (!d) { hw::Log("[skins] DEFLATE: no compressor"); return gen::Arr<uint8_t>{nullptr, 0, nullptr}; }
    size_t used = 0, wrote = 0, inAt = 0, outAt = 0;
    int r = 0;
    while (r == 0) {
        r = tfx_deflate_feed(d, data.data + inAt, (size_t)n - inAt, true, buf.data + outAt, (size_t)buf.len - outAt, &used, &wrote);
        inAt += used; outAt += wrote;
        if (r == 0 && outAt >= (size_t)buf.len) { r = -1; break; }
    }
    tfx_deflate_end(d);
    if (r != 1) { hw::Log("[skins] DEFLATE of %d bytes failed", n); return gen::Arr<uint8_t>{nullptr, 0, nullptr}; }
    auto out = NewBytes((int32_t)outAt);
    if (out.data) std::memcpy(out.data, buf.data, outAt);
    return out;
}

int32_t Inflate(const gen::Arr<uint8_t>& src, const gen::Arr<uint8_t>& into) {
    if (!src.data || !into.data) return -1;
    long r = tfx_inflate(src.data, (size_t)src.len, into.data, (size_t)into.len);
    return r < 0 ? -1 : (int32_t)r;
}

/* ---- textures */
void GetRect(void* texture2D, int32_t level, bool hasRect, int32_t x, int32_t y, int32_t w, int32_t h,
             const gen::Arr<gen::Color>& data, int32_t start, int32_t count) {
    if (!texture2D || !data.obj) return;
    struct NullableRect { int32_t x, y, w, h; uint8_t hasValue, pad[3]; };   /* Rectangle? (20 bytes) */
    NullableRect asked{x, y, w, h, (uint8_t)(hasRect ? 1 : 0), {}};
    int32_t resolved[4] = {};   /* (+0xb8bf0's out: the rect it reads) */
    gen::Fn<void (*)(void*, int32_t, int32_t, void*, void*, int32_t, int32_t, void*)>(0xb8bf0)(texture2D, level, 0, &asked, data.obj, start, count, resolved);
    NullableRect read{resolved[0], resolved[1], resolved[2], resolved[3], 1, {}};
    gen::Fn<void (*)(void*, int32_t, int32_t, void*, void*, int32_t, int32_t)>(0x9106e0)(texture2D, level, 0, &read, data.obj, start, count);
}

void SetRgba(void* texture2D, const gen::Arr<uint8_t>& rgba, const gen::Arr<gen::Color>& colors) {
    if (!texture2D || !rgba.data || !colors.obj) return;
    int32_t n = colors.len < rgba.len / 4 ? colors.len : rgba.len / 4;
    std::memcpy(colors.data, rgba.data, (size_t)n * 4);   /* Color: R, G, B, A bytes */
    SetColors(texture2D, colors);
}

void SetColors(void* texture2D, const gen::Arr<gen::Color>& colors) {
    if (!texture2D || !colors.obj) return;
    /* Texture2D.SetData<Color>(Color[]), as the game expands it (Monocle's Texture(int, int, Color) +0x956254..0x956290) */
    struct { uint8_t b[20]; } noRect{};   /* Rectangle? = null */
    struct { uint8_t b[16]; } x7{};
    gen::Fn<void (*)(void*, int32_t, int32_t, void*, void*, int32_t, int32_t, void*)>(0xb8bf0)(texture2D, 0, 0, &noRect, colors.obj, 0, colors.len, &x7);
    gen::Fn<void (*)(void*, int32_t, void*, int32_t, int32_t)>(0xb9120)(texture2D, 0, colors.obj, 0, colors.len);
}

/* ---- MessagePack */
namespace {
uint8_t* g_w = nullptr;
int32_t g_wLen = 0, g_wCap = 0;
const uint8_t* g_r = nullptr;
int32_t g_rLen = 0, g_rAt = 0;
bool g_rErr = false;

/* MessagePack-CSharp's buffer segments, followed byte for byte (its Lz4BlockArray compresses one LZ4 block per segment, and GGRS checksums
 * those bytes): MessagePackSerializer.Serialize writes into a pooled Sequence<byte> through a BufferWriter whose first GetSpan() has no
 * size hint, so the first segment is ArrayPool.Rent(4096) (Sequence.DefaultLengthFromArrayPool); every write asks GetSpan(hint) and a
 * hint bigger than the room left starts a new segment of Rent(max(32768, hint)) (SequencePool's MinimumSpanLength; ArrayPool.Create's
 * buckets are powers of two). The hint is the bytes written, except a string's (UTF-8 maximum + 5: WriteString_PrepareSpan) and a bin's
 * (header + payload). The Switch assumed 32 KB segments: states over ~4 KB checksummed differently (The Amaranth, 2026-10-24). */
constexpr int kMaxSegs = 64;
int32_t g_segRoom = 0, g_segEnds[kMaxSegs], g_segCount = 0;
void Need(int32_t hint) {
    if (hint <= g_segRoom) return;
    if (g_segCount < kMaxSegs - 1) g_segEnds[g_segCount++] = g_wLen;
    else hw::Log("[skins] MessagePack: more than %d segments; the rest merge into the last", kMaxSegs);
    int32_t cap = 16; while (cap < (hint > 32768 ? hint : 32768)) cap *= 2;
    g_segRoom = cap;
}

/* The writer's buffer: kept between writes (MpBegin only rewinds it) and grown by doubling, outside the GC heap (plain bytes, no references:
 * GC_malloc_atomic_uncollectable would do; MallocUncollectable is what the runtime has). Before 2026-10-24 each write made a new 64 KB managed
 * byte[]: TF.State's save every tick (3 a tick in the sync test) made the GC collect several times a second on the console. */
bool Room(int32_t more) {
    if (g_wLen + more <= g_wCap) return true;
    int32_t cap = g_wCap ? g_wCap : 64 * 1024;
    while (cap < g_wLen + more) cap *= 2;
    auto* a = static_cast<uint8_t*>(gen::Fn<void* (*)(uint64_t)>(tfex::game::brute::MallocUncollectable)((uint64_t)cap));
    if (!a) { hw::Log("[skins] MessagePack: no %d byte buffer", cap); return false; }
    if (g_wLen) std::memcpy(a, g_w, (size_t)g_wLen);
    if (g_w) hw::Log("[skins] MessagePack: writer buffer grown to %d bytes (the old %d stay allocated)", cap, g_wCap);
    g_w = a; g_wCap = cap;
    return true;
}
void Put(uint8_t b) { if (Room(1)) { g_w[g_wLen++] = b; g_segRoom--; } }
void PutBE(uint64_t v, int n) { if (!Room(n)) return; for (int i = n - 1; i >= 0; i--) g_w[g_wLen++] = (uint8_t)(v >> (8 * i)); g_segRoom -= n; }
void PutBytes(const void* p, int32_t n) { if (n > 0 && Room(n)) { std::memcpy(g_w + g_wLen, p, (size_t)n); g_wLen += n; g_segRoom -= n; } }
void Header(int32_t n, uint8_t fix, int fixMax, uint8_t b16, uint8_t b32) {
    Need(n <= fixMax ? 1 : n <= 0xffff ? 3 : 5);
    if (n <= fixMax) Put((uint8_t)(fix | n)); else if (n <= 0xffff) { Put(b16); PutBE((uint64_t)n, 2); } else { Put(b32); PutBE((uint64_t)n, 4); }
}

bool Have(int32_t n) { if (g_rErr || g_rAt + n > g_rLen) { g_rErr = true; return false; } return true; }
uint64_t GetBE(int n) { uint64_t v = 0; if (!Have(n)) return 0; for (int i = 0; i < n; i++) v = v << 8 | g_r[g_rAt++]; return v; }
uint8_t Peek() { return Have(1) ? g_r[g_rAt] : 0xc1; }
uint8_t Get() { return Have(1) ? g_r[g_rAt++] : 0xc1; }
int32_t Len(uint8_t t, uint8_t fixBase, uint8_t fixMask, uint8_t b8, uint8_t b16, uint8_t b32) {
    if ((t & ~fixMask) == fixBase) return t & fixMask;
    if (b8 && t == b8) return (int32_t)GetBE(1);
    if (t == b16) return (int32_t)GetBE(2);
    if (t == b32) { uint64_t n = GetBE(4); if (n > 0x7fffffff) { g_rErr = true; return 0; } return (int32_t)n; }
    g_rErr = true;
    return -1;
}
}  // namespace

void MpBegin() { g_wLen = 0; g_segRoom = 4096; g_segCount = 0; }   /* the buffer stays (Room); the first segment (above) */
void MpArray(int32_t n) { Header(n, 0x90, 15, 0xdc, 0xdd); }
void MpMap(int32_t n) { Header(n, 0x80, 15, 0xde, 0xdf); }
void MpNil() { Need(1); Put(0xc0); }
void MpBool(bool v) { Need(1); Put(v ? 0xc3 : 0xc2); }
void MpInt(int64_t v) {
    Need(v >= 0 ? (v <= 0x7f ? 1 : v <= 0xff ? 2 : v <= 0xffff ? 3 : v <= 0xffffffffLL ? 5 : 9) : (v >= -32 ? 1 : v >= -128 ? 2 : v >= -32768 ? 3 : v >= INT32_MIN ? 5 : 9));
    if (v >= 0) {
        if (v <= 0x7f) Put((uint8_t)v); else if (v <= 0xff) { Put(0xcc); PutBE((uint64_t)v, 1); }
        else if (v <= 0xffff) { Put(0xcd); PutBE((uint64_t)v, 2); } else if (v <= 0xffffffffLL) { Put(0xce); PutBE((uint64_t)v, 4); }
        else { Put(0xcf); PutBE((uint64_t)v, 8); }
    } else {
        if (v >= -32) Put((uint8_t)(int8_t)v); else if (v >= -128) { Put(0xd0); PutBE((uint64_t)(uint8_t)(int8_t)v, 1); }
        else if (v >= -32768) { Put(0xd1); PutBE((uint64_t)(uint16_t)(int16_t)v, 2); } else if (v >= INT32_MIN) { Put(0xd2); PutBE((uint64_t)(uint32_t)(int32_t)v, 4); }
        else { Put(0xd3); PutBE((uint64_t)v, 8); }
    }
}
void MpF32(float v) { Need(5); uint32_t b; std::memcpy(&b, &v, 4); Put(0xca); PutBE(b, 4); }
void MpUInt(uint64_t v) {
    Need(v <= 0x7f ? 1 : v <= 0xff ? 2 : v <= 0xffff ? 3 : v <= 0xffffffffULL ? 5 : 9);
    if (v <= 0x7f) Put((uint8_t)v); else if (v <= 0xff) { Put(0xcc); PutBE(v, 1); } else if (v <= 0xffff) { Put(0xcd); PutBE(v, 2); }
    else if (v <= 0xffffffffULL) { Put(0xce); PutBE(v, 4); } else { Put(0xcf); PutBE(v, 8); }
}
void MpF64(double v) { Need(9); uint64_t b; std::memcpy(&b, &v, 8); Put(0xcb); PutBE(b, 8); }
void MpStr(const void* s) {
    if (!s) { MpNil(); return; }
    int32_t n16 = gen::Len(s);
    static char buf[4096];
    int32_t n = n16 * 3 + 1 <= (int32_t)sizeof buf ? gen::Utf8Of(s, buf, (int)sizeof buf) : -1;
    if (n < 0) { hw::Log("[skins] MessagePack: a %d character string doesn't fit", n16); MpNil(); return; }
    Need((n16 + 1) * 3 + 5);   /* WriteString_PrepareSpan: UTF8.GetMaxByteCount(chars) + 5 */
    if (n <= 31) Put((uint8_t)(0xa0 | n)); else if (n <= 0xff) { Put(0xd9); PutBE((uint64_t)n, 1); } else if (n <= 0xffff) { Put(0xda); PutBE((uint64_t)n, 2); }
    else { Put(0xdb); PutBE((uint64_t)n, 4); }
    PutBytes(buf, n);
}
void MpBin(const gen::Arr<uint8_t>& b) {
    if (!b.data && !b.obj) { MpNil(); return; }
    int32_t n = b.len;
    Need(n + (n <= 0xff ? 2 : n <= 0xffff ? 3 : 5));   /* WriteBinHeader: header + payload */
    if (n <= 0xff) { Put(0xc4); PutBE((uint64_t)n, 1); } else if (n <= 0xffff) { Put(0xc5); PutBE((uint64_t)n, 2); } else { Put(0xc6); PutBE((uint64_t)n, 4); }
    PutBytes(b.data, n);
}
int32_t MpSegments(int32_t* ends, int32_t max) {
    int32_t n = 0;
    for (int32_t i = 0; i < g_segCount && n < max; i++) if (g_segEnds[i] > (n ? ends[n - 1] : 0)) ends[n++] = g_segEnds[i];   /* (an empty first segment isn't one) */
    return n;
}
gen::Arr<uint8_t> MpEnd() {
    auto out = NewBytes(g_wLen);
    if (out.data && g_wLen) std::memcpy(out.data, g_w, (size_t)g_wLen);
    g_wLen = 0;
    return out;
}

void MpOpen(const gen::Arr<uint8_t>& bytes) { g_r = bytes.data; g_rLen = bytes.data ? bytes.len : 0; g_rAt = 0; g_rErr = !bytes.data; }
bool MpNil_() { if (Peek() == 0xc0) { g_rAt++; return true; } return false; }
int32_t MpReadArray() { return Len(Get(), 0x90, 0x0f, 0, 0xdc, 0xdd); }
int32_t MpReadMap() { return Len(Get(), 0x80, 0x0f, 0, 0xde, 0xdf); }
bool MpReadBool() { uint8_t t = Get(); if (t == 0xc3) return true; if (t != 0xc2) g_rErr = true; return false; }
int64_t MpReadInt() {
    uint8_t t = Get();
    if (t <= 0x7f) return t;
    if (t >= 0xe0) return (int8_t)t;
    switch (t) {
        case 0xcc: return (int64_t)GetBE(1); case 0xcd: return (int64_t)GetBE(2); case 0xce: return (int64_t)GetBE(4); case 0xcf: return (int64_t)GetBE(8);
        case 0xd0: return (int8_t)GetBE(1); case 0xd1: return (int16_t)GetBE(2); case 0xd2: return (int32_t)GetBE(4); case 0xd3: return (int64_t)GetBE(8);
    }
    g_rErr = true;
    return 0;
}
float MpReadF32() {
    uint8_t t = Get();
    if (t == 0xca) { uint32_t b = (uint32_t)GetBE(4); float f; std::memcpy(&f, &b, 4); return f; }
    if (t == 0xcb) { uint64_t b = GetBE(8); double d; std::memcpy(&d, &b, 8); return (float)d; }
    g_rAt--; return (float)MpReadInt();   /* an integer where a float is expected (MessagePack-CSharp accepts it) */
}
uint64_t MpReadUInt() {
    uint8_t t = Get();
    if (t == 0xcf) return GetBE(8);
    g_rAt--; return (uint64_t)MpReadInt();
}
double MpReadF64() {
    uint8_t t = Get();
    if (t == 0xcb) { uint64_t b = GetBE(8); double d; std::memcpy(&d, &b, 8); return d; }
    if (t == 0xca) { uint32_t b = (uint32_t)GetBE(4); float f; std::memcpy(&f, &b, 4); return f; }
    g_rAt--; return (double)MpReadInt();
}
void* MpReadStr() {
    if (MpNil_()) return nullptr;
    int32_t n = Len(Get(), 0xa0, 0x1f, 0xd9, 0xda, 0xdb);
    if (n < 0 || !Have(n)) return nullptr;
    void* s = gen::ManagedUtf8(reinterpret_cast<const char*>(g_r + g_rAt), n);
    g_rAt += n;
    return s;
}
gen::Arr<uint8_t> MpReadBin() {
    if (MpNil_()) return gen::Arr<uint8_t>{nullptr, 0, nullptr};
    uint8_t t = Get();
    int32_t n = t == 0xc4 ? (int32_t)GetBE(1) : t == 0xc5 ? (int32_t)GetBE(2) : t == 0xc6 ? (int32_t)GetBE(4) : (g_rErr = true, 0);
    if (g_rErr || !Have(n)) return gen::Arr<uint8_t>{nullptr, 0, nullptr};
    auto out = NewBytes(n);
    if (out.data && n) std::memcpy(out.data, g_r + g_rAt, (size_t)n);
    g_rAt += n;
    return out;
}
void MpSkip() {
    uint8_t t = Get();
    if (g_rErr) return;
    if (t <= 0x7f || t >= 0xe0 || t == 0xc0 || t == 0xc2 || t == 0xc3) return;
    if ((t & 0xe0) == 0xa0) { if (Have(t & 0x1f)) g_rAt += t & 0x1f; return; }
    if ((t & 0xf0) == 0x90) { for (int i = 0; i < (t & 0x0f) && !g_rErr; i++) MpSkip(); return; }
    if ((t & 0xf0) == 0x80) { for (int i = 0; i < (t & 0x0f) * 2 && !g_rErr; i++) MpSkip(); return; }
    int32_t n = 0;
    switch (t) {
        case 0xcc: case 0xd0: g_rAt += Have(1) ? 1 : 0; return;
        case 0xcd: case 0xd1: g_rAt += Have(2) ? 2 : 0; return;
        case 0xca: case 0xce: case 0xd2: g_rAt += Have(4) ? 4 : 0; return;
        case 0xcb: case 0xcf: case 0xd3: g_rAt += Have(8) ? 8 : 0; return;
        case 0xc4: case 0xd9: n = (int32_t)GetBE(1); break;
        case 0xc5: case 0xda: n = (int32_t)GetBE(2); break;
        case 0xc6: case 0xdb: n = (int32_t)GetBE(4); break;
        case 0xdc: { int32_t c = (int32_t)GetBE(2); for (int i = 0; i < c && !g_rErr; i++) MpSkip(); return; }
        case 0xdd: { int32_t c = (int32_t)GetBE(4); for (int i = 0; i < c && !g_rErr; i++) MpSkip(); return; }
        case 0xde: { int32_t c = (int32_t)GetBE(2); for (int i = 0; i < c * 2 && !g_rErr; i++) MpSkip(); return; }
        case 0xdf: { int32_t c = (int32_t)GetBE(4); for (int i = 0; i < c * 2 && !g_rErr; i++) MpSkip(); return; }
        default: g_rErr = true; return;   /* ext types: not in a skin bundle */
    }
    if (Have(n)) g_rAt += n;
}
bool MpOk() { return !g_rErr; }
bool MpDone() { return !g_rErr && g_rAt == g_rLen; }
}  // namespace tfex::gen::skins
