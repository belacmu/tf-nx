/* tfexgen_code_rt.hpp's functions (hw-probe/exlaunch/source/program/tfex/runtime, compiled in place). */
#include <cstring>
#include "tfexgen_code_rt.hpp"
#include "tfexgen_binding.hpp"
#include <initializer_list>
#include <cstdio>
#include <cstdlib>
#include "../../hwlog.hpp"
#include "tfexgen_rt.hpp"
#include "../../netws.hpp"
#include "../../pools.hpp"
#include <lib/util/sys/modules.hpp>

extern "C" uint64_t svcGetSystemTick();

namespace xplay { int SimFrameForServices(bool* resim, bool* inMatch); }   /* xplay.cpp: inMatch = a lobby match (its rollback) runs */
namespace tfex::gen {
namespace {
constexpr int kFaultThreads = 8;
struct FaultSlot { volatile uintptr_t owner; volatile bool faulted; volatile bool wouldBlock; };
FaultSlot g_faults[kFaultThreads];
FaultSlot* MyFault() {
    uintptr_t self; __asm__ volatile("mrs %0, tpidrro_el0" : "=r"(self));
    for (auto& f : g_faults) if (f.owner == self) return &f;
    for (auto& f : g_faults) { uintptr_t none = 0; if (__atomic_compare_exchange_n(&f.owner, &none, self, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) return &f; }
    return nullptr;
}
} // namespace
void Fault(const char* what) {
    FaultSlot* f = MyFault();
    if (!f) { static int logged = 0; if (logged++ < 3) PoolFull("fault slots (threads using throwing stand-ins)", kFaultThreads); return; }
    f->faulted = true;
    static int n = 0;
    if (n++ < 20 || n % 100 == 0) hw::Log("[tfexgen] fault (as .NET throws): %s (#%d)", what ? what : "?", n);
}
bool Faulted() { FaultSlot* f = MyFault(); return f && f->faulted; }
void ClearFault() { if (FaultSlot* f = MyFault()) f->faulted = false; }
void SetWouldBlock() { if (FaultSlot* f = MyFault()) f->wouldBlock = true; }
bool WouldBlock() { FaultSlot* f = MyFault(); return f && f->wouldBlock; }
bool TakeWouldBlock() { FaultSlot* f = MyFault(); if (!f || !f->wouldBlock) return false; f->wouldBlock = false; return true; }
uintptr_t StringArrayType() { return bind::rt_StringArray; }
} // namespace tfex::gen

namespace tfex::gen {
uint64_t g_asCache[4096];   /* As's per-type-pair answers (tfexgen_code_rt.hpp) */

bool StrEq(const void* a, const void* b) {
    if (a == b) return true;
    if (!a || !b || Len(a) != Len(b)) return false;
    return !std::memcmp(Chars(a), Chars(b), (size_t)Len(a) * 2);
}
void* NewString(int32_t len) { return Fn<void* (*)(int32_t)>(bind::rt_String_InternalAllocateStr)(len); }
static char16_t* W(void* s) { return reinterpret_cast<char16_t*>(reinterpret_cast<uintptr_t>(s) + 0x14); }
bool StrEqI(const void* a, const void* b) {
    if (a == b) return true;
    if (!a || !b || Len(a) != Len(b)) return false;
    for (int32_t i = 0; i < Len(a); i++) {
        char16_t x = CharAt(a, i), y = CharAt(b, i);
        if (x >= u'a' && x <= u'z') x -= 32;
        if (y >= u'a' && y <= u'z') y -= 32;
        if (x != y) return false;
    }
    return true;
}
static void* CaseMap(const void* s, bool up) {
    if (!s) return nullptr;
    int32_t n = Len(s);
    void* r = NewString(n);
    if (!r) return nullptr;
    char16_t* d = const_cast<char16_t*>(Chars(r));
    for (int32_t i = 0; i < n; i++) {
        char16_t c = CharAt(s, i);
        if (up && c >= u'a' && c <= u'z') c -= 32;
        else if (!up && c >= u'A' && c <= u'Z') c += 32;
        d[i] = c;
    }
    return r;
}
void* Upper(const void* s) { return CaseMap(s, true); }
void* Lower(const void* s) { return CaseMap(s, false); }
void* Concat(const void* a, const void* b) {
    int la = Len(a), lb = Len(b);
    void* r = NewString(la + lb);
    if (!r) return nullptr;
    if (la) std::memcpy(W(r), Chars(a), (size_t)la * 2);
    if (lb) std::memcpy(W(r) + la, Chars(b), (size_t)lb * 2);
    return r;
}
void* Concat(const void* a, const void* b, const void* c) { return Concat(Concat(a, b), c); }
void* Repeat(char16_t c, int32_t n) {
    if (n < 0) n = 0;
    void* r = NewString(n);
    if (r) for (int i = 0; i < n; i++) W(r)[i] = c;
    return r;
}
void* FromAscii(const char* s) {
    int n = (int)std::strlen(s);
    void* r = NewString(n);
    if (r) for (int i = 0; i < n; i++) W(r)[i] = (char16_t)(unsigned char)s[i];
    return r;
}
void* TypeNameStr(void* obj) {
    if (!obj) return nullptr;
    uintptr_t desc = *reinterpret_cast<uintptr_t*>(obj);
    uintptr_t rec = desc ? *reinterpret_cast<uintptr_t*>(desc) : 0;
    const char16_t* name = rec ? *reinterpret_cast<const char16_t* const*>(rec + 0x20) : nullptr;
    if (!name) return nullptr;
    int32_t n = 0; while (n < 256 && name[n]) n++;
    return StrFromChars(name, n);
}
void* FromIntPad(int64_t v, int width) {
    char b[32]; int n = std::snprintf(b, sizeof b, "%0*lld", width < 1 ? 1 : width > 20 ? 20 : width, (long long)v);
    return ManagedUtf8(b, n);
}
void* FromIntHex(uint64_t v, int width, bool upper) {
    char b[32]; int n = std::snprintf(b, sizeof b, upper ? "%0*llX" : "%0*llx", width < 1 ? 1 : width > 16 ? 16 : width, (unsigned long long)v);
    return ManagedUtf8(b, n);
}
void* FromInt(int64_t v) {
    char b[24]; int i = 23; b[i] = 0;
    uint64_t u = v < 0 ? (uint64_t)(-(v + 1)) + 1 : (uint64_t)v;
    do { b[--i] = (char)('0' + u % 10); u /= 10; } while (u);
    if (v < 0) b[--i] = '-';
    return FromAscii(b + i);
}
/* double.ToString() (invariant): an integral value as an integer, else the shortest of %.15g / %.17g that reads back (C#'s round-trip
 * form; exponents print as e+NN where C# prints E+NN) */
void* FromDouble(double v) {
    if (v == (double)(int64_t)v && v > -1e15 && v < 1e15) return FromInt((int64_t)v);
    if (v != v) return FromAscii("NaN");
    if (v > 1.7976931348623157e308) return FromAscii("Infinity");
    if (v < -1.7976931348623157e308) return FromAscii("-Infinity");
    char b[40];
    std::snprintf(b, sizeof b, "%.15g", v);
    if (std::strtod(b, nullptr) != v) std::snprintf(b, sizeof b, "%.17g", v);
    return FromAscii(b);
}
bool IsNullOrEmpty(const void* s) { return !s || Len(s) == 0; }
bool DateTryParseExact(const void* s, const void* format, int64_t* out) {
    *out = 0;
    if (!s || !format) return false;
    const char16_t* p = Chars(s); int32_t n = Len(s), at = 0;
    const char16_t* f = Chars(format); int32_t fn = Len(format);
    int32_t v[6] = { 1, 1, 1, 0, 0, 0 };   /* year month day hour minute second */
    auto num = [&](int digits, int32_t& dst) {
        if (at + digits > n) return false;
        int32_t x = 0; for (int k = 0; k < digits; k++) { char16_t c = p[at + k]; if (c < u'0' || c > u'9') return false; x = x * 10 + (c - u'0'); }
        at += digits; dst = x; return true;
    };
    for (int32_t i = 0; i < fn;) {
        auto is = [&](const char16_t* tok, int len) { if (i + len > fn) return false; for (int k = 0; k < len; k++) if (f[i + k] != tok[k]) return false; return true; };
        bool ok;
        if (is(u"yyyy", 4)) { ok = num(4, v[0]); i += 4; }
        else if (is(u"MM", 2)) { ok = num(2, v[1]); i += 2; }
        else if (is(u"dd", 2)) { ok = num(2, v[2]); i += 2; }
        else if (is(u"HH", 2)) { ok = num(2, v[3]); i += 2; }
        else if (is(u"mm", 2)) { ok = num(2, v[4]); i += 2; }
        else if (is(u"ss", 2)) { ok = num(2, v[5]); i += 2; }
        else { ok = at < n && p[at] == f[i]; at++; i++; }
        if (!ok) return false;
    }
    if (at != n || v[0] < 1 || v[1] < 1 || v[1] > 12 || v[2] < 1 || v[3] > 23 || v[4] > 59 || v[5] > 59) return false;
    static const int mdays[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    bool leap = (v[0] % 4 == 0 && v[0] % 100 != 0) || v[0] % 400 == 0;
    if (v[2] > mdays[v[1] - 1] + (v[1] == 2 && leap ? 1 : 0)) return false;
    /* days since 0001-01-01 (proleptic Gregorian, as DateTime) */
    int64_t y = v[0] - 1, days = y * 365 + y / 4 - y / 100 + y / 400;
    for (int m = 1; m < v[1]; m++) days += mdays[m - 1] + (m == 2 && leap ? 1 : 0);
    days += v[2] - 1;
    *out = ((days * 24 + v[3]) * 60 + v[4]) * 60 * 10000000LL + (int64_t)v[5] * 10000000LL;
    return true;
}
void* RuntimeAlloc(size_t bytes) {
    void* p = Fn<void* (*)(uint64_t)>(game::brute::MallocUncollectable)(bytes);
    if (p) std::memset(p, 0, bytes);
    return p;
}
namespace {
/* The DynamicData side table: TF.EX's / TF.State's own state on game objects (TF.State's patches keep per-entity state there: a coroutine's
 * NAME, a chest's bottomlessCounter, a bat's timers). Open addressing on (object, name), with TF.EX's semantics (MonoMod's DynamicData is a
 * ConditionalWeakTable): an entry lives as long as its object. The keys sit in the module's .bss, which the GC doesn't scan, so an entry
 * doesn't keep its object alive, and each entry's object pointer is a Boehm disappearing link (GC_general_register_disappearing_link): the
 * GC clears it when the object dies, and the slot is reused (a tombstone until then). The values are in a scanned block (a value can be a
 * managed reference). Before, the table kept every object alive and never let go inside a match: TF.State's LoadState rebuilds entities as
 * new objects, so it filled with the number of loads (the 240 Hz sync test's POOL FULL at ~99 s, and its desync). Full of live entries is
 * still a clear error (pools rule). */
struct SideKey { uint64_t key; const char* name; void* obj; };
constexpr int kSide = 16384;
constexpr int kSideFull = kSide * 3 / 4;     /* probing stays short below this (live entries and tombstones) */
SideKey g_sideKeys[kSide];                   /* .bss: not a GC root (weak keys) */
uint64_t* g_sideVals = nullptr;              /* scanned, uncollectable */
int32_t g_sideUsed = 0, g_sidePeak = 0, g_sideRefused = 0, g_sideRebuilds = 0;   /* used: slots with a key (live or tombstone) */
constexpr uintptr_t kGcRegisterDisappearingLink = 0x2859d20;     /* int (void** link, const void* obj): Boehm's (its "Bad arg to ..." check) */
constexpr uintptr_t kGcUnregisterDisappearingLink = 0x285a060;   /* int (void** link): 1 if it was registered (same GC_dl_hashtbl, +0x4721fc0) */
void Watch(SideKey& e) { Fn<int (*)(void**, const void*)>(kGcRegisterDisappearingLink)(&e.obj, e.obj); }
void Unwatch(SideKey& e) { if (e.obj) Fn<int (*)(void**)>(kGcUnregisterDisappearingLink)(&e.obj); }
int SideLive() { int n = 0; for (auto& e : g_sideKeys) n += e.key && e.obj; return n; }
uint64_t SideKeyOf(const char* name, void* obj) {
    uint64_t h = 1469598103934665603ull;
    for (const char* c = name; *c; c++) h = (h ^ (uint8_t)*c) * 1099511628211ull;
    return (h ^ (reinterpret_cast<uint64_t>(obj) * 0x9e3779b97f4a7c15ull)) | 1;   /* never 0: 0 marks a free slot */
}
uint64_t* SideInsert(const char* name, void* obj, uint64_t key, uint64_t value);
/* the live entries reinserted without the tombstones (their links moved with them) */
void SideRebuild() {
    struct Live { const char* name; void* obj; uint64_t key, value; };
    /* in scanned memory: the objects stay reachable while their links are re-registered (a registration may allocate, and a collection
     * then must not free an object we are about to list again) */
    static Live* keep = nullptr;
    if (!keep) keep = static_cast<Live*>(Fn<void* (*)(uint64_t)>(game::brute::MallocUncollectable)(sizeof(Live) * kSide));
    if (!keep) { if (g_sideRefused++ == 0) PoolFull("DynamicData side table (its rebuild's scratch)", kSide); return; }
    int n = 0;
    for (int i = 0; i < kSide; i++) {
        SideKey& e = g_sideKeys[i];
        if (e.key && e.obj) { keep[n++] = Live{e.name, e.obj, e.key, g_sideVals[i]}; Unwatch(e); }
        e = SideKey{}; g_sideVals[i] = 0;
    }
    g_sideUsed = 0;
    for (int i = 0; i < n; i++) SideInsert(keep[i].name, keep[i].obj, keep[i].key, keep[i].value);
    std::memset(keep, 0, sizeof(Live) * (size_t)n);   /* (no longer holding them) */
    if (g_sideRebuilds++ < 8) hw::Log("[tfexgen] DynamicData side table: rebuilt without the collected objects' entries, %d live of %d", n, kSide);
}
uint64_t* SideInsert(const char* name, void* obj, uint64_t key, uint64_t value) {
    for (uint32_t i = (uint32_t)(key >> 17) % kSide, n = 0; n < (uint32_t)kSide; i = (i + 1) % kSide, n++) {
        SideKey& e = g_sideKeys[i];
        if (e.key && e.obj) continue;
        if (!e.key) g_sideUsed++;
        e = SideKey{key, name, obj}; g_sideVals[i] = value;
        Watch(e);
        return &g_sideVals[i];
    }
    return nullptr;
}
}
namespace {
struct CoRun { std::coroutine_handle<CoGen::promise_type> h; int32_t wait; bool done; };
struct CoBlock { size_t size; void* p; };
CoBlock g_coFree[64]; int g_coFreeCount = 0;   /* finished frames / run records, zeroed, by size */
}
void* CoAlloc(size_t n) {
    for (int i = g_coFreeCount - 1; i >= 0; i--)
        if (g_coFree[i].size == n) {
            void* p = g_coFree[i].p; g_coFree[i] = g_coFree[--g_coFreeCount];
            static int logged = 0;
            if (logged < 8) { logged++; hw::Log("[tfexgen] coroutine: a finished run's %d bytes reused", (int)n); }
            return p;
        }
    return Fn<void* (*)(uint64_t)>(game::brute::MallocUncollectable)(n);
}
void CoFree(void* p, size_t n) {
    if (!p) return;
    std::memset(p, 0, n);   /* no references left for the GC to follow */
    if (g_coFreeCount < 64) g_coFree[g_coFreeCount++] = CoBlock{n, p};   /* else kept (uncollectable, zeroed) */
}
int64_t CoStart(CoGen g) {
    auto* r = static_cast<CoRun*>(CoAlloc(sizeof(CoRun)));
    if (!r) return 0;
    r->h = g.h; r->wait = 0; r->done = false;
    return reinterpret_cast<int64_t>(r);
}
bool CoStep(int64_t run) {
    auto* r = reinterpret_cast<CoRun*>(run);
    if (!r || r->done || !r->h) return false;
    if (r->wait > 0) { r->wait--; return true; }   /* waitTimer.Update */
    r->h.resume();                                  /* enumerator.MoveNext() */
    if (r->h.done()) { r->h.destroy(); CoFree(r, sizeof(CoRun)); return false; }   /* the glue removes the Tween at once: no later step */
    r->wait = r->h.promise().wait; r->h.promise().wait = 0;
    return true;
}
void PrepareRuntime() {
    if (g_sideVals) return;
    g_sideVals = static_cast<uint64_t*>(Fn<void* (*)(uint64_t)>(game::brute::MallocUncollectable)(sizeof(uint64_t) * kSide));
    if (g_sideVals) std::memset(g_sideVals, 0, sizeof(uint64_t) * kSide);
    /* its use in the pools report (boot, every lobby match's end): live entries on this match's objects */
    if (g_sideVals) pools::Register("DynamicData side table (TF.EX / TF.State state on game objects)", [](pools::Use* u) { u->used = g_sideVals ? SideLive() : 0; u->peak = g_sidePeak; u->cap = kSideFull; });
    else hw::Log("[tfexgen] the DynamicData side table (%d entries) could not be allocated at boot", kSide);
}
/* (not emptied at a match's start: an entry on an object that outlives the match, the menu's MatchVariants, stays, as DynamicData's) */
void SideReport() {
    if (!g_sideVals) return;
    hw::Log("[tfexgen] DynamicData side table: %d live of %d entries at the match's start (peak %d, %d rebuilds)%s", SideLive(), kSide, g_sidePeak, g_sideRebuilds,
            g_sideRefused ? " -- REFUSED entries: the table was full" : "");
}
uint64_t* SideSlot(const char* name, void* obj, bool create) {
    if (!obj || !name) return nullptr;
    if (!g_sideVals) {
        if (!create) return nullptr;
        PrepareRuntime();
        if (!g_sideVals) return nullptr;
    }
    uint64_t key = SideKeyOf(name, obj);
    for (uint32_t i = (uint32_t)(key >> 17) % kSide, n = 0; n < (uint32_t)kSide; i = (i + 1) % kSide, n++) {
        SideKey& e = g_sideKeys[i];
        if (!e.key) break;   /* the end of the probe: not listed */
        if (e.key == key && e.obj == obj && !std::strcmp(e.name, name)) return &g_sideVals[i];
    }
    if (!create) return nullptr;
    if (g_sideUsed >= kSideFull) {   /* live entries and tombstones: the collected objects' slots first */
        int live = SideLive();
        if (live < kSideFull / 2) SideRebuild();
        else {
            if (g_sideRefused++ == 0) PoolFull("DynamicData side table (TF.EX / TF.State state on game objects)", kSideFull);
            return nullptr;
        }
    }
    uint64_t* v = SideInsert(name, obj, key, 0);
    int live = 0;
    if (v && (g_sideUsed & 255) == 0) { live = SideLive(); if (live > g_sidePeak) g_sidePeak = live; }
    return v;
}
void SpriteBatchBegin(void* batch, int32_t sortMode, void* blend, void* sampler, void* depthStencil, void* rasterizer, void* effect, const Matrix& transform) {
    struct NullableMatrix { Matrix m; uint8_t hasValue; uint8_t pad_[3]; };
    static_assert(sizeof(NullableMatrix) == 0x44, "MonoGame's Matrix? (SwitchHud.hpp)");
    NullableMatrix t{transform, 1, {}};
    constexpr uintptr_t kBegin = 0xc49d0;   /* SpriteBatch.Begin(..., Effect, Matrix?) (SwitchHud.hpp) */
    Fn<void (*)(void*, int32_t, void*, void*, void*, void*, void*, NullableMatrix*)>(kBegin)(batch, sortMode, blend, sampler, depthStencil, rasterizer, effect, &t);
}
void* NewGuidString() { char b[37]; netws::MakeUuid(b); return ManagedUtf8(b, 36); }
void* ManagedUtf8(const char* s, int n) {   /* UTF-8 -> a managed UTF-16 string of the full length: count, allocate, fill (BMP; others '?') */
    int k = 0;
    for (int i = 0; i < n; k++) { unsigned c = (unsigned char)s[i]; i += c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 1; }
    void* r = NewString(k);
    if (!r) return nullptr;
    char16_t* out = W(r);
    k = 0;
    for (int i = 0; i < n;) {
        unsigned c = (unsigned char)s[i];
        if (c < 0x80) { out[k++] = (char16_t)c; i++; }
        else if ((c >> 5) == 6 && i + 1 < n) { out[k++] = (char16_t)(((c & 0x1f) << 6) | (s[i + 1] & 0x3f)); i += 2; }
        else if ((c >> 4) == 14 && i + 2 < n) { out[k++] = (char16_t)(((c & 0x0f) << 12) | ((s[i + 1] & 0x3f) << 6) | (s[i + 2] & 0x3f)); i += 3; }
        else { out[k++] = u'?'; i += (c >> 3) == 30 ? 4 : 1; }
    }
    return r;
}
int Utf8Of(const void* m, char* out, int cap) {
    int n = 0, len = Len(m);
    const char16_t* c = Chars(m);
    for (int i = 0; i < len; i++) {
        unsigned u = c[i];
        if (u < 0x80) { if (n + 1 >= cap) break; out[n++] = (char)u; }
        else if (u < 0x800) { if (n + 2 >= cap) break; out[n++] = (char)(0xc0 | u >> 6); out[n++] = (char)(0x80 | (u & 0x3f)); }
        else { if (n + 3 >= cap) break; out[n++] = (char)(0xe0 | u >> 12); out[n++] = (char)(0x80 | ((u >> 6) & 0x3f)); out[n++] = (char)(0x80 | (u & 0x3f)); }
    }
    out[n] = 0;
    return n;
}
void* FormatFixed(double v, int decimals) {
    if (decimals < 0) decimals = 0;
    if (decimals > 9) decimals = 9;
    double scale = 1; for (int i = 0; i < decimals; i++) scale *= 10;
    double r = __builtin_round(v * scale) / scale;   /* (round: halves away from zero, as .NET's custom formats) */
    char b[48]; int n = std::snprintf(b, sizeof b, "%.*f", decimals, r);
    return ManagedUtf8(b, n < 0 ? 0 : n);
}
void* MonthAbbrev(int32_t month) {
    static const char* const k[13] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec", "" };
    const char* s = month >= 1 && month <= 13 ? k[month - 1] : "";
    return ManagedUtf8(s, (int)std::strlen(s));
}
void MapFull(int cap, int got) {
    static int logged = 0;
    if (logged++ < 8) hw::Log("[models] ERROR: a dictionary of %d entries in a model's KeyMap of %d (the rest dropped; raise its cap: translate.toml [models.caps])", got, cap);
}
void StrCut(const void* m, int written, int cap) {
    int full = 0, len = Len(m);
    const char16_t* c = Chars(m);
    for (int i = 0; i < len; i++) full += c[i] < 0x80 ? 1 : c[i] < 0x800 ? 2 : 3;
    if (full <= written) return;
    static int logged = 0;
    if (logged++ < 8) hw::Log("[models] ERROR: a %d-byte string cut to %d in a model's Str<%d> (raise its cap: translate.toml [models.caps])", full, written, cap);
}
bool MakeClass(Class& c, void* base, const uintptr_t* bdesc, uintptr_t baseGetter, uint32_t baseVtableBytes, uint32_t vtableBytes,
               uint32_t size, bool refs, const char16_t* name, const char16_t* ns, const char16_t* fullName, const char16_t* aqName, uint32_t hash) {
    using namespace game::brute;
    if (!base || !bdesc || reinterpret_cast<void*>(bdesc[0]) != base || vtableBytes > sizeof c.vtable || baseVtableBytes > vtableBytes) return false;
    if (Fld<uint64_t>(base, RtSize) > size) return false;
    uint8_t* t = static_cast<uint8_t*>(Fn<void* (*)(uint64_t)>(MallocUncollectable)(RtRecord));
    if (!t) return false;
    std::memcpy(t, base, RtRecord);                      /* the base's record: kind, interfaces, builders */
    Fld<uint64_t>(t, RtSize) = size;
    if (refs) Fld<uint32_t>(t, RtFlags) |= 8;             /* holds references: GC_malloc (scanned) */
    Fld<uint32_t>(t, RtHash) = hash;
    Fld<const char16_t*>(t, RtName) = name; Fld<const char16_t*>(t, RtNamespace) = ns;
    Fld<const char16_t*>(t, RtFullName) = fullName; Fld<const char16_t*>(t, RtAqName) = aqName;
    Fld<uintptr_t>(t, RtDesc) = reinterpret_cast<uintptr_t>(c.desc);
    Fld<uintptr_t>(t, RtCctor) = 0;                       /* no class constructor (EnsureClass makes our statics) */
    Fld<uintptr_t>(t, RtParent) = baseGetter;             /* `is` walks the base getters */
    Fld<uint16_t>(t, RtFieldCount) = 0; Fld<uint16_t>(t, RtPropCount) = 0; Fld<uint16_t>(t, RtMethodCount) = 0;
    for (uint32_t cache : {0xc8u, 0xf0u, 0x108u, 0x120u, 0x138u, 0x150u, 0x160u}) Fld<uintptr_t>(t, cache) = 0;   /* MakeType's lazy caches */
    std::memcpy(c.vtable, reinterpret_cast<void*>(bdesc[1]), baseVtableBytes);
    c.vtableBytes = vtableBytes;
    c.desc[0] = reinterpret_cast<uintptr_t>(t); c.desc[1] = reinterpret_cast<uintptr_t>(c.vtable); c.desc[2] = bdesc[2];
    for (auto& m : c.method) { Fld<const char16_t*>(m, 0) = name; Fld<void*>(m, 8) = t; }
    c.type = t; c.base = base;
    return true;
}
Class& HostClass(Class& c, const char16_t* name, uintptr_t objectGetter, uintptr_t objectDesc, uint32_t objectVtableBytes) {
    if (!c.type) {
        uint32_t h = 2166136261u;
        for (const char16_t* p = name; *p; p++) { h ^= *p; h *= 16777619u; }
        MakeClass(c, TypeOf(objectGetter), reinterpret_cast<const uintptr_t*>(Base() + objectDesc), Base() + objectGetter, objectVtableBytes,
                  objectVtableBytes, 0x10, false, name, u"TfexGen", name, name, h);
    }
    return c;
}
void* NewDelegate(Class& c, int methodIndex, uintptr_t delegateTypeGetter, void* target, void* fn) {
    using namespace game::brute;
    void* d = Fn<void* (*)(void*)>(NewObject)(TypeOf(delegateTypeGetter));
    if (!c.type || methodIndex < 0 || methodIndex >= 64) return nullptr;   /* no class: no delegate (C# would have one; callers null-check as for a failed new) */
    Fld<void*>(c.method[methodIndex], 0x18) = fn;
    Fld<void*>(d, DlgFn) = fn; Fld<void*>(d, DlgTarget) = target; Fld<void*>(d, DlgMethod) = c.method[methodIndex];
    return d;
}
[[noreturn]] void Throw(const char* where) { hw::Log("[tfexgen] FATAL: a C# throw in %s (the generated code stops here)", where ? where : "?"); __builtin_trap(); }
int64_t UtcNowTicks() { return (int64_t)(svcGetSystemTick() * 10000000ULL / 19200000ULL); }
int64_t TickCount64() { return (int64_t)(svcGetSystemTick() / 19200); }   /* the system tick runs at 19.2 MHz */

int32_t ParseInt(const void* s) {
    int32_t n = Len(s), i = 0;
    while (i < n && IsWs(CharAt(s, i))) i++;
    bool neg = false;
    if (i < n && (CharAt(s, i) == u'-' || CharAt(s, i) == u'+')) neg = CharAt(s, i++) == u'-';
    int64_t v = 0; int digits = 0;
    while (i < n && CharAt(s, i) >= u'0' && CharAt(s, i) <= u'9') { v = v * 10 + (CharAt(s, i++) - u'0'); digits++; if (v > 2147483648LL) Throw("Int32.Parse: overflow"); }
    while (i < n && IsWs(CharAt(s, i))) i++;
    if (!s || digits == 0 || i != n || (!neg && v > 2147483647LL)) Throw("Int32.Parse: not an integer");
    return (int32_t)(neg ? -v : v);
}

bool g_traceOnly = false, g_traceNow = false;   /* (the TF.State measurement: only while it runs its first ticks) */
void Trace(const char* what) {
    static uint32_t n = 0;
    if (g_traceOnly && !g_traceNow) return;
    if (++n <= 400 || n % 1000 == 0 || g_traceNow) hw::Log("[trace] %u %s", n, what);
}

/* MStr (tfexgen_rt.hpp): a model's managed string in a scanned slot */
void** NewRootSlot() {
    void** p = static_cast<void**>(Fn<void* (*)(uint64_t)>(game::brute::MallocUncollectable)(sizeof(void*)));
    if (p) *p = nullptr; else hw::Log("[models] no GC slot for a managed model string");
    return p;
}
void* ManagedFromJsonString(const char* raw, int n) {
    static char buf[300 * 1024];   /* game thread (models are read there); a SkinChunk's data is ~262 KB */
    int m = json::Unescape(raw, n, buf, (int)sizeof buf);
    if (m < 0 || m >= (int)sizeof buf - 1) { hw::Log("[models] a %d-byte JSON string doesn't fit %d bytes", n, (int)sizeof buf); return nullptr; }
    return ManagedUtf8(buf, m);
}
void JsonWriteManaged(json::Writer& w, const char* key, const void* managed) {
    if (!managed) { w.Null(key); return; }
    static char buf[300 * 1024];
    int n = Utf8Of(managed, buf, (int)sizeof buf);
    buf[n < (int)sizeof buf ? n : (int)sizeof buf - 1] = 0;
    w.String(key, buf);
}
char* JsonScratch(int* cap) { static char buf[300 * 1024]; *cap = (int)sizeof buf; return buf; }   /* JsonBytes' one buffer (game thread) */

/* int.TryParse(s, out n) (Int32.TryParse isn't in the Switch's binary): ParseInt's grammar, false instead of an exception */
bool TryParseInt(const void* s, int32_t* out) {
    *out = 0;
    if (!s) return false;
    int32_t n = Len(s), i = 0;
    while (i < n && IsWs(CharAt(s, i))) i++;
    bool neg = false;
    if (i < n && (CharAt(s, i) == u'-' || CharAt(s, i) == u'+')) neg = CharAt(s, i++) == u'-';
    int64_t v = 0; int digits = 0;
    while (i < n && CharAt(s, i) >= u'0' && CharAt(s, i) <= u'9') { v = v * 10 + (CharAt(s, i++) - u'0'); digits++; if (v > 2147483648LL) return false; }
    while (i < n && IsWs(CharAt(s, i))) i++;
    if (digits == 0 || i != n || (!neg && v > 2147483647LL)) return false;
    *out = (int32_t)(neg ? -v : v);
    return true;
}

void PoolFull(const char* what, int capacity, void* site) {
    static int n = 0;
    uintptr_t self = exl::util::GetSelfModuleInfo().m_Total.m_Start;
    if (n < 8 || (n & (n - 1)) == 0) hw::Log("[tfexgen] POOL FULL: %s holds %d elements; one dropped (%d so far; from +%#lx)", what, capacity, n + 1,
                                             site ? (unsigned long)(reinterpret_cast<uintptr_t>(site) - self) : 0ul);
    n++;
}

int32_t CorruptCount(const char* what, int32_t count, int capacity) {
    static int n = 0;
    if (n < 8 || (n & (n - 1)) == 0) hw::Log("[tfexgen] ERROR: a %s says it holds %d elements (capacity %d): its memory was overwritten; read as empty (%d so far)", what, (int)count, capacity, n + 1);
    n++;
    return 0;
}
} // namespace tfex::gen


namespace tfex::gen {
/* (tfexgen_code_rt.hpp TFEXGEN_STATE_GAP) the first hit of each site, then a count every 1024 hits of all */
alignas(16) uint8_t g_gapObject[4096];
void StateGap(const char* fn, int line) {
    struct Site { const char* fn; int line; };
    static Site seen[96]; static int nSeen = 0; static uint32_t total = 0;
    total++;
    for (int i = 0; i < nSeen; i++) if (seen[i].fn == fn && seen[i].line == line) { if ((total & 1023) == 0) hw::Log("[state] gap hits so far: %u (%d sites)", total, nSeen); return; }
    if (nSeen < 96) seen[nSeen++] = {fn, line};
    hw::Log("[state] gap: %s (line %d of its generated file) skipped (site %d)", fn, line, nSeen);
}
} // namespace tfex::gen
